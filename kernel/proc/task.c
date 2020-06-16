#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include <kernel/fs/procfs.h>
#include <kernel/fs/vfs.h>
#include <kernel/hal/output.h>
#include <kernel/irqs/handlers.h>
#include <kernel/mem/page.h>
#include <kernel/mem/page_frame_allocator.h>
#include <kernel/mem/vm_allocator.h>
#include <kernel/proc/elf64.h>
#include <kernel/proc/pid.h>
#include <kernel/proc/process_state.h>
#include <kernel/proc/task.h>
#include <kernel/proc/user_mutex.h>
#include <kernel/sched/task_sched.h>
#include <kernel/time/clock.h>

// #define TASK_SCHED_STATE_DEBUG
// #define TASK_SIGNAL_DEBUG

struct task *current_task;
struct task initial_kernel_task;
struct process initial_kernel_process;

void proc_clone_program_args(struct process *process, char **prepend_argv, char **argv, char **envp) {
    struct args_context *context = process->args_context = malloc(sizeof(struct args_context));

    context->prepend_argc = 0;
    size_t prepend_args_str_length = 0;
    while (prepend_argv && prepend_argv[context->prepend_argc] != NULL) {
        prepend_args_str_length += strlen(prepend_argv[context->prepend_argc++]) + 1;
    }

    context->argc = 0;
    size_t args_str_length = 0;
    while (argv[context->argc++] != NULL) {
        args_str_length += strlen(argv[context->argc - 1]) + 1;
    }

    context->envc = 0;
    size_t env_str_length = 0;
    while (envp[context->envc++] != NULL) {
        env_str_length += strlen(envp[context->envc - 1]) + 1;
    }

    context->prepend_args_copy = calloc(context->prepend_argc, sizeof(char **));
    context->args_copy = calloc(context->argc, sizeof(char **));
    context->envp_copy = calloc(context->envc, sizeof(char **));

    context->prepend_args_buffer = malloc(prepend_args_str_length + args_str_length);
    context->args_buffer = context->prepend_args_buffer + prepend_args_str_length;
    context->env_buffer = malloc(env_str_length);

    context->args_bytes = prepend_args_str_length + args_str_length;
    context->env_bytes = env_str_length;

    ssize_t j = 0;
    ssize_t i = 0;
    while (prepend_argv && prepend_argv[i]) {
        ssize_t last = j;
        while (prepend_argv[i][j - last] != '\0') {
            context->prepend_args_buffer[j] = prepend_argv[i][j - last];
            j++;
        }
        context->prepend_args_buffer[j++] = '\0';
        context->prepend_args_copy[i++] = context->prepend_args_buffer + last;
    }

    i = 0;
    j = 0;
    while (argv[i] != NULL) {
        ssize_t last = j;
        while (argv[i][j - last] != '\0') {
            context->args_buffer[j] = argv[i][j - last];
            j++;
        }
        context->args_buffer[j++] = '\0';
        context->args_copy[i++] = context->args_buffer + last;
    }
    context->args_copy[i] = NULL;

    j = 0;
    i = 0;
    while (envp[i] != NULL) {
        ssize_t last = j;
        while (envp[i][j - last] != '\0') {
            context->env_buffer[j] = envp[i][j - last];
            j++;
        }
        context->env_buffer[j++] = '\0';
        context->envp_copy[i++] = context->env_buffer + last;
    }
    context->envp_copy[i] = NULL;
}

/* Copying args and envp is necessary because they could be saved on the program stack we are about to overwrite */
uintptr_t map_program_args(uintptr_t start, struct args_context *context) {
    char **argv_start = (char **) (start - sizeof(char **));

    size_t count = context->prepend_argc + context->argc + context->envc;
    char *args_start = (char *) (argv_start - count);

    ssize_t i;
    for (i = 0; context->args_copy[i] != NULL; i++) {
        args_start -= strlen(context->args_copy[i]) + 1;
        strcpy(args_start, context->args_copy[i]);
        argv_start[i - context->argc] = args_start;
    }

    for (i = 0; i < (ssize_t) context->prepend_argc; i++) {
        args_start -= strlen(context->prepend_args_copy[i]) + 1;
        strcpy(args_start, context->prepend_args_copy[i]);
        argv_start[i - context->prepend_argc - context->argc] = args_start;
    }

    argv_start[0] = NULL;

    for (i = 0; context->envp_copy[i] != NULL; i++) {
        args_start -= strlen(context->envp_copy[i]) + 1;
        strcpy(args_start, context->envp_copy[i]);
        argv_start[i - count] = args_start;
    }

    argv_start[-(context->prepend_argc + context->argc + 1)] = NULL;

    args_start = (char *) ((((uintptr_t) args_start) & ~0x7) - 0x08);

    args_start -= sizeof(size_t);
    *((size_t *) args_start) = context->prepend_argc + context->argc - 1;
    args_start -= sizeof(char **);
    *((char ***) args_start) = argv_start - context->prepend_argc - context->argc;
    args_start -= sizeof(char **);
    *((char ***) args_start) = argv_start - count;

    return (uintptr_t) args_start;
}

void free_program_args(struct args_context *context) {
    free(context->prepend_args_copy);
    free(context->args_copy);
    free(context->envp_copy);
    free(context->prepend_args_buffer);
    free(context->env_buffer);
}

static spinlock_t tid_lock;
static int tid_counter = 1;

int get_next_tid() {
    spin_lock(&tid_lock);

    int tid = tid_counter++;

    spin_unlock(&tid_lock);
    return tid;
}

void init_kernel_task() {
    current_task = &initial_kernel_task;

    current_task->process = &initial_kernel_process;
    arch_init_kernel_task(current_task);

    initial_kernel_task.tid = get_next_tid();
    initial_kernel_task.kernel_task = true;
    initial_kernel_task.process->pid = 1;
    init_spinlock(&initial_kernel_process.lock);
    initial_kernel_task.sched_state = RUNNING_UNINTERRUPTIBLE;
    initial_kernel_task.next = NULL;
    initial_kernel_task.process->main_tid = initial_kernel_task.tid;
    initial_kernel_task.process->pgid = 1;
    initial_kernel_task.process->ppid = 1;
    initial_kernel_task.process->tty = -1;
    initial_kernel_process.start_time = time_read_clock(CLOCK_REALTIME);
}

struct task *load_kernel_task(uintptr_t entry, const char *name) {
    struct task *task = calloc(1, sizeof(struct task));
    struct process *process = calloc(1, sizeof(struct process));
    task->process = process;

    task->process->pid = get_next_pid();
    init_spinlock(&process->lock);
    proc_add_process(process);
    task->process->pgid = task->process->pid;
    task->process->ppid = initial_kernel_task.process->pid;
    task->process->process_memory = NULL;
    task->kernel_task = true;
    task->sched_state = RUNNING_UNINTERRUPTIBLE;
    task->process->cwd = malloc(2);
    task->process->tty = -1;
    task->tid = get_next_tid();
    task->process->main_tid = task->tid;
    task->process->cwd = NULL;
    task->next = NULL;
    process->name = strdup(name);
    process->start_time = time_read_clock(CLOCK_REALTIME);

    arch_load_kernel_task(task, entry);

    debug_log("Loaded Kernel Task: [ %d ]\n", task->process->pid);
    return task;
}

struct task *load_task(const char *file_name) {
    struct task *task = calloc(1, sizeof(struct task));
    struct process *process = calloc(1, sizeof(struct process));
    task->process = process;

    int error = 0;
    struct file *file = fs_openat(NULL, file_name, O_RDONLY, 0, &error);
    assert(error == 0);

    uintptr_t old_paging_structure = get_current_paging_structure();

    current_task = task;
    uintptr_t structure = create_paging_structure(task->process->process_memory, false, task->process);
    load_paging_structure(structure);

    size_t length = fs_file_size(file);
    char *buffer = (char *) fs_mmap(NULL, length, PROT_READ, MAP_SHARED, file, 0);
    assert(buffer != MAP_FAILED);

    task->process->exe = bump_tnode(fs_get_tnode_for_file(file));

    process->name = strdup(process->exe->name);

    task->tid = get_next_tid();
    task->process->pid = get_next_pid();
    task->process->main_tid = task->tid;
    init_spinlock(&process->lock);
    proc_add_process(process);
    task->process->pgid = task->process->pid;
    task->process->sid = task->process->pid;
    task->process->ppid = initial_kernel_task.process->pid;
    task->sched_state = RUNNING_INTERRUPTIBLE;
    task->process->tty = -1;
    task->process->cwd = bump_tnode(fs_root());
    task->process->process_clock = time_create_clock(CLOCK_PROCESS_CPUTIME_ID);
    task->task_clock = time_create_clock(CLOCK_THREAD_CPUTIME_ID);
    task->process->start_time = time_read_clock(CLOCK_REALTIME);

    task->next = NULL;

    task->kernel_task = true;
    assert(elf64_is_valid(buffer));
    elf64_load_program(buffer, length, file, task);
    elf64_map_heap(buffer, task);
    uintptr_t entry = elf64_get_entry(buffer);

    assert(fs_close(file) == 0);
    unmap_range((uintptr_t) buffer, length);

    proc_allocate_user_stack(process);
    arch_load_task(task, entry);

    current_task = &initial_kernel_task;
    task->kernel_task = false;

    load_paging_structure(old_paging_structure);

    task->process->files[0] = (struct file_descriptor) { fs_openat(NULL, "/dev/serial0", O_RDWR, 0, NULL), 0 };
    task->process->files[1] = fs_dup(task->process->files[0]);
    task->process->files[2] = fs_dup(task->process->files[0]);

    debug_log("Loaded Task: [ %d, %s ]\n", task->process->pid, file_name);
    return task;
}

/* Must be called from unpremptable context */
void run_task(struct task *task) {
    arch_run_task(task);
}

struct task *get_current_task() {
    return current_task;
}

/* Must be called from unpremptable context */
void free_task(struct task *task, bool free_paging_structure) {
    struct task *current_save = task == current_task ? &initial_kernel_task : current_task;
    current_task = task;

    debug_log("destroying: [ %d:%d ]\n", task->process->pid, task->tid);

    arch_free_task(task, free_paging_structure);

    struct queued_signal *si = task->queued_signals;
    while (si) {
        struct queued_signal *next = si->next;
        free(si);
        si = next;
    }

    time_destroy_clock(task->task_clock);
    proc_drop_process(task->process, task->tid, free_paging_structure);
    free(task);
    current_task = current_save;
}

void task_set_sig_pending(struct task *task, int signum) {
    task->sig_pending |= (UINT64_C(1) << (signum - UINT64_C(1)));
}

void task_unset_sig_pending(struct task *task, int signum) {
    task->sig_pending &= ~(UINT64_C(1) << (signum - UINT64_C(1)));
}

bool task_is_sig_pending(struct task *task, int signum) {
    return task->sig_pending & (UINT64_C(1) << (signum - UINT64_C(1))) ? 1 : 0;
}

void task_enqueue_signal_object(struct task *task, struct queued_signal *to_add) {
    unsigned long save = disable_interrupts_save();

    int signum = to_add->info.si_signo;

    struct queued_signal *tail = NULL;
    if (task->queued_signals && task->queued_signals->info.si_signo <= signum) {
        tail = task->queued_signals;
        for (;; tail = tail->next) {
            if (!tail->next || tail->next->info.si_signo > signum) {
                break;
            }
        }

        assert(tail);
        to_add->next = tail->next;
    } else {
        to_add->next = task->queued_signals;
    }

    if (!tail) {
        task->queued_signals = to_add;
    } else {
        tail->next = to_add;
    }

    task_set_sig_pending(task, signum);
    interrupts_restore(save);
}

void task_enqueue_signal(struct task *task, int signum, void *val, bool is_sigqueue) {
    unsigned long save = disable_interrupts_save();

    if (signum < SIGRTMIN && task_is_sig_pending(task, signum)) {
        goto task_enqueue_signal_end;
    }

    if (!(task->process->sig_state[signum].sa_flags & SA_SIGINFO)) {
        task_set_sig_pending(task, signum);
        goto task_enqueue_signal_end;
    }

    struct queued_signal *to_add = malloc(sizeof(struct queued_signal));
    to_add->info.si_signo = signum;
    to_add->info.si_code = is_sigqueue ? SI_QUEUE : SI_USER;
    to_add->info.si_addr = NULL;
    to_add->info.si_errno = 0;
    to_add->info.si_pid = get_current_task()->process->pid;
    to_add->info.si_uid = get_current_task()->process->uid;
    to_add->info.si_status = 0;
    to_add->info.si_band = 0;
    to_add->info.si_value.sival_ptr = val;
    to_add->flags = 0;

    task_enqueue_signal_object(task, to_add);

task_enqueue_signal_end:
    interrupts_restore(save);
}

void task_dequeue_signal(struct task *task) {
    struct queued_signal *next = task->queued_signals->next;
    if (!next || next->info.si_signo != task->queued_signals->info.si_signo) {
        task_unset_sig_pending(task, task->queued_signals->info.si_signo);
        debug_log("task_unset_sig_pending: [ %d ]\n", task->queued_signals->info.si_signo);
    }

    if (task->queued_signals->flags & QUEUED_SIGNAL_DONT_FREE_FLAG) {
        task->queued_signals->flags &= ~QUEUED_SIGNAL_DONT_FREE_FLAG;
        task->queued_signals->next = NULL;
    } else {
        free(task->queued_signals);
    }
    task->queued_signals = next;
}

int task_get_next_sig(struct task *task) {
    if (!task->sig_pending) {
        return -1; // Indicates we did nothing
    }

    for (size_t i = 1; i < _NSIG; i++) {
        if (task_is_sig_pending(task, i) && !task_is_sig_blocked(task, i)) {
            return i;
        }
    }

    return -1;
}

void proc_notify_parent(pid_t child_pid) {
    struct process *child = find_by_pid(child_pid);
    struct process *parent = find_by_pid(child->ppid);

    if (parent == NULL) {
        parent = &initial_kernel_process;
    }

    proc_set_sig_pending(parent, SIGCHLD);
}

void task_do_sigs_if_needed(struct task *task) {
    // No need to do signals if we should exit anyway.
    if (task->should_exit) {
        return;
    }

    int sig = task_get_next_sig(task);
    if (sig != -1) {
        task_do_sig(task, sig);
    }
}

void task_set_state_to_exiting(struct task *task) {
    if (task->sched_state == EXITING) {
        return;
    }

    if (task->blocking) {
        // Defer exit state change until after the blocking code has a chance
        // to clean up
        task_interrupt_blocking(task, -EINTR);
    }

    if (task->in_kernel) {
        task->should_exit = true;
    } else {
        task->sched_state = EXITING;
    }
}

void task_yield_if_state_changed(struct task *task) {
    if (task->should_exit) {
#ifdef TASK_SCHED_STATE_DEBUG
        debug_log("setting sched state to EXITING: [ %d:%d ]\n", task->process->pid, task->tid);
#endif /* TASK_SCHED_STATE_DEBUG */
        task->sched_state = EXITING;
        sys_sched_run_next(&task->arch_task.task_state);
    }
}

enum sig_default_behavior { TERMINATE, TERMINATE_AND_DUMP, IGNORE, STOP, CONTINUE, INVAL };

static enum sig_default_behavior sig_defaults[_NSIG] = {
    INVAL,              // INVAL
    TERMINATE,          // SIGHUP
    TERMINATE,          // SIGINT
    TERMINATE_AND_DUMP, // SIGQUIT
    TERMINATE_AND_DUMP, // SIGBUS
    TERMINATE_AND_DUMP, // SIGTRAP
    TERMINATE_AND_DUMP, // SIGABRT
    CONTINUE,           // SIGCONT
    TERMINATE_AND_DUMP, // SIGFPE
    TERMINATE,          // SIGKILL
    STOP,               // SIGTTIN
    STOP,               // SIGTTOU
    TERMINATE,          // SIGILL
    TERMINATE,          // SIGPIPE
    TERMINATE,          // SIGALRM
    TERMINATE,          // SIGTERM
    TERMINATE_AND_DUMP, // SIGSEGV
    STOP,               // SIGSTOP
    STOP,               // SIGTSTP
    TERMINATE,          // SIGUSR1
    TERMINATE,          // SIGUSR2
    TERMINATE,          // SIGPOLL
    TERMINATE,          // SIGPROF
    TERMINATE_AND_DUMP, // SIGSYS
    IGNORE,             // SIGURG
    TERMINATE,          // SIGVTALRM
    TERMINATE_AND_DUMP, // SIGXCPU
    TERMINATE_AND_DUMP, // SIGXFSZ
    IGNORE,             // SIGCHLD
    TERMINATE           // SIGWINCH
};

void task_do_sig(struct task *task, int signum) {
    assert(current_task == task);

#ifdef TASK_SIGNAL_DEBUG
    debug_log("Doing signal: [ %d, %s ]\n", task->process->pid, strsignal(signum));
#endif /* TASK_SIGNAL_DEBUG */

    if (task->process->sig_state[signum].sa_handler == SIG_IGN) {
        task_unset_sig_pending(task, signum);
        return;
    }

    if (task->process->sig_state[signum].sa_handler != SIG_DFL) {
        task_do_sig_handler(task, signum);
        return;
    }

    enum sig_default_behavior behavior = signum >= SIGRTMIN ? TERMINATE : sig_defaults[signum];
    task_unset_sig_pending(task, signum);
    switch (behavior) {
        case TERMINATE_AND_DUMP:
            elf64_stack_trace(task, true);
            // Fall through
        case TERMINATE:
            if (task->sched_state == EXITING) {
                break;
            }
            exit_process(task->process);
            proc_add_message(task->process->pid, proc_create_message(STATE_INTERRUPTED, signum));
            break;
        case STOP:
            if (task->sched_state == STOPPED) {
                break;
            }
            task->sched_state = STOPPED;
            proc_add_message(task->process->pid, proc_create_message(STATE_STOPPED, signum));
            break;
        case CONTINUE:
            if (task->sched_state != STOPPED) {
                break;
            }
            task->sched_state = task->blocking ? WAITING : task->in_kernel ? RUNNING_UNINTERRUPTIBLE : RUNNING_INTERRUPTIBLE;
            proc_add_message(task->process->pid, proc_create_message(STATE_CONTINUED, signum));
            break;
        case IGNORE:
            break;
        default:
            assert(false);
            break;
    }
}

const char *task_state_to_string(enum sched_state state) {
    switch (state) {
        case RUNNING_INTERRUPTIBLE:
            return "R (running)";
        case RUNNING_UNINTERRUPTIBLE:
            return "U (running uninterruptible)";
        case WAITING:
            return "W (waiting)";
        case EXITING:
            return "E (exiting)";
        case STOPPED:
            return "S (stopped)";
        default:
            return "? (Unknown)";
    }
}

bool task_is_sig_blocked(struct task *task, int signum) {
    assert(signum >= 1 && signum < _NSIG);
    return task->sig_mask & (UINT64_C(1) << (signum - UINT64_C(1)));
}
