#ifndef _KERNEL_HAL_X86_64_DRIVERS_ATA_DRIVE_H
#define _KERNEL_HAL_X86_64_DRIVERS_ATA_DRIVE_H 1

#include <kernel/hal/block.h>
#include <kernel/hal/hw_device.h>

struct ide_channel;

struct ata_drive {
    struct hw_device hw_device;
    struct ide_channel *channel;
    struct block_device *block_device;
    bool supports_lba_48;
    int drive;
};

struct ata_drive *ata_detect_drive(struct ide_channel *channel, int drive);

#endif /* _KERNEL_HAL_X86_64_DRIVERS_ATA_DRIVE_H */
