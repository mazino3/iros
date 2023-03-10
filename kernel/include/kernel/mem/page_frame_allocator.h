#ifndef _KERNEL_MEM_PAGE_FRAME_ALLOCATOR_H
#define _KERNEL_MEM_PAGE_FRAME_ALLOCATOR_H 1

#include <stdint.h>

#include <kernel/mem/page.h>

#define PAGE_BITMAP_SIZE (PAGE_SIZE * 32)

struct process;

void init_page_frame_allocator();
void mark_used(uintptr_t phys_addr_start, uintptr_t length);
uintptr_t get_next_phys_page(struct process *process);
uintptr_t get_contiguous_pages(size_t pages);
void free_phys_page(uintptr_t phys_addr, struct process *process);

#endif /* _KERNEL_MEM_PAGE_FRAME_ALLOCATOR_H */
