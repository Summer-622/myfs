#include "utils.h"
#include <cstring>
#include <iostream>
#include <ctime>
#include <unistd.h>
#include <vector>
#include <algorithm>
#include <cstddef>
#include <sstream> // 新增，用于路径解析

extern "C" {
    #include "ddriver.h"
}

#define DRIVER_BLK_SIZE 512

FileSystem& FileSystem::Instance() {
    static FileSystem instance;
    return instance;
}

FileSystem::FileSystem() {
    super = {}; 
}

FileSystem::~FileSystem() {
}

int FileSystem::driver_read(int offset, void* out_content, int size) {
    int down = MYFS_ROUND_DOWN(offset, DRIVER_BLK_SIZE);
    int up = MYFS_ROUND_UP(offset + size, DRIVER_BLK_SIZE);
    int bias = offset - down;
    
    std::vector<std::byte> tempdata(up - down);
    size_t block_count = tempdata.size() / DRIVER_BLK_SIZE;
    
    for (size_t i = 0; i < block_count; i++) {
        int current_offset = down + i * DRIVER_BLK_SIZE;
        ddriver_seek(super.driver_fd, current_offset, SEEK_SET);
        ddriver_read(super.driver_fd, (char *)(tempdata.data() + i * DRIVER_BLK_SIZE), DRIVER_BLK_SIZE);
    }
    
    std::memcpy(out_content, tempdata.data() + bias, size);
    
    return MYFS_ERROR_NONE;
}

int FileSystem::driver_write(int offset, void* in_content, int size) {
    int down = MYFS_ROUND_DOWN(offset, DRIVER_BLK_SIZE);
    int up = MYFS_ROUND_UP(offset + size, DRIVER_BLK_SIZE);
    int bias = offset - down;
    
    std::vector<std::byte> tempdata(up - down);
    size_t block_count = tempdata.size() / DRIVER_BLK_SIZE;
    
    for (size_t i = 0; i < block_count; i++) {
        int current_offset = down + i * DRIVER_BLK_SIZE; 
        ddriver_seek(super.driver_fd, current_offset, SEEK_SET);
        ddriver_read(super.driver_fd, (char *)(tempdata.data() + i * DRIVER_BLK_SIZE), DRIVER_BLK_SIZE);
    }

    std::memcpy(tempdata.data() + bias, static_cast<const std::byte*>(in_content), size);
    
    for (size_t i = 0; i < block_count; ++i) {
        int current_offset = down + i * DRIVER_BLK_SIZE;
        ddriver_seek(super.driver_fd, current_offset, SEEK_SET);
        ddriver_write(super.driver_fd, (char *)(tempdata.data() + i * DRIVER_BLK_SIZE), DRIVER_BLK_SIZE);
    }
    return MYFS_ERROR_NONE;
}

int FileSystem::get_inode_disk_offset(uint32_t ino) {
    return (super.inode_start * MYFS_BLK_SIZE) + 
           ((ino) / MYFS_INODE_PER_BLOCK * MYFS_BLK_SIZE) + 
           ((ino) % MYFS_INODE_PER_BLOCK * MYFS_INODE_DISK_SIZE);
}

int FileSystem::alloc_data_block() {
    int data_blocks_count = super.total_blocks - super.data_start;
  
    //遍历查找空闲位 (LSB First)
    for (int i = 0; i < data_blocks_count; i++) {
        int byte_idx = i / 8;
        int bit_idx = i % 8;
        
        // 检查位是否为0
        if (!(super.map_data[byte_idx] & (1 << bit_idx))) {
            // 找到空闲位，立即标记
            super.map_data[byte_idx] |= (1 << bit_idx);
            
            //写回 Bitmap
            driver_write(super.dbmap_start * MYFS_BLK_SIZE, super.map_data, MYFS_BLK_SIZE);
            
            int abs_blk_id = super.data_start + i;

            //清零新分配的数据块
            std::vector<uint8_t> empty_block(MYFS_BLK_SIZE, 0);
            driver_write(abs_blk_id * MYFS_BLK_SIZE, empty_block.data(), MYFS_BLK_SIZE);

            //返回数据块号
            return abs_blk_id;
        }
    }
    
    return -1; // 无空间
}

myfs_inode* FileSystem::alloc_inode(myfs_dentry *dentry, bool is_dir) {
    // 直接遍历查找空闲位
    int ino = -1;
    for (int i = 0; i < super.inode_count; i++) {
        int byte_idx = i / 8;
        int bit_idx = i % 8;
        
        if (!(super.map_inode[byte_idx] & (1 << bit_idx))) {
            super.map_inode[byte_idx] |= (1 << bit_idx);
            ino = i;
            break;
        }
    }
    
    if (ino == -1) return nullptr;
    
    // 写回 Bitmap
    driver_write(super.ibmap_start * MYFS_BLK_SIZE, super.map_inode, MYFS_BLK_SIZE);
    
    myfs_inode *inode = new myfs_inode();
    *inode = {};
    inode->ino = ino;
    inode->mode = is_dir ? (S_IFDIR | 0755) : (S_IFREG | 0644);
    inode->link_count = 1;
    inode->dentry = dentry;
    inode->atime = inode->mtime = inode->ctime = time(NULL);
    
    dentry->inode = inode;
    dentry->ino = ino;
    return inode;
}

// =================================================================
// Dentry / Path 操作
// =================================================================

myfs_dentry* FileSystem::new_dentry(std::string fname, FileType ftype) {
    myfs_dentry * dentry = new myfs_dentry(); 
    dentry->fname = fname; 
    
    dentry->ftype = ftype;
    dentry->ino = -1;
    return dentry; 
}

int FileSystem::alloc_dentry(myfs_inode *parent, myfs_dentry *dentry) {
    if (!parent) return -1;
    dentry->parent = parent->dentry;
    dentry->brother = parent->first_child;
    parent->first_child = dentry;
    
    // 更新父目录大小
    parent->size += sizeof(struct myfs_dentry_d);
    return 0;
}

myfs_dentry* FileSystem::lookup(const std::string& path, bool *is_find, bool *is_root) {
    if (path == "/") {
        *is_find = true;
        *is_root = true;
        return super.root_dentry;
    }
    
    struct myfs_dentry *current = super.root_dentry;
    bool found = false;
    
    std::stringstream ss(path);
    std::string token;

    while (std::getline(ss, token, '/')) {
        if (token.empty()) continue;    // 跳过重复的/

        found = false;
        
        if (current->inode == nullptr) {
            current->inode = read_inode(current->ino);
            if (current->inode) {
                current->inode->dentry = current;
            }
        }
        
        if (current->inode && MYFS_IS_DIR(current->inode)) {
            struct myfs_dentry *child = current->inode->first_child;
            while (child) {
                if (child->fname == token) {
                    current = child;
                    found = true;
                    break;
                }
                child = child->brother;
            }
        }
        
        if (!found) {
            *is_find = false;
            *is_root = false;
            return nullptr;
        }
    }
    
    *is_find = found;
    *is_root = false;
    return current;
}

// =================================================================
// Inode 同步与读取
// =================================================================

void FileSystem::sync_inode(myfs_inode *inode) {
    if (!inode) return;

    if (MYFS_IS_DIR(inode)) {
        // 处理目录项写入数据块
        struct myfs_dentry *child = inode->first_child;
        int blk_cnt = 0;
        int entries_in_block = 0;
        int max_entries_per_block = MYFS_BLK_SIZE / sizeof(struct myfs_dentry_d);
        
        // 遍历所有子目录项
        while (child && blk_cnt < MYFS_DIRECT_BLOCKS) {
            // 确保当前数据块已分配
            if (inode->block[blk_cnt] == 0) {
                int new_blk = alloc_data_block();
                if (new_blk == -1) break; // 没有空间
                inode->block[blk_cnt] = new_blk;
            }
            
            // 准备当前数据块的缓冲区
            std::vector<std::byte> buf(MYFS_BLK_SIZE);
            struct myfs_dentry_d *dentry_ptr = (struct myfs_dentry_d *)buf.data();
            entries_in_block = 0;
            
            // 填充当前数据块
            while (child && entries_in_block < max_entries_per_block) {
                dentry_ptr[entries_in_block].ino = child->ino;
                dentry_ptr[entries_in_block].reclen = sizeof(struct myfs_dentry_d);
                
                size_t name_len = child->fname.length();
                if (name_len >= MYFS_MAX_FILE_NAME) name_len = MYFS_MAX_FILE_NAME - 1;
                dentry_ptr[entries_in_block].namelen = name_len;
                
                // 确定文件类型
                if (child->inode) {
                    dentry_ptr[entries_in_block].file_type = MYFS_IS_DIR(child->inode) ? 1 : 0;
                } else {
                    dentry_ptr[entries_in_block].file_type = (child->ftype == FileType::DIR) ? 1 : 0;
                }
                
                std::memset(dentry_ptr[entries_in_block].fname, 0, MYFS_MAX_FILE_NAME);
                std::strncpy(dentry_ptr[entries_in_block].fname, child->fname.c_str(), name_len);
                
                entries_in_block++;
                child = child->brother;
            }
            
            // 写入当前数据块
            driver_write(inode->block[blk_cnt] * MYFS_BLK_SIZE, buf.data(), MYFS_BLK_SIZE);
            blk_cnt++;
        }
        
        // 更新目录大小
        inode->size = blk_cnt * MYFS_BLK_SIZE;
        
        // 递归同步子节点
        child = inode->first_child;
        while (child) {
            if (child->inode) sync_inode(child->inode);
            child = child->brother;
        }
    }

    // 同步inode元数据
    struct myfs_inode_d inode_d = {};
    inode_d.ino = inode->ino;
    inode_d.mode = inode->mode;
    inode_d.size = inode->size;
    inode_d.uid = inode->uid;
    inode_d.gid = inode->gid;
    inode_d.link_count = inode->link_count;
    inode_d.atime = inode->atime;
    inode_d.mtime = inode->mtime;
    inode_d.ctime = inode->ctime;
    std::memcpy(inode_d.block, inode->block, sizeof(inode->block));

    int offset = get_inode_disk_offset(inode->ino);
    driver_write(offset, (uint8_t *)&inode_d, sizeof(struct myfs_inode_d));
}

myfs_inode* FileSystem::read_inode(uint32_t ino) {
    if (ino >= super.inode_count) return nullptr;
    
    myfs_inode *inode = new myfs_inode();
    *inode = {};
    
    struct myfs_inode_d inode_d;
    int offset = get_inode_disk_offset(ino);
    driver_read(offset, (uint8_t *)&inode_d, sizeof(struct myfs_inode_d));
    
    inode->ino = inode_d.ino;
    inode->mode = inode_d.mode;
    inode->size = inode_d.size;
    inode->link_count = inode_d.link_count;
    inode->uid = inode_d.uid;
    inode->gid = inode_d.gid;
    inode->atime = inode_d.atime;
    inode->mtime = inode_d.mtime;
    inode->ctime = inode_d.ctime;
    std::memcpy(inode->block, inode_d.block, sizeof(inode->block));
    
    if (MYFS_IS_DIR(inode)) {
        //从所有数据块读取目录项
        for (int blk_cnt = 0; blk_cnt < MYFS_DIRECT_BLOCKS; blk_cnt++) {
            if (inode->block[blk_cnt] == 0) continue;
            
            std::vector<std::byte> buf(MYFS_BLK_SIZE);
            driver_read(inode->block[blk_cnt] * MYFS_BLK_SIZE, buf.data(), MYFS_BLK_SIZE);
            
            struct myfs_dentry_d *dentry_ptr = (struct myfs_dentry_d *)buf.data();
            int max_entries = MYFS_BLK_SIZE / sizeof(struct myfs_dentry_d);
            
            //反向构建链表
            for (int i = max_entries - 1; i >= 0; i--) {
                if (dentry_ptr[i].fname[0] == '\0') continue;
                
                FileType type = (dentry_ptr[i].file_type == 1) ? FileType::DIR : FileType::REG_FILE;
                //构造 string 对象传递给 new_dentry
                std::string fname_str(dentry_ptr[i].fname);
                struct myfs_dentry *child = new_dentry(fname_str, type);
                child->ino = dentry_ptr[i].ino;
                child->brother = inode->first_child;
                inode->first_child = child;
            }
        }
    }
    return inode;
}

// =================================================================
// 挂载/格式化
// =================================================================

void FileSystem::mount(const char* device) {
    super.driver_fd = ddriver_open(const_cast<char*>(device));

    struct myfs_super_d super_d_disk;
    driver_read(MYFS_SUPER_OFS, (uint8_t *)&super_d_disk, sizeof(struct myfs_super_d));

    myfs_dentry* root_dentry = new_dentry("/",FileType::DIR);
    myfs_inode* root_inode;

    if (super_d_disk.magic_num != MYFS_MAGIC_NUM) {
        
        // ==================== 格式化分支 (Format Path) ====================
        super.magic_num = MYFS_MAGIC_NUM;
        super.block_size = MYFS_BLK_SIZE;

        int dev_size = 0;
        ddriver_ioctl(super.driver_fd, IOC_REQ_DEVICE_SIZE, &dev_size);
        //计算总块数
        super.total_blocks = dev_size / MYFS_BLK_SIZE;

        //布局规划 目前最大支持8MB
        int reserved_reserved = 1; // Superblock
        int ibmap_blks = 1;
        int dbmap_blks = 1;
        
        int fixed_overhead = reserved_reserved + ibmap_blks + dbmap_blks;
        int available_blocks = super.total_blocks - fixed_overhead;

        //一个索引块存储8个索引节点，每个节点指向6个块，一共49块
        int inode_blks = available_blocks / 49;
        
        // 设置起始位置
        super.ibmap_start = 1;
        super.dbmap_start = 1 + ibmap_blks;
        super.inode_start = 1 + ibmap_blks + dbmap_blks; 
        super.data_start  = super.inode_start + inode_blks;

        // 填写统计信息
        super.inode_count = inode_blks * MYFS_INODE_PER_BLOCK;
        super.inode_per_block = MYFS_INODE_PER_BLOCK;

        //分配并清零位图
        super.map_inode = new uint8_t[MYFS_BLK_SIZE * ibmap_blks](); 
        super.map_data = new uint8_t[MYFS_BLK_SIZE * dbmap_blks]();

        root_inode = alloc_inode(root_dentry,MYFS_ISDIR);
        sync_inode(root_inode);

        //建立内存中的 Dentry 联系
        super.root_dentry = new_dentry("/", FileType::DIR);
        super.root_dentry->ino = MYFS_ROOT_INO;
        super.root_dentry->inode = root_inode;
        root_inode->dentry = super.root_dentry;

        driver_write(super.ibmap_start * MYFS_BLK_SIZE, super.map_inode, MYFS_BLK_SIZE * ibmap_blks);
        driver_write(super.dbmap_start * MYFS_BLK_SIZE, super.map_data, MYFS_BLK_SIZE * dbmap_blks);

        struct myfs_super_d new_super_d = {};
        new_super_d.magic_num = super.magic_num;
        new_super_d.block_size = super.block_size;
        new_super_d.total_blocks = super.total_blocks;
        new_super_d.inode_count = super.inode_count;
        new_super_d.inode_per_block = super.inode_per_block;
        
        new_super_d.ibmap_start = super.ibmap_start;
        new_super_d.ibmap_blks = ibmap_blks;
        new_super_d.dbmap_start = super.dbmap_start;
        new_super_d.dbmap_blks = dbmap_blks;
        
        new_super_d.inode_start = super.inode_start;
        new_super_d.inode_blks = inode_blks;
        
        new_super_d.data_start = super.data_start;
        new_super_d.data_blks = super.total_blocks - super.data_start;
        
        new_super_d.root_ino = MYFS_ROOT_INO;
        
        driver_write(MYFS_SUPER_OFS, (uint8_t *)&new_super_d, sizeof(struct myfs_super_d));

    } else {
        // ==================== 加载分支 (Load Path) ====================
        super.magic_num = super_d_disk.magic_num;
        super.block_size = super_d_disk.block_size;
        super.total_blocks = super_d_disk.total_blocks;
        super.inode_count = super_d_disk.inode_count;
        super.inode_per_block = super_d_disk.inode_per_block;
        
        super.ibmap_start = super_d_disk.ibmap_start;
        super.dbmap_start = super_d_disk.dbmap_start;
        super.inode_start = super_d_disk.inode_start;
        super.data_start = super_d_disk.data_start;
        
        // 动态分配位图大小
        int ibmap_size = super_d_disk.ibmap_blks * MYFS_BLK_SIZE;
        int dbmap_size = super_d_disk.dbmap_blks * MYFS_BLK_SIZE;

        super.map_inode = new uint8_t[ibmap_size];
        super.map_data = new uint8_t[dbmap_size];
        
        driver_read(super.ibmap_start * MYFS_BLK_SIZE, super.map_inode, ibmap_size);
        driver_read(super.dbmap_start * MYFS_BLK_SIZE, super.map_data, dbmap_size);
        
        super.root_dentry = new_dentry("/", FileType::DIR);
        super.root_dentry->ino = super_d_disk.root_ino;
        super.root_dentry->inode = read_inode(super_d_disk.root_ino);
        
        if (super.root_dentry->inode) {
            super.root_dentry->inode->dentry = super.root_dentry;
        }
    }

    super.is_mounted = true;
}

void FileSystem::umount() {
    if (!super.is_mounted) return;
    if (super.root_dentry && super.root_dentry->inode) {
        sync_inode(super.root_dentry->inode);
    }

    struct myfs_super_d super_d = {};
    super_d.magic_num = MYFS_MAGIC_NUM;
    super_d.block_size = MYFS_BLK_SIZE;
    super_d.total_blocks = super.total_blocks;
    super_d.inode_count = super.inode_count;
    super_d.inode_per_block = super.inode_per_block;
    super_d.ibmap_start = super.ibmap_start;
    super_d.ibmap_blks = 1;
    super_d.dbmap_start = super.dbmap_start;
    super_d.dbmap_blks = 1;
    super_d.inode_start = super.inode_start;
    super_d.inode_blks = (super.inode_count + MYFS_INODE_PER_BLOCK - 1) / MYFS_INODE_PER_BLOCK;
    super_d.data_start = super.data_start;
    super_d.data_blks = super.total_blocks - super.data_start;
    super_d.root_ino = MYFS_ROOT_INO;
    
    driver_write(MYFS_SUPER_OFS, (uint8_t *)&super_d, sizeof(struct myfs_super_d));
    driver_write(super.ibmap_start * MYFS_BLK_SIZE, super.map_inode, MYFS_BLK_SIZE);
    driver_write(super.dbmap_start * MYFS_BLK_SIZE, super.map_data, MYFS_BLK_SIZE);

    fsync(super.driver_fd);
    ddriver_close(super.driver_fd);
    super.is_mounted = false;
    
    delete[] super.map_inode;
    delete[] super.map_data;
}


// =================================================================
// FUSE 接口 (保持不变)
// =================================================================

int FileSystem::fuse_mkdir(const char* path, mode_t mode) {
    bool is_find, is_root;
    std::string s_path(path);
    myfs_dentry* existing = lookup(s_path, &is_find, &is_root);
    if (is_find) return -MYFS_ERROR_EXISTS;
    
    size_t last_slash = s_path.find_last_of('/');
    std::string dir_name;
    std::string base_name;
    
    if (last_slash == 0) {
        dir_name = "/";
        base_name = s_path.substr(1);
    } else {
        dir_name = s_path.substr(0, last_slash);
        base_name = s_path.substr(last_slash + 1);
    }
    
    myfs_dentry *parent_dentry = lookup(dir_name, &is_find, &is_root);

    if (!parent_dentry || !parent_dentry->inode) return -MYFS_ERROR_NOTFOUND;

    myfs_dentry *new_d = new_dentry(base_name, FileType::DIR);
    myfs_inode *new_in = alloc_inode(new_d, MYFS_ISDIR); 
    if (!new_in) return -MYFS_ERROR_NOSPACE;

    alloc_dentry(parent_dentry->inode, new_d);
    
    parent_dentry->inode->mtime = time(NULL);
    
    sync_inode(new_in);
    sync_inode(parent_dentry->inode);
    return 0;
}

int FileSystem::fuse_mknod(const char* path, mode_t mode, dev_t dev) {
    bool is_find, is_root;
    std::string s_path(path);
    myfs_dentry* existing = lookup(s_path, &is_find, &is_root);
    if (is_find) return -MYFS_ERROR_EXISTS;

    size_t last_slash = s_path.find_last_of('/');
    std::string dir_name;
    std::string base_name;
    
    if (last_slash == 0) {
        dir_name = "/";
        base_name = s_path.substr(1);
    } else {
        dir_name = s_path.substr(0, last_slash);
        base_name = s_path.substr(last_slash + 1);
    }

    myfs_dentry *parent_dentry = lookup(dir_name, &is_find, &is_root);

    if (!parent_dentry || !parent_dentry->inode) return -MYFS_ERROR_NOTFOUND;

    myfs_dentry *new_d = new_dentry(base_name, FileType::REG_FILE);
    myfs_inode *new_in = alloc_inode(new_d, MYFS_ISREG);
    if (!new_in) return -MYFS_ERROR_NOSPACE;

    alloc_dentry(parent_dentry->inode, new_d);

    parent_dentry->inode->mtime = time(NULL);

    sync_inode(new_in);
    sync_inode(parent_dentry->inode);
    return 0;
}

int FileSystem::fuse_write(const char* path, const char* buf, size_t size, off_t offset, struct fuse_file_info* fi) { 
    bool is_find, is_root;
    std::string s_path(path);
    myfs_dentry *dentry = lookup(s_path, &is_find, &is_root);
    if (!is_find || !dentry || !dentry->inode) return -MYFS_ERROR_NOTFOUND;

    myfs_inode *inode = dentry->inode;
    
    int start_blk_idx = (int)(offset / MYFS_BLK_SIZE);
    int end_blk_idx = (int)((offset + size - 1) / MYFS_BLK_SIZE);
    
    if (end_blk_idx >= MYFS_DIRECT_BLOCKS) return -MYFS_ERROR_NOSPACE;

    size_t wrote = 0;
    for (int i = start_blk_idx; i <= end_blk_idx; i++) {
        if (inode->block[i] == 0) {
            int blk = alloc_data_block();
            if (blk == -1) return -MYFS_ERROR_NOSPACE;
            inode->block[i] = blk;
        }
        
        size_t blk_offset = (i == start_blk_idx) ? (offset % MYFS_BLK_SIZE) : 0;
        size_t len = MYFS_BLK_SIZE - blk_offset;
        if (len > (size - wrote)) len = size - wrote;
        
        driver_write(inode->block[i] * MYFS_BLK_SIZE + blk_offset, (uint8_t*)(buf + wrote), len);
        wrote += len;
    }

    if (offset + size > inode->size) inode->size = (uint32_t)(offset + size);
    inode->mtime = time(NULL);
    
    sync_inode(inode);
    return size; 
}

int FileSystem::fuse_read(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi) { 
    bool is_find, is_root;
    std::string s_path(path);
    struct myfs_dentry *dentry = lookup(s_path, &is_find, &is_root);
    if (!is_find || !dentry || !dentry->inode) return -MYFS_ERROR_NOTFOUND;
    
    if (offset >= dentry->inode->size) return 0;
    if (offset + size > dentry->inode->size) size = dentry->inode->size - offset;

    int start_blk_idx = (int)(offset / MYFS_BLK_SIZE);
    int end_blk_idx = (int)((offset + size - 1) / MYFS_BLK_SIZE);
    
    size_t read_len = 0;
    for (int i = start_blk_idx; i <= end_blk_idx; i++) {
        int blk = dentry->inode->block[i];
        size_t blk_offset = (i == start_blk_idx) ? (offset % MYFS_BLK_SIZE) : 0;
        size_t len = MYFS_BLK_SIZE - blk_offset;
        if (len > (size - read_len)) len = size - read_len;

        if (blk == 0) {
            std::memset(buf + read_len, 0, len);
        } else {
            driver_read(blk * MYFS_BLK_SIZE + blk_offset, (uint8_t*)(buf + read_len), len);
        }
        read_len += len;
    }
    return read_len; 
}

int FileSystem::fuse_utimens(const char* path, const struct timespec tv[2]) {
    bool is_find, is_root;
    std::string s_path(path);
    struct myfs_dentry *dentry = lookup(s_path, &is_find, &is_root);
    if (!is_find || !dentry || !dentry->inode) return -MYFS_ERROR_NOTFOUND;
    
    if (tv) {
        dentry->inode->atime = tv[0].tv_sec;
        dentry->inode->mtime = tv[1].tv_sec;
    } else {
        dentry->inode->atime = time(NULL);
        dentry->inode->mtime = time(NULL);
    }
    sync_inode(dentry->inode);
    return 0;
}

int FileSystem::fuse_getattr(const char* path, struct stat * myfs_stat) {
    bool is_find, is_root;
    std::string s_path(path);
    struct myfs_dentry *dentry = lookup(s_path, &is_find, &is_root);
    
    if (!is_find || !dentry) {
        return -MYFS_ERROR_NOTFOUND;
    }

    if (dentry->inode == nullptr) {
        dentry->inode = read_inode(dentry->ino);
        dentry->inode->dentry = dentry;
    }

    struct myfs_inode *inode = dentry->inode;
    myfs_stat->st_mode = inode->mode;
    myfs_stat->st_nlink = inode->link_count;
    myfs_stat->st_uid = inode->uid;
    myfs_stat->st_gid = inode->gid;
    myfs_stat->st_size = inode->size;
    myfs_stat->st_atime = inode->atime;
    myfs_stat->st_mtime = inode->mtime;
    myfs_stat->st_ctime = inode->ctime;
    myfs_stat->st_blocks = (inode->size + MYFS_BLK_SIZE - 1) / MYFS_BLK_SIZE; 
    myfs_stat->st_blksize = MYFS_BLK_SIZE;

	return 0;
}

int FileSystem::fuse_readdir(const char * path, void * buf, fuse_fill_dir_t filler, off_t offset,
			    		 struct fuse_file_info * fi) {
    bool is_find, is_root;
    std::string s_path(path);
    struct myfs_dentry *dentry = lookup(s_path, &is_find, &is_root);
    
    if (!is_find || !dentry || !dentry->inode) return -MYFS_ERROR_NOTFOUND;
    if (!MYFS_IS_DIR(dentry->inode)) return -MYFS_ERROR_NOTFOUND;
    
    struct myfs_dentry *child = dentry->inode->first_child;
    while(child) {
        struct stat st;
        std::memset(&st, 0, sizeof(st));
        if (child->inode) st.st_mode = child->inode->mode;
        
        if (filler(buf, child->fname.c_str(), &st, 0)) break;
        child = child->brother;
    }
	
    return 0;
}