#ifndef _RELOCATIONS_H
#define _RELOCATIONS_H 1

#include "loader.h"

void got_resolver(void) LOADER_PRIVATE;
uintptr_t do_got_resolve(const struct dynamic_elf_object *obj, size_t plt_offset) LOADER_PRIVATE;
void do_process_relocations(const struct dynamic_elf_object *self) LOADER_PRIVATE;
void process_relocations(const struct dynamic_elf_object *self) LOADER_PRIVATE;

#endif /* _RELOCATIONS_H */
