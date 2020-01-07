#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <kernel/fs/dev.h>
#include <kernel/fs/inode_store.h>
#include <kernel/fs/vfs.h>
#include <kernel/proc/task.h>

static ssize_t dev_null_read(struct device *device, struct file *file, void *buffer, size_t len) {
    (void) device;
    (void) file;
    (void) buffer;
    (void) len;

    return 0;
}

static ssize_t dev_zero_read(struct device *device, struct file *file, void *buffer, size_t len) {
    (void) device;
    (void) file;

    memset(buffer, 0, len);
    return len;
}

static ssize_t dev_ignore_write(struct device *device, struct file *file, const void *buffer, size_t len) {
    (void) device;
    (void) file;
    (void) buffer;

    return len;
}

static struct file *fake_symlink(struct file *file, int flags, int *error) {
    if (file == NULL) {
        *error = ENOENT;
        return NULL;
    }

    struct inode *inode = fs_inode_get(file->device, file->inode_idenifier);
    assert(inode);

    struct inode *parent = inode->parent->inode;
    struct tnode *tnode = find_tnode_inode(parent->tnode_list, inode);

    char *path = get_tnode_path(tnode);
    debug_log("to: [ %s ]\n", path);

    struct file *ret = fs_open(path, flags, error);

    free(path);
    return ret;
}

static struct file *stdin_open(struct device *device, int flags, int *error) {
    (void) device;

    struct task *task = get_current_task();
    struct file *file = task->process->files[STDIN_FILENO].file;

    debug_log("redirecting: [ %s ]\n", "/dev/stdin");
    return fake_symlink(file, flags, error);
}

static struct file *stdout_open(struct device *device, int flags, int *error) {
    (void) device;

    struct task *task = get_current_task();
    struct file *file = task->process->files[STDOUT_FILENO].file;

    debug_log("redirecting: [ %s ]\n", "/dev/stdout");
    return fake_symlink(file, flags, error);
}

static struct file *stderr_open(struct device *device, int flags, int *error) {
    (void) device;

    struct task *task = get_current_task();
    struct file *file = task->process->files[STDERR_FILENO].file;

    debug_log("redirecting: [ %s ]\n", "/dev/stderr");
    return fake_symlink(file, flags, error);
}

static ssize_t full_read(struct device *device, struct file *file, void *buf, size_t n) {
    (void) device;
    (void) file;
    (void) buf;
    (void) n;

    return -ENOSPC;
}

static ssize_t full_write(struct device *device, struct file *file, const void *buf, size_t n) {
    (void) device;
    (void) file;
    (void) buf;
    (void) n;

    return -ENOSPC;
}

static struct device_ops dev_null_ops = { NULL, &dev_null_read, &dev_ignore_write, NULL, NULL, NULL, NULL, NULL, NULL };

static struct device dev_null = { 0x31, S_IFCHR, "null", false, &dev_null_ops, NULL, NULL };

static struct device_ops dev_zero_ops = { NULL, &dev_zero_read, &dev_ignore_write, NULL, NULL, NULL, NULL, NULL, NULL };

static struct device dev_zero = { 0x32, S_IFCHR, "zero", false, &dev_zero_ops, NULL, NULL };

static struct device_ops dev_stdin_ops = { &stdin_open, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };

static struct device_ops dev_stdout_ops = { &stdout_open, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };

static struct device_ops dev_stderr_ops = { &stderr_open, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };

static struct device dev_stdin = { 0x33, S_IFCHR, "stdin", false, &dev_stdin_ops, NULL, NULL };

static struct device dev_stdout = { 0x34, S_IFCHR, "stdout", false, &dev_stdout_ops, NULL, NULL };

static struct device dev_stderr = { 0x35, S_IFCHR, "stderr", false, &dev_stderr_ops, NULL, NULL };

static struct device_ops dev_full_ops = { NULL, &full_read, &full_write, NULL, NULL, NULL, NULL, NULL, NULL };

static struct device dev_full = { 0x36, S_IFCHR, "full", false, &dev_full_ops, NULL, NULL };

void init_virtual_devices() {
    dev_add(&dev_null, "null");
    dev_add(&dev_zero, "zero");
    dev_add(&dev_stdin, "stdin");
    dev_add(&dev_stdout, "stdout");
    dev_add(&dev_stderr, "stderr");
    dev_add(&dev_full, "full");
}