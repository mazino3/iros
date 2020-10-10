#include <kernel/fs/dev.h>
#include <kernel/hal/block.h>
#include <kernel/hal/gpt.h>
#include <kernel/hal/mbr.h>
#include <kernel/hal/output.h>
#include <kernel/hal/partition.h>
#include <kernel/mem/page.h>
#include <kernel/mem/phys_page.h>
#include <kernel/mem/vm_allocator.h>

void mbr_partition_device(struct block_device *block_device) {
    mutex_lock(&block_device->device->lock);
    struct phys_page *page = block_device->op->read_page(block_device, 0);
    mutex_unlock(&block_device->device->lock);

    struct mbr_table *mbr = create_phys_addr_mapping(page->phys_addr);
    if (mbr->boot_signature != MBR_SIGNATURE) {
        debug_log("Drive has no MBR: [ %lu ]\n", block_device->device->device_number);
        goto done;
    }

    debug_log("Drive has MBR: [ %lu ]\n", block_device->device->device_number);
    int ebr_index = -1;
    int i;
    for (i = 0; i < MBR_MAX_PARTITIONS; i++) {
        struct mbr_partition *partition = &mbr->partitions[i];
        if (i == 0 && partition->partition_type == GPT_MBR_TYPE) {
            gpt_partition_device(block_device);
            goto done;
        }

        if (partition->partition_type == 0) {
            continue;
        }

        blkcnt_t lba_end = partition->lba_start + partition->sector_count;
        if (lba_end > block_device->block_count) {
            debug_log("Partition goes past the end of the device: [ %d ]\n", i);
            goto done;
        }

        if (partition->partition_type == MBR_EBR_CHS || partition->partition_type == MBR_EBR_LBA) {
            debug_log("EBR detected: [ %u, %u ]\n", partition->lba_start, partition->sector_count);
            if (ebr_index != -1) {
                debug_log("Two EBRs detected, the drive is broken\n");
                goto done;
            }
            ebr_index = i;
            continue;
        }

        debug_log("MBR partition: [ %d, %u, %u, %u, %u ]\n", i + 1, partition->drive_attributes, partition->partition_type,
                  partition->lba_start, partition->sector_count);
        create_and_register_partition_device(block_device, partition->sector_count, partition->lba_start, i + 1);
    }

    if (ebr_index != -1) {
        struct mbr_partition *ebr_partition = &mbr->partitions[ebr_index];
        size_t extended_lba_start = ebr_partition->lba_start;
        size_t extended_lba_end = extended_lba_start + ebr_partition->sector_count;
        size_t ebr_lba = extended_lba_start;
        for (;;) {
            drop_phys_page(page);
            mutex_lock(&block_device->device->lock);
            page = block_device->op->read_page(block_device, ALIGN_DOWN(ebr_lba, (PAGE_SIZE / block_device->block_size)));
            mutex_unlock(&block_device->device->lock);

            mbr = create_phys_addr_mapping(page->phys_addr) + (ebr_lba % (PAGE_SIZE / block_device->block_size) * block_device->block_size);
            if (mbr->boot_signature != MBR_SIGNATURE) {
                debug_log("EBR partition is invalid\n");
                goto done;
            }

            struct mbr_partition *logical_partition = &mbr->partitions[0];
            if (logical_partition->partition_type == 0 || logical_partition->lba_start == 0) {
                break;
            }

            size_t logical_lba_start = ebr_lba + logical_partition->lba_start;
            size_t logical_lba_end = logical_lba_start + logical_partition->sector_count;

            if (logical_lba_end > extended_lba_end) {
                debug_log("Logical partition is outside of the extended partition\n");
                goto done;
            }

            create_and_register_partition_device(block_device, logical_lba_end - logical_lba_start, logical_lba_start, ++i);
            debug_log("MBR logical partition: [ %d, %u, %u, %lu, %lu ]\n", i, logical_partition->drive_attributes,
                      logical_partition->partition_type, logical_lba_start, logical_lba_end - logical_lba_start);

            ebr_partition = &mbr->partitions[1];
            if (ebr_partition->partition_type == 0) {
                break;
            }

            ebr_lba = ebr_partition->lba_start + extended_lba_start;
            if ((ebr_partition->partition_type != MBR_EBR_CHS && ebr_partition->partition_type != MBR_EBR_LBA) ||
                ebr_lba >= extended_lba_end) {
                debug_log("EBR partition next is invalid: [ %u, %lu ]\n", ebr_partition->partition_type, ebr_lba);
                goto done;
            }
        }
    }

done:
    if (page) {
        drop_phys_page(page);
    }
}
