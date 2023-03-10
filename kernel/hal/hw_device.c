#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include <kernel/fs/dev.h>
#include <kernel/hal/hw_device.h>
#include <kernel/hal/output.h>

#define HW_DEVICE_DEBUG

static void default_hw_device_destructor(struct hw_device *device) {
    free(device);
}

struct hw_device *create_hw_device(const char *name, struct hw_device *parent, struct hw_device_id id, struct fs_device *fs_device) {
    struct hw_device *device = malloc(sizeof(*device));
    init_hw_device(device, name, parent, id, fs_device, default_hw_device_destructor);
    return device;
}

void init_hw_device(struct hw_device *device, const char *name, struct hw_device *parent, struct hw_device_id id,
                    struct fs_device *fs_device, void (*destructor)(struct hw_device *device)) {
    strncpy(device->name, name, sizeof(device->name) - 1);
    device->name[sizeof(device->name) - 1] = '\0';

    init_list(&device->children);
    device->parent = parent;
    init_spinlock(&device->tree_lock);

    device->ref_count = 1;
    device->status = HW_STATUS_DETECTED;
    device->id = id;

    device->fs_device = fs_device;
    device->destructor = destructor;

    if (fs_device) {
        dev_register(fs_device);
    }

    if (parent) {
        spin_lock(&parent->tree_lock);
        list_append(&parent->children, &device->siblings);
        spin_unlock(&parent->tree_lock);
    }
}

static void kill_hw_device(struct hw_device *device) {
#ifdef HW_DEVICE_DEBUG
    debug_log("Destroying hw device: [ %s ]\n", device->name);
#endif /* HW_DEVICE_DEBUG */

    void (*destructor)(struct hw_device * self) = device->destructor;
    if (destructor) {
        destructor(device);
    }
}

void drop_hw_device(struct hw_device *device) {
    if (atomic_fetch_sub(&device->ref_count, 1) == 1) {
        kill_hw_device(device);
    }
}

struct hw_device *bump_hw_device(struct hw_device *device) {
    atomic_fetch_add(&device->ref_count, 1);
    return device;
}

static void remove_hw_device_without_parent(struct hw_device *device) {
    atomic_store(&device->status, HW_STATUS_REMOVED);

#ifdef HW_DEVICE_DEBUG
    debug_log("Removing hw device: [ %s ]\n", device->name);
#endif /* HW_DEVICE_DEBUG */

    device->parent = NULL;

    spin_lock(&device->tree_lock);
    struct list_node *children = &device->children;
    list_for_each_entry_safe(children, child, struct hw_device, siblings) {
        remove_hw_device_without_parent(child);
        list_remove(&child->siblings);
    }
    spin_unlock(&device->tree_lock);

    struct fs_device *fs_device = device->fs_device;
    if (fs_device) {
        dev_unregister(fs_device);
        device->fs_device = NULL;
    }

    drop_hw_device(device);
}

void remove_hw_device(struct hw_device *device) {
    struct hw_device *parent = device->parent;
    if (parent) {
        spin_lock(&parent->tree_lock);
        list_remove(&device->siblings);
        spin_unlock(&parent->tree_lock);
    }

    remove_hw_device_without_parent(device);
}

int show_hw_device_id(struct hw_device_id id, char *buffer, size_t buffer_length) {
    const char *type = hw_type_to_string(id.type);
    switch (id.type) {
        case HW_TYPE_PS2:
            return snprintf(buffer, buffer_length, "%s <%#.2X:%#.2X>", type, id.ps2_id.byte0, id.ps2_id.byte1);
        case HW_TYPE_PCI:
            return snprintf(buffer, buffer_length, "%s <%#.4X:%#.4X>", type, id.pci_id.vendor_id, id.pci_id.device_id);
        case HW_TYPE_NONE:
        case HW_TYPE_ISA:
        default:
            return snprintf(buffer, buffer_length, "%s", type);
    }
}

int show_hw_device(struct hw_device *device, char *buffer, size_t buffer_length) {
    char buf[64];
    show_hw_device_id(device->id, buf, sizeof(buf));
    return snprintf(buffer, buffer_length, "%s (%s) [%s]", device->name, buf, hw_status_to_string(device->status));
}

const char *hw_type_to_string(enum hw_device_type type) {
    switch (type) {
        case HW_TYPE_NONE:
            return "None";
        case HW_TYPE_ISA:
            return "ISA";
        case HW_TYPE_PS2:
            return "PS/2";
        case HW_TYPE_PCI:
            return "PCI";
        default:
            return "Invalid";
    }
}

const char *hw_status_to_string(enum hw_device_status status) {
    switch (status) {
        case HW_STATUS_ACTIVE:
            return "Active";
        case HW_STATUS_DETECTED:
            return "Detected";
        case HW_STATUS_REMOVED:
            return "Removed";
        default:
            return "Invalid";
    }
}
