#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/iros.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <sys/times.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>

#include <kernel/fs/file.h>
#include <kernel/fs/procfs.h>
#include <kernel/fs/vfs.h>
#include <kernel/hal/hal.h>
#include <kernel/hal/output.h>
#include <kernel/hal/processor.h>
#include <kernel/irqs/handlers.h>
#include <kernel/mem/anon_vm_object.h>
#include <kernel/mem/vm_allocator.h>
#include <kernel/net/socket.h>
#include <kernel/net/socket_syscalls.h>
#include <kernel/proc/elf64.h>
#include <kernel/proc/pid.h>
#include <kernel/proc/profile.h>
#include <kernel/proc/task.h>
#include <kernel/sched/task_sched.h>
#include <kernel/time/clock.h>
#include <kernel/time/timer.h>
#include <kernel/util/validators.h>

// #define DUP_DEBUG
// #define EXIT_DEBUG
// #define SET_PGID_DEBUG
// #define SETUID_HACK
// #define SIGACTION_DEBUG
// #define SIGPROCMASK_DEBUG
// #define SIGRETURN_DEBUG
#define SYSCALL_DEBUG
// #define USER_MUTEX_DEBUG
// #define WAIT_PID_DEBUG

#define SYS_CALL(n) void sys_##n##_entry(struct task_state *task_state)

#define SYS_VALIDATE(n, a, f)               \
    do {                                    \
        __DO_VALIDATE(n, a, f, SYS_RETURN); \
    } while (0)

#define SYS_PARAM(t, n, r) t n = (t) task_get_sys_call_arg##r(task_state)
#define SYS_PARAM_VALIDATE(t, n, r, f, a) \
    SYS_PARAM(t, n, r);                   \
    do {                                  \
        SYS_VALIDATE(n, a, f);            \
    } while (0)
#define SYS_PARAM_TRANSFORM(t, n, ot, r, f)                         \
    t n;                                                            \
    do {                                                            \
        int ret = f((ot) task_get_sys_call_arg##r(task_state), &n); \
        if (ret < 0) {                                              \
            SYS_RETURN(ret);                                        \
        }                                                           \
    } while (0)

#define SYS_PARAM1(t, n) SYS_PARAM(t, n, 1)
#define SYS_PARAM2(t, n) SYS_PARAM(t, n, 2)
#define SYS_PARAM3(t, n) SYS_PARAM(t, n, 3)
#define SYS_PARAM4(t, n) SYS_PARAM(t, n, 4)
#define SYS_PARAM5(t, n) SYS_PARAM(t, n, 5)
#define SYS_PARAM6(t, n) SYS_PARAM(t, n, 6)

#define SYS_PARAM1_VALIDATE(t, n, f, a) SYS_PARAM_VALIDATE(t, n, 1, f, a)
#define SYS_PARAM2_VALIDATE(t, n, f, a) SYS_PARAM_VALIDATE(t, n, 2, f, a)
#define SYS_PARAM3_VALIDATE(t, n, f, a) SYS_PARAM_VALIDATE(t, n, 3, f, a)
#define SYS_PARAM4_VALIDATE(t, n, f, a) SYS_PARAM_VALIDATE(t, n, 4, f, a)
#define SYS_PARAM5_VALIDATE(t, n, f, a) SYS_PARAM_VALIDATE(t, n, 5, f, a)
#define SYS_PARAM6_VALIDATE(t, n, f, a) SYS_PARAM_VALIDATE(t, n, 6, f, a)

#define SYS_PARAM1_TRANSFORM(t, n, ot, f) SYS_PARAM_TRANSFORM(t, n, ot, 1, f)
#define SYS_PARAM2_TRANSFORM(t, n, ot, f) SYS_PARAM_TRANSFORM(t, n, ot, 2, f)
#define SYS_PARAM3_TRANSFORM(t, n, ot, f) SYS_PARAM_TRANSFORM(t, n, ot, 3, f)
#define SYS_PARAM4_TRANSFORM(t, n, ot, f) SYS_PARAM_TRANSFORM(t, n, ot, 4, f)
#define SYS_PARAM5_TRANSFORM(t, n, ot, f) SYS_PARAM_TRANSFORM(t, n, ot, 5, f)
#define SYS_PARAM6_TRANSFORM(t, n, ot, f) SYS_PARAM_TRANSFORM(t, n, ot, 6, f)

#define SYS_BEGIN()                                                                                           \
    do {                                                                                                      \
        get_current_task()->user_task_state = (task_state);                                                   \
        get_current_task()->last_system_call = task_get_sys_call_number(get_current_task()->user_task_state); \
        task_set_sys_call_return_value(get_current_task()->user_task_state, -EINTR);                          \
        get_current_task()->in_kernel = true;                                                                 \
        get_current_task()->sched_state = RUNNING_UNINTERRUPTIBLE;                                            \
        enable_interrupts();                                                                                  \
    } while (0)

#define SYS_BEGIN_CAN_SEND_SELF_SIGNALS()                                                                     \
    do {                                                                                                      \
        get_current_task()->user_task_state = (task_state);                                                   \
        get_current_task()->last_system_call = task_get_sys_call_number(get_current_task()->user_task_state); \
        get_current_task()->in_kernel = true;                                                                 \
        get_current_task()->sched_state = RUNNING_UNINTERRUPTIBLE;                                            \
        task_set_sys_call_return_value(get_current_task()->user_task_state, 0);                               \
        enable_interrupts();                                                                                  \
    } while (0)

#define SYS_BEGIN_SIGSUSPEND()                                                                                \
    do {                                                                                                      \
        get_current_task()->user_task_state = (task_state);                                                   \
        get_current_task()->last_system_call = task_get_sys_call_number(get_current_task()->user_task_state); \
        task_set_sys_call_return_value(get_current_task()->user_task_state, -EINTR);                          \
        get_current_task()->sched_state = RUNNING_UNINTERRUPTIBLE;                                            \
        get_current_task()->in_kernel = true;                                                                 \
        get_current_task()->in_sigsuspend = true;                                                             \
    } while (0)

#define SYS_BEGIN_PSELECT()                                                                                   \
    do {                                                                                                      \
        get_current_task()->user_task_state = (task_state);                                                   \
        get_current_task()->last_system_call = task_get_sys_call_number(get_current_task()->user_task_state); \
        task_set_sys_call_return_value(get_current_task()->user_task_state, -EINTR);                          \
        get_current_task()->sched_state = RUNNING_UNINTERRUPTIBLE;                                            \
        get_current_task()->in_kernel = true;                                                                 \
    } while (0)

#define SYS_RETURN(val)                                          \
    do {                                                         \
        uintptr_t _val = (uintptr_t) (val);                      \
        disable_interrupts();                                    \
        task_set_sys_call_return_value(task_state, _val);        \
        task_do_sigs_if_needed(get_current_task());              \
        task_yield_if_state_changed(get_current_task());         \
        get_current_task()->in_kernel = false;                   \
        get_current_task()->user_task_state = NULL;              \
        get_current_task()->sched_state = RUNNING_INTERRUPTIBLE; \
        return;                                                  \
    } while (0)

#define SYS_RETURN_DONT_CHECK_SIGNALS(val)                       \
    do {                                                         \
        uintptr_t _val = (uintptr_t) (val);                      \
        disable_interrupts();                                    \
        task_set_sys_call_return_value(task_state, _val);        \
        task_yield_if_state_changed(get_current_task());         \
        get_current_task()->in_kernel = false;                   \
        get_current_task()->user_task_state = NULL;              \
        get_current_task()->sched_state = RUNNING_INTERRUPTIBLE; \
        return;                                                  \
    } while (0)

#define SYS_RETURN_RESTORE_SIGMASK(val)                                         \
    do {                                                                        \
        uint64_t _val = (uint64_t) (val);                                       \
        disable_interrupts();                                                   \
        task_set_sys_call_return_value(task_state, _val);                       \
        if (_val == (uint64_t) -EINTR) {                                        \
            task_do_sigs_if_needed(get_current_task());                         \
            task_yield_if_state_changed(get_current_task());                    \
        }                                                                       \
        memcpy(&current->sig_mask, &current->saved_sig_mask, sizeof(sigset_t)); \
        task_do_sigs_if_needed(get_current_task());                             \
        task_yield_if_state_changed(get_current_task());                        \
        get_current_task()->in_kernel = false;                                  \
        get_current_task()->in_sigsuspend = false;                              \
        get_current_task()->sched_state = RUNNING_INTERRUPTIBLE;                \
        return;                                                                 \
    } while (0)

#define SYS_RETURN_NORETURN()                                    \
    do {                                                         \
        disable_interrupts();                                    \
        task_yield_if_state_changed(get_current_task());         \
        get_current_task()->in_kernel = false;                   \
        get_current_task()->user_task_state = NULL;              \
        get_current_task()->sched_state = RUNNING_INTERRUPTIBLE; \
        return;                                                  \
    } while (0)

static int get_file(int fd, struct file **file) {
    if (fd < 0 || fd >= FOPEN_MAX) {
        return -EBADF;
    }

    struct process *current = get_current_task()->process;
    if (!current->files[fd].file) {
        return -EBADF;
    }

    *file = current->files[fd].file;
    return 0;
}

static int get_file_desc(int fd, struct file_descriptor **desc) {
    if (fd < 0 || fd >= FOPEN_MAX) {
        return -EBADF;
    }

    struct process *current = get_current_task()->process;
    if (!current->files[fd].file) {
        return -EBADF;
    }

    *desc = &current->files[fd];
    return 0;
}

static int get_at_directory(int fd, struct tnode **tnode) {
    struct process *current = get_current_task()->process;
    if (fd == AT_FDCWD) {
        *tnode = current->cwd;
        return 0;
    }

    if (fd < 0 || fd >= FOPEN_MAX) {
        return -EBADF;
    }

    struct file *file = current->files[fd].file;
    if (!file) {
        return -EBADF;
    }

    if (!(file->flags & FS_DIR)) {
        return -ENOTDIR;
    }

    *tnode = fs_get_tnode_for_file(file);
    if (!*tnode) {
        return -ENOTDIR;
    }
    return 0;
}

static int get_file_tnode(int fd, struct tnode **tnode) {
    struct process *current = get_current_task()->process;
    if (fd == AT_FDCWD) {
        *tnode = current->cwd;
        return 0;
    }

    if (fd < 0 || fd >= FOPEN_MAX) {
        return -EBADF;
    }

    struct file *file = current->files[fd].file;
    if (!file) {
        return -EBADF;
    }

    *tnode = fs_get_tnode_for_file(file);
    if (!*tnode) {
        return -ENOTDIR;
    }
    return 0;
}

static int get_socket(int fd, struct file **filep) {
    if (fd < 0 || fd > FOPEN_MAX) {
        return -EBADF;
    }

    struct file *file = get_current_task()->process->files[fd].file;
    if (!file) {
        return -EBADF;
    }

    if (!(file->flags & FS_SOCKET)) {
        return -ENOTSOCK;
    }

    *filep = file;
    return 0;
}

static int get_clock(clockid_t id, struct clock **clockp) {
    struct clock *clock = time_get_clock(id);
    if (!clock) {
        return -EINVAL;
    }

    *clockp = clock;
    return 0;
}

static int get_timer(timer_t id, struct timer **timerp) {
    struct timer *timer = time_get_timer(id);
    if (!timer) {
        return -EINVAL;
    }

    *timerp = timer;
    return 0;
}

SYS_CALL(exit) {
    SYS_BEGIN();

    SYS_PARAM1(int, exit_code);

    struct process *process = get_current_process();
    mutex_lock(&process->lock);
    // Some other thread exited first, we shouldn't overwrite their exit code.
    if (get_current_task()->should_exit) {
        mutex_unlock(&process->lock);
        SYS_RETURN_NORETURN();
    }

    if (process->should_profile) {
        spin_lock(&process->profile_buffer_lock);
        proc_record_profile_stack(NULL);
        spin_unlock(&process->profile_buffer_lock);
    }

    exit_process(process, NULL);
    proc_set_process_state(process, PS_TERMINATED, exit_code, false);
    mutex_unlock(&process->lock);

#ifdef EXIT_DEBUG
    debug_log("Process Exited: [ %d, %d ]\n", process->pid, exit_code);
#endif /* EXIT_DEBUG */

    SYS_RETURN_NORETURN();
}

SYS_CALL(sbrk) {
    SYS_BEGIN();

    SYS_PARAM1(intptr_t, increment);

    void *res;
    if (increment < 0) {
        res = add_vm_pages_end(0, VM_PROCESS_HEAP);
        remove_vm_pages_end(-increment, VM_PROCESS_HEAP);
    } else {
        res = add_vm_pages_end(increment, VM_PROCESS_HEAP);
    }

    if (res == NULL) {
        SYS_RETURN(-ENOMEM);
    }

    SYS_RETURN(res);
}

SYS_CALL(fork) {
    SYS_BEGIN();
    SYS_RETURN(proc_fork());
}

SYS_CALL(openat) {
    SYS_BEGIN();

    SYS_PARAM1_TRANSFORM(struct tnode *, base, int, get_at_directory);
    SYS_PARAM2_VALIDATE(const char *, path, validate_path, -1);
    SYS_PARAM3(int, flags);
    SYS_PARAM4(mode_t, mode);

    assert(path != NULL);

    int error = 0;

    struct task *task = get_current_task();

    struct file *file = fs_openat(base, path, flags, mode, &error);
    if (error < 0) {
        SYS_RETURN(error);
    }

    for (int i = 0; i < FOPEN_MAX; i++) {
        if (task->process->files[i].file == NULL) {
            task->process->files[i].file = file;
            task->process->files[i].fd_flags = (flags & O_CLOEXEC) ? FD_CLOEXEC : 0;
            SYS_RETURN(i);
        }
    }

    SYS_RETURN(-EMFILE);
}

SYS_CALL(read) {
    SYS_BEGIN();

    SYS_PARAM1_TRANSFORM(struct file *, file, int, get_file);
    SYS_PARAM3(size_t, count);
    SYS_PARAM2_VALIDATE(char *, buf, validate_write, count);

    SYS_RETURN(fs_read(file, buf, count));
}

SYS_CALL(write) {
    SYS_BEGIN();

    SYS_PARAM1_TRANSFORM(struct file *, file, int, get_file);
    SYS_PARAM3(size_t, count);
    SYS_PARAM2_VALIDATE(const void *, buf, validate_read, count);

    SYS_RETURN(fs_write(file, buf, count));
}

SYS_CALL(close) {
    SYS_BEGIN();

    SYS_PARAM1_TRANSFORM(struct file_descriptor *, desc, int, get_file_desc);

    // NOTE: the file descriptor is deallocated even on error.
    int error = fs_close(desc->file);
    desc->file = NULL;

    SYS_RETURN(error);
}

SYS_CALL(execve) {
    SYS_BEGIN();

    SYS_PARAM1_VALIDATE(char *, path, validate_path, -1);
    SYS_PARAM2_VALIDATE(char **, argv, validate_string_array, -1);
    SYS_PARAM3_VALIDATE(char **, envp, validate_string_array, -1);

    SYS_RETURN(proc_execve(path, argv, envp));
}

SYS_CALL(waitpid) {
    SYS_BEGIN();

    SYS_PARAM1(pid_t, pid);
    SYS_PARAM2_VALIDATE(int *, status, validate_write_or_null, sizeof(int));
    SYS_PARAM3(int, flags);

    SYS_RETURN(proc_waitpid(pid, status, flags));
}

SYS_CALL(getpid) {
    SYS_BEGIN();
    SYS_RETURN(get_current_task()->process->pid);
}

SYS_CALL(getcwd) {
    SYS_BEGIN();

    SYS_PARAM2(size_t, size);
    SYS_PARAM1_VALIDATE(char *, buffer, validate_write, size);

    struct task *current = get_current_task();
    char *full_path = get_tnode_path(current->process->cwd);

    size_t len = strlen(full_path);
    if (len > size) {
        free(full_path);
        SYS_RETURN(-ERANGE);
    }

    strcpy(buffer, full_path);

    free(full_path);
    SYS_RETURN(buffer);
}

SYS_CALL(chdir) {
    SYS_BEGIN();

    SYS_PARAM1_VALIDATE(const char *, path, validate_path, -1);

    struct task *task = get_current_task();

    struct tnode *tnode;
    int ret = iname(path, 0, &tnode);
    if (ret < 0) {
        SYS_RETURN(ret);
    }

    if (!(tnode->inode->flags & FS_DIR)) {
        drop_tnode(tnode);
        SYS_RETURN(-ENOTDIR);
    }

    if (!fs_can_execute_inode(tnode->inode)) {
        drop_tnode(tnode);
        SYS_RETURN(-EACCES);
    }

    drop_tnode(task->process->cwd);
    task->process->cwd = tnode;

    SYS_RETURN(0);
}

SYS_CALL(fstatat) {
    SYS_BEGIN();

    SYS_PARAM1_TRANSFORM(struct tnode *, base, int, get_file_tnode);
    SYS_PARAM2_VALIDATE(const char *, path, validate_path, 0);
    SYS_PARAM3_VALIDATE(void *, stat_struct, validate_write, sizeof(struct stat));
    SYS_PARAM4(int, flags);

    SYS_RETURN(fs_fstatat(base, path, stat_struct, flags));
}

SYS_CALL(lseek) {
    SYS_BEGIN();

    SYS_PARAM1_TRANSFORM(struct file *, file, int, get_file);
    SYS_PARAM2(off_t, offset);
    SYS_PARAM3(int, whence);

    SYS_RETURN(fs_seek(file, offset, whence));
}

SYS_CALL(ioctl) {
    SYS_BEGIN();

    SYS_PARAM1_TRANSFORM(struct file_descriptor *, desc, int, get_file_desc);
    SYS_PARAM2(unsigned long, request);
    SYS_PARAM3(void *, argp);

    SYS_RETURN(fs_ioctl(desc, request, argp));
}

SYS_CALL(ftruncate) {
    SYS_BEGIN();

    SYS_PARAM1_TRANSFORM(struct file *, file, int, get_file);
    SYS_PARAM2_VALIDATE(off_t, length, validate_positive, 1);

    SYS_RETURN(fs_ftruncate(file, length));
}

SYS_CALL(gettimeofday) {
    SYS_BEGIN();

    SYS_PARAM1_VALIDATE(struct timeval *, tv, validate_write_or_null, sizeof(struct timeval));
    SYS_PARAM2_VALIDATE(struct timezone *, tz, validate_write_or_null, sizeof(struct timezone));

    struct timespec time = time_read_clock(CLOCK_REALTIME);

    if (tv) {
        tv->tv_sec = time.tv_sec;
        tv->tv_usec = time.tv_nsec / 1000;
    }

    if (tz) {
        tz->tz_minuteswest = 0;
        tz->tz_minuteswest = 0;
    }

    SYS_RETURN(0);
}

SYS_CALL(mkdir) {
    SYS_BEGIN();

    SYS_PARAM1_VALIDATE(const char *, pathname, validate_path, -1);
    SYS_PARAM2(mode_t, mode);

    int error = 0;
    struct tnode *tnode = fs_mkdir(pathname, mode, &error);
    if (tnode) {
        drop_tnode(tnode);
    }
    SYS_RETURN(error);
}

SYS_CALL(dup2) {
    SYS_BEGIN();

    SYS_PARAM1(int, oldfd);
    SYS_PARAM2(int, newfd);

#ifdef DUP_DEBUG
    debug_log("Dup: [ %d, %d ]\n", oldfd, newfd);
#endif /* DUP_DEBUG */

    if (oldfd < 0 || oldfd >= FOPEN_MAX || newfd < 0 || newfd >= FOPEN_MAX) {
        SYS_RETURN(-EBADF);
    }

    struct task *task = get_current_task();
    if (!task->process->files[oldfd].file) {
        SYS_RETURN(-EBADF);
    }

    if (oldfd == newfd) {
        SYS_RETURN(newfd);
    }

    if (task->process->files[newfd].file != NULL) {
        // Explicitly ignore any errors when closing, as semantically closing the file
        // descriptor occurs regardless of whether or close succeeds.
        fs_close(task->process->files[newfd].file);
    }

    task->process->files[newfd] = fs_dup(task->process->files[oldfd]);
    SYS_RETURN(newfd);
}

SYS_CALL(pipe) {
    SYS_BEGIN();

    SYS_PARAM1_VALIDATE(int *, pipefd, validate_write, 2 * sizeof(int));

    struct file *pipe_files[2];
    int ret = fs_create_pipe(pipe_files);
    if (ret != 0) {
        SYS_RETURN(ret);
    }

    struct task *task = get_current_task();
    int j = 0;
    for (int i = 0; j < 2 && i < FOPEN_MAX; i++) {
        if (task->process->files[i].file == NULL) {
            task->process->files[i] = (struct file_descriptor) { pipe_files[j], 0 };
            pipefd[j] = i;
            j++;
        }
    }

    assert(j == 2);

    SYS_RETURN(0);
}

SYS_CALL(unlink) {
    SYS_BEGIN();

    SYS_PARAM1_VALIDATE(const char *, path, validate_path, -1);

    SYS_RETURN(fs_unlink(path, false));
}

SYS_CALL(rmdir) {
    SYS_BEGIN();

    SYS_PARAM1_VALIDATE(const char *, path, validate_path, -1);

    SYS_RETURN(fs_rmdir(path));
}

SYS_CALL(fchmodat) {
    SYS_BEGIN();

    SYS_PARAM1_TRANSFORM(struct tnode *, base, int, get_file_tnode);
    SYS_PARAM2_VALIDATE(const char *, path, validate_path, 0);
    SYS_PARAM3(mode_t, mode);
    SYS_PARAM4(int, flags);

    SYS_RETURN(fs_fchmodat(base, path, mode, flags));
}

SYS_CALL(kill) {
    SYS_BEGIN_CAN_SEND_SELF_SIGNALS();

    SYS_PARAM1(pid_t, pid);
    SYS_PARAM2_VALIDATE(int, signum, validate_signal_number, -1);

    struct task *current = get_current_task();

    // pid -1 is not yet implemented
    if (pid == -1) {
        SYS_RETURN(-EINVAL);
    }

    if (pid == 0) {
        pid = -current->process->pgid;
    }

    if (pid < 0) {
        SYS_RETURN(signal_process_group(-pid, signum));
    } else {
        SYS_RETURN(signal_process(pid, signum));
    }
}

SYS_CALL(setpgid) {
    SYS_BEGIN();

    SYS_PARAM1(pid_t, pid);
    SYS_PARAM2(pid_t, pgid);

    if (pgid < 0) {
        SYS_RETURN(-EINVAL);
    }

    if (pid == 0) {
        pid = get_current_task()->process->pid;
    }

    if (pgid == 0) {
        pgid = get_current_task()->process->pid;
    }

#ifdef SET_PGID_DEBUG
    debug_log("Setting pgid: [ %d, %d, %d ]\n", pid, get_current_task()->pgid, pgid);
#endif /* SET_PGID_DEBUG */

    struct process *process = find_by_pid(pid);
    if (process == NULL) {
        SYS_RETURN(-ESRCH);
    }

    mutex_lock(&process->lock);
    process->pgid = pgid;
    mutex_unlock(&process->lock);

    SYS_RETURN(0);
}

SYS_CALL(sigaction) {
    SYS_BEGIN();

    SYS_PARAM1_VALIDATE(int, signum, validate_signal_number, -1);
    SYS_PARAM2_VALIDATE(const struct sigaction *, act, validate_read_or_null, sizeof(struct sigaction));
    SYS_PARAM3_VALIDATE(struct sigaction *, old_act, validate_write_or_null, sizeof(struct sigaction));

    if (signum <= 0 || signum > _NSIG) {
        SYS_RETURN(-EINVAL);
    }

    struct task *current = get_current_task();
    if (old_act != NULL) {
        memcpy(old_act, &current->process->sig_state[signum], sizeof(struct sigaction));
    }

    if (act != NULL) {
#ifdef SIGACTION_DEBUG
        debug_log("Changing signal state: [ %s, %#.16lX ]\n", strsignal(signum), (uintptr_t) act->sa_handler);
#endif /* SIGACTION_DEBUG */
        memcpy(&current->process->sig_state[signum], act, sizeof(struct sigaction));
    }

    SYS_RETURN(0);
}

SYS_CALL(sigreturn) {
    struct task *task = get_current_task();
    siginfo_t *info = (siginfo_t *) (((uint64_t *) task_get_stack_pointer(task_state)) + 1);
    ucontext_t *context = (ucontext_t *) (info + 1);
    uint8_t *saved_fpu_state = (uint8_t *) (context + 1);
#ifdef SIGRETURN_DEBUG
    debug_log("Sig return: [ %p ]\n", context);
#endif /* SIGRETURN_DEBUG */

    memcpy(task->fpu.aligned_state, saved_fpu_state, FPU_IMAGE_SIZE);
    memcpy(&task->arch_task.task_state, &context->uc_mcontext, sizeof(struct task_state));

    // Restore mask
    task->sig_mask = context->uc_sigmask;

#ifdef SIGRETURN_DEBUG
    debug_log("State: [ %#.16lX, %#.16lX, %#.16lX, %#.16lX, %#.16lX ]\n", task->arch_task.task_state.stack_state.cs,
              task->arch_task.task_state.stack_state.rip, task->arch_task.task_state.stack_state.rflags,
              task->arch_task.task_state.stack_state.rsp, task->arch_task.task_state.stack_state.ss);
#endif /* SIGRETURN_DEBUG */

    task_do_sigs_if_needed(task);
    __run_task(&task->arch_task);
}

SYS_CALL(sigprocmask) {
    SYS_BEGIN();

    SYS_PARAM1(int, how);
    SYS_PARAM2_VALIDATE(const sigset_t *, set, validate_read_or_null, sizeof(sigset_t));
    SYS_PARAM3_VALIDATE(sigset_t *, old, validate_write_or_null, sizeof(sigset_t));

    struct task *current = get_current_task();

    if (old) {
        *old = current->sig_mask;
    }

    if (set) {
#ifdef SIGPROCMASK_DEBUG
        debug_log("Setting sigprocmask: [ %d, %#.16lX ]\n", how, *set);
#endif /* SIGPROCMASK_DEBUG */

        switch (how) {
            case SIG_SETMASK:
                current->sig_mask = *set;
                break;
            case SIG_BLOCK:
                current->sig_mask |= *set;
                break;
            case SIG_UNBLOCK:
                current->sig_mask &= ~*set;
                break;
            default:
                SYS_RETURN(-EINVAL);
        }

#ifdef SIGPROCMASK_DEBUG
        debug_log("New mask: [ %#.16lX ]\n", current->sig_mask);
#endif /* SIGPROCMASK_DEBUG */
    }

    SYS_RETURN(0);
}

SYS_CALL(dup) {
    SYS_BEGIN();

    SYS_PARAM1_TRANSFORM(struct file_descriptor *, old_desc, int, get_file_desc);

    struct task *current = get_current_task();

    // Should lock task to prevent races
    for (size_t i = 0; i < FOPEN_MAX; i++) {
        if (current->process->files[i].file == NULL) {
            current->process->files[i] = fs_dup(*old_desc);
#ifdef DUP_DEBUG
            debug_log("Dup: [ %d, %lu ]\n", oldfd, i);
#endif /* DUP_DEBUG */
            SYS_RETURN(i);
        }
    }

    SYS_RETURN(-EMFILE);
}

SYS_CALL(getpgid) {
    SYS_BEGIN();

    SYS_PARAM1_VALIDATE(pid_t, pid, validate_positive, 1);
    if (pid == 0) {
        SYS_RETURN(get_current_task()->process->pgid);
    }

    struct process *process = find_by_pid(pid);
    SYS_RETURN(process->pgid);
}

SYS_CALL(faccessat) {
    SYS_BEGIN();

    SYS_PARAM1_TRANSFORM(struct tnode *, base, int, get_at_directory);
    SYS_PARAM2_VALIDATE(const char *, path, validate_path, -1);
    SYS_PARAM3(int, mode);
    SYS_PARAM4(int, flags);

    SYS_RETURN(fs_faccessat(base, path, mode, flags));
}

SYS_CALL(accept4) {
    SYS_BEGIN();

    SYS_PARAM1_TRANSFORM(struct file *, file, int, get_socket);
    SYS_PARAM3_VALIDATE(socklen_t *, addrlen, validate_write_or_null, sizeof(socklen_t));
    SYS_PARAM4(int, flags);
    if (addrlen) {
        SYS_PARAM2_VALIDATE(struct sockaddr *, addr, validate_write, *addrlen);
        SYS_RETURN(net_accept(file, addr, addrlen, flags));
    } else {
        SYS_PARAM2(struct sockaddr *, addr);
        if (addr) {
            SYS_RETURN(-EINVAL);
        }
        SYS_RETURN(net_accept(file, addr, addrlen, flags));
    }
}

SYS_CALL(bind) {
    SYS_BEGIN();

    SYS_PARAM1_TRANSFORM(struct file *, file, int, get_socket);
    SYS_PARAM3(socklen_t, addrlen);
    SYS_PARAM2_VALIDATE(const struct sockaddr *, addr, validate_read, addrlen);

    SYS_RETURN(net_bind(file, addr, addrlen));
}

SYS_CALL(connect) {
    SYS_BEGIN();

    SYS_PARAM1_TRANSFORM(struct file *, file, int, get_socket);
    SYS_PARAM3(socklen_t, addrlen);
    SYS_PARAM2_VALIDATE(const struct sockaddr *, addr, validate_read, addrlen);

    SYS_RETURN(net_connect(file, addr, addrlen));
}

SYS_CALL(listen) {
    SYS_BEGIN();

    SYS_PARAM1_TRANSFORM(struct file *, file, int, get_socket);
    SYS_PARAM2_VALIDATE(int, backlog, validate_positive, 0);

    SYS_RETURN(net_listen(file, backlog));
}

SYS_CALL(socket) {
    SYS_BEGIN();

    SYS_PARAM1(int, domain);
    SYS_PARAM2(int, type);
    SYS_PARAM3(int, protocol);

    SYS_RETURN(net_socket(domain, type, protocol));
}

SYS_CALL(shutdown) {
    SYS_BEGIN();

    SYS_PARAM1_TRANSFORM(struct file *, file, int, get_socket);
    SYS_PARAM2(int, how);

    (void) file;
    (void) how;

    SYS_RETURN(-ENOSYS);
}

SYS_CALL(getsockopt) {
    SYS_BEGIN();

    SYS_PARAM1_TRANSFORM(struct file *, file, int, get_socket);
    SYS_PARAM2(int, level);
    SYS_PARAM3(int, optname);
    SYS_PARAM5_VALIDATE(socklen_t *, optlen, validate_write, sizeof(socklen_t));
    SYS_PARAM4_VALIDATE(void *, optval, validate_write, *optlen);

    SYS_RETURN(net_getsockopt(file, level, optname, optval, optlen));
}

SYS_CALL(setsockopt) {
    SYS_BEGIN();

    SYS_PARAM1_TRANSFORM(struct file *, file, int, get_socket);
    SYS_PARAM2(int, level);
    SYS_PARAM3(int, optname);
    SYS_PARAM5(socklen_t, optlen);
    SYS_PARAM4_VALIDATE(const void *, optval, validate_read, optlen);

    SYS_RETURN(net_setsockopt(file, level, optname, optval, optlen));
}

SYS_CALL(getpeername) {
    SYS_BEGIN();

    SYS_PARAM1_TRANSFORM(struct file *, file, int, get_socket);
    SYS_PARAM3_VALIDATE(socklen_t *, addrlen, validate_write, sizeof(socklen_t));
    SYS_PARAM2_VALIDATE(struct sockaddr *, addr, validate_write, *addrlen);

    SYS_RETURN(net_getpeername(file, addr, addrlen));
}

SYS_CALL(getsockname) {
    SYS_BEGIN();

    SYS_PARAM1_TRANSFORM(struct file *, file, int, get_socket);
    SYS_PARAM3_VALIDATE(socklen_t *, addrlen, validate_write, sizeof(socklen_t));
    SYS_PARAM2_VALIDATE(struct sockaddr *, addr, validate_write, *addrlen);

    SYS_RETURN(net_getsockname(file, addr, addrlen));
}

SYS_CALL(sendto) {
    SYS_BEGIN();

    SYS_PARAM1_TRANSFORM(struct file *, file, int, get_socket);
    SYS_PARAM3(size_t, len);
    SYS_PARAM2_VALIDATE(const void *, buf, validate_read, len);
    SYS_PARAM4(int, flags);
    SYS_PARAM6(socklen_t, addrlen);
    SYS_PARAM5_VALIDATE(const struct sockaddr *, dest, validate_read_or_null, addrlen);

    SYS_RETURN(net_sendto(file, buf, len, flags, dest, addrlen));
}

SYS_CALL(recvfrom) {
    SYS_BEGIN();

    SYS_PARAM1_TRANSFORM(struct file *, file, int, get_socket);
    SYS_PARAM3(size_t, len);
    SYS_PARAM2_VALIDATE(void *, buf, validate_write, len);
    SYS_PARAM4(int, flags);
    SYS_PARAM6_VALIDATE(socklen_t *, addrlen, validate_write_or_null, sizeof(socklen_t));
    SYS_PARAM5_VALIDATE(struct sockaddr *, source, validate_write_or_null, addrlen ? *addrlen : 0);

    SYS_RETURN(net_recvfrom(file, buf, len, flags, source, addrlen));
}

SYS_CALL(mmap) {
    SYS_BEGIN();

    SYS_PARAM1(void *, addr);
    SYS_PARAM2_VALIDATE(size_t, length, validate_positive, 0);
    SYS_PARAM3(int, prot);
    SYS_PARAM4(int, flags);
    SYS_PARAM5(int, fd);
    SYS_PARAM6(off_t, offset);

    if (flags & MAP_ANONYMOUS) {
        struct vm_region *region = map_region(addr, length, prot, flags, flags & MAP_STACK ? VM_TASK_STACK : VM_PROCESS_ANON_MAPPING);
        if (region == NULL) {
            SYS_RETURN(-ENOMEM);
        }

        struct vm_object *object = vm_create_anon_object(length);
        if (!object) {
            SYS_RETURN(-ENOMEM);
        }

        region->flags |= (flags & MAP_SHARED) ? VM_SHARED : 0;
        region->vm_object = object;
        region->vm_object_offset = 0;

        vm_map_region_with_object(region);
        SYS_RETURN(region->start);
    }

    if (fd < 0 || fd > FOPEN_MAX) {
        SYS_RETURN(-EINVAL);
    }

    struct file *file = get_current_task()->process->files[fd].file;
    if (!file) {
        SYS_RETURN(-EBADF);
    }

    SYS_RETURN(fs_mmap(addr, length, prot, flags, file, offset));
}

SYS_CALL(munmap) {
    SYS_BEGIN();

    SYS_PARAM1(void *, addr);
    SYS_PARAM2(size_t, length);

    SYS_RETURN(unmap_range((uintptr_t) addr, length));
}

SYS_CALL(rename) {
    SYS_BEGIN();

    SYS_PARAM1_VALIDATE(const char *, old_path, validate_path, -1);
    SYS_PARAM2_VALIDATE(const char *, new_path, validate_path, -1);

    SYS_RETURN(fs_rename(old_path, new_path));
}

SYS_CALL(fcntl) {
    SYS_BEGIN();

    SYS_PARAM1_TRANSFORM(struct file_descriptor *, desc, int, get_file_desc);
    SYS_PARAM2(int, command);
    SYS_PARAM3(int, arg);

    SYS_RETURN(fs_fcntl(desc, command, arg));
}

SYS_CALL(getppid) {
    SYS_BEGIN();
    SYS_RETURN(proc_getppid(get_current_process()));
}

SYS_CALL(sigsuspend) {
    SYS_BEGIN_SIGSUSPEND();

    SYS_PARAM1_VALIDATE(const sigset_t *, mask, validate_read, sizeof(sigset_t));

    struct task *current = get_current_task();

    memcpy(&current->saved_sig_mask, &current->sig_mask, sizeof(sigset_t));
    memcpy(&current->sig_mask, mask, sizeof(sigset_t));

    int ret = wait_prepare_interruptible(current);
    if (ret) {
        SYS_RETURN_RESTORE_SIGMASK(ret);
    }
    SYS_RETURN_RESTORE_SIGMASK(wait_do(current));
}

SYS_CALL(times) {
    SYS_BEGIN();

    SYS_PARAM1_VALIDATE(struct tms *, tms, validate_write, sizeof(struct tms));

    struct task *current = get_current_task();

    // Times values are measured in units of 1/100 seconds
    spin_lock(&current->process->children_lock);
    tms->tms_utime = current->process->rusage_self.ru_utime.tv_usec * 100 + current->process->rusage_self.ru_utime.tv_usec / 10000;
    tms->tms_stime = current->process->rusage_self.ru_stime.tv_usec * 100 + current->process->rusage_self.ru_stime.tv_usec / 10000;
    tms->tms_cutime = current->process->rusage_children.ru_utime.tv_usec * 100 + current->process->rusage_children.ru_utime.tv_usec / 10000;
    tms->tms_cstime = current->process->rusage_children.ru_stime.tv_usec * 100 + current->process->rusage_children.ru_stime.tv_usec / 10000;
    spin_unlock(&current->process->children_lock);

    struct timespec now = time_read_clock(CLOCK_MONOTONIC);
    SYS_RETURN(now.tv_sec * 100 + now.tv_nsec / 10000000L);
}

SYS_CALL(create_task) {
    SYS_BEGIN();

    SYS_PARAM1_VALIDATE(struct create_task_args *, args, validate_read, sizeof(struct create_task_args));

    // FIXME: a more robust check for the validity of the new stack could be helpful.
    uintptr_t new_rsp = (args->stack_start - sizeof(uintptr_t)) & ~0xF;
    SYS_VALIDATE((uintptr_t *) new_rsp, sizeof(uintptr_t), validate_write);

    debug_log("Creating task: [ %#.16lX, %#.16lX, %p, %p ]\n", args->entry, args->stack_start, args->tid_ptr, args->thread_self_pointer);

    struct task *current = get_current_task();
    proc_bump_process(current->process);

    struct task *task = calloc(1, sizeof(struct task));
    task->process = current->process;
    task->sig_mask = current->sig_mask;
    task->sig_pending = 0;
    init_list(&task->queued_signals);
    init_spinlock(&task->sig_lock);
    init_spinlock(&task->unblock_lock);
    task->sched_state = RUNNING_INTERRUPTIBLE;
    task->tid = get_next_tid();
    task->locked_robust_mutex_list_head = args->locked_robust_mutex_list_head;
    task->task_clock = time_create_clock(CLOCK_THREAD_CPUTIME_ID);

    task_align_fpu(task);
    memcpy(task->fpu.aligned_state, get_idle_task()->fpu.aligned_state, FPU_IMAGE_SIZE);

    task->kernel_stack = vm_allocate_kernel_region(KERNEL_STACK_SIZE);
    task->arch_task.user_thread_pointer = args->thread_self_pointer;

    arch_sys_create_task(task, args->entry, new_rsp, args->push_onto_stack, args->arg);

    *args->tid_ptr = task->tid;

    mutex_lock(&task->process->lock);
    list_append(&task->process->task_list, &task->process_list);
    mutex_unlock(&task->process->lock);

    sched_add_task(task);

    SYS_RETURN(0);
}

SYS_CALL(exit_task) {
    SYS_BEGIN();

    /* Disable Interrups To Prevent Premature Task Removal, Since Sched State Is Set */
    disable_interrupts();

    struct task *task = get_current_task();
    task_set_state_to_exiting(task);

    // At this point there should be no locked mutexes in the task, since it explicitly
    // exited. Also, the memory could now be freed and is no longer valid.
    task->locked_robust_mutex_list_head = NULL;

    debug_log("Task Exited: [ %d, %d ]\n", task->process->pid, task->tid);
    SYS_RETURN_NORETURN();
}

SYS_CALL(os_mutex) {
    SYS_BEGIN();

    SYS_PARAM1_VALIDATE(unsigned int *, __protected, validate_write, sizeof(unsigned int));
    SYS_PARAM2(int, operation);
    SYS_PARAM3(int, expected);
    SYS_PARAM4(int, to_place);
    SYS_PARAM5(int, to_wake);
    SYS_PARAM6_VALIDATE(unsigned int *, to_wait, validate_write_or_null, sizeof(unsigned int));

    struct task *current = get_current_task();
    struct wait_queue_entry wait_queue_entry = { .task = current };

    unsigned int *to_aquire = __protected;
    struct user_mutex *to_unlock = NULL;

    switch (operation) {
        case MUTEX_AQUIRE:
        do_mutex_aquire : {
            struct user_mutex *um = get_user_mutex_locked(to_aquire);
            if (*to_aquire != (unsigned int) expected) {
                // Case where MUTEX_RELEASE occurs before we lock/create the mutex
                unlock_user_mutex(um);
                SYS_RETURN(0);
            }

            disable_interrupts();
            add_to_user_mutex_queue(um, &wait_queue_entry);
            task_set_state_to_waiting(current);
            current->wait_interruptible = 1;
            if (to_unlock) {
                unlock_user_mutex(to_unlock);
            }
            unlock_user_mutex(um);
            int ret = kernel_yield();
            wait_queue_dequeue_entry(&um->wait_queue, &wait_queue_entry, __func__);
            SYS_RETURN(ret);
        }
        case MUTEX_WAKE_AND_SET: {
            struct user_mutex *um = get_user_mutex_locked_with_waiters_or_else_write_value(__protected, to_place & ~MUTEX_WAITERS);
            if (um == NULL) {
                SYS_RETURN(0);
            }

            wake_user_mutex(um, to_wake, &to_place);
            *__protected = to_place;
            unlock_user_mutex(um);
            SYS_RETURN(0);
        }
        case MUTEX_RELEASE_AND_WAIT: {
            if (!to_wait) {
                SYS_RETURN(EINVAL);
            }
            to_aquire = to_wait;
            struct user_mutex *um = get_user_mutex_locked_with_waiters_or_else_write_value(__protected, to_place & ~MUTEX_WAITERS);
            if (um == NULL) {
                goto do_mutex_aquire;
            }

            wake_user_mutex(um, to_wake, &to_place);
            to_unlock = um;
            goto do_mutex_aquire;
        }
        default: {
            SYS_RETURN(-EINVAL);
        }
    }
}

SYS_CALL(tgkill) {
    SYS_BEGIN_CAN_SEND_SELF_SIGNALS();

    SYS_PARAM1(int, tgid);
    SYS_PARAM2(int, tid);
    SYS_PARAM3_VALIDATE(int, signum, validate_signal_number, 1);

    struct task *current = get_current_task();

    if (tgid == 0) {
        tgid = current->process->pid;
    }

    if (tid == 0) {
        tid = current->tid;
    }

    SYS_RETURN(signal_task(tgid, tid, signum));
}

SYS_CALL(set_thread_self_pointer) {
    SYS_BEGIN();

    SYS_PARAM1(void *, thread_self_pointer);

    // NOTE: these list items need to be validated later, when they are
    //       traversed during task clean up
    SYS_PARAM2(struct __locked_robust_mutex_node **, locked_robust_mutex_list_head);

    struct task *current = get_current_task();
    current->locked_robust_mutex_list_head = locked_robust_mutex_list_head;

    // Ensure interrupts are disabled since this following code can may change
    // the processor state.
    disable_interrupts();

    arch_task_set_thread_self_pointer(current, thread_self_pointer);

    SYS_RETURN(0);
}

SYS_CALL(mprotect) {
    SYS_BEGIN();

    SYS_PARAM1(void *, addr);
    SYS_PARAM2(size_t, length);
    SYS_PARAM3(int, prot);

    SYS_RETURN(map_range_protections((uintptr_t) addr, length, prot));
}

SYS_CALL(sigpending) {
    SYS_BEGIN();

    SYS_PARAM1_VALIDATE(sigset_t *, set, validate_write, sizeof(sigset_t));

    memcpy(set, &get_current_task()->sig_pending, sizeof(sigset_t));
    SYS_RETURN(0);
}

SYS_CALL(sigaltstack) {
    SYS_BEGIN();

    SYS_PARAM1_VALIDATE(const stack_t *, stack, validate_read_or_null, sizeof(stack_t));
    SYS_PARAM2_VALIDATE(stack_t *, old_stack, validate_write_or_null, sizeof(stack_t));

    struct process *current_process = get_current_task()->process;

    if (old_stack) {
        memcpy(old_stack, &current_process->alt_stack, sizeof(stack_t));
        if (!(old_stack->ss_flags & __SS_ENABLED)) {
            old_stack->ss_flags |= SS_DISABLE;
        } else {
            old_stack->ss_flags &= ~__SS_ENABLED;
        }
    }

    if (stack) {
        if (stack->ss_size < MINSIGSTKSZ) {
            SYS_RETURN(-ENOMEM);
        }

        memcpy(&current_process->alt_stack, stack, sizeof(stack_t));
        if (!(stack->ss_flags & SS_DISABLE)) {
            current_process->alt_stack.ss_flags |= __SS_ENABLED;
        } else {
            current_process->alt_stack.ss_flags &= ~__SS_ENABLED;
        }
    }

    SYS_RETURN(0);
}

SYS_CALL(pselect) {
    SYS_BEGIN_PSELECT();

    SYS_PARAM1_VALIDATE(int, nfds, validate_positive, 1);
    size_t fd_set_size = ((nfds + sizeof(uint8_t) * CHAR_BIT - 1) / sizeof(uint8_t) / CHAR_BIT);

    SYS_PARAM2_VALIDATE(uint8_t *, readfds, validate_write_or_null, fd_set_size);
    SYS_PARAM3_VALIDATE(uint8_t *, writefds, validate_write_or_null, fd_set_size);
    SYS_PARAM4_VALIDATE(uint8_t *, exceptfds, validate_write_or_null, fd_set_size);
    SYS_PARAM5_VALIDATE(const struct timespec *, timeout, validate_read_or_null, sizeof(struct timespec));
    SYS_PARAM6_VALIDATE(const sigset_t *, sigmask, validate_read_or_null, sizeof(sigset_t));

    struct task *current = get_current_task();
    if (sigmask) {
        memcpy(&current->saved_sig_mask, &current->sig_mask, sizeof(sigset_t));
        memcpy(&current->sig_mask, sigmask, sizeof(sigset_t));
        current->in_sigsuspend = true;
    }

    int count = fs_select(nfds, readfds, writefds, exceptfds, timeout);

    if (current->in_sigsuspend) {
        SYS_RETURN_RESTORE_SIGMASK(count);
    }

    SYS_RETURN(count);
}

SYS_CALL(sched_yield) {
    SYS_BEGIN();

    kernel_yield();
    SYS_RETURN(0);
}

SYS_CALL(getuid) {
    SYS_BEGIN();

    SYS_RETURN(get_current_task()->process->uid);
}

SYS_CALL(geteuid) {
    SYS_BEGIN();

    SYS_RETURN(get_current_task()->process->euid);
}

SYS_CALL(getgid) {
    SYS_BEGIN();

    SYS_RETURN(get_current_task()->process->gid);
}

SYS_CALL(getegid) {
    SYS_BEGIN();

    SYS_RETURN(get_current_task()->process->egid);
}

SYS_CALL(setuid) {
    SYS_BEGIN();

    SYS_PARAM1(uid_t, uid);

#ifdef SETUID_HACK
    debug_log("uid hack: [ %p ]\n", (void *) task_get_sys_call_arg1(task_state));
#endif

    struct process *current = get_current_task()->process;
    if (current->euid == 0) {
        current->euid = uid;
        current->uid = uid;
        SYS_RETURN(0);
    }

    if (current->uid != uid) {
        SYS_RETURN(-EPERM);
    }

    current->euid = uid;
    SYS_RETURN(0);
}

SYS_CALL(seteuid) {
    SYS_BEGIN();

    SYS_PARAM1(uid_t, euid);

    struct process *current = get_current_task()->process;
    if (current->euid == 0) {
        current->uid = euid;
        SYS_RETURN(0);
    }

    if (current->euid != euid && current->uid != euid) {
        SYS_RETURN(-EPERM);
    }

    current->euid = euid;
    SYS_RETURN(0);
}

SYS_CALL(setgid) {
    SYS_BEGIN();

    SYS_PARAM1(gid_t, gid);

    struct process *current = get_current_task()->process;
    if (current->euid == 0) {
        current->egid = gid;
        current->gid = gid;
        SYS_RETURN(0);
    }

    if (current->gid != gid) {
        SYS_RETURN(-EPERM);
    }

    current->egid = gid;
    SYS_RETURN(0);
}

SYS_CALL(setegid) {
    SYS_BEGIN();

    SYS_PARAM1(gid_t, egid);

    struct process *current = get_current_task()->process;
    if (current->euid == 0) {
        current->gid = egid;
        SYS_RETURN(0);
    }

    if (current->egid != egid && current->gid != egid) {
        SYS_RETURN(-EPERM);
    }

    current->egid = egid;
    SYS_RETURN(0);
}

SYS_CALL(umask) {
    SYS_BEGIN();

    SYS_PARAM1(mode_t, new_mask);

    struct process *current = get_current_task()->process;
    mutex_lock(&current->lock);

    mode_t old_mask = current->umask;
    current->umask = new_mask;

    mutex_unlock(&current->lock);
    SYS_RETURN(old_mask);
}

SYS_CALL(uname) {
    SYS_BEGIN();

    SYS_PARAM1_VALIDATE(struct utsname *, buf, validate_write, sizeof(struct utsname));

    strcpy(buf->machine, "x86_64");
    strcpy(buf->sysname, "Iros");
    strcpy(buf->release, "0.0.1");
    strcpy(buf->version, "0");
    strcpy(buf->nodename, "iros-dev");

    SYS_RETURN(0);
}

SYS_CALL(getsid) {
    SYS_BEGIN();

    SYS_PARAM1_VALIDATE(pid_t, pid, validate_positive, 1);
    if (pid == 0) {
        SYS_RETURN(get_current_task()->process->sid);
    }

    struct process *process = find_by_pid(pid);
    if (!process) {
        SYS_RETURN(-ESRCH);
    }

    SYS_RETURN(process->sid);
}

SYS_CALL(setsid) {
    SYS_BEGIN();

    int ret = 0;

    struct process *current = get_current_task()->process;
    mutex_lock(&current->lock);

    if (current->pgid == current->pid) {
        ret = -EPERM;
        goto finish_setsid;
    }

    current->sid = current->pid;
    current->pgid = current->pid;

finish_setsid:
    mutex_unlock(&current->lock);
    SYS_RETURN(ret);
}

SYS_CALL(readlink) {
    SYS_BEGIN();

    SYS_PARAM1_VALIDATE(const char *, path, validate_path, -1);
    SYS_PARAM3(size_t, bufsiz);
    SYS_PARAM2_VALIDATE(char *, buf, validate_write, bufsiz);

    SYS_RETURN(fs_readlink(path, buf, bufsiz));
}

SYS_CALL(symlink) {
    SYS_BEGIN();

    SYS_PARAM1_VALIDATE(const char *, target, validate_path, -1);
    SYS_PARAM2_VALIDATE(const char *, linkpath, validate_path, -1);

    SYS_RETURN(fs_symlink(target, linkpath));
}

SYS_CALL(link) {
    SYS_BEGIN();

    SYS_PARAM1_VALIDATE(const char *, oldpath, validate_path, -1);
    SYS_PARAM2_VALIDATE(const char *, newpath, validate_path, -1);

    SYS_RETURN(fs_link(oldpath, newpath));
}

SYS_CALL(fchownat) {
    SYS_BEGIN();

    SYS_PARAM1_TRANSFORM(struct tnode *, base, int, get_file_tnode);
    SYS_PARAM2_VALIDATE(const char *, path, validate_path, 0);
    SYS_PARAM3(uid_t, uid);
    SYS_PARAM4(gid_t, gid);
    SYS_PARAM5(int, flags);

    SYS_RETURN(fs_fchownat(base, path, uid, gid, flags));
}

SYS_CALL(utimensat) {
    SYS_BEGIN();

    SYS_PARAM1_TRANSFORM(struct tnode *, base, int, get_file_tnode);
    SYS_PARAM2_VALIDATE(const char *, path, validate_path, 0);
    SYS_PARAM3_VALIDATE(const struct timespec *, times, validate_read_or_null, 2 * sizeof(struct timespec));
    SYS_PARAM4(int, flags);

    SYS_RETURN(fs_utimensat(base, path, times, flags));
}

SYS_CALL(pread) {
    SYS_BEGIN();

    SYS_PARAM1_TRANSFORM(struct file *, file, int, get_file);
    SYS_PARAM3(size_t, count);
    SYS_PARAM2_VALIDATE(void *, buf, validate_write, count);
    SYS_PARAM4_VALIDATE(off_t, offset, validate_positive, 1);

    SYS_RETURN(fs_pread(file, buf, count, offset));
}

SYS_CALL(pwrite) {
    SYS_BEGIN();

    SYS_PARAM1_TRANSFORM(struct file *, file, int, get_file);
    SYS_PARAM3(size_t, count);
    SYS_PARAM2_VALIDATE(const void *, buf, validate_read, count);
    SYS_PARAM4_VALIDATE(off_t, offset, validate_positive, 1);

    SYS_RETURN(fs_pwrite(file, buf, count, offset));
}

SYS_CALL(readv) {
    SYS_BEGIN();

    SYS_PARAM1_TRANSFORM(struct file *, file, int, get_file);
    SYS_PARAM3_VALIDATE(int, item_count, validate_positive, 1);
    SYS_PARAM2_VALIDATE(const struct iovec *, vec, validate_read, item_count * sizeof(struct iovec));

    SYS_RETURN(fs_readv(file, vec, item_count));
}

SYS_CALL(writev) {
    SYS_BEGIN();

    SYS_PARAM1_TRANSFORM(struct file *, file, int, get_file);
    SYS_PARAM3_VALIDATE(int, item_count, validate_positive, 1);
    SYS_PARAM2_VALIDATE(const struct iovec *, vec, validate_read, item_count + sizeof(struct iovec));

    SYS_RETURN(fs_writev(file, vec, item_count));
}

SYS_CALL(realpath) {
    SYS_BEGIN();

    SYS_PARAM1_VALIDATE(const char *, path, validate_path, -1);
    SYS_PARAM3(size_t, buf_max);
    SYS_PARAM2_VALIDATE(char *, buf, validate_write, buf_max);

    struct tnode *tnode;
    {
        int ret = iname(path, 0, &tnode);
        if (ret < 0) {
            SYS_RETURN(ret);
        }
    }

    char *full_path = get_tnode_path(tnode);
    drop_tnode(tnode);

    size_t full_path_len = strlen(full_path);
    int ret = 0;

    if (full_path_len >= buf_max) {
        ret = ERANGE;
        goto finish_realpath;
    }

    strcpy(buf, full_path);

finish_realpath:
    free(full_path);
    SYS_RETURN(ret);
}

SYS_CALL(clock_nanosleep) {
    SYS_BEGIN();

    SYS_PARAM1_TRANSFORM(struct clock *, clock, clockid_t, get_clock);
    SYS_PARAM2(int, flags);
    SYS_PARAM3_VALIDATE(const struct timespec *, amt, validate_read, sizeof(struct timespec));
    SYS_PARAM4_VALIDATE(struct timespec *, rem, validate_write_or_null, sizeof(struct timespec));

    // Can't sleep on your own clock, since while sleeping your clock will never advance.
    if (clock->id == CLOCK_THREAD_CPUTIME_ID) {
        SYS_RETURN(-EINVAL);
    }

    struct timespec start = clock->time;
    struct timespec delta_time;

    bool absolute = flags == TIMER_ABSTIME;
    if (absolute) {
        delta_time = time_sub(start, *amt);
    } else {
        delta_time = *amt;
    }

    int ret = time_wakeup_after(clock->id, &delta_time);
    if (!ret && !absolute && !!rem) {
        *rem = delta_time;
    }

    SYS_RETURN(ret);
}

SYS_CALL(clock_getres) {
    SYS_BEGIN();

    SYS_PARAM1_TRANSFORM(struct clock *, clock, clockid_t, get_clock);
    SYS_PARAM2_VALIDATE(struct timespec *, res, validate_write, sizeof(struct timespec));

    *res = clock->resolution;

    SYS_RETURN(0);
}

SYS_CALL(clock_gettime) {
    SYS_BEGIN();

    SYS_PARAM1_TRANSFORM(struct clock *, clock, clockid_t, get_clock);
    SYS_PARAM2_VALIDATE(struct timespec *, tp, validate_write, sizeof(struct timespec));

    *tp = clock->time;

    SYS_RETURN(0);
}

SYS_CALL(clock_settime) {
    SYS_BEGIN();

    SYS_PARAM1_TRANSFORM(struct clock *, clock, clockid_t, get_clock);
    SYS_PARAM2_VALIDATE(const struct timespec *, tp, validate_read, sizeof(struct timespec));

    if (clock->id != CLOCK_REALTIME) {
        SYS_RETURN(-EPERM);
    }

    if (get_current_task()->process->euid != 0) {
        SYS_RETURN(-EPERM);
    }

    // FIXME: We need to notify threads sleeping on this clock that their
    //        precomputed end_time is not correct anymore (or some other solution).
    clock->time = *tp;

    SYS_RETURN(0);
}

SYS_CALL(getcpuclockid) {
    SYS_BEGIN();

    SYS_PARAM1_VALIDATE(int, tgid, validate_positive, 1);
    SYS_PARAM2_VALIDATE(int, tid, validate_positive, 1);
    SYS_PARAM3_VALIDATE(clockid_t *, id, validate_write, sizeof(clockid_t));

    struct task *current = get_current_task();
    struct process *current_process = current->process;

    clockid_t ret;
    if (tgid == 0) {
        if (tid == 0) {
            ret = current_process->process_clock->id;
        } else {
            struct task *task = find_by_tid(current_process->pid, tid);
            if (!task) {
                SYS_RETURN(-ESRCH);
            }

            ret = task->task_clock->id;
        }
    } else {
        struct process *process = find_by_pid(tgid);
        if (!process) {
            SYS_RETURN(-ESRCH);
        }

        ret = process->process_clock->id;
    }

    *id = ret;
    SYS_RETURN(0);
}

SYS_CALL(sigqueue) {
    SYS_BEGIN_CAN_SEND_SELF_SIGNALS();

    SYS_PARAM1_VALIDATE(pid_t, pid, validate_positive, 0);
    SYS_PARAM2_VALIDATE(int, sig, validate_signal_number, 1);
    SYS_PARAM3(void *, val);

    SYS_RETURN(queue_signal_process(pid, sig, val));
}

SYS_CALL(timer_create) {
    SYS_BEGIN();

    SYS_PARAM1_TRANSFORM(struct clock *, clock, clockid_t, get_clock);
    SYS_PARAM2_VALIDATE(struct sigevent *, sevp, validate_read_or_null, sizeof(struct sigevent));
    SYS_PARAM3_VALIDATE(timer_t *, timerid, validate_write, sizeof(timer_t));

    SYS_RETURN(time_create_timer(clock, sevp, timerid));
}

SYS_CALL(timer_delete) {
    SYS_BEGIN();

    SYS_PARAM1_TRANSFORM(struct timer *, timer, timer_t, get_timer);

    SYS_RETURN(time_delete_timer(timer));
}

SYS_CALL(timer_getoverrun) {
    SYS_BEGIN();

    SYS_PARAM1_TRANSFORM(struct timer *, timer, timer_t, get_timer);

    SYS_RETURN(time_get_timer_overrun(timer));
}

SYS_CALL(timer_gettime) {
    SYS_BEGIN();

    SYS_PARAM1_TRANSFORM(struct timer *, timer, timer_t, get_timer);
    SYS_PARAM2_VALIDATE(struct itimerspec *, valp, validate_write, sizeof(struct itimerspec));

    SYS_RETURN(time_get_timer_value(timer, valp));
}

SYS_CALL(timer_settime) {
    SYS_BEGIN();

    SYS_PARAM1_TRANSFORM(struct timer *, timer, timer_t, get_timer);
    SYS_PARAM2(int, flags);
    SYS_PARAM3_VALIDATE(const struct itimerspec *, new_value, validate_read_or_null, sizeof(struct itimerspec));
    SYS_PARAM4_VALIDATE(struct itimerspec *, old, validate_write_or_null, sizeof(struct itimerspec));

    SYS_RETURN(time_set_timer(timer, flags, new_value, old));
}

SYS_CALL(fstatvfs) {
    SYS_BEGIN();

    SYS_PARAM1_TRANSFORM(struct file *, file, int, get_file);
    SYS_PARAM2_VALIDATE(struct statvfs *, buf, validate_write, sizeof(struct statvfs));

    SYS_RETURN(fs_fstatvfs(file, buf));
}

SYS_CALL(statvfs) {
    SYS_BEGIN();

    SYS_PARAM1_VALIDATE(const char *, path, validate_path, -1);
    SYS_PARAM2_VALIDATE(struct statvfs *, buf, validate_write, sizeof(struct statvfs));

    SYS_RETURN(fs_statvfs(path, buf));
}

SYS_CALL(fchdir) {
    SYS_BEGIN();

    SYS_PARAM1_TRANSFORM(struct file *, file, int, get_file);

    struct task *task = get_current_task();

    if (!(fs_file_inode(file)->flags & FS_DIR)) {
        SYS_RETURN(-ENOTDIR);
    }

    if (!fs_can_execute_inode(fs_file_inode(file))) {
        SYS_RETURN(-EACCES);
    }

    drop_tnode(task->process->cwd);
    task->process->cwd = bump_tnode(file->tnode);

    SYS_RETURN(0);
}

SYS_CALL(truncate) {
    SYS_BEGIN();

    SYS_PARAM1_VALIDATE(const char *, path, validate_path, -1);
    SYS_PARAM2_VALIDATE(off_t, length, validate_positive, 1);

    SYS_RETURN(fs_truncate(path, length));
}

SYS_CALL(getgroups) {
    SYS_BEGIN();

    SYS_PARAM1_VALIDATE(int, size, validate_positive, 1);
    SYS_PARAM2_VALIDATE(gid_t *, list, validate_write, size * sizeof(gid_t));

    SYS_RETURN(proc_getgroups(size, list));
}

SYS_CALL(setgroups) {
    SYS_BEGIN();

    SYS_PARAM1(size_t, size);
    SYS_PARAM2_VALIDATE(const gid_t *, list, validate_read, size * sizeof(gid_t));

    SYS_RETURN(proc_setgroups(size, list));
}

SYS_CALL(mknod) {
    SYS_BEGIN();

    SYS_PARAM1_VALIDATE(const char *, path, validate_path, 0);
    SYS_PARAM2(mode_t, mode);
    SYS_PARAM3(dev_t, dev);

    int error = 0;
    struct tnode *tnode = fs_mknod(path, mode, dev, &error);
    if (tnode) {
        drop_tnode(tnode);
    }
    SYS_RETURN(error);
}

SYS_CALL(ppoll) {
    SYS_BEGIN_PSELECT();

    SYS_PARAM2(nfds_t, nfds);
    SYS_PARAM1_VALIDATE(struct pollfd *, user_fds, validate_write, nfds * sizeof(struct pollfd));
    SYS_PARAM3_VALIDATE(const struct timespec *, timeout, validate_read_or_null, sizeof(struct timespec));
    SYS_PARAM4_VALIDATE(const sigset_t *, sigmask, validate_read_or_null, sizeof(sigset_t));

    struct task *current = get_current_task();

    if (sigmask) {
        memcpy(&current->saved_sig_mask, &current->sig_mask, sizeof(sigset_t));
        memcpy(&current->sig_mask, sigmask, sizeof(sigset_t));
        current->in_sigsuspend = true;
    }

    int count = fs_poll(user_fds, nfds, timeout);

    if (current->in_sigsuspend) {
        SYS_RETURN_RESTORE_SIGMASK(count);
    }

    SYS_RETURN(count);
}

SYS_CALL(enable_profiling) {
    SYS_BEGIN();

    SYS_PARAM1(pid_t, pid);

    SYS_RETURN(proc_enable_profiling(pid));
}

SYS_CALL(read_profile) {
    SYS_BEGIN();

    SYS_PARAM1(pid_t, pid);
    SYS_PARAM3(size_t, len);
    SYS_PARAM2_VALIDATE(void *, buffer, validate_write, len);

    SYS_RETURN(proc_read_profile(pid, buffer, len));
}

SYS_CALL(disable_profiling) {
    SYS_BEGIN();

    SYS_PARAM1(pid_t, pid);

    SYS_RETURN(proc_disable_profiling(pid));
}

SYS_CALL(getrlimit) {
    SYS_BEGIN();

    SYS_PARAM1(int, what);
    SYS_PARAM2_VALIDATE(struct rlimit *, limit, validate_write, sizeof(struct rlimit));

    SYS_RETURN(proc_getrlimit(get_current_process(), what, limit));
}

SYS_CALL(setrlimit) {
    SYS_BEGIN();

    SYS_PARAM1(int, what);
    SYS_PARAM2_VALIDATE(const struct rlimit *, limit, validate_read, sizeof(const struct rlimit));

    SYS_RETURN(proc_setrlimit(get_current_process(), what, limit));
}

SYS_CALL(socketpair) {
    SYS_BEGIN();

    SYS_PARAM1(int, domain);
    SYS_PARAM2(int, type);
    SYS_PARAM3(int, protocol);
    SYS_PARAM4_VALIDATE(int *, fds, validate_write, 2 * sizeof(int));

    SYS_RETURN(net_socketpair(domain, type, protocol, fds));
}

SYS_CALL(getrusage) {
    SYS_BEGIN();

    SYS_PARAM1(int, who);
    SYS_PARAM2_VALIDATE(struct rusage *, rusage, validate_write, sizeof(struct rusage));

    SYS_RETURN(proc_getrusage(who, rusage));
}

SYS_CALL(nice) {
    SYS_BEGIN();

    SYS_PARAM1(int, inc);

    SYS_RETURN(proc_nice(inc));
}

SYS_CALL(getpriority) {
    SYS_BEGIN();

    SYS_PARAM1(int, which);
    SYS_PARAM2(id_t, who);

    SYS_RETURN(proc_getpriority(which, who));
}

SYS_CALL(setprioity) {
    SYS_BEGIN();

    SYS_PARAM1(int, which);
    SYS_PARAM2(id_t, who);
    SYS_PARAM3(int, value);

    SYS_RETURN(proc_setpriority(which, who, value));
}

SYS_CALL(getitimer) {
    SYS_BEGIN();

    SYS_PARAM1(int, which);
    SYS_PARAM2_VALIDATE(struct itimerval *, value, validate_write, sizeof(struct itimerval));

    SYS_RETURN(time_getitimer(which, value));
}

SYS_CALL(setitimer) {
    SYS_BEGIN();

    SYS_PARAM1(int, which);
    SYS_PARAM2_VALIDATE(struct itimerval *, nvalp, validate_read_or_null, sizeof(struct itimerval));
    SYS_PARAM3_VALIDATE(struct itimerval *, ovalp, validate_write_or_null, sizeof(struct itimerval));

    SYS_RETURN(time_setitimer(which, nvalp, ovalp));
}

SYS_CALL(mount) {
    SYS_BEGIN();

    SYS_PARAM1_VALIDATE(const char *, source, validate_path, -1);
    SYS_PARAM2_VALIDATE(const char *, target, validate_path, -1);
    SYS_PARAM3_VALIDATE(const char *, type, validate_string, -1);
    SYS_PARAM4(unsigned long, flags);
    SYS_PARAM5(const void *, data);

    SYS_RETURN(fs_mount(source, target, type, flags, data));
}

SYS_CALL(umount) {
    SYS_BEGIN();

    SYS_PARAM1_VALIDATE(const char *, target, validate_path, -1);

    SYS_RETURN(fs_umount(target));
}

SYS_CALL(poweroff) {
    SYS_BEGIN();

    struct task *task = get_current_task();
    if (task->process->euid != 0) {
        SYS_RETURN(-EPERM);
    }

    hal_poweroff();

    abort();
}

SYS_CALL(invalid_system_call) {
    SYS_BEGIN();
    SYS_RETURN(-ENOSYS);
}

void do_syscall(struct task_state *task_state) {
#ifdef SYSCALL_DEBUG
#undef __ENUMERATE_SYSCALL
#define __ENUMERATE_SYSCALL(x, a)       \
    case SYS_##x:                       \
        debug_log("syscall: %s\n", #x); \
        break;
    if (get_current_task()->process->should_trace) {
        switch (task_get_sys_call_number(task_state)) {
            ENUMERATE_SYSCALLS
            default:
                debug_log("unknown syscall: [ %#.8X ]\n", task_get_sys_call_number(task_state));
                break;
        }
    }
#endif /* SYSCALL_DEBUG */

#undef __ENUMERATE_SYSCALL
#define __ENUMERATE_SYSCALL(x, a)    \
    case SYS_##x:                    \
        sys_##x##_entry(task_state); \
        break;

    switch (task_get_sys_call_number(task_state)) {
        ENUMERATE_SYSCALLS
        default:
            sys_invalid_system_call_entry(task_state);
            break;
    }

    return;
}

bool arch_system_call_entry(struct irq_context *context) {
    struct task_state *task_state = context->task_state;
    do_syscall(task_state);
    return true;
}
