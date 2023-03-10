#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>

#include <kernel/boot/boot_info.h>
#include <kernel/fs/vfs.h>
#include <kernel/hal/output.h>
#include <kernel/hal/processor.h>
#include <kernel/hal/x86/drivers/vga.h>
#include <kernel/mem/kernel_vm.h>
#include <kernel/mem/page.h>
#include <kernel/mem/page_frame_allocator.h>
#include <kernel/mem/vm_allocator.h>
#include <kernel/mem/vm_object.h>
#include <kernel/mem/vm_region.h>
#include <kernel/proc/profile.h>
#include <kernel/proc/stats.h>
#include <kernel/proc/task.h>
#include <kernel/util/spinlock.h>

// #define MMAP_DEBUG

static struct vm_region *kernel_vm_list = NULL;
#ifdef __x86_64__
static struct vm_region kernel_phys_id;
#endif /* ARCH==X86_64 */
static struct vm_region kernel_text;
static struct vm_region kernel_rod;
static struct vm_region kernel_data;
static struct vm_region kernel_heap;
static struct vm_region initrd;
static spinlock_t kernel_vm_lock = SPINLOCK_INITIALIZER;

void init_vm_allocator(void) {
#ifdef __x86_64__
    kernel_phys_id.start = PHYS_ID_START;
    kernel_phys_id.end = kernel_phys_id.start + g_phys_page_stats.phys_memory_max;
    kernel_phys_id.flags = VM_NO_EXEC | VM_GLOBAL | VM_WRITE;
    kernel_phys_id.type = VM_KERNEL_PHYS_ID;
    kernel_vm_list = add_vm_region(kernel_vm_list, &kernel_phys_id);
    assert(kernel_vm_list == &kernel_phys_id);
#endif /* ARCH==X86_64 */

    kernel_text.start = KERNEL_VM_START & ~0xFFF;
    kernel_text.end = kernel_text.start + NUM_PAGES(KERNEL_TEXT_START, KERNEL_TEXT_END) * PAGE_SIZE;
    kernel_text.flags = VM_GLOBAL;
    kernel_text.type = VM_KERNEL_TEXT;
    kernel_vm_list = add_vm_region(kernel_vm_list, &kernel_text);

    kernel_rod.start = kernel_text.end;
    kernel_rod.end = kernel_rod.start + NUM_PAGES(KERNEL_ROD_START, KERNEL_ROD_END) * PAGE_SIZE;
    kernel_rod.flags = VM_GLOBAL | VM_NO_EXEC;
    kernel_rod.type = VM_KERNEL_ROD;
    kernel_vm_list = add_vm_region(kernel_vm_list, &kernel_rod);

    kernel_data.start = kernel_rod.end;
    kernel_data.end = kernel_data.start + NUM_PAGES(KERNEL_DATA_START, KERNEL_BSS_END) * PAGE_SIZE;
    kernel_data.flags = VM_WRITE | VM_GLOBAL | VM_NO_EXEC;
    kernel_data.type = VM_KERNEL_DATA;
    kernel_vm_list = add_vm_region(kernel_vm_list, &kernel_data);

    struct boot_info *boot_info = boot_get_boot_info();
    initrd.start = KERNEL_HEAP_START;
    initrd.end = ((initrd.start + boot_info->initrd_phys_end - boot_info->initrd_phys_start) & ~0xFFF) + PAGE_SIZE;
    initrd.flags = VM_GLOBAL | VM_NO_EXEC;
    initrd.type = VM_INITRD;
    kernel_vm_list = add_vm_region(kernel_vm_list, &initrd);

    kernel_heap.start = initrd.end;
    kernel_heap.end = kernel_heap.start;
    kernel_heap.flags = VM_WRITE | VM_GLOBAL | VM_NO_EXEC;
    kernel_heap.type = VM_KERNEL_HEAP;
    kernel_vm_list = add_vm_region(kernel_vm_list, &kernel_heap);

    vm_bootstrap_temp_page_mapping();

    clear_initial_page_mappings();
    for (int i = 0; initrd.start + i < initrd.end; i += PAGE_SIZE) {
        map_phys_page(boot_info->initrd_phys_start + i, initrd.start + i, initrd.flags, &idle_kernel_process);
    }

    for (struct vm_region *region = kernel_vm_list; region; region = region->next) {
        if (region->type != VM_KERNEL_PHYS_ID && region->type != VM_INITRD) {
            map_vm_region_flags(region, &idle_kernel_process);
        }
    }

    idle_kernel_process.process_memory = kernel_vm_list;
}

void dump_kernel_regions(uintptr_t addr) {
    struct vm_region *region = kernel_vm_list;
    while (region) {
        debug_log(" region: [ %p, %p, %p, %#.16" PRIX64 ", %s ]\n", region, (void *) region->start, (void *) region->end, region->flags,
                  vm_type_to_string(region->type));
        if (addr >= region->start && addr <= region->end) {
            debug_log("Addr found in above: [ %p ]\n", (void *) addr);
        }
        region = region->next;
    }
}

void *add_vm_pages_end(size_t n, uint64_t type) {
    struct vm_region *list;
    if (type > VM_KERNEL_HEAP) {
        list = get_current_task()->process->process_memory;
    } else {
        list = kernel_vm_list;
        spin_lock(&kernel_vm_lock);
    }

    struct vm_region *region = get_vm_region(list, type);

    uintptr_t old_end = region->end;
    if (extend_vm_region_end(list, type, n) < 0) {
        return NULL; // indicate there is no room
    }

    if (region->vm_object) {
        if (!region->vm_object->ops->extend || region->vm_object->ops->extend(region->vm_object, n)) {
            return NULL;
        }
    } else {
        for (size_t i = 0; i < n; i++) {
            map_page(old_end + i * PAGE_SIZE, region->flags, list == kernel_vm_list ? &idle_kernel_process : get_current_task()->process);
        }

        memset((void *) old_end, 0, n * PAGE_SIZE);
    }

    if (type <= VM_KERNEL_HEAP) {
        spin_unlock(&kernel_vm_lock);
    }

    return (void *) old_end;
}

size_t vm_compute_total_virtual_memory(struct process *process) {
    size_t result = 0;

    struct vm_region *region = process->process_memory;
    while (region) {
        result += region->end - region->start;
        region = region->next;
    }

    return result;
}

void *add_vm_pages_start(size_t n, uint64_t type) {
    struct vm_region *list;
    if (type > VM_KERNEL_HEAP) {
        list = get_current_task()->process->process_memory;
    } else {
        list = kernel_vm_list;
        spin_lock(&kernel_vm_lock);
    }

    struct vm_region *region = get_vm_region(list, type);
    uintptr_t old_start = region->start;
    if (extend_vm_region_start(list, type, n) < 0) {
        return NULL; // indicate there is no room
    }
    for (size_t i = 1; i <= n; i++) {
        map_page(old_start - i * PAGE_SIZE, region->flags, list == kernel_vm_list ? &idle_kernel_process : get_current_task()->process);
    }

    if (type <= VM_KERNEL_HEAP) {
        spin_unlock(&kernel_vm_lock);
    }

    memset((void *) (old_start - n * PAGE_SIZE), 0, n * PAGE_SIZE);
    return (void *) old_start;
}

void remove_vm_pages_end(size_t n, uint64_t type) {
    struct vm_region *list;
    if (type > VM_KERNEL_HEAP) {
        list = get_current_task()->process->process_memory;
    } else {
        list = kernel_vm_list;
        spin_lock(&kernel_vm_lock);
    }

    uintptr_t old_end = get_vm_region(list, type)->end;
    if (contract_vm_region_end(list, type, n) < 0) {
        printf("%s\n", "Error: Removed to much memory");
        abort();
    }

    if (type <= VM_KERNEL_HEAP) {
        spin_unlock(&kernel_vm_lock);
    }

    for (size_t i = 1; i <= n; i++) {
        unmap_page(old_end - i * PAGE_SIZE, list == kernel_vm_list ? &idle_kernel_process : get_current_task()->process);
    }
}

bool vm_is_kernel_address(uintptr_t addr) {
    return addr >= find_first_kernel_vm_region()->start;
}

static int do_unmap_range(uintptr_t addr, size_t length) {
    struct process *process = get_current_task()->process;
    int thread_count = process->ref_count;
    bool broadcast_tlb_flush = thread_count > 1;

    struct vm_region *r;
    while ((r = find_user_vm_region_in_range(addr, addr + length))) {
        if (r->start < addr && r->end > addr + length) {
#ifdef MMAP_DEBUG
            debug_log("Removing region (split): [ %#.16lX, %#.16lX, %#.16lX, %#.16lX ]\n", r->start, r->end, addr, length);
#endif /* MMAP_DEBUG */

            for (uintptr_t i = addr; i < addr + length; i += PAGE_SIZE) {
                do_unmap_page(i, !r->vm_object, true, broadcast_tlb_flush, process);
            }

            struct vm_region *to_add = calloc(1, sizeof(struct vm_region));
            to_add->start = addr + length;
            to_add->end = r->end;
            to_add->flags = r->flags;
            to_add->type = r->type;
            to_add->vm_object_offset = r->vm_object_offset + r->end - r->start;
            to_add->vm_object = r->vm_object;
            bump_vm_object(to_add->vm_object);

            r->end = addr;
            process->process_memory = add_vm_region(process->process_memory, to_add);
            break;
        }

        if (r->start < addr) {
#ifdef MMAP_DEBUG
            debug_log("Removing region (start): [ %#.16lX, %#.16lX, %#.16lX, %#.16lX ]\n", r->start, r->end, addr, length);
#endif /* MMAP_DEBUG */

            assert(r->end >= addr);
            size_t end_save = r->end;

            while (r->end != addr) {
                r->end -= PAGE_SIZE;
                do_unmap_page(r->end, !r->vm_object, true, broadcast_tlb_flush, process);
            }

            length -= (end_save - addr);
            addr = end_save;
            continue;
        }

        if (r->end > addr + length) {
#ifdef MMAP_DEBUG
            debug_log("Removing region (end): [ %#.16lX, %#.16lX, %#.16lX, %#.16lX ]\n", r->start, r->end, addr, length);
#endif /* MMAP_DEBUG */

            assert(r->start <= addr + length);
            while (r->start != addr + length) {
                do_unmap_page(r->start, !r->vm_object, broadcast_tlb_flush, true, process);
                r->start += PAGE_SIZE;
                r->vm_object_offset += PAGE_SIZE;
            }

            // We are definately at the end
            break;
        }

#ifdef MMAP_DEBUG
        debug_log("Removing region: [ %#.16lX, %#.16lX, %#.16lX, %#.16lX ]\n", r->start, r->end, addr, length);
#endif /* MMAP_DEBUG */

        bool region_important_to_profiler = !(r->flags & VM_NO_EXEC) && r->vm_object && r->vm_object->type == VM_INODE;
        if (r->vm_object) {
            drop_vm_object(r->vm_object);
        }

        for (uintptr_t i = r->start; i < r->end; i += PAGE_SIZE) {
            do_unmap_page(i, !r->vm_object, true, broadcast_tlb_flush, process);
        }

        if (r == process->process_memory) {
            process->process_memory = r->next;
        } else {
            struct vm_region *prev = process->process_memory;
            while (prev->next != r) {
                prev = prev->next;
            }

            prev->next = r->next;
        }

        free(r);

        if (region_important_to_profiler && atomic_load(&process->should_profile)) {
            proc_record_memory_map(process);
        }
    }
    return 0;
}

int unmap_range(uintptr_t addr, size_t length) {
    if (addr % PAGE_SIZE != 0) {
        return -EINVAL;
    }

    if (length == 0) {
        return 0;
    }

    length = ((length + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE;

    struct process *process = get_current_task()->process;
    mutex_lock(&process->lock);

    int ret = do_unmap_range(addr, length);

    mutex_unlock(&process->lock);
    return ret;
}

struct vm_region *map_region(void *addr, size_t len, int prot, int flags, uint64_t type) {
    len = ((len + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE;

    if (addr != NULL && !(flags & MAP_FIXED)) {
        // Do not overwrite an already existing region
        if (find_user_vm_region_in_range((uintptr_t) addr, (uintptr_t) addr + len)) {
            addr = NULL;
        }
    }

    if (addr == NULL && !(flags & MAP_FIXED)) {
        if ((flags & MAP_STACK) || (type == VM_TASK_STACK)) {
            uintptr_t to_search = find_vm_region(VM_TASK_STACK)->start;
            struct vm_region *r;
            while ((r = find_user_vm_region_in_range(to_search - len, to_search))) {
                to_search = r->start;
            }
            addr = (void *) (to_search - len);
        } else {
            struct vm_region *heap = find_vm_region(VM_PROCESS_HEAP);
            uintptr_t to_search = heap ? heap->end + 0x10000000 : 0x10000000;
            struct vm_region *r;
            while ((r = find_user_vm_region_in_range(to_search, to_search + len))) {
                to_search = r->end + 5 * PAGE_SIZE;
            }
            addr = (void *) to_search;
        }
    }

    struct vm_region *to_add = calloc(1, sizeof(struct vm_region));
    to_add->start = (uintptr_t) addr;
    to_add->end = to_add->start + len;
    to_add->type = type;
    to_add->flags =
        (prot & PROT_WRITE ? VM_WRITE : 0) | (prot & PROT_EXEC ? 0 : VM_NO_EXEC) | (type == VM_TASK_STACK ? VM_STACK : 0) | VM_USER;

    struct process *process = get_current_task()->process;
    mutex_lock(&process->lock);
    do_unmap_range((uintptr_t) addr, len);
    process->process_memory = add_vm_region(process->process_memory, to_add);
    mutex_unlock(&process->lock);

#ifdef MMAP_DEBUG
    debug_log("Mapping region: [ %#.16lX, %#.16lX ]\n", to_add->start, to_add->end);
#endif /* MMAP_DEBUG */
    return to_add;
}

int map_range_protections(uintptr_t addr, size_t length, int prot) {
    if (addr % PAGE_SIZE != 0) {
        return -EINVAL;
    }

    if (length == 0) {
        return 0;
    }

    length = ((length + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE;

    int flags = prot == PROT_NONE ? VM_PROT_NONE : ((prot & PROT_WRITE ? VM_WRITE : 0) | VM_USER | (!(prot & PROT_EXEC) ? VM_NO_EXEC : 0));

    struct process *process = get_current_task()->process;
    mutex_lock(&process->lock);

    struct vm_region *r;
    while ((r = find_user_vm_region_in_range(addr, addr + length))) {
        if (r->start < addr && r->end > addr + length) {
#ifdef MMAP_DEBUG
            debug_log("Protecting region (split): [ %#.16lX, %#.16lX, %#.16lX, %#.16lX ]\n", r->start, r->end, addr, length);
#endif /* MMAP_DEBUG */

            if (r->type != VM_PROCESS_ANON_MAPPING && r->type != VM_TASK_STACK && r->type != VM_TASK_STACK_GUARD) {
                break;
            }

            struct vm_region *to_add = calloc(1, sizeof(struct vm_region));
            to_add->start = addr;
            to_add->end = addr + length;
            to_add->flags = (r->flags & VM_STACK) | flags;
            to_add->type = r->type;
            to_add->vm_object_offset = r->vm_object_offset;
            to_add->vm_object = r->vm_object;
            bump_vm_object(to_add->vm_object);
            process->process_memory = add_vm_region(process->process_memory, to_add);

            for (uintptr_t i = addr; i < addr + length; i += PAGE_SIZE) {
                map_page_flags(i, to_add->flags);
            }

            struct vm_region *to_add_last = calloc(1, sizeof(struct vm_region));
            to_add_last->start = addr + length;
            to_add_last->end = r->end;
            to_add_last->flags = r->flags;
            to_add_last->type = r->type;
            to_add_last->vm_object_offset = to_add->vm_object_offset + addr - to_add->start;
            to_add_last->vm_object = r->vm_object;
            bump_vm_object(to_add_last->vm_object);

            r->vm_object_offset += r->start - to_add->start;
            r->end = addr;

            process->process_memory = add_vm_region(process->process_memory, to_add_last);
            break;
        }

        if (r->start < addr) {
#ifdef MMAP_DEBUG
            debug_log("Protecting region (start): [ %#.16lX, %#.16lX, %#.16lX, %#.16lX ]\n", r->start, r->end, addr, length);
#endif /* MMAP_DEBUG */

            assert(r->end >= addr);
            size_t end_save = r->end;

            struct vm_region *to_add = calloc(1, sizeof(struct vm_region));
            to_add->start = addr;
            to_add->end = r->end;
            to_add->flags = (r->flags & VM_STACK) | flags;
            to_add->type = r->type;
            to_add->vm_object_offset = r->vm_object_offset + (addr - r->start);
            to_add->vm_object = r->vm_object;
            bump_vm_object(to_add->vm_object);

            while (r->end != addr) {
                r->end -= PAGE_SIZE;
                map_page_flags(r->end, to_add->flags);
            }

            length -= (end_save - addr);
            addr = end_save;

            process->process_memory = add_vm_region(process->process_memory, to_add);
            continue;
        }

        if (r->end > addr + length) {
#ifdef MMAP_DEBUG
            debug_log("Protecting region (end): [ %#.16lX, %#.16lX, %#.16lX, %#.16lX ]\n", r->start, r->end, addr, length);
#endif /* MMAP_DEBUG */

            assert(r->start <= addr + length);
            if (r->type != VM_PROCESS_ANON_MAPPING && r->type != VM_TASK_STACK && r->type != VM_TASK_STACK_GUARD) {
                break;
            }

            struct vm_region *to_add = calloc(1, sizeof(struct vm_region));
            to_add->start = r->start;
            to_add->end = addr + length;
            to_add->flags = (r->flags & VM_STACK) | flags;
            to_add->type = r->type;
            to_add->vm_object_offset = r->vm_object_offset;
            to_add->vm_object = r->vm_object;
            bump_vm_object(to_add->vm_object);

            while (r->start != addr + length) {
                map_page_flags(r->start, to_add->flags);
                r->start += PAGE_SIZE;
            }

            r->vm_object_offset += r->start - to_add->start;

            process->process_memory = add_vm_region(process->process_memory, to_add);
            // We are definately at the end
            break;
        }

#ifdef MMAP_DEBUG
        debug_log("Protecting region: [ %#.16lX, %#.16lX, %#.16lX, %#.16lX ]\n", r->start, r->end, addr, length);
#endif /* MMAP_DEBUG */

        r->flags = (r->flags & VM_STACK) | flags;

        for (uintptr_t i = r->start; i < r->end; i += PAGE_SIZE) {
            map_page_flags(i, r->flags);
        }

        length -= r->end - addr;
        addr = r->end;
    }

    mutex_unlock(&process->lock);
    return 0;
}

void remove_vm_pages_start(size_t n, uint64_t type) {
    struct vm_region *list;
    if (type > VM_KERNEL_HEAP) {
        list = get_current_task()->process->process_memory;
    } else {
        list = kernel_vm_list;
        spin_lock(&kernel_vm_lock);
    }

    uintptr_t old_start = get_vm_region(list, type)->start;
    if (contract_vm_region_start(list, type, n) < 0) {
        printf("%s\n", "Error: Removed to much memory");
        abort();
    }

    if (type <= VM_KERNEL_HEAP) {
        spin_unlock(&kernel_vm_lock);
    }

    struct process *process = get_current_task()->process;
    for (size_t i = 0; i < n; i++) {
        unmap_page(old_start + i * PAGE_SIZE, process);
    }
}

struct vm_region *find_first_kernel_vm_region() {
    struct vm_region *list = kernel_vm_list;
    struct vm_region *first = list;
    while (list != NULL) {
        if (list->start < first->start) {
            first = list;
        }

        list = list->next;
    }

    return first;
}

struct vm_region *find_vm_region(uint64_t type) {
    struct vm_region *list;
    if (type > VM_KERNEL_HEAP) {
        list = get_current_task()->process->process_memory;
    } else {
        list = kernel_vm_list;
        spin_lock(&kernel_vm_lock);
    }

    struct vm_region *region = get_vm_region(list, type);

    if (type <= VM_KERNEL_HEAP) {
        spin_unlock(&kernel_vm_lock);
    }

    return region;
}

struct vm_region *find_user_vm_region_by_addr(uintptr_t addr) {
    struct vm_region *region = get_current_task()->process->process_memory;

    while (region) {
        if (region->start <= addr && addr < region->end) {
            return region;
        }
        region = region->next;
    }

    return NULL;
}

struct vm_region *find_kernel_vm_region_by_addr(uintptr_t addr) {
    struct vm_region *region = get_current_task()->kernel_stack;
    if (region && region->start <= addr && addr < region->end) {
        return region;
    }

    spin_lock(&kernel_vm_lock);
    region = kernel_vm_list;

    while (region) {
        if (region->start <= addr && addr < region->end) {
            spin_unlock(&kernel_vm_lock);
            return region;
        }
        region = region->next;
    }

    spin_unlock(&kernel_vm_lock);
    return NULL;
}

struct vm_region *find_user_vm_region_in_range(uintptr_t start, uintptr_t end) {
    assert(start <= end);

    if (start == end) {
        return NULL;
    }

    struct vm_region *region = get_current_task()->process->process_memory;

    while (region) {
        if (((start <= region->start) && (region->end <= end)) ||  // start-end contain the region
            ((start >= region->start) && (start < region->end)) || // region contains start
            ((end > region->start) && (end <= region->end))        // region contains end
        ) {
            return region;
        }
        region = region->next;
    }

    return NULL;
}

struct vm_region *clone_process_vm() {
    struct process *process_to_clone = get_current_task()->process;
    struct vm_region *list = process_to_clone->process_memory;
    struct vm_region *new_list = NULL;
    struct vm_region *region = list;

    // Note that process should be locked by the caller. (recursive mutexes would be nice).
    while (region != NULL) {
        struct vm_region *to_add = calloc(1, sizeof(struct vm_region));
        memcpy(to_add, region, sizeof(struct vm_region));

        assert(to_add->vm_object || to_add->type == VM_KERNEL_STACK);
        if (to_add->vm_object) {
            if ((to_add->flags & VM_SHARED) || !(to_add->flags & VM_WRITE)) {
                bump_vm_object(to_add->vm_object);
            } else {
                mark_region_as_cow(region);
                to_add->vm_object = vm_clone_object(to_add->vm_object, to_add->vm_object_offset, to_add->end - to_add->start);
                to_add->vm_object_offset = 0;
            }
        }

        new_list = add_vm_region(new_list, to_add);
        region = region->next;
    }

    return new_list;
}

void dump_process_regions(struct process *process) {
    struct vm_region *region = process->process_memory;
    while (region) {
        debug_log("region: [ %p, %p, %p, %#.16" PRIX64 ", %s ]\n", region, (void *) region->start, (void *) region->end, region->flags,
                  vm_type_to_string(region->type));
        region = region->next;
    }
}

static struct vm_region *make_kernel_region(size_t size, uint64_t type) {
    struct vm_region *region = calloc(1, sizeof(struct vm_region));
    spin_lock(&kernel_vm_lock);

    struct vm_region *vm = get_vm_region(kernel_vm_list, VM_KERNEL_HEAP);
    while (vm->next && vm->next->type != VM_KERNEL_TEXT) {
        vm = vm->next;
    }

    // FIXME: use a better allocation strategy then sequential assignments
    uintptr_t start = vm->type == VM_KERNEL_HEAP ? vm->end + 1073741824ULL / 8ULL : vm->end;

    region->flags = VM_WRITE | VM_NO_EXEC;
    region->start = start;
    region->end = region->start + size;
    region->type = type;
    vm->next = region;

    spin_unlock(&kernel_vm_lock);
    return region;
}

struct vm_region *vm_allocate_kernel_region(size_t size) {
    assert(size % PAGE_SIZE == 0);

    struct vm_region *region = make_kernel_region(size, VM_KERNEL_ANON_MAPPING);
    for (size_t s = region->start; s < region->end; s += PAGE_SIZE) {
        map_page(s, region->flags, &idle_kernel_process);
    }
    return region;
}

struct vm_region *vm_reallocate_kernel_region(struct vm_region *kernel_region, size_t new_size) {
    struct vm_region *save = kernel_region;
    kernel_region = vm_allocate_kernel_region(new_size);
    if (save) {
        memcpy((void *) kernel_region->start, (void *) save->start, MIN(new_size, save->end - save->start));
        vm_free_kernel_region(save);
    }
    return kernel_region;
}

static void do_vm_free_kernel_region(struct vm_region *region, bool free_phys) {
    spin_lock(&kernel_vm_lock);

    struct vm_region *vm = kernel_vm_list;

    // NOTE: region can never be first, since it must have been allocated with vm_allocate_kernel_region()
    while (vm->next != region) {
        vm = vm->next;
    }
    vm->next = region->next;

    if (free_phys) {
        for (uintptr_t s = region->start; s < region->end; s += PAGE_SIZE) {
            unmap_page(s, &idle_kernel_process);
        }
    }
    free(region);

    spin_unlock(&kernel_vm_lock);
}

void vm_free_kernel_region(struct vm_region *region) {
    assert(region->type == VM_KERNEL_ANON_MAPPING);
    do_vm_free_kernel_region(region, true);
}

struct vm_region *vm_allocate_physically_mapped_kernel_region(uintptr_t phys_addr, size_t size) {
    assert(phys_addr % PAGE_SIZE == 0);

    size = ALIGN_UP(size, PAGE_SIZE);

    struct vm_region *region = make_kernel_region(size, VM_KERNEL_PHYS_MAPPING);
    if (!region) {
        return NULL;
    }

    for (uintptr_t addr = region->start; addr < region->end; addr += PAGE_SIZE, phys_addr += PAGE_SIZE) {
        map_phys_page(phys_addr, addr, region->flags, &initial_kernel_process);
    }

    return region;
}

void vm_free_physically_mapped_kernel_region(struct vm_region *region) {
    assert(region->type == VM_KERNEL_PHYS_MAPPING);
    do_vm_free_kernel_region(region, false);
}

struct vm_region *vm_allocate_kernel_vm(size_t size) {
    return make_kernel_region(size, VM_KERNEL_VM_MAPPING);
}

void vm_free_kernel_vm(struct vm_region *region) {
    assert(region->type == VM_KERNEL_VM_MAPPING);
    do_vm_free_kernel_region(region, false);
}

struct vm_region *vm_allocate_low_identity_map(uintptr_t start, uintptr_t size) {
    assert(start + size <= 0x100000);

    struct vm_region *region = calloc(1, sizeof(struct vm_region));
    region->flags = VM_WRITE;
    region->start = start & ~(PAGE_SIZE - 1);
    region->end = ((start + size + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE;
    region->type = VM_KERNEL_ID_MAPPING;

    for (size_t s = region->start; s < region->end; s += PAGE_SIZE) {
        map_phys_page(s, s, region->flags, &idle_kernel_process);
    }

    return region;
}

void vm_free_low_identity_map(struct vm_region *region) {
    for (size_t s = region->start; s < region->end; s += PAGE_SIZE) {
        do_unmap_page(s, false, true, true, &idle_kernel_process);
    }
    free(region);
}

struct vm_region *vm_allocate_dma_region(size_t size) {
    assert(size % PAGE_SIZE == 0);

    struct vm_region *region = make_kernel_region(size, VM_KERNEL_DMA_MAPPING);
    uint64_t phys_base = get_contiguous_pages(size / PAGE_SIZE);
    for (size_t s = region->start; s < region->end; s += PAGE_SIZE) {
        map_phys_page(phys_base + s - region->start, s, region->flags, &idle_kernel_process);
    }
    return region;
}

void vm_free_dma_region(struct vm_region *region) {
    assert(region->type == VM_KERNEL_DMA_MAPPING);
    do_vm_free_kernel_region(region, true);
}
