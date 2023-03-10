#include <assert.h>
#include <errno.h>
#include <search.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include <kernel/fs/procfs.h>
#include <kernel/fs/vfs.h>
#include <kernel/hal/output.h>
#include <kernel/hal/processor.h>
#include <kernel/mem/anon_vm_object.h>
#include <kernel/mem/vm_allocator.h>
#include <kernel/mem/vm_region.h>
#include <kernel/proc/pid.h>
#include <kernel/proc/process.h>
#include <kernel/proc/profile.h>
#include <kernel/proc/task.h>
#include <kernel/sched/task_sched.h>
#include <kernel/time/clock.h>
#include <kernel/time/timer.h>
#include <kernel/util/hash_map.h>

// #define PROC_REF_COUNT_DEBUG
// #define PROCESSES_DEBUG

static struct hash_map *map;

HASH_DEFINE_FUNCTIONS(process, struct process, pid_t, pid)

struct process *get_current_process(void) {
    return get_current_task()->process;
}

static void free_process_vm(struct process *process) {
    struct vm_region *region = process->process_memory;
    while (region != NULL) {
        if (region->vm_object) {
            drop_vm_object(region->vm_object);
        }

        region = region->next;
    }

    region = process->process_memory;
    while (region != NULL) {
        struct vm_region *temp = region->next;
        free(region);
        region = temp;
    }
    process->process_memory = NULL;

    struct user_mutex *user_mutex = process->used_user_mutexes;
    while (user_mutex != NULL) {
        struct user_mutex *next = user_mutex->next;
        free_user_mutex(user_mutex);
        user_mutex = next;
    }
    process->used_user_mutexes = NULL;

    list_for_each_entry_safe(&process->timer_list, timer, struct timer, proc_list) {
        if (timer != process->alarm_timer && timer != process->profile_timer && timer != process->virtual_timer) {
            time_delete_timer(timer);
        }
    }
}

static void free_process_name_info(struct process *process) {
    if (process->exe) {
        drop_tnode(process->exe);
    }
    process->exe = NULL;

    free(process->name);
    process->name = NULL;
}

void proc_reset_for_execve(struct process *process) {
    free_process_name_info(process);
    free_process_vm(process);
}

void proc_drop_process(struct process *process, struct task *task, bool free_paging_structure) {
    if (task) {
        // Reassign the main tid of the process if it exits early, and delete tid from the task list
        bool no_remaining_tasks = false;
        mutex_lock(&process->lock);
        list_remove(&task->process_list);

        // There's only one task left, notify anyone who cares (execve does).
        if (list_is_singular(&process->task_list)) {
            wake_up_all(&process->one_task_left_queue);
        }

        if (process->main_tid == task->tid) {
            struct task *new_task = list_first_entry(&process->task_list, struct task, process_list);
            process->main_tid = new_task ? new_task->tid : -1;
            no_remaining_tasks = !new_task;
            assert(process->main_tid != task->tid);
        }
        mutex_unlock(&process->lock);

        if (no_remaining_tasks) {
            if (process->pid == 1) {
                debug_log("PID 1 exited, panicking\n");
                abort();
            }

#ifdef PROCESSES_DEBUG
            debug_log("Destroying process: [ %d ]\n", process->pid);
#endif /* PROCESSES_DEBUG */

            // The process is now a zombie, so free as much resources now as possible.
            process->zombie = true;

            // Make sure that any orphaned children won't attempt to report anything to us.
            process->sig_state[SIGCHLD].sa_flags = SA_NOCLDSTOP | SA_NOCLDWAIT;
            spin_lock(&process->children_lock);
            if (process->children) {
                // First move over the orphaned children to the initial kernel process.
                spin_lock(&initial_kernel_process.children_lock);
                struct process *first_child = process->children;
                struct process *last_child;
                for (last_child = first_child; last_child->sibling_next; last_child = last_child->sibling_next)
                    ;
                last_child->sibling_next = initial_kernel_process.children;
                if (last_child->sibling_next) {
                    last_child->sibling_next->sibling_prev = last_child;
                }
                initial_kernel_process.children = first_child;
                spin_unlock(&initial_kernel_process.children_lock);

                // Now switch the parent pointer to the correct value.
                for (struct process *child = first_child;; child = child->sibling_next) {
                    spin_lock(&child->parent_lock);
                    child->parent = &initial_kernel_process;
                    spin_unlock(&child->parent_lock);

                    if (child == last_child) {
                        break;
                    }
                }
            }
            spin_unlock(&process->children_lock);

            proc_kill_arch_process(process, free_paging_structure);

#ifdef PROCESSES_DEBUG
            debug_log("Destroyed arch process: [ %d ]\n", process->pid);
#endif /* PROCESSES_DEBUG */

            free_process_vm(process);

            for (size_t i = 0; i < FOPEN_MAX; i++) {
                if (process->files[i].file != NULL) {
                    fs_close(process->files[i].file);
                    process->files[i].file = NULL;
                }
            }

            if (process->alarm_timer) {
                time_delete_timer(process->alarm_timer);
            }

            if (process->profile_timer) {
                time_delete_timer(process->profile_timer);
            }

            if (process->virtual_timer) {
                time_delete_timer(process->virtual_timer);
            }

            if (process->process_clock) {
                time_destroy_clock(process->process_clock);
                process->process_clock = NULL;
            }

            if (process->args_context) {
                free_program_args(process->args_context);
            }
            process->args_context = NULL;

            if (process->cwd) {
                drop_tnode(process->cwd);
            }
            process->cwd = NULL;

            free(process->supplemental_gids);
            process->supplemental_gids = NULL;
        }
    }

    int fetched_ref_count = atomic_fetch_sub(&process->ref_count, 1);

#ifdef PROC_REF_COUNT_DEBUG
    debug_log("Process ref count: [ %d, %d ]\n", process->pid, fetched_ref_count - 1);
#endif /* PROC_REF_COUNT_DEBUG */

    assert(fetched_ref_count > 0);
    if (fetched_ref_count == 1) {
        assert(process->zombie);

        // Finish cleanup now that there are no outstanding references.
        hash_del(map, &process->pid);
        procfs_unregister_process(process);
        free_pid(process->pid);

        free_process_name_info(process);

        if (process->profile_buffer) {
            vm_free_kernel_region(process->profile_buffer);
            proc_maybe_stop_profile_timer();
        }
        process->profile_buffer = NULL;

#ifdef PROC_REF_COUNT_DEBUG
        debug_log("Finished destroying process: [ %d ]\n", process->pid);
#endif /* PROC_REF_COUNT_DEBUG */

        free(process);
        return;
    }
}

void proc_add_process(struct process *process) {
#ifdef PROC_REF_COUNT_DEBUG
    debug_log("Process ref count: [ %d, %d ]\n", process->pid, 1);
#endif /* PROC_REF_COUNT_DEBUG */
#ifdef PROCESSES_DEBUG
    debug_log("Adding process: [ %d ]\n", process->pid);
#endif /* PROCESSES_DEBUG */
    process->ref_count = 1;
    hash_put(map, &process->hash);

    procfs_register_process(process);
}

struct process *proc_bump_process(struct process *process) {
    int fetched_ref_count = atomic_fetch_add(&process->ref_count, 1);
    (void) fetched_ref_count;
#ifdef PROC_REF_COUNT_DEBUG
    debug_log("Process ref count: [ %d, %d ]\n", process->pid, fetched_ref_count + 1);
#endif /* PROC_REF_COUNT_DEBUG */
    assert(fetched_ref_count > 0);
    return process;
}

void proc_add_child(struct process *parent, struct process *child) {
    spin_lock(&parent->children_lock);
    child->sibling_next = parent->children;
    child->sibling_prev = NULL;
    if (parent->children) {
        parent->children->sibling_prev = child;
    }
    parent->children = child;
    spin_unlock(&parent->children_lock);
}

int proc_get_waitable_process(struct process *parent, pid_t wait_spec, struct process **process_out) {
    if (wait_spec == 0) {
        wait_spec = -get_current_process()->pgid;
    }
    bool any_waitable = false;
    for (struct process *child = parent->children; child; child = child->sibling_next) {
        if (wait_spec == -1 || (wait_spec < 0 && child->pgid == -wait_spec) || child->pid == wait_spec) {
            any_waitable = true;
            if (child->state != PS_NONE) {
                *process_out = child;
                return child->pid;
            }
        }
    }

    *process_out = NULL;
    return any_waitable ? 0 : -ECHILD;
}

static void proc_update_rusage(struct process *parent, struct process *child) {
    parent->rusage_children.ru_utime =
        time_add_timeval(parent->rusage_children.ru_utime, time_add_timeval(child->rusage_self.ru_utime, child->rusage_children.ru_utime));
    parent->rusage_children.ru_stime =
        time_add_timeval(parent->rusage_children.ru_stime, time_add_timeval(child->rusage_self.ru_stime, child->rusage_children.ru_stime));
    parent->rusage_children.ru_maxrss =
        MAX(parent->rusage_children.ru_maxrss, MAX(parent->rusage_self.ru_maxrss, parent->rusage_children.ru_maxrss));
    parent->rusage_children.ru_minflt += child->rusage_self.ru_minflt + child->rusage_children.ru_minflt;
    parent->rusage_children.ru_majflt += child->rusage_self.ru_majflt + child->rusage_children.ru_majflt;
    parent->rusage_children.ru_inblock += child->rusage_self.ru_inblock + child->rusage_children.ru_inblock;
    parent->rusage_children.ru_oublock += child->rusage_self.ru_oublock + child->rusage_children.ru_oublock;
    parent->rusage_children.ru_nvcsw += child->rusage_self.ru_nvcsw + child->rusage_children.ru_nvcsw;
    parent->rusage_children.ru_nivcsw += child->rusage_self.ru_nivcsw + child->rusage_children.ru_nivcsw;
}

void proc_consume_wait_info(struct process *parent, struct process *child, enum process_state state) {
    switch (state) {
        case PS_TERMINATED: {
            if (child->pid == 1) {
                debug_log("Init process exited, time to panic\n");
                assert(false);
            }

            if (parent->children == child) {
                parent->children = child->sibling_next;
            }
            remque(child);
            proc_update_rusage(parent, child);
            proc_drop_process(child, NULL, false);
            break;
        }
        case PS_CONTINUED:
        case PS_STOPPED:
            child->state = PS_NONE;
            break;
        default:
            assert(false);
            break;
    }
}

void proc_set_process_state(struct process *process, enum process_state state, int info, bool terminated_bc_signal) {
    // PID 1 has no parent, so its events should just be ignored.
    if (process->pid == 1) {
        return;
    }

    struct process *parent = proc_get_parent(process);
    struct sigaction parent_signal_disposition = parent->sig_state[SIGCHLD];

    if (((parent_signal_disposition.sa_flags & SA_NOCLDWAIT) || parent_signal_disposition.sa_handler == SIG_IGN) &&
        (state == PS_TERMINATED)) {
        proc_consume_wait_info(parent, process, PS_TERMINATED);
        goto done;
    }

    if (((parent_signal_disposition.sa_flags & SA_NOCLDSTOP) || parent_signal_disposition.sa_handler == SIG_IGN) &&
        (state == PS_STOPPED || state == PS_CONTINUED)) {
        proc_consume_wait_info(parent, process, state);
        goto done;
    }

    // FIXME: synchronize this somehow
    process->state = state;
    process->exit_code = info;
    process->terminated_bc_signal = terminated_bc_signal;

    proc_notify_parent(parent);
    wake_up_all(&parent->child_wait_queue);

done:
    proc_drop_parent(parent);
}

uintptr_t proc_allocate_user_stack(struct process *process, struct initial_process_info *info) {
    // Guard Pages: 0xFFFFFE7FFFDFE000 - 0xFFFFFE7FFFDFF000
    // Stack Pages: 0xFFFFFE7FFFDFF000 - 0xFFFFFE7FFFFFF000

    struct vm_object *stack_object = vm_create_anon_object(PAGE_SIZE + 2 * 1024 * 1024);
    assert(stack_object);

    struct vm_region *task_stack = calloc(1, sizeof(struct vm_region));
    task_stack->flags = VM_USER | VM_WRITE | VM_NO_EXEC | VM_STACK;
    task_stack->type = VM_TASK_STACK;
    task_stack->start = find_first_kernel_vm_region()->start - PAGE_SIZE - 2 * 1024 * 1024;
    task_stack->end = task_stack->start + 2 * 1024 * 1024;
    task_stack->vm_object = stack_object;
    task_stack->vm_object_offset = PAGE_SIZE;
    process->process_memory = add_vm_region(process->process_memory, task_stack);

    struct vm_region *guard_page = calloc(1, sizeof(struct vm_region));
    guard_page->flags = VM_PROT_NONE | VM_NO_EXEC;
    guard_page->type = VM_TASK_STACK_GUARD;
    guard_page->start = task_stack->start - PAGE_SIZE;
    guard_page->end = guard_page->start + PAGE_SIZE;
    guard_page->vm_object = bump_vm_object(stack_object);
    guard_page->vm_object_offset = 0;
    process->process_memory = add_vm_region(process->process_memory, guard_page);

    info->guard_size = PAGE_SIZE;
    info->stack_size = 2 * 1024 * 1024;
    info->stack_start = (void *) task_stack->start - PAGE_SIZE;

    return task_stack->end;
}

struct process *find_by_pid(pid_t pid) {
    return hash_get_entry(map, &pid, struct process);
}

struct proc_for_each_with_pgid_closure {
    void (*callback)(struct process *process, void *closure);
    void *closure;
    pid_t pgid;
};

static void for_each_with_pgid_iter(struct hash_entry *_process, void *_cls) {
    struct process *process = hash_table_entry(_process, struct process);
    struct proc_for_each_with_pgid_closure *cls = _cls;
    if (process->pgid == cls->pgid) {
        cls->callback(process, cls->closure);
    }
}

void proc_for_each_with_pgid(pid_t pgid, void (*callback)(struct process *process, void *closure), void *closure) {
    struct proc_for_each_with_pgid_closure cls = { .callback = callback, .closure = closure, .pgid = pgid };
    hash_for_each(map, for_each_with_pgid_iter, &cls);
}

struct proc_for_each_with_euid_closure {
    void (*callback)(struct process *process, void *closure);
    void *closure;
    uid_t euid;
};

static void for_each_with_euid_iter(struct hash_entry *_process, void *_cls) {
    struct process *process = hash_table_entry(_process, struct process);
    struct proc_for_each_with_euid_closure *cls = _cls;
    if (process->euid == cls->euid) {
        cls->callback(process, cls->closure);
    }
}

void proc_for_each_with_euid(uid_t euid, void (*callback)(struct process *process, void *closure), void *closure) {
    struct proc_for_each_with_euid_closure cls = { .callback = callback, .closure = closure, .euid = euid };
    hash_for_each(map, for_each_with_euid_iter, &cls);
}

void proc_set_sig_pending(struct process *process, int n) {
    // FIXME: dispatch signals to a different task than the first if it makes sense.
    mutex_lock(&process->lock);
    struct task *task_to_use = list_first_entry(&process->task_list, struct task, process_list);
    mutex_unlock(&process->lock);

    task_enqueue_signal(task_to_use, n, NULL, false);
}

int proc_getgroups(size_t size, gid_t *list) {
    struct process *process = get_current_task()->process;
    if (size == 0) {
        return process->supplemental_gids_size;
    }

    if (size < process->supplemental_gids_size) {
        return -EINVAL;
    }

    memcpy(list, process->supplemental_gids, process->supplemental_gids_size * sizeof(gid_t));
    return process->supplemental_gids_size;
}

int proc_setgroups(size_t size, const gid_t *list) {
    struct process *process = get_current_task()->process;
    if (process->euid != 0) {
        return -EPERM;
    }

    process->supplemental_gids_size = size;
    process->supplemental_gids = realloc(process->supplemental_gids, size * sizeof(gid_t));
    memcpy(process->supplemental_gids, list, size * sizeof(gid_t));

    return 0;
}

bool proc_in_group(struct process *process, gid_t group) {
    if (!process->supplemental_gids) {
        return false;
    }

    for (size_t i = 0; i < process->supplemental_gids_size; i++) {
        if (process->supplemental_gids[i] == group) {
            return true;
        }
    }

    return false;
}

int proc_getrusage(int who, struct rusage *rusage) {
    struct process *process = get_current_process();
    struct rusage *to_copy;
    switch (who) {
        case RUSAGE_SELF:
            to_copy = &process->rusage_self;
            break;
        case RUSAGE_CHILDREN:
            to_copy = &process->rusage_children;
            break;
        default:
            return -EINVAL;
    }

    spin_lock(&process->children_lock);
    memcpy(rusage, to_copy, sizeof(struct rusage));
    spin_unlock(&process->children_lock);
    return 0;
}

int proc_getrlimit(struct process *process, int what, struct rlimit *limit) {
    if (what < 0 || what >= RLIMIT_NLIMITS) {
        return -EINVAL;
    }

    mutex_lock(&process->lock);
    memcpy(limit, &process->limits[what], sizeof(struct rlimit));
    mutex_unlock(&process->lock);
    return 0;
}

int proc_setrlimit(struct process *process, int what, const struct rlimit *user_limit) {
    if (what < 0 || what >= RLIMIT_NLIMITS) {
        return -EINVAL;
    }

    struct rlimit new_limit;
    memcpy(&new_limit, user_limit, sizeof(struct rlimit));
    if (new_limit.rlim_cur > new_limit.rlim_max) {
        return -EINVAL;
    }

    int ret = 0;
    mutex_lock(&process->lock);
    if (process->euid == 0) {
        process->limits[what] = new_limit;
    } else {
        struct rlimit *current_limit = &process->limits[what];
        if ((new_limit.rlim_max > current_limit->rlim_max) || (new_limit.rlim_cur > current_limit->rlim_max)) {
            ret = -EPERM;
        } else {
            process->limits[what] = new_limit;
        }
    }
    mutex_unlock(&process->lock);
    return ret;
}

int proc_nice(int inc) {
    struct process *process = get_current_process();
    if (inc < 0 && process->euid != 0) {
        return -EPERM;
    }

    mutex_lock(&process->lock);
    int ret = process->priority + inc;
    ret = MAX(0, ret);
    ret = MIN(ret, PROCESS_MAX_PRIORITY);
    process->priority = ret;
    mutex_unlock(&process->lock);

    return ret;
}

struct prio_iter_data {
    int value;
    int get_or_set;
    int found;
};

static int set_priority(struct process *process, int value) {
    struct process *current = get_current_process();
    if (current->euid != 0 && current->euid != process->euid) {
        return -EPERM;
    }

    int old_value = process->priority;
    if (current->euid != 0 && old_value > value) {
        return -EPERM;
    }

    process->priority = value;
    return 0;
}

static void prio_iter(struct process *process, void *closure) {
    struct prio_iter_data *data = closure;
    data->found = 1;
    mutex_lock(&process->lock);
    if (data->get_or_set) {
        if (data->value < 0) {
            data->value = process->priority;
        } else {
            data->value = MIN(data->value, process->priority);
        }
    } else if (data->value >= 0) {
        int ret = set_priority(process, data->value);
        if (ret) {
            data->value = ret;
        }
    }
    mutex_unlock(&process->lock);
}

int proc_getpriority(int which, id_t who) {
    struct process *current = get_current_process();
    switch (which) {
        case PRIO_PROCESS: {
            struct process *process = current;
            if (who) {
                process = find_by_pid(who);
            }

            if (!process) {
                return -ESRCH;
            }

            return process->priority;
        }
        case PRIO_PGRP: {
            pid_t pgid = current->pgid;
            if (who) {
                pgid = who;
            }

            struct prio_iter_data data = { .value = -ESRCH, .get_or_set = 1 };
            proc_for_each_with_pgid(pgid, prio_iter, &data);
            return data.value;
        }
        case PRIO_USER: {
            uid_t uid = current->euid;
            if (who) {
                uid = who;
            }

            struct prio_iter_data data = { .value = -ESRCH, .get_or_set = 1 };
            proc_for_each_with_euid(uid, prio_iter, &data);
            return data.value;
        }
        default:
            return -EINVAL;
    }
}

int proc_setpriority(int which, id_t who, int value) {
    if (value < 0 || value > PROCESS_MAX_PRIORITY) {
        return -EINVAL;
    }

    struct process *current = get_current_process();
    switch (which) {
        case PRIO_PROCESS: {
            struct process *process = current;
            if (who) {
                process = find_by_pid(who);
            }

            if (!process) {
                return -ESRCH;
            }

            mutex_lock(&process->lock);
            int ret = set_priority(process, value);
            mutex_unlock(&process->lock);
            return ret;
        }
        case PRIO_PGRP: {
            pid_t pgid = current->pgid;
            if (who) {
                pgid = who;
            }

            struct prio_iter_data data = { .value = value, .get_or_set = 0 };
            proc_for_each_with_pgid(pgid, prio_iter, &data);
            return data.found ? data.value : -ESRCH;
        }
        case PRIO_USER: {
            uid_t uid = current->euid;
            if (who) {
                uid = who;
            }

            struct prio_iter_data data = { .value = value, .get_or_set = 0 };
            proc_for_each_with_euid(uid, prio_iter, &data);
            return data.found ? data.value : -ESRCH;
        }
        default:
            return -EINVAL;
    }
}

void init_processes() {
    map = hash_create_hash_map(process_hash, process_equals, process_key);
    assert(map);
    proc_add_process(&initial_kernel_process);
    initial_kernel_process.name = strdup("init");
}
