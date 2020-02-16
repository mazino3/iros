#include <assert.h>
#include <stdlib.h>

#include <kernel/fs/vfs.h>
#include <kernel/hal/output.h>
#include <kernel/mem/vm_allocator.h>
#include <kernel/mem/vm_region.h>
#include <kernel/proc/process.h>
#include <kernel/proc/task.h>
#include <kernel/sched/task_sched.h>
#include <kernel/time/clock.h>
#include <kernel/time/timer.h>
#include <kernel/util/hash_map.h>

// #define PROC_REF_COUNT_DEBUG
// #define PROCESSES_DEBUG

extern struct process initial_kernel_process;

static struct hash_map *map;

static int hash(void *pid, int num_buckets) {
    assert(pid);
    return *((pid_t *) pid) % num_buckets;
}

static int equals(void *p1, void *p2) {
    assert(p1);
    assert(p2);
    return *((pid_t *) p1) == *((pid_t *) p2);
}

static void *key(void *p) {
    assert(p);
    return &((struct process *) p)->pid;
}

void proc_drop_process_unlocked(struct process *process, bool free_paging_structure) {
#ifdef PROC_REF_COUNT_DEBUG
    debug_log("Process ref count: [ %d, %d ]\n", process->pid, process->ref_count - 1);
#endif /* PROC_REF_COUNT_DEBUG */

    assert(process->ref_count > 0);
    process->ref_count--;
    if (process->ref_count == 0) {
#ifdef PROCESSES_DEBUG
        debug_log("Destroying process: [ %d ]\n", process->pid);
#endif /* PROCESSES_DEBUG */
        hash_del(map, &process->pid);
        spin_unlock(&process->lock);

        proc_kill_arch_process(process, free_paging_structure);
#ifdef PROCESSES_DEBUG
        debug_log("Destroyed arch process: [ %d ]\n", process->pid);
#endif /* PROCESSES_DEBUG */

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

        for (size_t i = 0; i < FOPEN_MAX; i++) {
            if (process->files[i].file != NULL) {
                fs_close(process->files[i].file);
                process->files[i].file = NULL;
            }
        }

        struct user_mutex *user_mutex = process->used_user_mutexes;
        while (user_mutex != NULL) {
            struct user_mutex *next = user_mutex->next;
            free(user_mutex);
            user_mutex = next;
        }

        struct timer *timer = process->timers;
        while (timer) {
            debug_log("Destroying timer: [ %p ]\n", timer);
            struct timer *next = timer->proc_next;
            time_delete_timer(timer);
            timer = next;
        }

#ifdef PROC_REF_COUNT_DEBUG
        debug_log("Finished destroying process: [ %d ]\n", process->pid);
#endif /* PROC_REF_COUNT_DEBUG */
        if (process->cwd) {
            drop_tnode(process->cwd);
        }

        time_destroy_clock(process->process_clock);
        free(process);
        return;
    }

    spin_unlock(&process->lock);
}

void proc_drop_process(struct process *process, bool free_paging_structure) {
    spin_lock(&process->lock);
    proc_drop_process_unlocked(process, free_paging_structure);
}

void proc_add_process(struct process *process) {
#ifdef PROC_REF_COUNT_DEBUG
    debug_log("Process ref count: [ %d, %d ]\n", process->pid, 1);
#endif /* PROC_REF_COUNT_DEBUG */
#ifdef PROCESSES_DEBUG
    debug_log("Adding process: [ %d ]\n", process->pid);
#endif /* PROCESSES_DEBUG */
    process->ref_count = 1;
    hash_put(map, process);
}

void proc_bump_process(struct process *process) {
    spin_lock(&process->lock);
#ifdef PROC_REF_COUNT_DEBUG
    debug_log("Process ref count: [ %d, %d ]\n", process->pid, process->ref_count + 1);
#endif /* PROC_REF_COUNT_DEBUG */
    assert(process->ref_count > 0);
    process->ref_count++;
    spin_unlock(&process->lock);
}

uintptr_t proc_allocate_user_stack(struct process *process) {
    struct vm_region *task_stack = calloc(1, sizeof(struct vm_region));
    task_stack->flags = VM_USER | VM_WRITE | VM_NO_EXEC | VM_STACK;
    task_stack->type = VM_TASK_STACK;
    task_stack->start = find_first_kernel_vm_region()->start - PAGE_SIZE - 2 * 1024 * 1024;
    task_stack->end = task_stack->start + 2 * 1024 * 1024;
    process->process_memory = add_vm_region(process->process_memory, task_stack);
    map_page(task_stack->end - PAGE_SIZE, task_stack->flags);

    struct vm_region *guard_page = calloc(1, sizeof(struct vm_region));
    guard_page->flags = VM_PROT_NONE;
    guard_page->type = VM_TASK_STACK_GUARD;
    guard_page->start = task_stack->end;
    guard_page->end = guard_page->start + PAGE_SIZE;
    process->process_memory = add_vm_region(process->process_memory, guard_page);

    return task_stack->end;
}

struct process *find_by_pid(pid_t pid) {
    return hash_get(map, &pid);
}

void proc_set_sig_pending(struct process *process, int n) {
    task_set_sig_pending(find_task_for_process(process->pid), n);
}

void init_processes() {
    debug_log("Initializing processes\n");
    map = hash_create_hash_map(hash, equals, key);
    assert(map);
    proc_add_process(&initial_kernel_process);
}