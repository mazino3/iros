#define __libc_internal
#include <assert.h>
#include <bits/dynamic_elf_object.h>
#include <bits/mapped_elf_file.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

// #define DL_LOG

void *dlopen(const char *file, int flags) {
    if (!file) {
        struct dynamic_elf_object *global_handle = __loader_get_dynamic_object_head();
        assert(global_handle->is_program);
        return global_handle;
    }

#ifdef DL_LOG
    fprintf(stderr, "opening file `%s' <LAZY=%d NOW=%d GLOBAL=%d LOCAL=%d>\n", file, !!(flags & RTLD_LAZY), !!(flags & RTLD_NOW),
            !!(flags & RTLD_GLOBAL), !!(flags & RTLD_LOCAL));
#endif /* DL_LOG */

    if (!!(flags & RTLD_LAZY) && !!(flags & RTLD_NOW)) {
        __dl_set_error("dlopen cannot be called with both RTLD_LAZY and RTLD_NOW set");
        return NULL;
    }

    if (!(flags & RTLD_LAZY) && !(flags & RTLD_NOW)) {
        __dl_set_error("dlopen cannot be called with neither RTLD_LAZY and RTLD_NOW set");
        return NULL;
    }

    if (flags & ~(RTLD_LAZY | RTLD_NOW | RTLD_GLOBAL | RTLD_LOCAL)) {
        __dl_set_error("dlopen called with invalid flag bits set");
        return NULL;
    }

    struct dynamic_elf_object *ret = NULL;

    struct mapped_elf_file mapped_file = __loader_build_mapped_elf_file(file);
    if (mapped_file.base == NULL) {
        return ret;
    }

    // Check for an object that was already loaded with this exact path.
    for (struct dynamic_elf_object *obj = __loader_get_dynamic_object_head()->next; obj; obj = obj->next) {
        if (strcmp(obj->full_path, file) == 0) {
            ret = __loader_bump_dynamic_elf_object(obj);
            goto cleanup_mapped_file;
        }
    }

    struct dynamic_elf_object *object = __loader_load_mapped_elf_file(&mapped_file, file, !!(flags & RTLD_GLOBAL), false);
    if (!object) {
        goto cleanup_mapped_file;
    }

    // Check for an object with the same so name value.
    const char *so_name = __loader_object_name(object);
    for (struct dynamic_elf_object *obj = __loader_get_dynamic_object_head()->next; obj && obj != object; obj = obj->next) {
        if (strcmp(__loader_object_name(obj), so_name) == 0) {
            ret = __loader_bump_dynamic_elf_object(obj);
            goto cleanup_dynamic_object;
        }
    }

    if (__loader_load_dependencies(object)) {
        goto cleanup_dynamic_object;
    }

    if (__loader_process_relocations(object, !!(flags & RTLD_NOW))) {
        goto cleanup_dynamic_object;
    }

    __loader_call_init_functions(object, 0, NULL, NULL);

    ret = object;
    goto cleanup_mapped_file;

cleanup_dynamic_object:
    __loader_drop_dynamic_elf_object(object);

cleanup_mapped_file:
    __loader_destroy_mapped_elf_file(&mapped_file);
    return ret;
}
