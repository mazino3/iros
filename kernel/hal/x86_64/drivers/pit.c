#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>

#include <kernel/arch/x86_64/proc/task.h>
#include <kernel/hal/output.h>
#include <kernel/hal/x86_64/drivers/pic.h>
#include <kernel/hal/x86_64/drivers/pit.h>
#include <kernel/proc/task.h>

static void (*sched_callback)(struct task_state *) = NULL;
static unsigned int sched_count = 0;
static unsigned int sched_count_to = 0;

static void (*callback)(void) = NULL;
static unsigned int count = 0;
static unsigned int count_to = 0;

static time_t ms = 0;

void pit_set_time(time_t time) {
    debug_log("PIT set_time: [ %lu ]\n", time);
    ms = time * 1000;
}

void handle_pit_interrupt(struct task_state *task_state) {
    sendEOI(PIT_IRQ_LINE);

    /* Increment time count */
    ms++;

    struct task *current = get_current_task();
    if (current->in_kernel) {
        current->process->times.tms_stime++;
    } else {
        current->process->times.tms_utime++;
    }

    if (sched_callback != NULL) {
        sched_count++;
        if (sched_count >= sched_count_to) {
            sched_count = 0;
            sched_callback(task_state);
        }
    }

    if (callback != NULL) {
        count++;
        if (count >= count_to) {
            count = 0;
            callback();
        }
    }
}

time_t pit_get_time() {
    return ms;
}

void pit_set_sched_callback(void (*_sched_callback)(struct task_state *), unsigned int ms) {
    sched_callback = _sched_callback;
    sched_count_to = ms;
}

void pit_register_callback(void (*_callback)(void), unsigned int ms) {
    callback = _callback;
    count_to = ms;
}

void pit_set_rate(unsigned int rate) {
    PIT_SET_MODE(0, PIT_ACCESS_LOHI, PIT_MODE_SQUARE_WAVE);
    outb(PIT_CHANNEL_0, PIT_GET_DIVISOR(rate) & 0xFF);
    outb(PIT_CHANNEL_0, PIT_GET_DIVISOR(rate) >> 8);
}

void init_pit() {
    register_irq_line_handler(&handle_pit_interrupt_entry, PIT_IRQ_LINE, false);

    pit_set_rate(1);
}