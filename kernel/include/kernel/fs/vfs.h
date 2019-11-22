#ifndef _KERNEL_FS_VFS_H
#define _KERNEL_FS_VFS_H 1

#include <sys/types.h>
#include <sys/stat.h>

#include <kernel/fs/file_system.h>
#include <kernel/fs/inode.h>

void drop_inode_reference_unlocked(struct inode *inode);
void drop_inode_reference(struct inode *inode);
struct tnode *iname(const char *path);

int fs_create(const char *path, mode_t mode);
struct file *fs_open(const char *file_name, int flags, int *error);
int fs_close(struct file *file);
ssize_t fs_read(struct file *file, void *buffer, size_t len);
ssize_t fs_write(struct file *file, const void *buffer, size_t len);
off_t fs_seek(struct file *file, off_t offset, int whence);
long fs_tell(struct file *file);
int fs_stat(const char *file_name, struct stat *stat_struct);
int fs_ioctl(struct file *file, unsigned long request, void *argp);
int fs_truncate(struct file *file, off_t length);
int fs_mkdir(const char *path, mode_t mode);
int fs_create_pipe(struct file *pipe_files[2]);
int fs_unlink(const char *path);
int fs_rmdir(const char *path);
int fs_chmod(const char *path, mode_t mode);
int fs_access(const char *path, int mode);
int fs_fcntl(struct file *file, int command, int arg);
int fs_fstat(struct file *file, struct stat *stat_struct);
int fs_fchmod(struct file *file, mode_t mode);
intptr_t fs_mmap(void *addr, size_t length, int prot, int flags, struct file *file, off_t offset);
int fs_munmap(void *addr, size_t length);
int fs_rename(char *old_path, char *new_path);
int fs_mount(const char *src, const char *path, const char *type);

struct file *fs_clone(struct file *file);
struct file *fs_dup(struct file *file);

int fs_bind_socket_to_inode(struct inode *inode, unsigned long socket_id);

void load_fs(struct file_system *fs);

char *get_full_path(char *cwd, const char *relative_path);
char *get_tnode_path(struct tnode *tnode);

void init_vfs();

static inline int fs_mode_to_flags(mode_t mode) {
    if (S_ISREG(mode)) {
        return FS_FILE;
    } else if (S_ISDIR(mode)) {
        return FS_DIR;
    } else if (S_ISFIFO(mode)) {
        return FS_FIFO;
    } else if (S_ISOCK(mode)) {
        return FS_SOCKET;
    } else {
        return 0;
    }
}

#endif /* _KERNEL_FS_VFS_H */