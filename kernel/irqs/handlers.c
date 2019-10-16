#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#include <kernel/hal/output.h>
#include <kernel/irqs/handlers.h>
#include <kernel/hal/irqs.h>
#include <kernel/hal/output.h>
#include <kernel/proc/process.h>

#include <kernel/arch/arch.h>
#include ARCH_SPECIFIC(asm_utils.h)

void init_irq_handlers() {
    register_irq_handler(&handle_invalid_opcode_entry, 6, false);
    register_irq_handler(&handle_device_not_available_entry, 7, false);
    register_irq_handler(&handle_double_fault_entry, 8, false);
    register_irq_handler(&handle_general_protection_fault_entry, 13, false);
    register_irq_handler(&handle_page_fault_entry, 14, false);
    register_irq_handler(&handle_fpu_exception_entry, 16, false);

    register_irq_handler(&sys_call_entry, 128, true);

    debug_log("Finished Initializing Handlers\n");
}

void handle_double_fault() {
    dump_registers_to_screen();
    printf("\n\033[31m%s\033[0m\n", "Double Fault");
    abort();
}

void handle_general_protection_fault(uintptr_t error) {
    dump_registers_to_screen();
    printf("\n\033[31m%s: Error %#lX\033[0m\n", "General Protection Fault", error);
    abort();
}

void handle_page_fault(uintptr_t address, uintptr_t error) {
    dump_registers_to_screen();
    printf("\n\033[31m%s: Error %lX\n", "Page Fault", error);
    printf("Address: %#.16lX\n", address);
    printf("Process: %d\033[0m\n", get_current_process()->pid);
    abort();
}

void handle_invalid_opcode() {
    dump_registers_to_screen();
    printf("\n\033[31m%s\033[0m\n", "Invalid Opcode");
    abort();
}

void handle_fpu_exception() {
    dump_registers_to_screen();
    printf("\n\033[31m%s\033[0m\n", "FPU Exception");
    abort();
}

static struct process *last_saved = NULL;

void invalidate_last_saved(struct process *process) {
    if (last_saved == process) {
        last_saved = NULL;
    }
}

void handle_device_not_available() {
    struct process *current = get_current_process();
    assert(current);

    if (last_saved == current) {
        return;
    }

    if (last_saved == NULL) {
        last_saved = current;
        fninit();
        return;
    } else {
        fxsave((uintptr_t) &last_saved->fpu.raw_fpu_state);
    }

    if (!last_saved->fpu.saved) {
        last_saved->fpu.saved = true;
    }

    if (current->fpu.saved) {
        fxrstor((uintptr_t) &last_saved->fpu.raw_fpu_state);
    } else {
        fninit();
    }

    return;
}