# 你的README文档
MyFS: 基于 FUSE 的 EXT2 类文件系统实现哈尔滨工业大学（深圳） | 操作系统 实验五作者: 杨帅 (2023311613) | 班级: 计算机6班📖 项目简介MyFS 是一个运行在用户空间（User Space）的文件系统，基于 FUSE (Filesystem in Userspace) 框架开发。本项目模拟了 EXT2 文件系统的核心磁盘布局和功能逻辑，旨在深入理解文件系统的底层原理，包括元数据管理、位图分配、目录项链接以及磁盘块映射等机制。本项目的一个显著特点是完全采用现代 C++ (C++17) 进行重构，摒弃了传统的 C 语言实现方式，利用 RAII、OOP 和强类型系统极大地提升了代码的鲁棒性、安全性和可维护性。✨ 核心特性 (Features)相比于传统的 C 语言实现，本项目引入了以下现代 C++ 特性：🛡️ 内存安全 (RAII)使用 std::vector<std::byte> 管理 I/O 缓冲区，利用作用域规则自动释放内存，彻底解决了 C 语言中常见的缓冲区溢出和内存泄漏问题。🧵 现代字符串处理摒弃 strtok/strcpy，使用 std::string 和 std::stringstream 处理路径解析。lookup 函数中的路径分割逻辑简洁安全，消除了指针悬挂风险。🏗️ 面向对象架构 (OOP)封装 FileSystem 类，将 Superblock、Bitmaps 等核心数据设为私有成员，防止全局变量被外部污染。使用单例模式管理文件系统实例，生命周期清晰可控。🔒 类型安全与静态检查使用 std::byte 明确区分二进制数据流与文本。引入 static_assert 在编译期强制检查 Inode 和 Dentry 结构体的大小与对齐（如确保 sizeof(myfs_inode_d) == 128），避免磁盘读写错位。🧩 混合编译支持通过 extern "C" 和 CMake 配置，实现了 C++ 核心逻辑与底层 C 语言驱动/FUSE 库的无缝链接。🏗️ 系统架构系统自上而下分为三层：接口层 (FUSE Interface)位于 yfs.cpp。实现 fuse_operations 结构体回调（如 myfs_read, myfs_write, myfs_mkdir），作为内核与用户态逻辑的桥梁。核心逻辑层 (Core Logic)位于 utils.cpp / FileSystem 类。元数据管理: Inode 与 Dentry 的内存构建、链接与磁盘同步 (sync_inode)。资源分配: 基于位图 (Bitmap) 的空闲块与 Inode 分配 (alloc_inode, alloc_data_block)。路径解析: 目录树遍历查找 (lookup)。I/O 抽象层 (Driver Adapter)实现 RMW (Read-Modify-Write) 机制。处理非 512B 对齐的读写请求，将用户逻辑偏移量映射为物理磁盘块号，确保数据完整性。💾 磁盘布局与限制Block Size: 512 Bytes (物理扇区大小)Inode Size: 128 Bytes (每块存储 8 个 Inode)布局:Superblock (1 Block)Inode Bitmap (1 Block)Data Bitmap (1 Block)Inode Blocks (83 Blocks)Data Blocks (剩余空间)容量限制: 最大支持 664 个文件（基于 Inode 数量限制），单文件最大 6.125 KB (直接索引模式)。🛠️ 关键功能实现1. 挂载与持久化 (Mount/Umount)Mount: 校验 Magic Number。若匹配则加载 Superblock 和 Bitmaps；若不匹配则自动格式化。Umount: 触发 fsync，将内存中的 Superblock、Bitmaps 和 Root Inode 强制刷回磁盘。2. 目录管理 (Lookup & Readdir)采用延迟加载策略：仅在需要时通过 read_inode 从磁盘加载 Inode 元数据。目录项在内存中通过 first_child 和 brother 指针构成链表。lookup 支持多级路径解析（如 /home/user/docs）。3. 文件读写 (Read/Write)Write: 自动计算跨越的块索引范围，动态分配缺少的 Data Block，并更新文件大小 (size) 和修改时间 (mtime)。Read: 映射逻辑偏移到物理块，对于文件空洞（未分配块）自动填充零。4. 原子性操作模拟Rename: 通过 Link New -> Unlink Old 模拟原子重命名。Truncate: 缩小文件时自动释放多余的数据块并清空对应的 Bitmap 位。🔨 编译与运行本项目使用 CMake 构建系统。环境要求Linux 环境 (Ubuntu/WSL2)C++17 编译器 (GCC/Clang)FUSE 开发库 (libfuse-dev)CMake编译步骤# 1. 创建构建目录
mkdir build && cd build

# 2. 生成 Makefile
cmake ..

# 3. 编译
make
运行文件系统准备镜像文件:# 创建一个全零的虚拟磁盘文件 (例如 4MB)
dd if=/dev/zero of=myfs.img bs=1M count=4
挂载:# 创建挂载点
mkdir mnt

# 运行 MyFS (前台运行模式，便于查看日志)
./myfs -f -s -d mnt
# 或者指定镜像文件路径 (视具体 main 函数实现而定)
# ./myfs myfs.img mnt
测试:另开一个终端，进入 mnt 目录进行标准文件操作 (ls, touch, echo "hello" > file, cat file 等)。卸载:fusermount -u mnt
✅ 测试结果项目通过了全部 34 个自动化测试用例，覆盖了以下场景：✅ 多级目录创建与删除✅ 文件读写与内容一致性校验✅ 挂载/卸载后的数据持久化✅ 位图分配与回收正确性Score: 34/34 (Test Pass!!!)Created for Operating System Course Lab 5, HIT-SZ.
