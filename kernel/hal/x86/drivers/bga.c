#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <kernel/fs/dev.h>
#include <kernel/hal/hal.h>
#include <kernel/hal/output.h>
#include <kernel/hal/pci_driver.h>
#include <kernel/hal/x86/drivers/bga.h>
#include <kernel/mem/page_frame_allocator.h>
#include <kernel/mem/phys_vm_object.h>
#include <kernel/mem/vm_allocator.h>
#include <kernel/mem/vm_region.h>
#include <kernel/proc/task.h>

struct bga_data {
    uintptr_t frame_buffer;
    uint16_t x_res;
    uint16_t y_res;
};

static int bga_ioctl(struct fs_device *device, unsigned long request, void *argp);
static intptr_t bga_mmap(struct fs_device *device, void *addr, size_t len, int prot, int flags, off_t offset);

static struct bga_data data = { 0 };

struct fs_device_ops bga_ops = {
    .ioctl = bga_ioctl,
    .mmap = bga_mmap,
};

struct fs_device bga_device = {
    .name = "fb0",
    .device_number = 0x00600,
    .mode = S_IFCHR | 0600,
    .ops = &bga_ops,
    .private = &data,
    .lock = MUTEX_INITIALIZER(bga_device.lock),
};

static int bga_ioctl(struct fs_device *device, unsigned long request, void *argp) {
    assert(device);

    switch (request) {
        case SSRES: {
            struct screen_res *res = argp;
            bga_write(BGA_INDEX_ENABLE, BGA_VBE_DISABLED);
            bga_write(BGA_INDEX_X_RES, res->x);
            bga_write(BGA_INDEX_Y_RES, res->y);
            bga_write(BGA_INDEX_VIRT_WIDTH, res->x);
            bga_write(BGA_INDEX_VIRT_HEIGHT, res->y * 2);
            bga_write(BGA_INDEX_X_OFFSET, 0);
            bga_write(BGA_INDEX_Y_OFFSET, 0);
            bga_write(BGA_INDEX_BPP, BGA_BPP_32);
            bga_write(BGA_INDEX_BANK, 0);
            bga_write(BGA_INDEX_ENABLE, BGA_VBE_ENABLED | BGA_LFB_ENABLED);
            data.x_res = res->x;
            data.y_res = res->y;
            return 0;
        }
        case SSWAPBUF: {
            uintptr_t new_buffer = (uintptr_t) argp;
            bga_write(BGA_INDEX_Y_OFFSET, (new_buffer - find_user_vm_region_by_addr(new_buffer)->start) / data.x_res / sizeof(uint32_t));
            return 0;
        }
        default:
            return -ENOTTY;
    }
}

static intptr_t bga_mmap(struct fs_device *device, void *addr, size_t len, int prot, int flags, off_t offset) {
    if (offset != 0 || len != sizeof(uint32_t) * data.x_res * data.y_res * 2 || !(flags & MAP_SHARED)) {
        return -ENODEV;
    }

    size_t total_size = sizeof(uint32_t) * (size_t) data.x_res * (size_t) data.y_res * (size_t) 2;
    if (!device->vm_object) {
        device->vm_object = vm_create_phys_object(data.frame_buffer, total_size, device_on_kill, device);
    } else {
        bump_vm_object(device->vm_object);
    }

    struct vm_region *region = map_region(addr, len, prot, flags, VM_DEVICE_MEMORY_MAP_DONT_FREE_PHYS_PAGES);
    region->vm_object = device->vm_object;
    region->vm_object_offset = 0;
    region->flags |= VM_SHARED;

    int ret = vm_map_region_with_object(region);
    if (ret < 0) {
        return (intptr_t) ret;
    }

    // FIXME: this should probably be done during the device's construction
    for (uintptr_t i = region->start; i < region->end; i += PAGE_SIZE) {
        mark_used(data.frame_buffer + (i - region->start), PAGE_SIZE);
    }

    return (intptr_t) region->start;
}

static struct pci_device *bga_create(struct hw_device *parent, struct pci_device_location location, struct pci_device_id id,
                                     struct pci_device_info info) {
    if (!kernel_use_graphics()) {
        return NULL;
    }

    data.frame_buffer = pci_config_read32(location, PCI_CONFIG_BAR(0)) & ~0xF;
    debug_log("Detected bga device: [ %p ]\n", (void *) data.frame_buffer);

    struct pci_device *device = create_pci_device(location, info);
    init_hw_device(&device->hw_device, "BGA Graphics Card", parent, hw_device_id_pci(id), &bga_device, NULL);
    device->hw_device.status = HW_STATUS_ACTIVE;
    return device;
}

static struct pci_device_id bga_device_ids[] = {
    { .vendor_id = 0x1234, .device_id = 0x1111 },
};

static struct pci_driver_ops bga_pci_ops = {
    .create = bga_create,
};

static struct pci_driver bga_driver = {
    .name = "BGA Driver",
    .device_id_table = bga_device_ids,
    .device_id_count = sizeof(bga_device_ids) / sizeof(bga_device_ids[0]),
    .ops = &bga_pci_ops,
};

PCI_DRIVER_INIT(bga_driver);
