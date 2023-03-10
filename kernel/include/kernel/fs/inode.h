#ifndef _KERNEL_FS_INODE_H
#define _KERNEL_FS_INODE_H 1

#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <kernel/fs/file.h>
#include <kernel/fs/mount.h>
#include <kernel/fs/super_block.h>
#include <kernel/fs/tnode.h>
#include <kernel/util/list.h>
#include <kernel/util/mutex.h>

struct fs_device;
struct hash_map;
struct inode;
struct pipe_data;
struct timeval;

struct inode_operations {
    struct inode *(*lookup)(struct inode *inode, const char *name);
    struct file *(*open)(struct inode *inode, int flags, int *error);
    int (*stat)(struct inode *inode, struct stat *stat_struct);
    int (*ioctl)(struct inode *inode, unsigned long request, void *argp);
    struct inode *(*mkdir)(struct tnode *tnode, const char *name, mode_t mode, int *error);
    struct inode *(*mknod)(struct tnode *tnode, const char *name, mode_t mode, dev_t device, int *error);
    int (*unlink)(struct tnode *tnode);
    int (*rmdir)(struct tnode *tnode);
    int (*chmod)(struct inode *inode, mode_t mode);
    int (*chown)(struct inode *inode, uid_t uid, gid_t gid);
    intptr_t (*mmap)(void *addr, size_t len, int prot, int flags, struct inode *inode, off_t offset);
    struct inode *(*symlink)(struct tnode *tnode, const char *name, const char *target, int *error);
    int (*link)(struct tnode *tnode, const char *name, const struct tnode *target);
    int (*read_all)(struct inode *inode, void *buffer);
    int (*utimes)(struct inode *inode, const struct timespec *times);
    void (*on_inode_destruction)(struct inode *inode);
    ssize_t (*read)(struct inode *inode, void *buffer, size_t size, off_t offset);
    int (*truncate)(struct inode *inode, off_t length);
    int (*sync)(struct inode *inode);
};

struct inode {
    mode_t mode;

#define FS_FILE   1U
#define FS_DIR    2U
#define FS_FIFO   4U
#define FS_SOCKET 8U
#define FS_LINK   16U
#define FS_DEVICE 32U
    unsigned int flags;

    struct inode_operations *i_op;
    struct super_block *super_block;

    struct socket *socket;

    uid_t uid;
    gid_t gid;

    struct timespec access_time;
    struct timespec modify_time;
    struct timespec change_time;

    /* Device id of filesystem */
    dev_t fsid;

    // Device id associated with the file
    dev_t device_id;

    // Only present for FIFO inodes that are currently opened.
    struct pipe_data *pipe_data;

    /* File system size */
    size_t size;

    /* Unique inode identifier (for the filesystem) */
    ino_t index;

    struct mount *mount;

    struct file_state file_state;

    // Marker of whether the inode's metadata is differnet from what is saved on disk.
    bool dirty : 1;

    // Delete inode when count is 0 (only applies to pipes right now)
    // Should be atomic
    int ref_count;

    // Count of the number of files open for this inode
    // When zero, the vfs will call i_op::all_files_closed()
    int open_file_count;

    // Dirent cache for path name resolution
    struct hash_map *dirent_cache;

    // Underlying vm_object (used for mmap)
    // Should be lazily initialized
    struct vm_object *vm_object;

    // Linked list of "dirty" inodes whose metadata must be written to disk
    struct list_node dirty_inodes;

    // Linked list for use by the super block.
    struct list_node sb_list;

    // Linked list of watcher objects (for UMessage Watch), synchronized by watchers_lock
    struct list_node watchers;
    spinlock_t watchers_lock;

    mutex_t lock;

    void *private_data;
};

#define inode_poll_wait(inode, flags, timeout) fs_poll_wait(&(inode)->file_state, &(inode)->lock, flags, timeout)

#endif /* _KERNEL_FS_INODE_H */
