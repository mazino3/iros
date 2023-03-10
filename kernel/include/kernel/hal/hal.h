#ifndef _KERNEL_HAL_HAL_H
#define _KERNEL_HAL_HAL_H 1

#include <stdbool.h>

#include <kernel/arch/arch.h>
#include <kernel/hal/arch.h>

#include ARCH_SPECIFIC(asm_utils.h)
#include HAL_ARCH_SPECIFIC(hal.h)

void init_hal(void);
void init_cpus(void);
void init_smp(void);
void enumerate_devices(void);

void kernel_disable_graphics(void);
bool kernel_use_graphics(void);

void hal_poweroff(void);

#endif /* _KERNEL_HAL_HAL_H */
