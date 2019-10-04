#ifndef _KERNEL_HAL_TIMER_H
#define _KERNEL_HAL_TIMER_H 1

#include <sys/time.h>

#include <kernel/proc/process.h>
#include <kernel/sched/process_sched.h>

void register_callback(void (*callback)(void), unsigned int ms);
void set_sched_callback(void (*callback)(SCHED_CALLBACK_ARG_TYPE), unsigned int ms);

time_t get_time();

#endif /* _KERNEL_HAL_TIMER_H */