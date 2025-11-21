#ifndef _TYPES_H_
#define _TYPES_H_

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h> // For mode_t and file type macros (S_ISDIR, S_ISREG)

/******************************************************************************
* SECTION: Type def
*******************************************************************************/
typedef int        boolean;
typedef uint16_t   flag16;

typedef enum myfs_file_type {
    MYFS_REG_FILE,
    MYFS_DIR,
    MYFS_SYM_LINK 
} MYFS_FILE_TYPE;

/******************************************************************************
* SECTION: Macro - Base Definition
*******************************************************************************/
#define TRUE                   1
#define FALSE                  0

#define MYFS_MAGIC_NUM         0x52415453 // 文件系统幻数
#define MYFS_ROOT_INO          0          // 根目录 Inode 编号

// 错误码 
#define MYFS_ERROR_NONE        0
#define MYFS_ERROR_ACCESS      EACCES
#define MYFS_ERROR_ISDIR       EISDIR
#define MYFS_ERROR_NOSPACE     ENOSPC
#define MYFS_ERROR_EXISTS      EEXIST
#define MYFS_ERROR_NOTFOUND    ENOENT
#define MYFS_ERROR_IO          EIO
#define MYFS_ERROR_INVAL       EINVAL

#define MYFS_MAX_FILE_NAME     128 // 文件名最大长度
#define MYFS_DEFAULT_PERM      0777 // 默认权限 rwxrwxrwx

/******************************************************************************
* SECTION: Macro - EXT2 Lite Parameters (核心参数)
*******************************************************************************/
#define MYFS_BLK_SIZE           1024 // 逻辑块大小 1024B
#define MYFS_DIRECT_BLOCKS      6    // 直接索引块数 (对应 6KB 最大文件数据)
#define MYFS_INODE_DISK_SIZE    128  // Inode 磁盘结构大小 (128B)
#define MYFS_INODE_PER_BLOCK    (MYFS_BLK_SIZE / MYFS_INODE_DISK_SIZE) // 8 个 Inode/块

/******************************************************************************
* SECTION: Macro Function
*******************************************************************************/
// 假设全局变量为 struct myfs_super myfs_super;
#define MYFS_ROUND_DOWN(value, round)  ((value) % (round) == 0 ? (value) : ((value) / (round)) * (round))
#define MYFS_ROUND_UP(value, round)    ((value) % (round) == 0 ? (value) : ((value) / (round) + 1) * (round))

// 方便判断文件类型，使用 mode 字段
#define MYFS_IS_DIR(pinode)            (S_ISDIR(pinode->mode))
#define MYFS_IS_REG(pinode)            (S_ISREG(pinode->mode))

// 计算 Inode 在磁盘上的绝对偏移 (需要 myfs_super 在全局可用)
#define MYFS_GET_INODE_DISK_OFS(ino)   \
    ((myfs_super.inode_start * MYFS_BLK_SIZE) + \
    ((ino) / MYFS_INODE_PER_BLOCK * MYFS_BLK_SIZE) + \
    ((ino) % MYFS_INODE_PER_BLOCK * MYFS_INODE_DISK_SIZE))

/******************************************************************************
* SECTION: FS Specific Structure - In memory structure (In-Mem)
*******************************************************************************/
struct myfs_dentry;
struct myfs_inode;
struct myfs_super;

struct custom_options {
    const char* device;
    boolean     show_help;
};

// 1. 内存中的 Inode 结构 (In-Mem)
struct myfs_inode
{
    uint32_t ino;                 // Inode 编号
    
    // 文件元数据 (同步自磁盘结构)
    uint32_t mode;                // 文件类型和权限 (st_mode)
    uint32_t size;                // 文件大小（字节）
    uint32_t atime;
    uint32_t mtime;
    uint32_t ctime;
    uint16_t uid;
    uint16_t gid;
    uint16_t link_count;          // 硬链接数
    uint32_t block[MYFS_DIRECT_BLOCKS]; // 6 个直接数据块块号
    
    // 内存特有字段
    struct myfs_dentry* dentry;    // 指向该 Inode 的 dentry
    struct myfs_dentry* first_child; // 目录文件：指向子文件 dentry 链表头
    uint8_t* data_buf;    // 存储文件数据（目录项或文件内容）的缓冲区
}; 

// 2. 内存中的 Dentry 结构 (In-Mem)
struct myfs_dentry
{
    char             fname[MYFS_MAX_FILE_NAME];
    uint32_t         ino;               // 对应的 Inode 编号

    // 内存特有字段
    struct myfs_dentry* parent;          // 父亲目录的 dentry 
    struct myfs_dentry* brother;         // 兄弟 dentry
    struct myfs_inode* inode;           // 指向 Inode
};

// 3. 内存中的超级块结构 (In-Mem)
struct myfs_super
{
    // 磁盘持久化信息 (从 myfs_super_d 读取/写入)
    uint32_t magic_num;
    uint32_t block_size;  
    uint32_t total_blocks;
    
    // 布局信息
    uint32_t ibmap_start; 
    uint32_t dbmap_start; 
    uint32_t inode_start; 
    uint32_t data_start;  

    uint32_t inode_count; 
    uint32_t inode_per_block; 
    
    // 内存特有字段
    int        driver_fd; // 驱动文件描述符
    boolean    is_mounted;
    
    uint8_t* map_inode;     // 内存中的 Inode 位图副本
    uint8_t* map_data;      // 内存中的 Data 位图副本
    
    struct myfs_dentry* root_dentry;
};

static inline struct myfs_dentry* myfs_new_dentry(char * fname) {
    // 辅助函数定义
    struct myfs_dentry * dentry = (struct myfs_dentry *)malloc(sizeof(struct myfs_dentry));
    if (dentry) {
        memset(dentry, 0, sizeof(struct myfs_dentry));
        strncpy(dentry->fname, fname, MYFS_MAX_FILE_NAME - 1);
        dentry->ino     = (uint32_t)-1;
    }
    return dentry; 
}

/******************************************************************************
* SECTION: FS Specific Structure - Disk structure (To-Disk)
*******************************************************************************/

// 1. 超级块磁盘结构体 (To-Disk)
struct myfs_super_d {
    uint32_t magic_num;
    uint32_t block_size;
    uint32_t total_blocks;
    
    uint32_t inode_count;
    uint32_t inode_per_block;

    // 五个区域的布局信息 (起始块号和块数)
    uint32_t ibmap_start;
    uint32_t ibmap_blks;
    
    uint32_t dbmap_start;
    uint32_t dbmap_blks;
    
    uint32_t inode_start;
    uint32_t inode_blks;
    
    uint32_t data_start;
    uint32_t data_blks;

    uint32_t root_ino;
    
    // 填充至 1024B
    char     padding[MYFS_BLK_SIZE - (16 * sizeof(uint32_t))]; 
};

// 2. 索引节点磁盘结构体 (To-Disk) - 必须为 128B
struct myfs_inode_d {
    uint32_t ino; 
    
    // 文件元数据
    uint32_t mode;        
    uint32_t size;        
    
    uint32_t atime;
    uint32_t mtime;
    uint32_t ctime;
    
    uint16_t uid;
    uint16_t gid;
    uint16_t link_count;
    
    uint32_t block[MYFS_DIRECT_BLOCKS]; // 6个直接数据块块号
    
    // 占用 56 字节 (4*4 + 3*4 + 2*2 + 6*4 = 56 字节)
    char     padding[MYFS_INODE_DISK_SIZE - 56]; // 填充至 128B
}; 

// 3. 目录项磁盘结构体 (To-Disk)
struct myfs_dentry_d {
    uint32_t ino;
    uint16_t reclen;
    uint8_t  namelen;
    uint8_t  file_type; 
    char     fname[MYFS_MAX_FILE_NAME];
}; 

#endif /* _TYPES_H_ */