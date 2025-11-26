#ifndef _UTILS_H_
#define _UTILS_H_

#include "types.h"
#include <fuse.h>
#include <string> // 引入 string

class FileSystem {
public:
    static FileSystem& Instance(); 

    void mount(const char* device);
    void umount();
    
    // FUSE 接口对应的核心逻辑
    int fuse_mkdir(const char* path, mode_t mode);
    int fuse_mknod(const char* path, mode_t mode, dev_t dev); // 新增
    int fuse_getattr(const char* path, struct stat* st);
    int fuse_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi);
    int fuse_write(const char* path, const char* buf, size_t size, off_t offset, struct fuse_file_info* fi);
    int fuse_read(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi); // 新增
    int fuse_utimens(const char* path, const struct timespec tv[2]); // 新增

private:
    FileSystem(); 
    ~FileSystem();

    // 禁止拷贝
    FileSystem(const FileSystem&) = delete;
    FileSystem& operator=(const FileSystem&) = delete;

    struct myfs_super super;
    struct CustomOptions options;

    int driver_read(int offset, void* out_content, int size);
    int driver_write(int offset, void* in_content, int size);
    
    int find_free_bit(uint8_t* map, int count);
    void set_bit(uint8_t* map, int index);
    void clear_bit(uint8_t* map, int index);
    int alloc_data_block();
    int get_block(myfs_inode* inode, int logical_block_idx, bool create);

    void sync_inode(myfs_inode* inode);
    myfs_inode* read_inode(uint32_t ino);
    myfs_inode* alloc_inode(myfs_dentry* dentry, bool is_dir);
    
    // [修改] 使用 std::string 传递参数
    myfs_dentry* new_dentry(std::string fname, FileType ftype);
    int alloc_dentry(myfs_inode* parent, myfs_dentry* dentry);
    myfs_dentry* lookup(const std::string& path, bool* is_find, bool* is_root);

    void format();
    int get_inode_disk_offset(uint32_t ino);
};

#endif