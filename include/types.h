#ifndef _TYPES_H_
#define _TYPES_H_

#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <sys/stat.h>
#include <string> // 引入 string
#include <type_traits> // 用于 static_assert 检查结构体大小

/******************************************************************************
* SECTION: Base Definition (基础定义)
*******************************************************************************/
const uint32_t MYFS_MAGIC_NUM = 0x52415453; // 幻数
const int MYFS_SUPER_OFS = 0;               // 超级块偏移量（第0块）
const int MYFS_ROOT_INO = 0;                // 根目录的 Inode 号

#define MYFS_ISDIR             true
#define MYFS_ISREG             false

// 错误码映射 (映射到标准 POSIX 错误码)
#define MYFS_ERROR_NONE        0
#define MYFS_ERROR_ACCESS      EACCES       // 权限拒绝
#define MYFS_ERROR_ISDIR       EISDIR       // 是目录
#define MYFS_ERROR_NOSPACE     ENOSPC       // 空间不足
#define MYFS_ERROR_EXISTS      EEXIST       // 文件已存在
#define MYFS_ERROR_NOTFOUND    ENOENT       // 文件未找到
#define MYFS_ERROR_IO          EIO          // IO错误
#define MYFS_ERROR_INVAL       EINVAL       // 参数无效

const int MYFS_MAX_FILE_NAME = 128;         // 最大文件名长度
const int MYFS_DEFAULT_PERM = 0777;         // 默认权限

/******************************************************************************
* SECTION: EXT2 Lite Parameters (核心参数)
*******************************************************************************/
const int MYFS_BLK_SIZE = 1024;             // 块大小 1KB
const int MYFS_DIRECT_BLOCKS = 6;           // 直接索引块数量 (6KB文件上限)
const int MYFS_INODE_DISK_SIZE = 128;       // 磁盘上每个 Inode 的大小
const int MYFS_INODE_PER_BLOCK = (MYFS_BLK_SIZE / MYFS_INODE_DISK_SIZE); // 每块存8个Inode

// 宏：判断 Inode 模式
#define MYFS_IS_DIR(pinode)            (S_ISDIR(pinode->mode))
#define MYFS_IS_REG(pinode)            (S_ISREG(pinode->mode))

/******************************************************************************
* SECTION: 辅助宏
*******************************************************************************/
// 向上/向下取整对齐
#define MYFS_ROUND_DOWN(value, round)    ((value) % (round) == 0 ? (value) : ((value) / (round)) * (round))
#define MYFS_ROUND_UP(value, round)      ((value) % (round) == 0 ? (value) : ((value) / (round) + 1) * (round))

// 文件类型枚举
enum class FileType {
    REG_FILE, // 普通文件
    DIR,      // 目录
    SYM_LINK  // 符号链接
};

// 前置声明
struct myfs_dentry;
struct myfs_inode;

// 命令行参数结构
struct CustomOptions {
    const char* device;
    bool show_help;
};

/******************************************************************************
* SECTION: Memory Structures (内存结构 - 运行时使用)
* 包含指针等运行时特有的信息，不直接写入磁盘
*******************************************************************************/

//内存中的 Inode
struct myfs_inode {
    // --- 对应磁盘的数据 ---
    uint32_t ino;
    uint32_t mode;
    uint32_t size;
    uint32_t atime;
    uint32_t mtime;
    uint32_t ctime;
    uint16_t uid;
    uint16_t gid;
    uint16_t link_count;
    uint32_t block[MYFS_DIRECT_BLOCKS];
    
    // --- 内存特有运行时字段 (不会写盘) ---
    struct myfs_dentry* dentry = nullptr;      // 反向指向 dentry
    struct myfs_dentry* first_child = nullptr; 
    uint8_t* data_buf = nullptr;               // 数据缓冲区
}; 

//内存中的 Dentry (目录项)
struct myfs_dentry {
    std::string fname; // [修改] 使用 std::string 代替 char数组，方便操作
    uint32_t ino;
    FileType ftype;

    // --- 目录树指针 ---
    struct myfs_dentry* parent = nullptr;
    struct myfs_dentry* brother = nullptr;
    struct myfs_inode* inode = nullptr;        // 关联的内存 Inode
};

//内存中的超级块
struct myfs_super {
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
    
    // --- 运行时句柄 ---
    int driver_fd;                             // 磁盘设备文件描述符
    bool is_mounted;
    
    uint8_t* map_inode = nullptr;              // Inode位图缓存
    uint8_t* map_data = nullptr;               // 数据块位图缓存
    
    struct myfs_dentry* root_dentry = nullptr; // 根目录 dentry
};

/******************************************************************************
* SECTION: Disk structures (磁盘物理结构) - Must match binary layout
* 直接用于磁盘读写，大小和布局必须严格固定
*******************************************************************************/

// 磁盘超级块: 占用 1024 Bytes
struct myfs_super_d {
    uint32_t magic_num;
    uint32_t block_size;
    uint32_t total_blocks;

    uint32_t inode_count;
    uint32_t inode_per_block;

    // 区域起始位置和大小
    uint32_t ibmap_start;
    uint32_t ibmap_blks;
    uint32_t dbmap_start;
    uint32_t dbmap_blks;

    uint32_t inode_start;
    uint32_t inode_blks;
    uint32_t data_start;
    uint32_t data_blks;

    uint32_t root_ino;
    
    // 填充至 1024 字节
    uint8_t padding[MYFS_BLK_SIZE - (14 * sizeof(uint32_t))]; 
};
static_assert(sizeof(myfs_super_d) == 1024, "SuperBlock Size Mismatch");

// 磁盘 Inode: 占用 128 Bytes
struct myfs_inode_d {
    uint32_t ino; 
    uint32_t mode;        
    uint32_t size;  

    uint32_t atime;
    uint32_t mtime;
    uint32_t ctime;

    uint16_t uid;
    uint16_t gid;
    uint16_t link_count;
    
    uint32_t block[MYFS_DIRECT_BLOCKS]; 
    
    // 填充至 128 字节
    char padding[MYFS_INODE_DISK_SIZE - 56]; 
}; 
static_assert(sizeof(myfs_inode_d) == 128, "Inode Disk Size Mismatch");

// 磁盘目录项
// [重要] 这里不能改 string，必须是 POD 类型才能直接写入磁盘
struct myfs_dentry_d {
    uint32_t ino;
    uint16_t reclen;      // 记录长度
    uint8_t  namelen;     // 文件名长度
    uint8_t  file_type;   // 文件类型
    char     fname[MYFS_MAX_FILE_NAME];
}; 

#endif