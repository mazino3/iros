#include <assert.h>
#include <limits.h>

#include <kernel/fs/inode.h>
#include <kernel/fs/pipe.h>
#include <kernel/fs/vfs.h>
#include <kernel/hal/timer.h>
#include <kernel/net/socket.h>
#include <kernel/proc/blockers.h>
#include <kernel/proc/process_state.h>
#include <kernel/proc/task.h>
#include <kernel/sched/task_sched.h>

static bool sleep_milliseconds_blocker(struct block_info *info) {
    assert(info->type == SLEEP_MILLISECONDS);
    return get_time() >= info->sleep_milliseconds_info.end_time;
}

void proc_block_sleep_milliseconds(struct task *current, time_t end_time) {
    disable_interrupts();
    current->block_info.sleep_milliseconds_info.end_time = end_time;
    current->block_info.type = SLEEP_MILLISECONDS;
    current->block_info.should_unblock = &sleep_milliseconds_blocker;
    current->blocking = true;
    current->sched_state = WAITING;
    __kernel_yield();
}

static bool until_inode_is_readable_blocker(struct block_info *info) {
    assert(info->type == UNTIL_INODE_IS_READABLE);
    return info->until_inode_is_readable_info.inode->readable;
}

void proc_block_until_inode_is_readable(struct task *current, struct inode *inode) {
    disable_interrupts();
    current->block_info.until_inode_is_readable_info.inode = inode;
    current->block_info.type = UNTIL_INODE_IS_READABLE;
    current->block_info.should_unblock = &until_inode_is_readable_blocker;
    current->blocking = true;
    current->sched_state = WAITING;
    __kernel_yield();
}

static bool until_pipe_is_readable_blocker(struct block_info *info) {
    assert(info->type == UNTIL_PIPE_IS_READABLE);

    // Make sure to unblock the process if the other end of the pipe disconnects
    struct inode *inode = info->until_pipe_is_readable_info.inode;
    return inode->readable || !is_pipe_write_end_open(inode);
}

void proc_block_until_pipe_is_readable(struct task *current, struct inode *inode) {
    assert(inode->flags & FS_FIFO);
    disable_interrupts();
    current->block_info.until_pipe_is_readable_info.inode = inode;
    current->block_info.type = UNTIL_PIPE_IS_READABLE;
    current->block_info.should_unblock = &until_pipe_is_readable_blocker;
    current->blocking = true;
    current->sched_state = WAITING;
    __kernel_yield();
}

static bool until_socket_is_connected_blocker(struct block_info *info) {
    assert(info->type == UNTIL_SOCKET_IS_CONNECTED);

    return info->until_socket_is_connected_info.socket->state == CONNECTED;
}

void proc_block_until_socket_is_connected(struct task *current, struct socket *socket) {
    assert(socket->state != CONNECTED);
    disable_interrupts();
    current->block_info.until_socket_is_connected_info.socket = socket;
    current->block_info.type = UNTIL_SOCKET_IS_CONNECTED;
    current->block_info.should_unblock = &until_socket_is_connected_blocker;
    current->blocking = true;
    current->sched_state = WAITING;
    __kernel_yield();
}

static bool until_inode_is_readable_or_timeout_blocker(struct block_info *info) {
    assert(info->type == UNTIL_INODE_IS_READABLE_OR_TIMEOUT);

    return get_time() >= info->until_inode_is_readable_or_timeout_info.end_time ||
           info->until_inode_is_readable_or_timeout_info.inode->readable;
}

void proc_block_until_inode_is_readable_or_timeout(struct task *current, struct inode *inode, time_t end_time) {
    disable_interrupts();
    current->block_info.until_inode_is_readable_or_timeout_info.inode = inode;
    current->block_info.until_inode_is_readable_or_timeout_info.end_time = end_time;
    current->block_info.type = UNTIL_INODE_IS_READABLE_OR_TIMEOUT;
    current->block_info.should_unblock = &until_inode_is_readable_or_timeout_blocker;
    current->blocking = true;
    current->sched_state = WAITING;
    __kernel_yield();
}

static bool until_inode_is_writable_blocker(struct block_info *info) {
    assert(info->type == UNTIL_INODE_IS_WRITABLE);
    return info->until_inode_is_writable_info.inode->writeable;
}

void proc_block_until_inode_is_writable(struct task *current, struct inode *inode) {
    disable_interrupts();
    current->block_info.until_inode_is_writable_info.inode = inode;
    current->block_info.type = UNTIL_INODE_IS_WRITABLE;
    current->block_info.should_unblock = &until_inode_is_writable_blocker;
    current->blocking = true;
    current->sched_state = WAITING;
    __kernel_yield();
}

static bool custom_blocker(struct block_info *info) {
    assert(info->type == CUSTOM);

    // The custom blocker is designed so that an irq
    // will manually change the caller's sched_state to
    // RUNNING_UNINTERRUPTIBLE
    return false;
}

void proc_block_custom(struct task *current) {
    disable_interrupts();
    current->block_info.type = CUSTOM;
    current->block_info.should_unblock = &custom_blocker;
    current->blocking = true;
    current->sched_state = WAITING;
    __kernel_yield();
}

static bool until_socket_has_connection_blocker(struct block_info *info) {
    assert(info->type == UNTIL_SOCKET_HAS_CONNECTION);

    return info->until_socket_has_connection_info.socket->pending[0] != NULL;
}

void proc_block_until_socket_has_connection(struct task *current, struct socket *socket) {
    assert(socket->state == LISTENING);
    disable_interrupts();
    current->block_info.until_socket_has_connection_info.socket = socket;
    current->block_info.type = UNTIL_SOCKET_HAS_CONNECTION;
    current->block_info.should_unblock = &until_socket_has_connection_blocker;
    current->blocking = true;
    current->sched_state = WAITING;
    __kernel_yield();
}

static bool until_socket_is_readable_blocker(struct block_info *info) {
    assert(info->type == UNTIL_SOCKET_IS_READABLE);

    return info->until_socket_is_readable_info.socket->readable || info->until_socket_is_readable_info.socket->exceptional;
}

void proc_block_until_socket_is_readable(struct task *current, struct socket *socket) {
    disable_interrupts();
    current->block_info.until_socket_is_readable_info.socket = socket;
    current->block_info.type = UNTIL_SOCKET_IS_READABLE;
    current->block_info.should_unblock = &until_socket_is_readable_blocker;
    current->blocking = true;
    current->sched_state = WAITING;
    __kernel_yield();
}

static bool until_socket_is_readable_with_timeout_blocker(struct block_info *info) {
    assert(info->type == UNTIL_SOCKET_IS_READABLE_WITH_TIMEOUT);

    return get_time() >= info->until_socket_is_readable_with_timeout_info.end_time ||
           info->until_socket_is_readable_with_timeout_info.socket->readable || info->until_socket_is_readable_info.socket->exceptional;
}

void proc_block_until_socket_is_readable_with_timeout(struct task *current, struct socket *socket, time_t end_time) {
    disable_interrupts();
    current->block_info.until_socket_is_readable_with_timeout_info.socket = socket;
    current->block_info.until_socket_is_readable_with_timeout_info.end_time = end_time;
    current->block_info.type = UNTIL_SOCKET_IS_READABLE_WITH_TIMEOUT;
    current->block_info.should_unblock = &until_socket_is_readable_with_timeout_blocker;
    current->blocking = true;
    current->sched_state = WAITING;
    __kernel_yield();
}

static bool select_blocker_helper(int nfds, uint8_t *readfds, uint8_t *writefds, uint8_t *exceptfds) {
    struct task *current = get_current_task();
    size_t fd_set_size = ((nfds + sizeof(uint8_t) * CHAR_BIT - 1) / sizeof(uint8_t) / CHAR_BIT) * sizeof(uint8_t);

    if (readfds) {
        for (size_t i = 0; i < fd_set_size / sizeof(uint8_t); i++) {
            if (readfds[i]) {
                for (size_t j = 0; i * sizeof(uint8_t) * CHAR_BIT + j < (size_t) nfds && j < sizeof(uint8_t) * CHAR_BIT; j++) {
                    if (readfds[i] & (1U << j)) {
                        struct file *to_check = current->process->files[i * sizeof(uint8_t) * CHAR_BIT + j].file;
                        if (fs_is_readable(to_check)) {
                            return true;
                        }
                    }
                }
            }
        }
    }

    if (writefds) {
        for (size_t i = 0; i < fd_set_size / sizeof(uint8_t); i++) {
            if (writefds[i]) {
                for (size_t j = 0; i * sizeof(uint8_t) * CHAR_BIT + j < (size_t) nfds && j < sizeof(uint8_t) * CHAR_BIT; j++) {
                    if (writefds[i] & (1U << j)) {
                        struct file *to_check = current->process->files[i * sizeof(uint8_t) * CHAR_BIT + j].file;
                        if (fs_is_writable(to_check)) {
                            return true;
                        }
                    }
                }
            }
        }
    }

    if (exceptfds) {
        for (size_t i = 0; i < fd_set_size / sizeof(uint8_t); i++) {
            if (exceptfds[i]) {
                for (size_t j = 0; i * sizeof(uint8_t) * CHAR_BIT + j < (size_t) nfds && j < sizeof(uint8_t) * CHAR_BIT; j++) {
                    if (exceptfds[i] & (1U << j)) {
                        struct file *to_check = current->process->files[i * sizeof(uint8_t) * CHAR_BIT + j].file;
                        if (fs_is_exceptional(to_check)) {
                            return true;
                        }
                    }
                }
            }
        }
    }

    return false;
}

static bool select_blocker(struct block_info *info) {
    assert(info->type == SELECT);

    return select_blocker_helper(info->select_info.nfds, info->select_info.readfds, info->select_info.writefds,
                                 info->select_info.exceptfds);
}

void proc_block_select(struct task *current, int nfds, uint8_t *readfds, uint8_t *writefds, uint8_t *exceptfds) {
    disable_interrupts();
    current->block_info.select_info.nfds = nfds;
    current->block_info.select_info.readfds = readfds;
    current->block_info.select_info.writefds = writefds;
    current->block_info.select_info.exceptfds = exceptfds;
    current->block_info.type = SELECT;
    current->block_info.should_unblock = &select_blocker;
    current->blocking = true;
    current->sched_state = WAITING;
    __kernel_yield();
}

static bool select_timeout_blocker(struct block_info *info) {
    assert(info->type == SELECT_TIMEOUT);

    if (get_time() >= info->select_timeout_info.end_time) {
        return true;
    }

    return select_blocker_helper(info->select_timeout_info.nfds, info->select_timeout_info.readfds, info->select_timeout_info.writefds,
                                 info->select_timeout_info.exceptfds);
}

void proc_block_select_timeout(struct task *current, int nfds, uint8_t *readfds, uint8_t *writefds, uint8_t *exceptfds, time_t end_time) {
    disable_interrupts();
    current->block_info.select_timeout_info.nfds = nfds;
    current->block_info.select_timeout_info.readfds = readfds;
    current->block_info.select_timeout_info.writefds = writefds;
    current->block_info.select_timeout_info.exceptfds = exceptfds;
    current->block_info.select_timeout_info.end_time = end_time;
    current->block_info.type = SELECT_TIMEOUT;
    current->block_info.should_unblock = &select_timeout_blocker;
    current->blocking = true;
    current->sched_state = WAITING;
    __kernel_yield();
}

static bool waitpid_blocker(struct block_info *info) {
    assert(info->type == WAITPID);

    pid_t pid = info->waitpid_info.pid;

    if (pid < -1) {
        return proc_num_messages_by_pg(-pid) != 0;
    } else if (pid == -1) {
        return proc_num_messages_by_parent(get_current_task()->process->pid) != 0;
    } else {
        return proc_num_messages(pid) != 0;
    }
}

void proc_block_waitpid(struct task *current, pid_t pid) {
    disable_interrupts();
    current->block_info.waitpid_info.pid = pid;
    current->block_info.type = WAITPID;
    current->block_info.should_unblock = &waitpid_blocker;
    current->blocking = true;
    current->sched_state = WAITING;
    __kernel_yield();
}