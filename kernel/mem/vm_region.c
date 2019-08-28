#include <stddef.h>
#include <stdint.h>

#include <kernel/mem/page.h>
#include <kernel/mem/vm_region.h>

struct vm_region *add_vm_region(struct vm_region *list, struct vm_region *to_add) {
    struct vm_region **link = &list;
    while (*link != NULL && (*link)->start < to_add->end) {
        link = &(*link)->next;
    }
    to_add->next = *link;
    *link = to_add;
    return list;
}

struct vm_region *remove_vm_region(struct vm_region *list, uint64_t type) {
    struct vm_region **link = &list;
    while (*link != NULL && (*link)->type != type) {
        link = &(*link)->next;
    }
    *link = (*link)->next;
    return list;
}

struct vm_region *get_vm_region(struct vm_region *list, uint64_t type) {
    while (list != NULL && list->type != type) {
        list = list->next;
    }
    return list;
}

int extend_vm_region_end(struct vm_region *list, uint64_t type, size_t num_pages) {
    list = get_vm_region(list, type);
    if (list == NULL) {
        return -1;
    }

    uintptr_t new_end = list->end + num_pages * PAGE_SIZE;
    if (list->next != NULL && new_end > list->next->start) {
        return -2; // Indicate there is no room
    }
    list->end = new_end;
    return 0;
}

int extend_vm_region_start(struct vm_region *list, uint64_t type, size_t num_pages) {
    struct vm_region *entry = list;
    if (entry == NULL) { return -1; }
    if (entry->type == type) {
        entry->start -= num_pages * PAGE_SIZE;
        return 0;
    }

    while (entry->next != NULL && entry->next->type != type) {
        entry = entry->next;
    }

    if (entry->next == NULL) { return -1; }

    uintptr_t new_start = list->next->start - num_pages * PAGE_SIZE;
    if (list->end > new_start) {
        return -2; // Indicate there is no room
    }
    list->next->start = new_start;
    return 0;
}

int contract_vm_region_end(struct vm_region *list, uint64_t type, size_t num_pages) {
    list = get_vm_region(list, type);
    if (list == NULL) {
        return -1;
    }

    uintptr_t new_end = list->end - num_pages * PAGE_SIZE;
    if (new_end < list->start) {
        return -2; // Indicate took away too much
    }
    list->end = new_end;
    return 0;
}

int contract_vm_region_start(struct vm_region *list, uint64_t type, size_t num_pages) {
    list = get_vm_region(list, type);
    if (list == NULL) {
        return -1;
    }

    uintptr_t new_start = list->start + num_pages * PAGE_SIZE;
    if (new_start > list->end) {
        return -2; // Indicate took away too much
    }
    list->start = new_start;
    return 0;
}