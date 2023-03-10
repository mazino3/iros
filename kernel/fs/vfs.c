#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/umessage.h>

#include <kernel/fs/cached_dirent.h>
#include <kernel/fs/dev.h>
#include <kernel/fs/file_system.h>
#include <kernel/fs/inode.h>
#include <kernel/fs/pipe.h>
#include <kernel/fs/vfs.h>
#include <kernel/fs/watch.h>
#include <kernel/hal/output.h>
#include <kernel/hal/processor.h>
#include <kernel/mem/inode_vm_object.h>
#include <kernel/mem/vm_allocator.h>
#include <kernel/net/socket.h>
#include <kernel/net/socket_syscalls.h>
#include <kernel/net/umessage.h>
#include <kernel/proc/task.h>
#include <kernel/time/clock.h>
#include <kernel/util/init.h>
#include <kernel/util/validators.h>

// #define INAME_DEBUG
// #define INODE_REF_COUNT_DEBUG
// #define VFS_DEBUG

static struct list_node file_systems = INIT_LIST(file_systems);
static struct mount *root;

struct inode *bump_inode_reference(struct inode *inode) {
    int fetched_ref_count = atomic_fetch_add(&inode->ref_count, 1);
    (void) fetched_ref_count;

#ifdef INODE_REF_COUNT_DEBUG
    debug_log("+Ref count: [ %lu, %llu, %d ]\n", inode->fsid, inode->index, fetched_ref_count + 1);
#endif /* INODE_REF_COUNT_DEBUG */

    assert(fetched_ref_count > 0);
    return inode;
}

void drop_inode_reference(struct inode *inode) {
    int fetched_ref_count = atomic_fetch_sub(&inode->ref_count, 1);

#ifdef INODE_REF_COUNT_DEBUG
    debug_log("-Ref count: [ %lu, %llu, %d ]\n", inode->fsid, inode->index, fetched_ref_count - 1);
#endif /* INODE_REF_COUNT_DEBUG */

    // Only delete inode if it's refcount is zero
    assert(fetched_ref_count > 0);
    if (fetched_ref_count == 1) {
#ifdef INODE_REF_COUNT_DEBUG
        debug_log("Destroying inode: [ %lu, %llu ]\n", inode->fsid, inode->index);
#endif /* INODE_REF_COUNT_DEBUG */

        // NOTE: all watchers should be removed when the event corresponding to the inode's destruction occurs.
        assert(list_is_empty(&inode->watchers));

        if (inode->i_op->on_inode_destruction) {
            inode->i_op->on_inode_destruction(inode);
        }

        if (inode->dirent_cache) {
            fs_destroy_dirent_cache(inode->dirent_cache);
        }

        free(inode);
        return;
    }
}

int inode_poll(struct file *file, struct wait_queue_entry *entry, int mask) {
    struct inode *inode = fs_file_inode(file);
    assert(inode);
    return fs_do_poll(entry, mask, &inode->file_state);
}

void inode_poll_finish(struct file *file, struct wait_queue_entry *entry) {
    struct inode *inode = fs_file_inode(file);
    assert(inode);
    fs_do_poll_finish(entry, &inode->file_state);
}

int fs_read_all_inode_with_buffer(struct inode *inode, void *buffer) {
    if (!inode->i_op->read_all) {
        return -EINVAL;
    }

    if (inode->i_op->lookup) {
        inode->i_op->lookup(inode, NULL);
    }

    if (inode->size == 0) {
        return 0;
    }

    return inode->i_op->read_all(inode, buffer);
}

int fs_read_all_inode(struct inode *inode, void **buffer, size_t *buffer_len) {
    if (!inode->i_op->read_all) {
        return -EINVAL;
    }

    if (inode->i_op->lookup) {
        inode->i_op->lookup(inode, NULL);
    }

    if (inode->size == 0) {
        *buffer = NULL;
        if (buffer_len) {
            *buffer_len = 0;
        }

        return 0;
    }

    *buffer = malloc(inode->size);
    if (!*buffer) {
        return -ENOMEM;
    }

    int ret = inode->i_op->read_all(inode, *buffer);
    if (ret < 0) {
        return ret;
    }

    if (buffer_len) {
        *buffer_len = inode->size;
    }

    return 0;
}

int fs_read_all_path(const char *path, void **buffer, size_t *buffer_len, struct tnode **tnode_out) {
    struct tnode *tnode = NULL;
    int ret = iname(path, 0, &tnode);
    if (ret < 0) {
        return ret;
    }

    struct inode *inode = tnode->inode;
    if (tnode_out) {
        *tnode_out = tnode;
    } else {
        drop_tnode(tnode);
    }

    assert(inode);
#ifdef VFS_DEBUG
    debug_log("reading from: [ %s ]\n", path);
#endif /* VFS_DEBUG */
    return fs_read_all_inode(inode, buffer, buffer_len);
}

struct file *fs_create_file(struct inode *inode, int type, int abilities, int flags, struct file_operations *operations, void *private) {
    struct file *file = calloc(1, sizeof(struct file));
    assert(file);

    if (inode) {
        file->inode = bump_inode_reference(inode);
        atomic_fetch_add(&file->inode->open_file_count, 1);
    }

    init_mutex(&file->lock);
    file->ref_count = 1;
    file->open_flags = flags;
    file->flags = type;
    file->abilities = abilities & FS_FILE_CANT_SEEK;
    file->f_op = operations;
    file->private_data = private;
    if (flags & O_RDWR) {
        file->abilities |= FS_FILE_CAN_WRITE | FS_FILE_CAN_READ;
    } else if (flags & O_RDONLY) {
        file->abilities |= FS_FILE_CAN_READ;
    } else if (flags & O_WRONLY) {
        file->abilities |= FS_FILE_CAN_WRITE;
    }

    return file;
}

void __fs_register_watcher(struct inode *inode, struct watcher *watcher) {
    list_append(&inode->watchers, &watcher->list_for_inode);
}

void __fs_unregister_watcher(struct inode *, struct watcher *watcher) {
    fs_lock_watcher(watcher);
    if (!watcher->removed_from_inode) {
        list_remove(&watcher->list_for_inode);
        watcher->removed_from_inode = true;
    }
    fs_unlock_watcher(watcher);
}

void fs_register_watcher(struct inode *inode, struct watcher *watcher) {
    spin_lock(&inode->watchers_lock);
    __fs_register_watcher(inode, watcher);
    spin_unlock(&inode->watchers_lock);
}

void fs_unregister_watcher(struct inode *inode, struct watcher *watcher) {
    spin_lock(&inode->watchers_lock);
    __fs_unregister_watcher(inode, watcher);
    spin_unlock(&inode->watchers_lock);
}

void fs_notify_watchers_inode_modified(struct inode *inode) {
    spin_lock(&inode->watchers_lock);
    list_for_each_entry(&inode->watchers, watcher, struct watcher, list_for_inode) {
        struct queued_umessage *umessage =
            net_create_umessage(UMESSAGE_WATCH, UMESSAGE_WATCH_INODE_MODIFIED, 0, sizeof(struct umessage_watch_inode_modified), NULL);

        struct umessage_watch_inode_modified *event = (void *) &umessage->message;
        event->identifier = watcher->identifier;

        fs_notify_watcher(watcher, umessage);
        net_drop_umessage(umessage);
    }
    spin_unlock(&inode->watchers_lock);
}

void fs_notify_watchers_inode_removed(struct inode *inode) {
    spin_lock(&inode->watchers_lock);
    list_for_each_entry_safe(&inode->watchers, watcher, struct watcher, list_for_inode) {
        struct queued_umessage *umessage =
            net_create_umessage(UMESSAGE_WATCH, UMESSAGE_WATCH_INODE_REMOVED, 0, sizeof(struct umessage_watch_inode_removed), NULL);

        struct umessage_watch_inode_modified *event = (void *) &umessage->message;
        event->identifier = watcher->identifier;

        fs_notify_watcher(watcher, umessage);
        net_drop_umessage(umessage);

        __fs_unregister_watcher(inode, watcher);
    }
    spin_unlock(&inode->watchers_lock);
}

void fs_notify_watcher(struct watcher *watcher, struct queued_umessage *umessage) {
    net_post_umessage_to(watcher->queue, umessage);
}

struct inode *fs_create_inode(struct super_block *sb, ino_t id, uid_t uid, gid_t gid, mode_t mode, size_t size,
                              struct inode_operations *ops, void *private) {
    struct inode *inode = fs_create_inode_without_sb(sb->fsid, id, uid, gid, mode, size, ops, private);
    inode->super_block = sb;
    inode->file_state.poll_flags &= ~(!!(sb->flags & ST_RDONLY) ? POLLOUT : 0);
    return inode;
}

struct inode *fs_create_inode_without_sb(dev_t fsid, ino_t id, uid_t uid, gid_t gid, mode_t mode, size_t size, struct inode_operations *ops,
                                         void *private) {
    struct inode *inode = calloc(1, sizeof(struct inode));
    assert(inode);

    inode->access_time = inode->change_time = inode->modify_time = time_read_clock(CLOCK_REALTIME);
    inode->flags = fs_mode_to_flags(mode);
    inode->fsid = fsid;
    inode->gid = gid;
    inode->i_op = ops;
    inode->index = id;
    init_mutex(&inode->lock);
    init_list(&inode->watchers);
    init_spinlock(&inode->watchers_lock);
    inode->mode = mode;
    inode->private_data = private;
    inode->ref_count = 1;
    inode->size = size;
    inode->uid = uid;
    init_file_state(&inode->file_state, !!size, true);

    if (inode->flags & FS_DIR) {
        inode->dirent_cache = fs_create_dirent_cache();
    }
    return inode;
}

static struct tnode *t_root;

struct tnode *fs_root(void) {
    return t_root;
}

bool fs_is_mount_point(struct inode *inode) {
    return !!inode->mount;
}

static struct inode *maybe_cross_mount_point(struct inode *inode, struct mount **mount_if_present) {
    if (!inode->mount) {
        return inode;
    }

    if (mount_if_present) {
        *mount_if_present = inode->mount;
    }
    return inode->mount->super_block->root;
}

static int do_iname(const char *_path, int flags, struct tnode *t_root, struct mount **current_mount, struct tnode *t_parent,
                    struct tnode **result, int *depth) {
    if (*depth >= 25) {
        return -ELOOP;
    }

#ifdef INAME_DEBUG
    debug_log("path: [ %s ]\n", _path);
#endif /* INAME_DEBUG */

    assert(t_root->inode != NULL);
    assert(t_root->inode->i_op != NULL);

    char *path = flags & INAME_TAKE_OWNERSHIP_OF_PATH ? (char *) _path : strdup(_path);
    char *save_path = path;

    struct tnode *parent;
    if (_path[0] != '/') {
        path--;
        parent = bump_tnode(t_parent);
        *current_mount = t_parent->mount;
    } else {
        parent = bump_tnode(t_root);
        *current_mount = t_root->mount;
    }

    char *last_slash = strchr(path + 1, '/');

    struct process *process = get_current_task()->process;
    uid_t uid_to_check = (flags & INAME_CHECK_PERMISSIONS_WITH_REAL_UID_AND_GID) ? process->uid : process->euid;
    gid_t gid_to_check = (flags & INAME_CHECK_PERMISSIONS_WITH_REAL_UID_AND_GID) ? process->gid : process->egid;

    /* Main VFS Loop */
    while (parent != NULL && path != NULL && path[1] != '\0') {
        if (parent->inode->flags & FS_LINK) {
            if (parent->inode->i_op->lookup) {
                parent->inode->i_op->lookup(parent->inode, NULL);
            }

            if (parent->inode->size == 0 || !parent->inode->i_op->read_all) {
                free(save_path);
                drop_tnode(parent);
                return -ENOENT;
            }

            char *link_path = malloc(parent->inode->size + 1);
            int ret = parent->inode->i_op->read_all(parent->inode, link_path);
            if (ret < 0) {
                free(link_path);
                free(save_path);
                drop_tnode(parent);
                return ret;
            }
            link_path[parent->inode->size] = '\0';

            if (strcmp(parent->name, link_path) == 0) {
                free(link_path);
                free(save_path);
                drop_tnode(parent);
                return -ELOOP;
            }

            (*depth)++;
            struct tnode *parent_to_pass = bump_tnode(parent->parent);
            drop_tnode(parent);
            ret = do_iname(link_path, flags | INAME_TAKE_OWNERSHIP_OF_PATH, t_root, current_mount, parent_to_pass, &parent, depth);
            drop_tnode(parent_to_pass);
            if (ret < 0) {
                free(save_path);
                return ret;
            }

            continue;
        }

        if (!fs_can_execute_inode_impl(parent->inode, uid_to_check, gid_to_check)) {
            drop_tnode(parent);
            free(save_path);
            return -EACCES;
        }

        /* Exit if we're trying to lookup past a file */
        if (!(parent->inode->flags & FS_DIR)) {
            drop_tnode(parent);
            free(save_path);
            return -ENOTDIR;
        }

        /* Check for . and .. */
        if (path[1] == '.' && (path[2] == '/' || path[2] == '\0')) {
            /* Dot means current directory so just skip tasking */
            goto vfs_loop_end;
        }

        if (path[1] == '.' && path[2] == '.' && (path[3] == '/' || path[3] == '\0')) {
            if (parent == t_root) {
                goto vfs_loop_end;
            }

            /* Simply go to parent */
            struct tnode *next = bump_tnode(parent->parent);
            drop_tnode(parent);
            parent = next;
            goto vfs_loop_end;
        }

        /* Ensures passed name will be correct */
        if (last_slash != NULL) {
            *last_slash = '\0';
        }

        /* Check using lookup */
        assert(parent->inode);
        assert(parent->inode->i_op);
        assert(parent->inode->i_op->lookup);

#ifdef INAME_DEBUG
        debug_log("looking up: [ %s, %s ]\n", parent->name, path + 1);
#endif /* INAME_DEBUG */
        struct inode *inode = parent->inode->i_op->lookup(parent->inode, path + 1);
        if (!inode) {
            drop_tnode(parent);
            parent = NULL;
            break;
        }

        bool trailing_component = !last_slash || last_slash[1] == '\0';

        if (!trailing_component || !(flags & INAME_DONT_FOLLOW_TRAILING_MOUNT_POINT)) {
            inode = maybe_cross_mount_point(inode, current_mount);
        }
        parent = create_tnode(path + 1, parent, inode, *current_mount);

    vfs_loop_end:
        path = last_slash;
        if (path != NULL) {
            path[0] = '/';
            last_slash = strchr(path + 1, '/');
        }
    }

    if (parent == NULL) {
        /* Couldn't find what we were looking for */
        free(save_path);
        return -ENOENT;
    }

    struct inode *inode = parent->inode;

    /* Shouldn't let you at a / at the end of a file name or root (but only if the path is // and not /./) */
    if ((path != NULL && path >= save_path && path[0] == '/') &&
        ((inode->flags & FS_FILE) || (parent->parent == NULL && strlen(_path) == 2))) {
        free(save_path);
        drop_tnode(parent);
        return -ENOENT;
    }

    free(save_path);

    if ((flags & INAME_DONT_FOLLOW_TRAILING_SYMLINK) || !(inode->flags & FS_LINK)) {
        *result = parent;
        return 0;
    }

    if (inode->i_op->lookup) {
        inode->i_op->lookup(inode, NULL);
    }

    if (inode->size == 0 || !inode->i_op->read_all) {
        drop_tnode(parent);
        return -ENOENT;
    }

    char *sym_path = malloc(inode->size + 1);

    int ret = inode->i_op->read_all(inode, sym_path);
    if (ret < 0) {
        free(sym_path);
        drop_tnode(parent);
        return ret;
    }
    sym_path[inode->size] = '\0';

    if (strcmp(parent->name, sym_path) == 0) {
        free(sym_path);
        drop_tnode(parent);
        return -ELOOP;
    }

    (*depth)++;
    struct tnode *parent_to_pass = bump_tnode(parent->parent);
    drop_tnode(parent);
    ret = do_iname(sym_path, flags | INAME_TAKE_OWNERSHIP_OF_PATH, t_root, current_mount, parent_to_pass, result, depth);
    drop_tnode(parent_to_pass);

    return ret;
}

int iname(const char *_path, int flags, struct tnode **result) {
    struct task *current = get_current_task();
    struct tnode *cwd = NULL;
    if (current && current->process && current->process->cwd) {
        cwd = current->process->cwd;
    }
    return iname_with_base(cwd, _path, flags, result);
}

int iname_with_base(struct tnode *base, const char *_path, int flags, struct tnode **result) {
    assert(root != NULL);
    assert(root->super_block != NULL);

    struct tnode *t_root = fs_root();
    if (t_root == NULL) {
        return -ENOENT;
    }

    if (!base) {
        base = t_root;
    }

    int depth_storage = 0;
    struct mount *mount_storage = NULL;
    return do_iname(_path, flags, t_root, &mount_storage, base, result, &depth_storage);
}

static int do_truncate(struct inode *inode, off_t length) {
    if (!inode) {
        return 0;
    }

    if (inode->flags & FS_DIR) {
        return -EISDIR;
    }

    if (!(inode->flags & FS_FILE)) {
        return 0;
    }

    if (inode->super_block && (inode->super_block->flags & ST_RDONLY)) {
        return -EROFS;
    }

    if (!inode->i_op->truncate) {
        return -EPERM;
    }

    if (inode->size == (size_t) length) {
        return 0;
    }

    return inode->i_op->truncate(inode, length);
}

struct file *fs_openat(struct tnode *base, const char *file_name, int flags, mode_t mode, int *error) {
    if (file_name == NULL) {
        *error = -EINVAL;
        return NULL;
    }

    struct tnode *tnode;
    int ret = iname_with_base(base, file_name, flags & O_NOFOLLOW ? INAME_DONT_FOLLOW_TRAILING_SYMLINK : 0, &tnode);
    bool created_file = false;
    if (ret == -ENOENT) {
        if (flags & O_CREAT) {
#ifdef VFS_DEBUG
            debug_log("Creating file: [ %s ]\n", file_name);
#endif /* VFS_DEBUG */

            ret = 0;
            tnode = fs_mknod(file_name, (mode & 07777) | S_IFREG, 0, &ret);
            if (ret < 0) {
                *error = ret;
                return NULL;
            }
            created_file = true;
            assert(tnode);
        } else {
#ifdef VFS_DEBUG
            debug_log("File Not Found: [ %s ]\n", file_name);
#endif /* VFS_DEBUG */
            *error = ret;
            return NULL;
        }
    } else if (ret < 0) {
        *error = ret;
        return NULL;
    } else if (flags & O_EXCL) {
        drop_tnode(tnode);
        *error = -EEXIST;
        return NULL;
    }

    if (tnode->inode->flags & FS_LINK) {
        assert(flags & O_NOFOLLOW);
        drop_tnode(tnode);
        *error = -ELOOP;
        return NULL;
    }

    if (tnode->inode->flags & FS_SOCKET) {
        drop_tnode(tnode);
        *error = -ENXIO;
        return NULL;
    }

    if (!(tnode->inode->flags & FS_DIR) && (flags & O_DIRECTORY)) {
        drop_tnode(tnode);
        *error = -ENOTDIR;
        return NULL;
    }

    if (tnode->inode->flags & FS_DIR && !(flags & O_DIRECTORY)) {
        drop_tnode(tnode);
        *error = -EISDIR;
        return NULL;
    }

    if (tnode->inode->i_op->lookup) {
        tnode->inode->i_op->lookup(tnode->inode, NULL);
    }

    // Permission checks are ignored if the file was just created.
    if (!created_file) {
        if (flags & O_RDWR) {
            if (!fs_can_read_inode(tnode->inode) || !fs_can_write_inode(tnode->inode)) {
                drop_tnode(tnode);
                *error = -EACCES;
                return NULL;
            }
        } else if (flags & O_RDONLY) {
            if (!fs_can_read_inode(tnode->inode)) {
                drop_tnode(tnode);
                *error = -EACCES;
                return NULL;
            }
        } else if (flags & O_WRONLY) {
            if (!fs_can_write_inode(tnode->inode)) {
                drop_tnode(tnode);
                *error = -EACCES;
                return NULL;
            }
        }
    }

    if (((flags & O_RDWR) || (flags & O_WRONLY)) && (tnode->inode->super_block && (tnode->inode->super_block->flags & ST_RDONLY))) {
        drop_tnode(tnode);
        *error = -EROFS;
        return NULL;
    }

    struct file *file = NULL;
    if (tnode->inode->flags & FS_DEVICE) {
        file = dev_open(tnode->inode, flags, error);
    } else if (tnode->inode->flags & FS_FIFO) {
        file = pipe_open(tnode->inode, flags, error);
    } else {
        if (flags & O_TRUNC) {
            ret = do_truncate(tnode->inode, 0);
            if (ret) {
                drop_tnode(tnode);
                *error = ret;
                return NULL;
            }
        }

        file = tnode->inode->i_op->open(tnode->inode, flags, error);
    }

    if (file == NULL) {
        drop_tnode(tnode);
        return file;
    }

    if (file->tnode) {
        // NOTE: some calls to open act like a symlink (e.g. /dev/tty), and thus specify their own tnode.
        drop_tnode(tnode);
    } else {
        file->tnode = tnode;
    }

    if ((flags & O_APPEND) && !(file->abilities & FS_FILE_CANT_SEEK)) {
        fs_seek(file, 0, SEEK_END);
    }

    return file;
}

/* Should potentially remove inode from inode_store */
int fs_close(struct file *file) {
    assert(file);

    struct inode *inode = fs_file_inode(file);

    int fetched_ref_count = atomic_fetch_sub(&file->ref_count, 1);
    assert(fetched_ref_count > 0);
    if (fetched_ref_count == 1) {
        bool all_files_closed = false;
        if (inode) {
            int old_open_file_count = atomic_fetch_sub(&inode->open_file_count, 1);
            all_files_closed = old_open_file_count == 1;
            drop_inode_reference(inode);
        }

        if (file->tnode) {
            drop_tnode(file->tnode);
        }

        int error = 0;
        if (file->f_op->close) {
            error = file->f_op->close(file);
        }

        free(file);

        if (all_files_closed && (inode->flags & FS_FIFO)) {
            pipe_all_files_closed(inode);
        }

        return error;
    }

    return 0;
}

/* Default dir read: works for file systems completely cached in memory */
static ssize_t default_dir_read(struct file *file, void *buffer, size_t len) {
    if (len != sizeof(struct dirent)) {
        return -EINVAL;
    }

    struct inode *inode = maybe_cross_mount_point(fs_file_inode(file), NULL);

    struct dirent *entry = (struct dirent *) buffer;
    if (file->position == 0) {
        entry->d_ino = inode->index;
        strcpy(entry->d_name, ".");
        file->position++;
        return len;
    } else if (file->position == 1) {
        entry->d_ino = file->tnode->parent ? file->tnode->parent->inode->index : inode->index;
        strcpy(entry->d_name, "..");
        file->position++;
        return len;
    }

    assert(inode->i_op->lookup);
    inode->i_op->lookup(inode, NULL);

    mutex_lock(&inode->lock);
    struct cached_dirent *tnode = fs_lookup_in_cache_with_index(inode->dirent_cache, file->position - 2);

    if (!tnode) {
        mutex_unlock(&inode->lock);
        return 0;
    }

    file->position++;

    entry->d_ino = tnode->inode->index;
    strcpy(entry->d_name, tnode->name);

    mutex_unlock(&inode->lock);

    return (ssize_t) len;
}

ssize_t fs_read(struct file *file, void *buffer, size_t len) {
    assert(file);
    assert(file->f_op);
    if (!(file->abilities & FS_FILE_CAN_READ)) {
        return -EBADF;
    }

    if (len == 0) {
        return 0;
    }

    if (file->f_op->read) {
        if (file->abilities & FS_FILE_CANT_SEEK) {
            return file->f_op->read(file, 0, buffer, len);
        }

        ssize_t ret = file->f_op->read(file, file->position, buffer, len);
        if (ret > 0) {
            file->position += ret;
        }

        return ret;
    }

    if (file->flags & FS_DIR) {
        return default_dir_read(file, buffer, len);
    }

    return -EINVAL;
}

ssize_t fs_pread(struct file *file, void *buffer, size_t len, off_t offset) {
    if (!(file->abilities & FS_FILE_CAN_READ)) {
        return -EBADF;
    }

    if (file->abilities & FS_FILE_CANT_SEEK) {
        return -ESPIPE;
    }

    if (len == 0) {
        return 0;
    }

    assert(file);
    assert(file->f_op);
    if (file->f_op->read) {
        return file->f_op->read(file, offset, buffer, len);
    }

    return -EINVAL;
}

ssize_t fs_write(struct file *file, const void *buffer, size_t len) {
    if (!(file->abilities & FS_FILE_CAN_WRITE)) {
        return -EBADF;
    }

    if (len == 0) {
        return 0;
    }

    if (file->f_op->write) {
        if (file->abilities & FS_FILE_CANT_SEEK) {
            return file->f_op->write(file, 0, buffer, len);
        }

        if (file->open_flags & O_APPEND) {
            // FIXME: do this race condition free
            fs_seek(file, 0, SEEK_END);
        }

        ssize_t ret = file->f_op->write(file, file->position, buffer, len);
        if (ret > 0) {
            assert(file->inode);
            fs_notify_watchers_inode_modified(file->inode);
            file->position += ret;
        }

        return ret;
    }

    return -EINVAL;
}

ssize_t fs_pwrite(struct file *file, const void *buffer, size_t len, off_t offset) {
    if (!(file->abilities & FS_FILE_CAN_WRITE)) {
        return -EBADF;
    }

    if (file->abilities & FS_FILE_CANT_SEEK) {
        return -ESPIPE;
    }

    if (len == 0) {
        return 0;
    }

    if (file->f_op->write) {
        return file->f_op->write(file, offset, buffer, len);
    }

    return -EINVAL;
}

ssize_t fs_readv(struct file *file, const struct iovec *vec, int item_count) {
    ssize_t ret = 0;

    // FIXME: this function should lock the file or inode somehow
    for (int i = 0; i < item_count; i++) {
        VALIDATE(vec[i].iov_base, vec[i].iov_len, validate_write);
        ssize_t res = fs_read(file, vec[i].iov_base, vec[i].iov_len);
        if (res < 0) {
            return res;
        }

        ret += res;
        if (res < (ssize_t) vec[i].iov_len) {
            break;
        }
    }

    return ret;
}

ssize_t fs_writev(struct file *file, const struct iovec *vec, int item_count) {
    ssize_t ret = 0;

    // FIXME: this function should lock the file or inode somehow
    for (int i = 0; i < item_count; i++) {
        VALIDATE(vec[i].iov_base, vec[i].iov_len, validate_read);
        ssize_t res = fs_write(file, vec[i].iov_base, vec[i].iov_len);
        if (res < 0) {
            return res;
        }

        ret += res;
        if (res < (ssize_t) vec[i].iov_len) {
            break;
        }
    }

    return ret;
}

off_t fs_seek(struct file *file, off_t offset, int whence) {
    if (file->abilities & FS_FILE_CANT_SEEK) {
        return -ESPIPE;
    }

    off_t new_position;
    if (whence == SEEK_SET) {
        new_position = offset;
    } else if (whence == SEEK_CUR) {
        new_position = file->position + offset;
    } else if (whence == SEEK_END) {
        struct inode *inode = fs_file_inode(file);
        new_position = inode->size + offset;
    } else {
        debug_log("Invalid arguments for fs_seek - whence: %d\n", whence);
        return -EINVAL;
    }

    file->position = new_position;
    return new_position;
}

long fs_tell(struct file *file) {
    return file->position;
}

size_t fs_file_size(struct file *file) {
    struct inode *inode = fs_file_inode(file);
    if (!inode) {
        return 0;
    }

    if (inode->i_op->lookup) {
        inode->i_op->lookup(inode, NULL);
    }
    return inode->size;
}

void register_fs(struct file_system *fs) {
    assert(fs);

    debug_log("Registering file system: [ %s ]\n", fs->name);

    assert(fs->mount);
    assert(fs->umount);

    list_append(&file_systems, &fs->list);
}

static int do_stat(struct inode *inode, struct stat *stat_struct) {
    stat_struct->st_rdev = 0;
    stat_struct->st_nlink = 1;

    if (inode->i_op->lookup) {
        inode->i_op->lookup(inode, NULL);
    }

    stat_struct->st_dev = inode->fsid;
    stat_struct->st_rdev = inode->device_id;
    stat_struct->st_ino = inode->index;
    stat_struct->st_mode = inode->mode;
    stat_struct->st_uid = inode->uid;
    stat_struct->st_gid = inode->gid;
    stat_struct->st_size = inode->size;
    stat_struct->st_atim = inode->access_time;
    stat_struct->st_ctim = inode->modify_time;
    stat_struct->st_mtim = inode->change_time;

    // There's no super block for some generated inode, like dynamic master/slave pseudo terminals
    if (inode->super_block) {
        stat_struct->st_blksize = inode->super_block->block_size;
        stat_struct->st_blocks = (inode->size + inode->super_block->block_size - 1) / inode->super_block->block_size;
    } else {
        stat_struct->st_blksize = PAGE_SIZE;
        stat_struct->st_blocks = 0;
    }

    if (inode->i_op->stat) {
        int ret = inode->i_op->stat(inode, stat_struct);
        if (ret < 0) {
            return ret;
        }
    }

    return 0;
}

int fs_fstatat(struct tnode *base, const char *file_name, struct stat *stat_struct, int flags) {
    if (*file_name == '\0' && (flags & AT_EMPTY_PATH)) {
        return do_stat(base->inode, stat_struct);
    }

    if (!file_name) {
        return -EFAULT;
    }

    if (!(base->inode->flags & FS_DIR)) {
        return -ENOTDIR;
    }

    struct tnode *tnode;
    int ret = iname_with_base(base, file_name, (flags & AT_SYMLINK_NOFOLLOW) ? INAME_DONT_FOLLOW_TRAILING_SYMLINK : 0, &tnode);
    if (ret < 0) {
        return ret;
    }

    struct inode *inode = tnode->inode;
    drop_tnode(tnode);
    return do_stat(inode, stat_struct);
}

int fs_ioctl(struct file_descriptor *desc, unsigned long request, void *argp) {
    switch (request) {
        case FIONBIO: {
            if (validate_read(argp, sizeof(int))) {
                return -EFAULT;
            }
            int value = *(int *) argp;
            if (!value) {
                desc->file->open_flags &= ~O_NONBLOCK;
            } else {
                desc->file->open_flags |= O_NONBLOCK;
            }
            return 0;
        }
        case FIOCLEX:
            desc->fd_flags |= FD_CLOEXEC;
            return 0;
        case FIONCLEX:
            desc->fd_flags &= ~FD_CLOEXEC;
            return 0;
        default:
            break;
    }

    if (desc->file->flags & FS_SOCKET) {
        return net_socket_ioctl(desc->file, request, argp);
    }

    struct inode *inode = fs_file_inode(desc->file);
    if (!inode) {
        return -ENOTTY;
    }

    if (inode->flags & FS_DEVICE) {
        return dev_ioctl(desc->file, request, argp);
    }

    if (inode->i_op->ioctl) {
        return inode->i_op->ioctl(inode, request, argp);
    }

    return -ENOTTY;
}

int fs_ftruncate(struct file *file, off_t length) {
    assert(file);

    struct inode *inode = fs_file_inode(file);
    return do_truncate(inode, length);
}

int fs_truncate(const char *path, off_t length) {
    struct tnode *tnode;
    int ret = iname(path, 0, &tnode);
    if (ret) {
        return ret;
    }

    struct inode *inode = tnode->inode;
    ret = do_truncate(inode, length);
    drop_tnode(tnode);
    return ret;
}

struct tnode *fs_mkdir(const char *_path, mode_t mode, int *error) {
    mode &= ~get_current_task()->process->umask;

    size_t length = strlen(_path);
    char *path = malloc(length + 1);
    memcpy(path, _path, length);
    path[length] = '\0';

    char *last_slash;
    do {
        last_slash = strrchr(path, '/');
    } while (last_slash && (last_slash == path + length - 1) && (*last_slash = '\0', length--, 1));
    struct tnode *tparent;

    int ret = 0;
    if (last_slash == path) {
        tparent = bump_tnode(fs_root());
    } else if (last_slash == NULL) {
        tparent = bump_tnode(get_current_task()->process->cwd);
        last_slash = path - 1;
    } else {
        *last_slash = '\0';
        ret = iname(path, 0, &tparent);
        if (ret < 0) {
            free(path);
            *error = ret;
            return NULL;
        }
    }

    if (!(tparent->inode->flags & FS_DIR)) {
        drop_tnode(tparent);
        free(path);
        *error = -ENOTDIR;
        return NULL;
    }

    tparent->inode->i_op->lookup(tparent->inode, NULL);
    if (fs_lookup_in_cache(tparent->inode->dirent_cache, last_slash + 1) != NULL) {
        free(path);
        drop_tnode(tparent);
        *error = -EEXIST;
        return NULL;
    }

    if (!tparent->inode->i_op->mkdir) {
        free(path);
        drop_tnode(tparent);
        *error = -EINVAL;
        return NULL;
    }

#ifdef VFS_DEBUG
    debug_log("Adding dir to: [ %s ]\n", tparent->name);
#endif /* VFS_DEBUG */

    struct inode *inode = tparent->inode->i_op->mkdir(tparent, last_slash + 1, mode | S_IFDIR, error);
    if (inode == NULL) {
        drop_tnode(tparent);
        free(path);
        return NULL;
    }

    fs_put_dirent_cache(tparent->inode->dirent_cache, inode, last_slash + 1, strlen(last_slash + 1));

    struct tnode *tnode = create_tnode(last_slash + 1, tparent, inode, tparent->mount);
    free(path);
    return tnode;
}

struct tnode *fs_mknod(const char *_path, mode_t mode, dev_t device, int *error) {
    if (((mode & 0770000) == 0)) {
        mode |= S_IFREG;
    }

    if (S_ISDIR(mode)) {
        return fs_mkdir(_path, mode, error);
    }
    if (S_ISLNK(mode)) {
        *error = -EINVAL;
        return NULL;
    }

    mode &= ~get_current_task()->process->umask;

    size_t length = strlen(_path);
    char *path = malloc(length + 1);
    memcpy(path, _path, length);
    path[length] = '\0';

    char *last_slash;
    do {
        last_slash = strrchr(path, '/');
    } while (last_slash && (last_slash == path + length - 1) && (*last_slash = '\0', length--, 1));
    struct tnode *tparent;

    int ret = 0;
    if (last_slash == path) {
        tparent = bump_tnode(fs_root());
    } else if (last_slash == NULL) {
        tparent = bump_tnode(get_current_task()->process->cwd);
        last_slash = path - 1;
    } else {
        *last_slash = '\0';
        ret = iname(path, 0, &tparent);
        if (ret < 0) {
            free(path);
            *error = ret;
            return NULL;
        }
    }

    if (!(tparent->inode->flags & FS_DIR)) {
        drop_tnode(tparent);
        free(path);
        *error = -ENOTDIR;
        return NULL;
    }

    tparent->inode->i_op->lookup(tparent->inode, NULL);
    if (fs_lookup_in_cache(tparent->inode->dirent_cache, last_slash + 1) != NULL) {
        free(path);
        drop_tnode(tparent);
        *error = -EEXIST;
        return NULL;
    }

    if (!tparent->inode->i_op->mknod) {
        free(path);
        drop_tnode(tparent);
        *error = -EINVAL;
        return NULL;
    }

#ifdef VFS_DEBUG
    debug_log("Adding node to: [ %s, %#.5lX ]\n", tparent->name, device);
#endif /* VFS_DEBUG */

    struct inode *inode = tparent->inode->i_op->mknod(tparent, last_slash + 1, mode, device, error);
    if (inode == NULL) {
        drop_tnode(tparent);
        free(path);
        return NULL;
    }

    fs_put_dirent_cache(tparent->inode->dirent_cache, inode, last_slash + 1, strlen(last_slash + 1));

    struct tnode *tnode = create_tnode(last_slash + 1, tparent, inode, tparent->mount);
    free(path);
    return tnode;
}

int fs_create_pipe(struct file *pipe_files[2]) {
    struct inode *pipe_inode = pipe_new_inode();
    int error = 0;
    pipe_files[0] = pipe_inode->i_op->open(pipe_inode, O_RDONLY | O_NONBLOCK, &error);
    if (error != 0) {
        return error;
    }
    // Passing O_NONBLOCK to open is a hack to make sure that the opening of the pipe does not
    // block until the otherside is open (which it would do it were a named FIFO), since the
    // other end of the pipe is about to be created.
    pipe_files[0]->open_flags &= ~O_NONBLOCK;

    pipe_files[1] = pipe_inode->i_op->open(pipe_inode, O_WRONLY, &error);
    if (error != 0) {
        fs_close(pipe_files[0]);
        return error;
    }

    return 0;
}

int fs_unlink(const char *path, bool ignore_permission_checks) {
    assert(path);

#ifdef VFS_DEBUG
    debug_log("Unlinking: [ %s ]\n", path);
#endif /* VFS_DEBUG */

    struct tnode *tnode;
    int ret = iname(path, INAME_DONT_FOLLOW_TRAILING_SYMLINK, &tnode);
    if (ret < 0) {
        return ret;
    }

    // Can't remove a unix socket that is being currently being used
    if (tnode->inode->socket != NULL) {
        drop_tnode(tnode);
        return -EBUSY;
    }

    struct inode *parent = tnode->parent ? tnode->parent->inode : NULL;
    if (parent && !ignore_permission_checks && !fs_can_write_inode(parent)) {
        drop_tnode(tnode);
        return -EACCES;
    }

    if (tnode->inode->flags & FS_DIR) {
        drop_tnode(tnode);
        return -EISDIR;
    }

    if (tnode->inode->i_op->unlink == NULL) {
        drop_tnode(tnode);
        return -EINVAL;
    }

    mutex_lock(&tnode->inode->lock);
    ret = tnode->inode->i_op->unlink(tnode);
    if (ret != 0) {
        mutex_unlock(&tnode->inode->lock);
        drop_tnode(tnode);
        return ret;
    }
    mutex_unlock(&tnode->inode->lock);

    fs_del_dirent_cache(tnode->parent->inode->dirent_cache, tnode->name);
    fs_notify_watchers_inode_removed(tnode->inode);
    drop_inode_reference(tnode->inode);
    drop_tnode(tnode);
    return 0;
}

static bool dir_empty(struct inode *inode) {
    assert(inode);
    assert(inode->flags & FS_DIR);

    inode->i_op->lookup(inode, NULL);
    return fs_get_dirent_cache_size(inode->dirent_cache) == 0;
}

int fs_rmdir(const char *path) {
    assert(path);

    struct tnode *tnode;
    int ret = iname(path, INAME_DONT_FOLLOW_TRAILING_SYMLINK, &tnode);
    if (ret < 0) {
        return -ENOENT;
    }

    if (!(tnode->inode->flags & FS_DIR)) {
        drop_tnode(tnode);
        return -ENOTDIR;
    }

    struct inode *parent = tnode->parent->inode;
    if (parent && !fs_can_write_inode(parent)) {
        drop_tnode(tnode);
        return -EACCES;
    }

    if (fs_is_mount_point(tnode->inode)) {
        drop_tnode(tnode);
        return -EBUSY;
    }

    if (!dir_empty(tnode->inode)) {
        drop_tnode(tnode);
        return -ENOTEMPTY;
    }

    if (tnode->inode->i_op->rmdir == NULL) {
        drop_tnode(tnode);
        return -EINVAL;
    }

    mutex_lock(&tnode->inode->lock);

    ret = tnode->inode->i_op->rmdir(tnode);
    if (ret != 0) {
        drop_tnode(tnode);
        return ret;
    }

    fs_del_dirent_cache(tnode->parent->inode->dirent_cache, tnode->name);
    drop_tnode(tnode);
    return 0;
}

static int do_chown(struct inode *inode, uid_t uid, gid_t gid) {
    if (!inode->i_op->chown) {
        return -EPERM;
    }

    if (get_current_task()->process->euid != 0) {
        return -EPERM;
    }

    if (uid == (uid_t) -1) {
        uid = inode->uid;
    }

    if (gid == (gid_t) -1) {
        gid = inode->gid;
    }

    if (inode->uid == uid && inode->gid == gid) {
        return 0;
    }

    return inode->i_op->chown(inode, uid, gid);
}

int fs_fchownat(struct tnode *base, const char *path, uid_t uid, gid_t gid, int flags) {
    if (*path == '\0' && (flags & AT_EMPTY_PATH)) {
        return do_chown(base->inode, uid, gid);
    }

    struct tnode *tnode;
    int ret = iname_with_base(base, path, (flags & AT_SYMLINK_NOFOLLOW) ? INAME_DONT_FOLLOW_TRAILING_SYMLINK : 0, &tnode);
    if (ret < 0) {
        return ret;
    }

    ret = do_chown(tnode->inode, uid, gid);
    drop_tnode(tnode);
    return ret;
}

static int do_chmod(struct inode *inode, mode_t mode) {
    struct process *process = get_current_task()->process;
    if (process->euid != 0 && process->euid != inode->uid) {
        return -EPERM;
    }

    if (!inode->i_op->chmod) {
        return -EPERM;
    }

    mode &= 07777;

    // Retain type information
    mode |= inode->mode & ~07777;

    return inode->i_op->chmod(inode, mode);
}

int fs_fchmodat(struct tnode *base, const char *path, mode_t mode, int flags) {
    if (*path == '\0' && (flags & AT_EMPTY_PATH)) {
        return do_chmod(base->inode, mode);
    }

    struct tnode *tnode;
    int ret = iname_with_base(base, path, (flags & AT_SYMLINK_NOFOLLOW) ? INAME_DONT_FOLLOW_TRAILING_SYMLINK : 0, &tnode);
    if (ret < 0) {
        return ret;
    }

    ret = do_chmod(tnode->inode, mode);
    drop_tnode(tnode);
    return ret;
}

static int fs_do_utimens(struct inode *inode, const struct timespec *in_times) {
    struct timespec now = time_read_clock(CLOCK_REALTIME);

    struct timespec times[2];

    if (in_times == NULL) {
        times[0] = now;
        times[1] = now;
    } else {
        times[0] = in_times[0];
        times[1] = in_times[1];
    }

    if (times[0].tv_nsec == UTIME_NOW) {
        times[0].tv_nsec = now.tv_nsec;
    }

    if (times[1].tv_nsec == UTIME_NOW) {
        times[1].tv_nsec = now.tv_nsec;
    }

    if (!inode->i_op->utimes) {
        return -EPERM;
    }

    return inode->i_op->utimes(inode, times);
}

int fs_utimensat(struct tnode *base, const char *path, const struct timespec *times, int flags) {
    if (*path == '\0' && (flags & AT_EMPTY_PATH)) {
        return fs_do_utimens(base->inode, times);
    }

    struct tnode *tnode;
    int ret = iname_with_base(base, path, (flags & AT_SYMLINK_NOFOLLOW) ? INAME_DONT_FOLLOW_TRAILING_SYMLINK : 0, &tnode);
    if (ret < 0) {
        return ret;
    }

    ret = fs_do_utimens(tnode->inode, times);
    drop_tnode(tnode);
    return ret;
}

intptr_t fs_default_mmap(void *addr, size_t len, int prot, int flags, struct inode *inode, off_t offset) {
    len += offset - (offset & ~0xFFF);
    offset &= ~0xFFF;
    len = ((len + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE;

    if (inode->i_op->lookup) {
        inode->i_op->lookup(inode, NULL);
    }

    struct vm_region *region = map_region(addr, len, prot, flags, VM_DEVICE_MEMORY_MAP_DONT_FREE_PHYS_PAGES);
    if (!region) {
        return -ENOMEM;
    }

    assert(offset % PAGE_SIZE == 0);
    assert(len % PAGE_SIZE == 0);

    struct vm_object *object = NULL;
    if (flags & MAP_PRIVATE) {
        object = vm_create_inode_object(inode, flags);
    } else {
        region->flags |= VM_SHARED;
        if (!inode->vm_object) {
            object = vm_create_inode_object(inode, flags);
            inode->vm_object = object;
        } else {
            object = bump_vm_object(inode->vm_object);
        }
    }

    assert(object);
    region->vm_object = object;
    region->vm_object_offset = offset;

    int ret = vm_map_region_with_object(region);
    if (ret < 0) {
        return (intptr_t) ret;
    }

    return region->start;
}

intptr_t fs_mmap(void *addr, size_t len, int prot, int flags, struct file *file, off_t offset) {
    if (file == NULL) {
        return -EINVAL;
    }

    struct inode *inode = fs_file_inode(file);
    assert(inode);
    if (inode->flags & FS_DEVICE) {
        bump_inode_reference(inode);
        return dev_mmap(addr, len, prot, flags, file, offset);
    }

    if (inode->i_op->mmap) {
        return inode->i_op->mmap(addr, len, prot, flags, inode, offset);
    }

    return -ENODEV;
}

int fs_rename(const char *old_path, const char *new_path) {
    assert(old_path);
    assert(new_path);

#ifdef VFS_DEBUG
    debug_log("Rename: [ %s, %s ]\n", old_path, new_path);
#endif /* VFS_DEBUG */

    struct tnode *old;
    int ret = iname(old_path, 0, &old);
    if (ret < 0) {
        return ret;
    }

    // Write permission is needed to modify old's parent, and old's .. entry (if it's a directory)
    if (!fs_can_write_inode(old->parent->inode) || ((old->inode->flags & FS_DIR) && !fs_can_write_inode(old->inode))) {
        drop_tnode(old);
        return -EACCES;
    }

    char *new_path_last_slash = strrchr(new_path, '/');
    struct tnode *new_parent;
    ret = 0;
    if (!new_path_last_slash) {
        new_parent = bump_tnode(get_current_task()->process->cwd);
    } else if (new_path_last_slash == new_path) {
        new_parent = bump_tnode(fs_root());
    } else {
        *new_path_last_slash = '\0';
        ret = iname(new_path, 0, &new_parent);
        *new_path_last_slash = '/';
    }

    if (ret < 0) {
        drop_tnode(old);
        return ret;
    }

    if (!fs_can_write_inode(new_parent->inode)) {
        drop_tnode(old);
        drop_tnode(new_parent);
        return -EACCES;
    }

    if (old->inode->super_block != new_parent->inode->super_block) {
        drop_tnode(old);
        drop_tnode(new_parent);
        return -EXDEV;
    }

    struct tnode *existing_tnode = NULL;
    ret = iname(new_path, 0, &existing_tnode);
    if (ret < 0 && ret != -ENOENT) {
        drop_tnode(old);
        drop_tnode(new_parent);
        return ret;
    }

    // Do nothing if the two tnode's point to the same inode
    if (existing_tnode && existing_tnode->inode == old->inode) {
        drop_tnode(old);
        drop_tnode(new_parent);
        drop_tnode(existing_tnode);
        return 0;
    }

    if (existing_tnode && (((existing_tnode->inode->flags & FS_DIR) && !(old->inode->flags & FS_DIR)) ||
                           (!(existing_tnode->inode->flags & FS_DIR) && (old->inode->flags & FS_DIR)))) {
        drop_tnode(old);
        drop_tnode(new_parent);
        drop_tnode(existing_tnode);
        return -ENOTDIR;
    }

    if (existing_tnode && (existing_tnode->inode->flags & FS_DIR) && !dir_empty(existing_tnode->inode)) {
        drop_tnode(old);
        drop_tnode(new_parent);
        drop_tnode(existing_tnode);
        return -ENOTEMPTY;
    }

    if (!old->inode->super_block->op || !old->inode->super_block->op->rename) {
        drop_tnode(old);
        drop_tnode(new_parent);
        drop_tnode(existing_tnode);
        return -EINVAL;
    }

    // Destroy the existing tnode if necessary
    if (existing_tnode) {
        if (existing_tnode->inode == old->inode) {
            drop_tnode(old);
            drop_tnode(new_parent);
            drop_tnode(existing_tnode);
            return 0;
        }

        if (((existing_tnode->inode->flags & FS_DIR) && !existing_tnode->inode->i_op->rmdir) || !existing_tnode->inode->i_op->unlink) {
            drop_tnode(old);
            drop_tnode(new_parent);
            drop_tnode(existing_tnode);
            return -EINVAL;
        }

        mutex_lock(&existing_tnode->inode->lock);

        int ret = 0;
        if (existing_tnode->inode->flags & FS_DIR) {
            ret = existing_tnode->inode->i_op->rmdir(existing_tnode);
        } else {
            ret = existing_tnode->inode->i_op->unlink(existing_tnode);
        }

        mutex_unlock(&existing_tnode->inode->lock);

        if (ret != 0) {
            drop_tnode(old);
            drop_tnode(new_parent);
            drop_tnode(existing_tnode);
            return ret;
        }

        struct inode *parent = existing_tnode->parent->inode;
        fs_del_dirent_cache(parent->dirent_cache, existing_tnode->name);
        drop_tnode(existing_tnode);
    }

    const char *name = !new_path_last_slash ? new_path : new_path_last_slash + 1;
    ret = old->inode->super_block->op->rename(old, new_parent, name);
    if (ret != 0) {
        drop_tnode(old);
        drop_tnode(new_parent);
        return ret;
    }

    struct tnode *old_parent = old->parent;
    assert(old_parent);

    fs_put_dirent_cache(new_parent->inode->dirent_cache, old->inode, name, strlen(name));
    fs_del_dirent_cache(old_parent->inode->dirent_cache, old->name);
    drop_tnode(old);
    drop_tnode(new_parent);
    return 0;
}

static int do_statvfs(struct super_block *sb, struct statvfs *buf) {
    buf->f_bsize = sb->block_size;
    buf->f_frsize = sb->block_size;

    buf->f_blocks = sb->num_blocks;
    buf->f_bfree = sb->free_blocks;
    buf->f_bavail = sb->available_blocks;

    buf->f_files = sb->num_inodes;
    buf->f_ffree = sb->free_inodes;
    buf->f_favail = sb->available_blocks;

    buf->f_fsid = sb->fsid;
    buf->f_flag = sb->flags;
    buf->f_namemax = NAME_MAX;

    return 0;
}

int fs_fstatvfs(struct file *file, struct statvfs *buf) {
    struct inode *inode = fs_file_inode(file);
    return do_statvfs(inode->super_block, buf);
}

int fs_statvfs(const char *path, struct statvfs *buf) {
    struct tnode *tnode;
    int ret = iname(path, 0, &tnode);
    if (ret < 0) {
        return ret;
    }

    struct inode *inode = tnode->inode;
    drop_tnode(tnode);
    return do_statvfs(inode->super_block, buf);
}

struct list_node *fs_file_system_list(void) {
    return &file_systems;
}

int fs_mount(const char *source, const char *target, const char *type, unsigned long flags, const void *data) {
    if (get_current_process()->euid != 0) {
        return -EPERM;
    }

    struct block_device *device = NULL;
    if (strcmp(source, "") != 0) {
        struct tnode *tnode;
        int ret = iname(source, 0, &tnode);
        if (ret < 0) {
            return ret;
        }

        dev_t device_id = tnode->inode->device_id;
        drop_tnode(tnode);

        struct fs_device *fs_device = dev_get_device(device_id);
        if (!fs_device || !(fs_device->mode & S_IFBLK)) {
            return -ENXIO;
        }
        device = dev_fs_device_to_block_device(fs_device);
    }

    return fs_do_mount(device, target, type, flags, data);
}

int fs_umount(const char *target) {
    if (get_current_process()->euid != 0) {
        return -EPERM;
    }

    debug_log("Unmount: [ %s ]\n", target);

    struct tnode *tnode;
    int ret = iname(target, INAME_DONT_FOLLOW_TRAILING_MOUNT_POINT, &tnode);
    if (ret < 0) {
        return ret;
    }

    struct inode *inode = tnode->inode;
    if (!fs_is_mount_point(inode)) {
        ret = -EINVAL;
        goto finish_umount;
    }

    struct mount *mount = inode->mount;

    // FIXME: every with atomic access, this is still very racey.
    if (atomic_load(&mount->busy_count) > 0) {
        ret = -EBUSY;
        goto finish_umount;
    }

    ret = mount->fs->umount(mount->super_block);
    if (ret < 0) {
        return ret;
    }

    atomic_store(&inode->mount, NULL);
    fs_free_mount(mount);

finish_umount:
    drop_tnode(tnode);
    return ret;
}

int fs_do_mount(struct block_device *device, const char *target, const char *type, unsigned long flags, const void *data) {
    debug_log("Mounting FS: [ %s, %s ]\n", type, target);

    struct file_system *file_system = fs_file_system_from_name(type);
    if (!file_system) {
        return -ENODEV;
    }

    struct tnode *t_mount_point;
    int ret = iname(target, INAME_DONT_FOLLOW_TRAILING_MOUNT_POINT, &t_mount_point);
    if (ret < 0) {
        return ret;
    }

    struct inode *mount_point = t_mount_point->inode;

    mutex_lock(&mount_point->lock);
    if (fs_is_mount_point(mount_point)) {
        ret = -EBUSY;
        goto finish_mount;
    }

    if (!(mount_point->flags & FS_DIR)) {
        ret = -ENOTDIR;
        goto finish_mount;
    }

    struct super_block *super_block;
    ret = file_system->mount(device, flags, data, &super_block);
    if (ret < 0) {
        goto finish_mount;
    }

    mount_point->mount = fs_create_mount(super_block, file_system, t_mount_point->mount, get_tnode_path(t_mount_point));

finish_mount:
    mutex_unlock(&mount_point->lock);
    drop_tnode(t_mount_point);
    return ret;
}

bool fs_can_read_inode_impl(struct inode *inode, uid_t uid, gid_t gid) {
    if (uid == 0) {
        return true;
    }

    if (inode->i_op->lookup) {
        inode->i_op->lookup(inode, NULL);
    }

    if (inode->uid == uid) {
        return !!(inode->mode & S_IRUSR);
    }

    if (inode->gid == gid || proc_in_group(get_current_task()->process, inode->gid)) {
        return !!(inode->mode & S_IRGRP);
    }

    return !!(inode->mode & S_IROTH);
}

bool fs_can_write_inode_impl(struct inode *inode, uid_t uid, gid_t gid) {
    if (uid == 0) {
        return true;
    }

    if (inode->i_op->lookup) {
        inode->i_op->lookup(inode, NULL);
    }

    if (inode->uid == uid) {
        return !!(inode->mode & S_IWUSR);
    }

    if (inode->gid == gid || proc_in_group(get_current_task()->process, inode->gid)) {
        return !!(inode->mode & S_IWGRP);
    }

    return !!(inode->mode & S_IWOTH);
}

bool fs_can_execute_inode_impl(struct inode *inode, uid_t uid, gid_t gid) {
    if (uid == 0) {
        return true;
    }

    if (inode->i_op->lookup) {
        inode->i_op->lookup(inode, NULL);
    }

    if (inode->uid == uid) {
        return !!(inode->mode & S_IXUSR);
    }

    if (inode->gid == gid || proc_in_group(get_current_task()->process, inode->gid)) {
        return !!(inode->mode & S_IXGRP);
    }

    return !!(inode->mode & S_IXOTH);
}

bool fs_can_read_inode(struct inode *inode) {
    return fs_can_read_inode_impl(inode, get_current_task()->process->euid, get_current_task()->process->egid);
}

bool fs_can_write_inode(struct inode *inode) {
    return fs_can_write_inode_impl(inode, get_current_task()->process->euid, get_current_task()->process->egid);
}

bool fs_can_execute_inode(struct inode *inode) {
    return fs_can_execute_inode_impl(inode, get_current_task()->process->euid, get_current_task()->process->egid);
}

int fs_faccessat(struct tnode *base, const char *path, int mode, int flags) {
    struct tnode *tnode;
    int ret = iname_with_base(
        base, path,
        INAME_CHECK_PERMISSIONS_WITH_REAL_UID_AND_GID | ((flags & AT_SYMLINK_NOFOLLOW) ? INAME_DONT_FOLLOW_TRAILING_SYMLINK : 0), &tnode);
    if (ret < 0) {
        return ret;
    }

    struct inode *inode = tnode->inode;
    drop_tnode(tnode);
    if (mode == F_OK) {
        return 0;
    }

    if (mode &= R_OK && !fs_can_read_inode_impl(inode, get_current_task()->process->uid, get_current_task()->process->gid)) {
        return -EACCES;
    }

    if (mode &= W_OK && !fs_can_write_inode_impl(inode, get_current_task()->process->uid, get_current_task()->process->gid)) {
        return -EACCES;
    }

    if (mode &= X_OK && !fs_can_execute_inode_impl(inode, get_current_task()->process->uid, get_current_task()->process->gid)) {
        return -EACCES;
    }

    return 0;
}

int fs_fcntl(struct file_descriptor *desc, int command, int arg) {
    assert(desc->file);

    switch (command) {
        case F_DUPFD_CLOEXEC:
            desc->fd_flags |= FD_CLOEXEC;
            // Fall through
        case F_DUPFD: {
            struct task *current = get_current_task();
            if (arg < 0 || arg > FOPEN_MAX) {
                return -EINVAL;
            }

            for (int i = arg; i < FOPEN_MAX; i++) {
                if (current->process->files[i].file == NULL) {
                    current->process->files[i] = fs_dup(*desc);
                    return i;
                }
            }

            return -EMFILE;
        }
        case F_GETFD:
            return desc->fd_flags;
        case F_SETFD:
            if (arg == 0 || arg == FD_CLOEXEC) {
                desc->fd_flags = arg;
                return 0;
            }

            return -EINVAL;
        case F_GETFL:
            return desc->file->open_flags;
        case F_SETFL:
            // FIXME: this should do validity checks and update the files capabilites
            desc->file->open_flags = arg;

            if (!!(desc->file->flags & FS_SOCKET) && !desc->file->inode) {
                struct socket *socket = desc->file->private_data;
                if (arg & O_NONBLOCK) {
                    socket->type |= SOCK_NONBLOCK;
                } else {
                    socket->type &= ~SOCK_NONBLOCK;
                }
            }
            return 0;
        default:
            return -EINVAL;
    }
}

ssize_t fs_readlink(const char *path, char *buf, size_t bufsiz) {
    struct tnode *link;
    {
        int ret = iname(path, INAME_DONT_FOLLOW_TRAILING_SYMLINK, &link);
        if (ret < 0) {
            return ret;
        }
    }

    if (!(link->inode->flags & FS_LINK)) {
        drop_tnode(link);
        return -EINVAL;
    }

    void *buffer;
    size_t buffer_len;
    int ret = fs_read_all_inode(link->inode, &buffer, &buffer_len);
    if (ret < 0 || buffer == NULL) {
        drop_tnode(link);
        return ret;
    }

    size_t to_write = MIN(bufsiz, buffer_len);
    memcpy(buf, buffer, to_write);

    free(buffer);
    drop_tnode(link);
    return to_write;
}

int fs_symlink(const char *target, const char *linkpath) {
    char *path = malloc(strlen(linkpath) + 1);
    strcpy(path, linkpath);

    char *last_slash = strrchr(path, '/');
    struct tnode *tparent;

    int ret = 0;
    if (last_slash == path) {
        tparent = bump_tnode(fs_root());
    } else if (last_slash == NULL) {
        tparent = bump_tnode(get_current_task()->process->cwd);
        last_slash = path - 1;
    } else {
        *last_slash = '\0';
        ret = iname(path, 0, &tparent);
        if (ret < 0) {
            free(path);
            return ret;
        }
    }

    if (!fs_can_write_inode(tparent->inode)) {
        drop_tnode(tparent);
        free(path);
        return -EACCES;
    }

    tparent->inode->i_op->lookup(tparent->inode, NULL);
    if (fs_lookup_in_cache(tparent->inode->dirent_cache, last_slash + 1) != NULL) {
        free(path);
        drop_tnode(tparent);
        return -EEXIST;
    }

    if (!tparent->inode->i_op->symlink) {
        free(path);
        drop_tnode(tparent);
        return -EINVAL;
    }

#ifdef VFS_DEBUG
    debug_log("Adding symlink to: [ %s ]\n", tparent->name);
#endif /* VFS_DEBUG */

    int error = 0;
    struct inode *inode = tparent->inode->i_op->symlink(tparent, last_slash + 1, target, &error);
    if (inode == NULL) {
        free(path);
        drop_tnode(tparent);
        return error;
    }

    fs_put_dirent_cache(tparent->inode->dirent_cache, inode, last_slash + 1, strlen(last_slash + 1));
    drop_tnode(tparent);

    free(path);
    return 0;
}

int fs_link(const char *oldpath, const char *newpath) {
    struct tnode *target;

    int ret = iname(oldpath, 0, &target);
    if (ret < 0) {
        return ret;
    }

    if (target->inode->flags & FS_DIR) {
        return -EPERM;
    }

    char *path = malloc(strlen(newpath) + 1);
    strcpy(path, newpath);

    char *last_slash = strrchr(path, '/');
    struct tnode *tparent;

    if (last_slash == path) {
        tparent = bump_tnode(fs_root());
    } else if (last_slash == NULL) {
        tparent = bump_tnode(get_current_task()->process->cwd);
        last_slash = path - 1;
    } else {
        *last_slash = '\0';
        ret = iname(path, 0, &tparent);
        if (ret < 0) {
            free(path);
            return ret;
        }
    }

    if (!fs_can_write_inode(tparent->inode)) {
        drop_tnode(tparent);
        free(path);
        return -EACCES;
    }

    tparent->inode->i_op->lookup(tparent->inode, NULL);
    if (fs_lookup_in_cache(tparent->inode->dirent_cache, last_slash + 1) != NULL) {
        free(path);
        drop_tnode(tparent);
        return -EEXIST;
    }

    if (!tparent->inode->i_op->link) {
        free(path);
        drop_tnode(tparent);
        return -EINVAL;
    }

#ifdef VFS_DEBUG
    debug_log("Adding hard link to: [ %s ]\n", tparent->name);
#endif /* VFS_DEBUG */

    ret = tparent->inode->i_op->link(tparent, last_slash + 1, target);
    if (ret < 0) {
        drop_tnode(tparent);
        return ret;
    }

    bump_inode_reference(target->inode);

    fs_put_dirent_cache(tparent->inode->dirent_cache, target->inode, last_slash + 1, strlen(last_slash + 1));

    drop_tnode(tparent);
    free(path);
    return 0;
}

int fs_poll_wait(struct file_state *state, mutex_t *lock, int mask, struct timespec *timeout) {
    struct wait_queue_entry entry = { .task = get_current_task() };
    if (fs_do_poll(&entry, mask, state)) {
        return 0;
    }

    if (!timeout) {
        return ___wait_for(entry.task, &entry, fs_do_poll(NULL, mask, state), &state->queue, mutex_unlock(lock), mutex_lock(lock),
                           wait_do(entry.task), true, true, false);
    }

    return ___wait_for(entry.task, &entry, (timeout->tv_sec == 0 && timeout->tv_nsec == 0) || !!fs_do_poll(NULL, mask, state),
                       &state->queue, mutex_unlock(lock), mutex_lock(lock), __time_wakeup_after(CLOCK_MONOTONIC, timeout), true, true,
                       false);
}

struct poll_entry {
    struct wait_queue_entry wq_entry;
    struct file *file;
    int mask;
    int events;
    int fd;
    bool wq_valid : 1;
    bool rdset : 1;
    bool wrset : 1;
    bool exset : 1;
};

static int fs_do_sys_poll(struct poll_entry *entries, int count, const struct timespec *user_timeout) {
    struct timespec timeout_storage;
    struct timespec *timeout = NULL;
    if (user_timeout) {
        timeout_storage = *user_timeout;
        timeout = &timeout_storage;
    }

    struct task *current = get_current_task();
    int ret = 0;
    bool wq_queue = true;
    for (;;) {
        ret = __wait_prepare(current, true);
        if (ret) {
            break;
        }

        for (int i = 0; i < count; i++) {
            struct poll_entry *entry = &entries[i];
            struct file *file = entry->file;
            int events = POLLERR & entry->mask;
            if (file && file->f_op->poll) {
                struct wait_queue_entry *wq_entry = wq_queue ? &entry->wq_entry : NULL;
                events = file->f_op->poll(file, wq_entry, entry->mask);
                if (!events && !!wq_entry) {
                    entry->wq_valid = true;
                }
            }

            if (events) {
                ret++;
                wq_queue = false;
                entry->events = events;
            }
        }

        if (ret > 0) {
            __wait_cancel(current);
            break;
        }

        wq_queue = false;
        if (timeout) {
            ret = __time_wakeup_after(CLOCK_MONOTONIC, timeout);
            if (!ret && timeout->tv_sec == 0 && timeout->tv_nsec == 0) {
                break;
            }
        } else {
            ret = wait_do(current);
        }

        if (ret) {
            break;
        }
    }

    for (int i = 0; i < count; i++) {
        struct poll_entry *entry = &entries[i];
        struct file *file = entry->file;
        if (entry->wq_valid) {
            file->f_op->poll_finish(file, &entry->wq_entry);
        }
        if (file) {
            fs_close(file);
        }
    }
    return ret;
}

int fs_poll(struct pollfd *fds, nfds_t nfds, const struct timespec *user_timeout) {
    if (nfds > FOPEN_MAX) {
        return -EINVAL;
    }

    struct poll_entry entries[FOPEN_MAX] = { 0 };
    struct process *process = get_current_process();

    mutex_lock(&process->lock);
    for (nfds_t i = 0; i < nfds; i++) {
        struct pollfd pollfd = fds[i];
        int fd = pollfd.fd;
        entries[i].mask = pollfd.events;
        entries[i].wq_entry.task = get_current_task();

        if (fd < 0 || fd >= FOPEN_MAX) {
            continue;
        }

        struct file *file = process->files[fd].file;
        if (file) {
            entries[i].file = file;
            atomic_fetch_add(&file->ref_count, 1);
        }
    }
    mutex_unlock(&process->lock);

    int ret = fs_do_sys_poll(entries, nfds, user_timeout);
    if (ret < 0) {
        return ret;
    }

    for (nfds_t i = 0; i < nfds; i++) {
        fds[i].revents = entries[i].events;
    }
    return ret;
}

#define POLLIN_SET  (POLLRDNORM | POLLRDBAND | POLLIN | POLLHUP | POLLERR)
#define POLLOUT_SET (POLLWRBAND | POLLWRNORM | POLLOUT | POLLERR)
#define POLLEX_SET  (POLLPRI)

int fs_select(int nfds, uint8_t *readfds, uint8_t *writefds, uint8_t *exceptfds, const struct timespec *user_timeout) {
    if (nfds < 0 || nfds > FOPEN_MAX) {
        return -EINVAL;
    }

    struct poll_entry entries[FOPEN_MAX] = { 0 };
    int count = 0;
    struct process *process = get_current_process();
    bool bad = false;

    mutex_lock(&process->lock);
    for (int i = 0; i < nfds; i++) {
        bool rdset = readfds && !!(readfds[i / CHAR_BIT] & (1U << (i % CHAR_BIT)));
        bool wrset = writefds && !!(writefds[i / CHAR_BIT] & (1U << (i % CHAR_BIT)));
        bool exset = exceptfds && !!(exceptfds[i / CHAR_BIT] & (1U << (i % CHAR_BIT)));

        if (rdset || wrset || exset) {
            struct poll_entry *entry = &entries[count++];
            entry->fd = i;
            entry->wq_entry.task = get_current_task();
            entry->mask = (rdset ? POLLIN_SET : 0) | (wrset ? POLLOUT_SET : 0) | (exset ? POLLEX_SET : 0);
            entry->rdset = rdset;
            entry->wrset = wrset;
            entry->exset = exset;
            entry->file = process->files[i].file;
            if (!entry->file) {
                bad = true;
                break;
            }
            atomic_fetch_add(&entry->file->ref_count, 1);
        }
    }
    mutex_unlock(&process->lock);

    int ret = fs_do_sys_poll(entries, count, user_timeout);
    if (bad) {
        return -EBADF;
    }
    if (ret < 0) {
        return ret;
    }

    for (int i = 0; i < count; i++) {
        struct poll_entry *entry = &entries[i];
        int fd = entry->fd;
        if (entry->rdset) {
            bool res = !!(entry->events & POLLIN_SET);
            if (!res) {
                readfds[fd / CHAR_BIT] &= ~(1U << (fd % CHAR_BIT));
            }
        }
        if (entry->wrset) {
            bool res = !!(entry->events & POLLOUT_SET);
            if (!res) {
                writefds[fd / CHAR_BIT] &= ~(1U << (fd % CHAR_BIT));
            }
        }
        if (entry->exset) {
            bool res = !!(entry->events & POLLEX_SET);
            if (!res) {
                exceptfds[fd / CHAR_BIT] &= ~(1U << (fd % CHAR_BIT));
            }
        }
    }
    return ret;
}

// NOTE: we don't have to write out to disk, because we only loose info
//       stored on the inode after rebooting, and at that point, the binding
//       task will no longer exist.
int fs_bind_socket_to_inode(struct inode *inode, struct socket *socket) {
    assert(inode->flags & FS_SOCKET && S_ISSOCK(inode->mode));
    inode->socket = socket;

    return 0;
}

struct file_descriptor fs_dup(struct file_descriptor desc) {
    if (desc.file == NULL) {
        return (struct file_descriptor) { NULL, 0 };
    }

    int fetched_ref_count = atomic_fetch_add(&desc.file->ref_count, 1);
    assert(fetched_ref_count > 0);

    // NOTE: the new descriptor reset the FD_CLOEXEC flag
    return (struct file_descriptor) { desc.file, 0 };
}

struct file_descriptor fs_dup_accross_fork(struct file_descriptor desc) {
    if (desc.file == NULL) {
        return (struct file_descriptor) { NULL, 0 };
    }

    int fetched_ref_count = atomic_fetch_add(&desc.file->ref_count, 1);
    assert(fetched_ref_count > 0);

    return (struct file_descriptor) { desc.file, desc.fd_flags };
}

char *get_tnode_path(struct tnode *tnode) {
    /* Check if root */
    if (!tnode->parent) {
        char *ret = malloc(2);
        strcpy(ret, "/");
        return ret;
    }

    ssize_t size = 15;
    char **name_buffer = malloc(size * sizeof(char **));

    ssize_t len = 1;
    ssize_t i;
    for (i = 0; tnode->parent != NULL; i++) {
        name_buffer[i] = tnode->name;
        len += strlen(tnode->name) + 1;
        tnode = tnode->parent;

        if (i >= size) {
            size *= 2;
            name_buffer = realloc(name_buffer, size);
        }
    }

    char *ret = malloc(len);
    ret[0] = '\0';
    for (i--; i >= 0; i--) {
        strcat(ret, "/");
        strcat(ret, name_buffer[i]);
    }

    free(name_buffer);
    return ret;
}

struct tnode *fs_get_tnode_for_file(struct file *file) {
    assert(file);
    return file->tnode;
}

ssize_t fs_do_read(char *buf, off_t offset, size_t n, const char *source, size_t source_max) {
    size_t to_read = MIN(n, source_max - offset);
    memcpy(buf, source + offset, to_read);
    return to_read;
}

int fs_show_file_system(struct file_system *fs, char *buffer, size_t _buffer_length) {
    int position = 0;
    int buffer_length = _buffer_length;
    position += snprintf(buffer + position, MAX(buffer_length - position, 0), "NAME: %s\n", fs->name);

    char aux_buffer[128];
    for (size_t i = 0; i < fs->id_count; i++) {
        block_show_device_id(fs->id_table[i], aux_buffer, sizeof(aux_buffer));
        position += snprintf(buffer + position, MAX(buffer_length - position, 0), "ID[%lu]: %s\n", i, aux_buffer);
    }

    return position;
}

struct file_system *fs_file_system_from_name(const char *name) {
    fs_for_each_file_system(fs) {
        if (!strcmp(fs->name, name)) {
            return fs;
        }
    }
    return NULL;
}

static int try_mount_root(struct file_system *file_system, struct block_device *device) {
    struct super_block *super_block;
    int ret = file_system->mount(device, 0, NULL, &super_block);
    if (ret < 0) {
        return ret;
    }

    root = fs_create_mount(super_block, file_system, NULL, strdup("/"));
    t_root = create_root_tnode(super_block->root, root);

    return 0;
}

int fs_mount_initrd(void) {
    struct file_system *initrd = fs_file_system_from_name("initrd");
    assert(initrd);

    return try_mount_root(initrd, NULL);
}

int fs_mount_root(struct fs_root_desc desc) {
    struct block_device *device = NULL;
    struct file_system *fs = NULL;
    switch (desc.type) {
        case FS_ROOT_TYPE_FS_NAME: {
            fs = fs_file_system_from_name(desc.fs_name);
            if (!fs) {
                debug_log("Fatal Error: Cannot find root file system type: [ %s ]\n", desc.fs_name);
                return -ENODEV;
            }
            block_for_each_device(iter) {
                if (fs_id_matches_file_system(iter->info.filesystem_type_id, fs)) {
                    device = iter;
                    break;
                }
            }
            break;
        }
        case FS_ROOT_TYPE_PARTITION_ID: {
            block_for_each_device(iter) {
                if (block_device_id_equals(iter->info.partition_id, desc.id)) {
                    device = iter;
                    break;
                }
            }
            break;
        }
        case FS_ROOT_TYPE_FS_ID: {
            block_for_each_device(iter) {
                if (block_device_id_equals(iter->info.filesystem_id, desc.id)) {
                    device = iter;
                    break;
                }
            }
            break;
        }
    }

    // Destroy to old initrd root.
    assert(root->fs->umount(root->super_block) == 0);
    fs_free_mount(root);
    drop_tnode(t_root);

    root = NULL;
    t_root = NULL;

    if (!device) {
        debug_log("Fatal Error: Could not find root device\n");
        return -ENODEV;
    }

    if (fs) {
        int ret = try_mount_root(fs, device);
        if (ret < 0) {
            debug_log("Fatal Error: Cannot mount root file system: [ %s ]\n", strerror(-ret));
        }

        return ret;
    }

    fs_for_each_file_system(iter) {
        if (fs_id_matches_file_system(device->info.filesystem_type_id, iter)) {
            int ret = try_mount_root(iter, device);
            if (ret < 0) {
                debug_log("Fatal Error: Cannot mount root file system: [ %s ]\n", strerror(-ret));
            }

            return ret;
        }
    }

    debug_log("Fatal Error: Cannot mount root file system\n");
    return -EIO;
}
