#ifndef _KERNEL_ARCH_I686_PROC_ARCH_TASK_H
#define _KERNEL_ARCH_I686_PROC_ARCH_TASK_H 1

#include <stdbool.h>
#include <stdint.h>
#include <sys/syscall.h>

#include <kernel/arch/x86/proc/fpu.h>
#include <kernel/mem/page.h>

#define KERNEL_STACK_SIZE PAGE_SIZE

struct cpu_state {
    uint16_t ds;
    uint16_t es;
    uint16_t fs;
    uint16_t gs;

    uint32_t ebp;
    uint32_t edi;
    uint32_t esi;
    uint32_t edx;
    uint32_t ecx;
    uint32_t ebx;
    uint32_t eax;
} __attribute__((packed));

struct stack_state {
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
    uint32_t esp;
    uint32_t ss;
} __attribute__((packed));

struct task_state {
    struct cpu_state cpu_state;
    struct stack_state stack_state;
} __attribute__((packed));

struct arch_task {
    struct task_state task_state;
    void *user_thread_pointer;
};

struct task;

void task_align_fpu(struct task *task);
void task_setup_user_state(struct task_state *task_state);

extern void __run_task(struct arch_task *state);

static inline void task_set_ip(struct task_state *task_state, uintptr_t ip) {
    task_state->stack_state.eip = ip;
}

static inline void task_set_sp(struct task_state *task_state, uintptr_t sp) {
    task_state->stack_state.esp = sp;
}

static inline uint32_t task_get_instruction_pointer(struct task_state *task_state) {
    return task_state->stack_state.eip;
}

static inline uint32_t task_get_stack_pointer(struct task_state *task_state) {
    return task_state->stack_state.esp;
}

static inline uint32_t task_get_base_pointer(struct task_state *task_state) {
    return task_state->cpu_state.ebp;
}

static inline enum sc_number task_get_sys_call_number(struct task_state *task_state) {
    return (enum sc_number) task_state->cpu_state.eax;
}

static inline uint32_t task_get_sys_call_arg1(struct task_state *task_state) {
    return task_state->cpu_state.ebx;
}

static inline uint32_t task_get_sys_call_arg2(struct task_state *task_state) {
    return task_state->cpu_state.ecx;
}

static inline uint32_t task_get_sys_call_arg3(struct task_state *task_state) {
    return task_state->cpu_state.edx;
}

static inline uint32_t task_get_sys_call_arg4(struct task_state *task_state) {
    return task_state->cpu_state.esi;
}

static inline uint32_t task_get_sys_call_arg5(struct task_state *task_state) {
    return task_state->cpu_state.edi;
}

static inline uint32_t task_get_sys_call_arg6(struct task_state *task_state) {
    return task_state->cpu_state.ebp;
}

static inline void task_set_sys_call_return_value(struct task_state *task_state, uint32_t value) {
    task_state->cpu_state.eax = value;
}

#endif /* _KERNEL_ARCH_I686_ARCH_PROC_TASK_H */
