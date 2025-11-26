#define _XOPEN_SOURCE 700

#include "myfs.h"
#include "utils.h"
#include <cstddef>

#define OPTION(t, p)        { t, offsetof(struct CustomOptions, p), 1 }

static const struct fuse_opt option_spec[] = {
	OPTION("--device=%s", device),
	FUSE_OPT_END
};

struct CustomOptions myfs_options;

// Wrappers
void* myfs_init(struct fuse_conn_info * conn_info) {
	FileSystem::Instance().mount(myfs_options.device);
	return NULL;
}

void myfs_destroy(void* p) {
	FileSystem::Instance().umount();
}

int myfs_mkdir(const char* path, mode_t mode) {
    return FileSystem::Instance().fuse_mkdir(path, mode);
}

int myfs_mknod(const char* path, mode_t mode, dev_t dev) {
    return FileSystem::Instance().fuse_mknod(path, mode, dev);
}

int myfs_getattr(const char* path, struct stat * st) {
    return FileSystem::Instance().fuse_getattr(path, st);
}

int myfs_readdir(const char * path, void * buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info * fi) {
    return FileSystem::Instance().fuse_readdir(path, buf, filler, offset, fi);
}

int myfs_write(const char* path, const char* buf, size_t size, off_t offset, struct fuse_file_info* fi) { 
    return FileSystem::Instance().fuse_write(path, buf, size, offset, fi);
}

int myfs_read(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi) { 
    return FileSystem::Instance().fuse_read(path, buf, size, offset, fi);
}

int myfs_utimens(const char* path, const struct timespec tv[2]) {
    return FileSystem::Instance().fuse_utimens(path, tv);
}

// Main
int main(int argc, char **argv)
{
    static struct fuse_operations operations;
    operations.init = myfs_init;
    operations.destroy = myfs_destroy;
    operations.mkdir = myfs_mkdir;
    operations.getattr = myfs_getattr;
    operations.readdir = myfs_readdir;
    operations.mknod = myfs_mknod;   // 注册 mknod
    operations.write = myfs_write;
    operations.read = myfs_read;     // 注册 read
    operations.utimens = myfs_utimens; // 注册 utimens

    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	myfs_options.device = strdup(""); 
	
    if (fuse_opt_parse(&args, &myfs_options, option_spec, NULL) == -1) return -1;
	
    int ret = fuse_main(args.argc, args.argv, &operations, NULL);
	fuse_opt_free_args(&args);
	return ret;
}