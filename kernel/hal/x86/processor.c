#include <kernel/arch/x86/asm_utils.h>
#include <kernel/hal/processor.h>
#include <kernel/hal/x86/drivers/local_apic.h>
#include <kernel/mem/page.h>
#include <kernel/mem/vm_allocator.h>
#include <kernel/proc/task.h>
#include <kernel/sched/task_sched.h>

// #define PROCESSOR_IPI_DEBUG

void arch_broadcast_panic(void) {
    local_apic_broadcast_ipi(LOCAL_APIC_PANIC_IRQ);
}

void arch_broadcast_ipi(void) {
    local_apic_broadcast_ipi(LOCAL_APIC_IPI_IRQ);
}

void arch_send_ipi(struct processor *processor) {
    local_apic_send_ipi(processor->arch_processor.local_apic_id, LOCAL_APIC_IPI_IRQ);
}

void handle_processor_messages(void) {
    struct processor *processor = get_current_processor();
    assert(processor);

    spin_lock_internal(&processor->ipi_messages_lock, __func__, false);
    struct processor_ipi_message *message = processor->ipi_messages_head;
    while (message) {
        switch (message->type) {
            case PROCESSOR_IPI_FLUSH_TLB:
#ifdef PROCESSOR_IPI_DEBUG
                debug_log("Flushing TLB message: [ %d, %#.16lX, %lu ]\n", processor->id, message->flush_tlb.base, message->flush_tlb.pages);
#endif /* PROCESSOR_IPI_DEBUG */
                for (size_t i = 0; i < message->flush_tlb.pages; i++) {
                    invlpg(message->flush_tlb.base + i * PAGE_SIZE);
                }
                break;
            case PROCESSOR_IPI_SCHEDULE_TASK:
#ifdef PROCESSOR_IPI_DEBUG
                debug_log("Scheduling task message: [ %d, %d:%d ]\n", processor->id, message->schedule_task.task->process->pid,
                          message->schedule_task.task->tid);
#endif /* PROCESSOR_IPI_DEBUG */
                local_sched_add_task(processor, message->schedule_task.task);
                break;
            default:
                debug_log("Unkown message type: [ %d ]\n", message->type);
                assert(false);
        }

        struct processor_ipi_message *next = message->next[processor->id];
        drop_processor_ipi_message(message);
        message = next;
    }
    processor->ipi_messages_head = processor->ipi_messages_tail = NULL;
    spin_unlock(&processor->ipi_messages_lock);
}
