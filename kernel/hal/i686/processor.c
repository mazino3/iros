#include <kernel/arch/i686/asm_utils.h>
#include <kernel/hal/hal.h>
#include <kernel/hal/processor.h>
#include <kernel/hal/x86/drivers/local_apic.h>
#include <kernel/mem/page.h>
#include <kernel/mem/vm_allocator.h>
#include <kernel/proc/task.h>
#include <kernel/sched/task_sched.h>

void arch_init_processor(struct processor *processor) {
    if (found_acpi_tables()) {
        init_local_apic();
    }
    init_gdt(processor);
    init_idle_task(processor);
    if (processor->id != 0) {
        init_local_sched(processor);
    }
}
