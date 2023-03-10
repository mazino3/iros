#include <elf.h>

#include "dynamic_elf_object.h"
#include "symbols.h"

static bool is_dependency(const struct dynamic_elf_object *obj, const struct dynamic_elf_object *maybe_parent) {
    if (obj == maybe_parent) {
        return true;
    }

    if (!maybe_parent->dependencies_were_loaded) {
        return false;
    }

    for (size_t i = 0; i < maybe_parent->dependencies_size; i++) {
        if (is_dependency(obj, maybe_parent->dependencies[i].resolved_object)) {
            return true;
        }
    }

    return false;
}

struct symbol_lookup_result do_symbol_lookup(const char *s, const struct dynamic_elf_object *current_object, int flags) {
    struct dynamic_elf_object *obj = dynamic_object_head;
#ifdef LOADER_SYMBOL_DEBUG
    loader_log("looking up `%s' for `%s'", s, object_name(current_object));
#endif /* LOADER_SYMBOL_DEBUG */
    while (obj) {
        if ((!(flags & SYMBOL_LOOKUP_NOT_CURRENT) || (obj != current_object)) && (obj->global || is_dependency(obj, current_object))) {
            const ElfW(Sym) *sym = lookup_symbol(obj, s);
            if (sym && sym->st_shndx != STN_UNDEF) {
                uint8_t visibility = sym->st_info >> 4;
                if (visibility == STB_GLOBAL || visibility == STB_WEAK) {
                    return (struct symbol_lookup_result) { .symbol = sym, .object = obj };
                }
            }
        }
        obj = obj->next;
    }
    return (struct symbol_lookup_result) { .symbol = NULL, .object = NULL };
}
LOADER_HIDDEN_EXPORT(do_symbol_lookup, __loader_do_symbol_lookup);

struct symbol_lookup_result do_addr_lookup(void *_addr) {
    struct dynamic_elf_object *obj = dynamic_object_head;
    uintptr_t addr = (uintptr_t) _addr;
    while (obj) {
        uintptr_t obj_vm_base = (uintptr_t) obj->raw_data;
        if (addr >= obj_vm_base && addr < obj_vm_base + obj->raw_data_size) {
            struct symbol_lookup_result result = (struct symbol_lookup_result) { .symbol = NULL, .object = obj };
            result.symbol = lookup_addr(obj, addr);
            return result;
        }
        obj = obj->next;
    }
    return (struct symbol_lookup_result) { .symbol = NULL, .object = NULL };
}
LOADER_HIDDEN_EXPORT(do_addr_lookup, __loader_do_addr_lookup);
