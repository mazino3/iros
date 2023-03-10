#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include <kernel/fs/vfs.h>
#include <kernel/hal/output.h>
#include <kernel/hal/processor.h>
#include <kernel/mem/inode_vm_object.h>
#include <kernel/mem/page.h>
#include <kernel/mem/page_frame_allocator.h>
#include <kernel/mem/phys_page.h>
#include <kernel/mem/vm_allocator.h>
#include <kernel/mem/vm_region.h>
#include <kernel/proc/task.h>

// #define INODE_VM_OBJECT_DEBUG

static int inode_map(struct vm_object *self, struct vm_region *region) {
    struct inode_vm_object_data *data = self->private_data;

    struct task *current_task = get_current_task();

    mutex_lock(&self->lock);
    for (uintptr_t i = region->start; i < region->end; i += PAGE_SIZE) {
        size_t page_index = (i + region->vm_object_offset - region->start) / PAGE_SIZE;
        assert(page_index < data->pages);

        if (data->phys_pages[page_index]) {
            if (!data->owned) {
                map_phys_page((uintptr_t) data->phys_pages[page_index], i, region->flags, current_task->process);
            } else {
                map_phys_page(data->phys_pages[page_index]->phys_addr, i, region->flags, current_task->process);
            }
        }
    }

    mutex_unlock(&self->lock);
    return 0;
}

static uintptr_t inode_handle_fault(struct vm_object *self, uintptr_t offset_into_self, bool *is_cow) {
    struct inode_vm_object_data *data = self->private_data;

    size_t page_index = offset_into_self / PAGE_SIZE;
    assert(page_index < data->pages);

    // FIXME: this is called from an exception handler, should we really be allowed
    //        to take a mutex here.
    mutex_lock(&self->lock);
    if (!data->owned) {
        return (uintptr_t) data->phys_pages[page_index];
    }

    if (data->phys_pages[page_index]) {
        uintptr_t ret = data->phys_pages[page_index]->phys_addr;
        mutex_unlock(&self->lock);
        return ret;
    }

    struct phys_page *page = allocate_phys_page();
    data->phys_pages[page_index] = page;

    uintptr_t phys_addr = page->phys_addr;

    // FIXME: either directly map the page into the current process's address space or
    //        add an API to read physical pages directly, without having to create a temporary
    //        mapping.
    // NOTE:  a temporary physical address mapping cannot be used here, since reading from an inode
    //        takes a mutex.
    struct vm_region *region = vm_allocate_physically_mapped_kernel_region(phys_addr, PAGE_SIZE);
    char *phys_page_mapping = (void *) region->start;

    struct inode *inode = data->inode;
    ssize_t read = inode->i_op->read(inode, phys_page_mapping, PAGE_SIZE, page_index * PAGE_SIZE);
    if (read == -1) {
        debug_log("Failed to read from disk: [ %s ]\n", strerror(-read));
        read = 0;
    }

    memset(phys_page_mapping + read, 0, PAGE_SIZE - read);
    vm_free_physically_mapped_kernel_region(region);
    mutex_unlock(&self->lock);

    *is_cow = false;
    return phys_addr;
}

static int inode_kill(struct vm_object *self) {
    struct inode_vm_object_data *data = self->private_data;

#ifdef INODE_VM_OBJECT_DEBUG
    debug_log("Destroying inode_vm_object: [ %p, %lu, %llu ]\n", self, data->inode->fsid, data->inode->index);
#endif /* INODE_VM_OBJECT_DEBUG */

    if (data->owned) {
        if (data->kernel_region) {
            vm_free_kernel_region(data->kernel_region);
        } else {
            for (size_t i = 0; i < data->pages; i++) {
                if (data->phys_pages[i]) {
                    drop_phys_page(data->phys_pages[i]);
                }
            }
        }
    }

    mutex_lock(&data->inode->lock);
    if (data->inode->vm_object == self) {
        data->inode->vm_object = NULL;
    }
    mutex_unlock(&data->inode->lock);
    drop_inode_reference(data->inode);

    free(data);
    return 0;
}

static struct vm_object_operations inode_ops = { .map = &inode_map, .handle_fault = &inode_handle_fault, .kill = &inode_kill };

struct vm_object *vm_create_inode_object(struct inode *inode, int map_flags __attribute__((unused))) {
    size_t num_pages = ((inode->size + PAGE_SIZE - 1) / PAGE_SIZE);
    struct inode_vm_object_data *data = malloc(sizeof(struct inode_vm_object_data) + num_pages * sizeof(struct phys_page *));
    assert(data);

    assert(inode->i_op->read);

    data->inode = bump_inode_reference(inode);
    data->owned = true;
    data->pages = num_pages;
    data->kernel_region = NULL;
    memset(data->phys_pages, 0, num_pages * sizeof(struct phys_page *));

    return vm_create_object(VM_INODE, &inode_ops, data);
}

struct vm_object *vm_create_direct_inode_object(struct inode *inode, struct vm_region *kernel_region) {
    size_t num_pages = ((inode->size + PAGE_SIZE - 1) / PAGE_SIZE);
    struct inode_vm_object_data *data = malloc(sizeof(struct inode_vm_object_data) + num_pages * sizeof(uintptr_t));
    assert(data);

    data->inode = bump_inode_reference(inode);
    data->owned = false;
    data->pages = num_pages;
    data->kernel_region = kernel_region;

    char *buffer = (char *) kernel_region->start;
    for (size_t i = 0; i < num_pages; i++) {
        data->phys_pages[i] = (void *) get_phys_addr((uintptr_t) (buffer + (i * PAGE_SIZE)));
    }

    // Make sure to zero excess bytes before allowing the pages to be mapped in
    memset(buffer + inode->size, 0, num_pages * PAGE_SIZE - inode->size);
    return vm_create_object(VM_INODE, &inode_ops, data);
}
