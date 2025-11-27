#ifndef _UTILS_H_
#define _UTILS_H_

#include "types.h"
#include <fuse.h>
#include <string>

class FileSystem {
public:
    static FileSystem& Instance(); 

    void mount(const char* device);
    void umount();
    
    // FUSE 接口
    int fuse_mkdir(const char* path, mode_t mode);
    int fuse_mknod(const char* path, mode_t mode, dev_t dev);
    int fuse_getattr(const char* path, struct stat* st);
    int fuse_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi);
    int fuse_utimens(const char* path, const struct timespec tv[2]);

    // 选做功能的接口声明
    int fuse_write(const char* path, const char* buf, size_t size, off_t offset, struct fuse_file_info* fi);
    int fuse_read(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi);
    int fuse_access(const char* path, int mask);
    int fuse_open(const char* path, struct fuse_file_info* fi);
    int fuse_opendir(const char* path, struct fuse_file_info* fi);
    int fuse_truncate(const char* path, off_t size);
    int fuse_unlink(const char* path);
    int fuse_rmdir(const char* path);
    int fuse_rename(const char* from, const char* to);

private:
    FileSystem(); 
    ~FileSystem();

    FileSystem(const FileSystem&) = delete;
    FileSystem& operator=(const FileSystem&) = delete;

    struct myfs_super super;
    struct CustomOptions options;

    int driver_read(int offset, void* out_content, int size);
    int driver_write(int offset, void* in_content, int size);
    
    void clear_bit(uint8_t* map, int index);
    void free_data_block(int blk_no);

    void release_inode(myfs_inode* inode);
    int delete_dentry(myfs_inode* parent, myfs_dentry* child);

    int alloc_data_block();
    int get_block(myfs_inode* inode, int logical_block_idx, bool create);

    void sync_inode(myfs_inode* inode);
    myfs_inode* read_inode(uint32_t ino);
    myfs_inode* alloc_inode(myfs_dentry* dentry, bool is_dir);
    
    myfs_dentry* new_dentry(std::string fname, FileType ftype);
    int alloc_dentry(myfs_inode* parent, myfs_dentry* dentry);
    myfs_dentry* lookup(const std::string& path, bool* is_find, bool* is_root);

    int get_inode_disk_offset(uint32_t ino);
};

#endif