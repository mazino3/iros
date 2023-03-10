#include <elf.h>
#include <inttypes.h>
#include <link.h>
#include <stdatomic.h>
#include <sys/param.h>

#include "dynamic_elf_object.h"
#include "mapped_elf_file.h"
#include "tls_record.h"

static void do_call_fini_functions(struct dynamic_elf_object *obj);

struct dynamic_elf_object build_dynamic_elf_object(const ElfW(Dyn) * dynamic_table, size_t dynamic_count, uint8_t *base, size_t size,
                                                   size_t relocation_offset, void *phdr_start, size_t phdr_count, size_t tls_module_id,
                                                   const char *full_path, bool global) {
    struct dynamic_elf_object self = { 0 };
    self.full_path = loader_malloc(strlen(full_path) + 1);
    strcpy(self.full_path, full_path);
    self.tls_module_id = tls_module_id;
    self.raw_data = base;
    self.raw_data_size = size;
    self.relocation_offset = relocation_offset;
    self.phdr_start = phdr_start;
    self.phdr_count = phdr_count;
    self.global = global;
    self.ref_count = 1;
    for (size_t i = 0; i < dynamic_count; i++) {
        const ElfW(Dyn) *entry = &dynamic_table[i];
        switch (entry->d_tag) {
            case DT_NULL:
                return self;
            case DT_NEEDED:
                if (self.dependencies_size >= self.dependencies_max) {
                    self.dependencies_max = MAX(20, self.dependencies_max * 2);
                    self.dependencies =
                        loader_realloc(self.dependencies, self.dependencies_max * sizeof(union dynamic_elf_object_dependency));
                }
                self.dependencies[self.dependencies_size++].string_table_offset = entry->d_un.d_val;
                break;
            case DT_PLTRELSZ:
                self.plt_size = entry->d_un.d_val;
                break;
            case DT_PLTGOT:
                self.got_addr = entry->d_un.d_ptr;
                break;
            case DT_HASH:
                self.hash_table = entry->d_un.d_ptr;
                break;
            case DT_STRTAB:
                self.string_table = entry->d_un.d_ptr;
                break;
            case DT_SYMTAB:
                self.symbol_table = entry->d_un.d_ptr;
                break;
            case DT_RELA:
                self.rela_addr = entry->d_un.d_ptr;
                break;
            case DT_RELASZ:
                self.rela_size = entry->d_un.d_val;
                break;
            case DT_RELAENT:
                self.rela_entry_size = entry->d_un.d_val;
                break;
            case DT_STRSZ:
                self.string_table_size = entry->d_un.d_val;
                break;
            case DT_SYMENT:
                self.symbol_entry_size = entry->d_un.d_val;
                break;
            case DT_INIT:
                self.init_addr = entry->d_un.d_ptr;
                break;
            case DT_FINI:
                self.fini_addr = entry->d_un.d_ptr;
                break;
            case DT_SONAME:
                self.so_name_offset = entry->d_un.d_val;
                break;
            case DT_RPATH:
                // ignored for now
                break;
            case DT_SYMBOLIC:
                // meaningless during dynamic linking
                break;
            case DT_REL:
                self.rel_addr = entry->d_un.d_ptr;
                break;
            case DT_RELSZ:
                self.rel_size = entry->d_un.d_val;
                break;
            case DT_RELENT:
                self.rel_entry_size = entry->d_un.d_val;
                break;
            case DT_PLTREL:
                self.plt_type = entry->d_un.d_val;
                break;
            case DT_DEBUG:
                break;
            case DT_TEXTREL:
                break;
            case DT_JMPREL:
                self.plt_addr = entry->d_un.d_ptr;
                break;
            case DT_INIT_ARRAY:
                self.init_array = entry->d_un.d_ptr;
                break;
            case DT_FINI_ARRAY:
                self.fini_array = entry->d_un.d_ptr;
                break;
            case DT_INIT_ARRAYSZ:
                self.init_array_size = entry->d_un.d_val;
                break;
            case DT_FINI_ARRAYSZ:
                self.fini_array_size = entry->d_un.d_val;
                break;
            case DT_FLAGS:
                self.dt_flags = entry->d_un.d_val;
                break;
            case DT_PREINIT_ARRAY:
                self.preinit_array = entry->d_un.d_ptr;
                break;
            case DT_PREINIT_ARRAY_SZ:
                self.preinit_array_size = entry->d_un.d_val;
                break;
            case DT_VERSYM:
                break;
            case DT_RELACOUNT:
                break;
            case DT_RELCOUNT:
                break;
            case DT_VERDEF:
                break;
            case DT_VERDEFNUM:
                break;
            case DT_VERNEED:
                break;
            case DT_VERNEEDNUM:
                break;
            default:
#ifdef __x86_64__
                loader_log("Unkown DT_* value %#.16lX", entry->d_tag);
#else
                loader_log("Unkown DT_* value %#.8X", entry->d_tag);
#endif
                break;
        }
    }
    return self;
}

void destroy_dynamic_elf_object(struct dynamic_elf_object *self) {
#ifdef LOADER_DEBUG
    loader_log("destroying `%s'", object_name(self));
#endif /* LOADER_DEBUG */

    if (self->init_functions_called) {
        do_call_fini_functions(self);
    }
    if (self->dependencies_were_loaded) {
        for (size_t i = 0; i < self->dependencies_size; i++) {
            drop_dynamic_elf_object(self->dependencies[i].resolved_object);
        }
    }
    loader_free(self->dependencies);

    loader_free(self->full_path);

    // NOTE: If this object was the program of the loader, this could not be freed. However, only dlopen'ed objects can be destroyed.
    loader_free(self->phdr_start);

    if (self->tls_module_id) {
        remove_tls_record(self->tls_module_id);
    }
    munmap(self->raw_data, self->raw_data_size);
}

const char *dynamic_strings(const struct dynamic_elf_object *self) {
    return (const char *) (self->string_table + self->relocation_offset);
}

const char *dynamic_string(const struct dynamic_elf_object *self, size_t i) {
    return &dynamic_strings(self)[i];
}
LOADER_HIDDEN_EXPORT(dynamic_string, __loader_dynamic_string);

const char *object_name(const struct dynamic_elf_object *self) {
    if (self->so_name_offset) {
        return dynamic_string(self, self->so_name_offset);
    }
    return self->full_path;
}
LOADER_HIDDEN_EXPORT(object_name, __loader_object_name);

size_t rela_count(const struct dynamic_elf_object *self) {
    if (!self->rela_size || !self->rela_entry_size) {
        return 0;
    }
    return self->rela_size / self->rela_entry_size;
}

const ElfW(Rela) * rela_table(const struct dynamic_elf_object *self) {
    return (const ElfW(Rela) *) (self->rela_addr + self->relocation_offset);
}

const ElfW(Rela) * rela_at(const struct dynamic_elf_object *self, size_t i) {
    return &rela_table(self)[i];
}

size_t rel_count(const struct dynamic_elf_object *self) {
    if (!self->rel_size || !self->rel_entry_size) {
        return 0;
    }
    return self->rel_size / self->rel_entry_size;
}

const ElfW(Rel) * rel_table(const struct dynamic_elf_object *self) {
    return (const ElfW(Rel) *) (self->rel_addr + self->relocation_offset);
}

const ElfW(Rel) * rel_at(const struct dynamic_elf_object *self, size_t i) {
    return &rel_table(self)[i];
}

size_t plt_relocation_count(const struct dynamic_elf_object *self) {
    size_t ent_size = self->plt_type == DT_RELA ? sizeof(ElfW(Rela)) : sizeof(ElfW(Rel));

    if (!self->plt_size) {
        return 0;
    }
    return self->plt_size / ent_size;
}

const void *plt_relocation_table(const struct dynamic_elf_object *self) {
    return (void *) (self->plt_addr + self->relocation_offset);
}

const void *plt_relocation_at(const struct dynamic_elf_object *self, size_t i) {
    if (self->plt_type == DT_RELA) {
        const ElfW(Rela) *tbl = plt_relocation_table(self);
        return &tbl[i];
    }
    const ElfW(Rel) *tbl = plt_relocation_table(self);
    return &tbl[i];
}

const ElfW(Sym) * symbol_table(const struct dynamic_elf_object *self) {
    return (const ElfW(Sym) *) (self->symbol_table + self->relocation_offset);
}

const ElfW(Sym) * symbol_at(const struct dynamic_elf_object *self, size_t i) {
    return &symbol_table(self)[i];
}

const char *symbol_name(const struct dynamic_elf_object *self, size_t i) {
    return dynamic_string(self, symbol_at(self, i)->st_name);
}

const ElfW(Word) * hash_table(const struct dynamic_elf_object *self) {
    return (const ElfW(Word) *) (self->hash_table + self->relocation_offset);
}

const ElfW(Sym) * lookup_symbol(const struct dynamic_elf_object *self, const char *s) {
    const ElfW(Word) *ht = hash_table(self);
    ElfW(Word) nbucket = ht[0];
    ElfW(Word) nchain = ht[1];
    unsigned long hashed_value = elf_hash(s);
    unsigned long bucket_index = hashed_value % nbucket;
    ElfW(Word) symbol_index = ht[2 + bucket_index];
    while (symbol_index != STN_UNDEF) {
        if (strcmp(symbol_name(self, symbol_index), s) == 0) {
            return symbol_at(self, symbol_index);
        }

        size_t chain_index = 2 + nbucket + symbol_index;
        if (symbol_index >= nchain) {
            return NULL;
        }
        symbol_index = ht[chain_index];
    }
    return NULL;
}
LOADER_HIDDEN_EXPORT(lookup_symbol, __loader_lookup_symbol);

const ElfW(Sym) * lookup_addr(const struct dynamic_elf_object *self, uintptr_t addr) {
    addr -= self->relocation_offset;

    const ElfW(Word) *ht = hash_table(self);
    ElfW(Word) nbucket = ht[0];
    ElfW(Word) nchain = ht[1];
    size_t num_symbols = nbucket + nchain;
    for (size_t i = 0; i < num_symbols; i++) {
        const ElfW(Sym) *sym = symbol_at(self, i);
        if (addr >= sym->st_value && addr < sym->st_value + MAX(1, sym->st_size)) {
            return sym;
        }
    }
    return NULL;
}

void free_dynamic_elf_object(struct dynamic_elf_object *self) {
    remove_dynamic_object(self);
    destroy_dynamic_elf_object(self);
    loader_free(self);
}

void drop_dynamic_elf_object(struct dynamic_elf_object *self) {
    int ref_count = atomic_fetch_sub(&self->ref_count, 1);
    if (ref_count == 1) {
        free_dynamic_elf_object(self);
    }
}
LOADER_HIDDEN_EXPORT(drop_dynamic_elf_object, __loader_drop_dynamic_elf_object);

struct dynamic_elf_object *bump_dynamic_elf_object(struct dynamic_elf_object *self) {
    atomic_fetch_add(&self->ref_count, 1);
    return self;
}
LOADER_HIDDEN_EXPORT(bump_dynamic_elf_object, __loader_bump_dynamic_elf_object);

static void do_call_init_functions(struct dynamic_elf_object *obj, int argc, char **argv, char **envp) {
    if (obj->init_functions_called) {
        return;
    }
    obj->init_functions_called = true;

#ifdef LOADER_DEBUG
    loader_log("doing init functions for `%s'", object_name(obj));
#endif /* LOADER_DEBUG */

    if (obj->preinit_array_size) {
        init_function_t *preinit = (init_function_t *) (obj->preinit_array + obj->relocation_offset);
        for (size_t i = 0; i < obj->preinit_array_size / sizeof(init_function_t); i++) {
            preinit[i](argc, argv, envp);
        }
    }

    if (obj->init_addr) {
        init_function_t init = (init_function_t) (obj->init_addr + obj->relocation_offset);
        init(argc, argv, envp);
    }

    if (obj->init_array_size) {
        init_function_t *init = (init_function_t *) (obj->init_array + obj->relocation_offset);
        for (size_t i = 0; i < obj->init_array_size / sizeof(init_function_t); i++) {
            init[i](argc, argv, envp);
        }
    }
}

void call_init_functions(struct dynamic_elf_object *self, int argc, char **argv, char **envp) {
    // Before the init functions can be called, the objects must be sorted such that objects
    // are initialized after all of their dependencies. If cycles occur, this is impossible,
    // and such code must not rely on a well defined global constructor order. This sorting is
    // necessary in cases where a shared object has dependencies not found in the initial object.
    // For instance:
    //   /bin/git
    //     libz.so
    //     libcurl.so
    //       libcrypto.so
    //         libc.so
    //     libc.so
    // Without sorting, objects will be loaded in a DFS manner: /bin/git, libz.so, libcurl.so, libc.so, libcrypto.so
    // but since libcrypto attempts to call getenv(), libcrypto must be reordered to appear before libc.so.
    struct dynamic_elf_object *cycle_start = NULL;
    for (struct dynamic_elf_object *obj = dynamic_object_tail; obj != self;) {
    again:
        // This means that there is a cyclic dependency.
        if (obj == cycle_start) {
            obj = obj->prev;
            cycle_start = NULL;
            continue;
        }

        // Iterate through the object's dependencies, and any of these dependencies were loaded
        // before this object. If so, move this object before said dependency, so that its
        // constructor will be called in the proper order. To prevent cycles from looping forever,
        // the start of such a cycle is remembered, so that this loop is only one once per each
        // object per each "iteration" (denoted by reducing the number of objects looked at).
        for (size_t i = 0; i < obj->dependencies_size; i++) {
            struct dynamic_elf_object *dependency = obj->dependencies[i].resolved_object;
            for (struct dynamic_elf_object *o = self; o != obj; o = o->next) {
                if (o == dependency) {
                    if (!cycle_start) {
                        cycle_start = obj;
                    }
                    struct dynamic_elf_object *obj_prev = obj->prev;
                    obj_prev->next = obj->next;
                    if (obj_prev->next) {
                        obj_prev->next->prev = obj_prev;
                    }

                    dependency->prev->next = obj;
                    obj->prev = dependency->prev;
                    obj->next = dependency;
                    dependency->prev = obj;

                    obj = obj_prev;
                    goto again;
                }
            }
        }

        obj = obj->prev;
        cycle_start = NULL;
        continue;
    }

    for (struct dynamic_elf_object *obj = dynamic_object_tail; obj; obj = obj->prev) {
        do_call_init_functions(obj, argc, argv, envp);
        if (obj == self) {
            break;
        }
    }
}
LOADER_HIDDEN_EXPORT(call_init_functions, __loader_call_init_functions);

static void do_call_fini_functions(struct dynamic_elf_object *obj) {
    if (obj->fini_functions_called) {
        return;
    }
    obj->fini_functions_called = true;

#ifdef LOADER_DEBUG
    loader_log("doing fini functions for `%s'", object_name(obj));
#endif /* LOADER_DEBUG */

    if (obj->fini_array_size) {
        fini_function_t *fini = (fini_function_t *) (obj->fini_array + obj->relocation_offset);
        for (size_t i = 0; i < obj->fini_array_size / sizeof(fini_function_t); i++) {
            fini[i]();
        }
    }

    if (obj->fini_addr) {
        fini_function_t fini = (fini_function_t) (obj->fini_addr + obj->relocation_offset);
        fini();
    }
}

void call_fini_functions(struct dynamic_elf_object *self) {
    for (struct dynamic_elf_object *obj = self; obj; obj = obj->next) {
        do_call_fini_functions(obj);
    }
}
LOADER_HIDDEN_EXPORT(call_fini_functions, __loader_call_fini_functions);

void add_dynamic_object(struct dynamic_elf_object *obj) {
    if (!dynamic_object_head) {
        dynamic_object_head = dynamic_object_tail = obj;
    } else {
        insque(obj, dynamic_object_tail);
        dynamic_object_tail = obj;
    }
}

void remove_dynamic_object(struct dynamic_elf_object *obj) {
    if (dynamic_object_tail == obj) {
        dynamic_object_tail = obj->prev;
    }
    remque(obj);
}

static struct dynamic_elf_object *find_dynamic_object_by_name(const char *lib_name) {
    struct dynamic_elf_object *obj = dynamic_object_head;
    while (obj) {
        if (strcmp(object_name(obj), lib_name) == 0) {
            return obj;
        }
        obj = obj->next;
    }
    return NULL;
}

static int do_load_dependencies(struct dynamic_elf_object *obj, bool use_initial_tls) {
    if (obj->dependencies_were_loaded) {
        return 0;
    }
    obj->dependencies_were_loaded = true;

    for (size_t i = 0; i < obj->dependencies_size; i++) {
        const char *lib_name = dynamic_string(obj, obj->dependencies[i].string_table_offset);
        struct dynamic_elf_object *existing = find_dynamic_object_by_name(lib_name);
        if (existing) {
            obj->dependencies[i].resolved_object = bump_dynamic_elf_object(existing);
            continue;
        }

        char path[256];
        strcpy(path, "/usr/lib/");
        strcpy(path + strlen("/usr/lib/"), lib_name);
#ifdef LOADER_DEBUG
        loader_log("Loading dependency of `%s': `%s'", object_name(obj), lib_name);
#endif /* LOADER_DEBUG */

        struct mapped_elf_file lib = build_mapped_elf_file(path);
        if (lib.base == NULL) {
            // Prevent the code from thinking the unloaded dependencies were loaded.
            obj->dependencies_size = i;
            return -1;
        }

        struct dynamic_elf_object *loaded_lib = load_mapped_elf_file(&lib, path, obj->global, use_initial_tls);
        destroy_mapped_elf_file(&lib);

        if (!loaded_lib) {
            obj->dependencies_size = i;
            return -1;
        }

        if ((loaded_lib->dt_flags & DF_STATIC_TLS) && !use_initial_tls) {
            loader_err("`%s' wants to use static tls, but is being dynamically loaded", object_name(loaded_lib));
            obj->dependencies_size = i;
            free_dynamic_elf_object(loaded_lib);
            return -1;
        }

        obj->dependencies[i].resolved_object = loaded_lib;
    }
    return 0;
}

int load_dependencies(struct dynamic_elf_object *obj_head) {
    for (struct dynamic_elf_object *obj = obj_head; obj; obj = obj->next) {
        int ret = do_load_dependencies(obj, obj_head->is_program);
        if (ret) {
            return ret;
        }
    }
    return 0;
}
LOADER_HIDDEN_EXPORT(load_dependencies, __loader_load_dependencies);

struct dynamic_elf_object *get_dynamic_object_head(void) {
    return dynamic_object_head;
}
LOADER_HIDDEN_EXPORT(get_dynamic_object_head, __loader_get_dynamic_object_head);

struct dynamic_elf_object *get_dynamic_object_tail(void) {
    return dynamic_object_tail;
}
LOADER_HIDDEN_EXPORT(get_dynamic_object_tail, __loader_get_dynamic_object_tail);

int iter_phdr(int (*iter)(struct dl_phdr_info *info, size_t size, void *closure), void *closure) {
    int ret = 0;
    for (struct dynamic_elf_object *obj = dynamic_object_head; obj; obj = obj->next) {
        void *tls_addr = NULL;
        if (obj->tls_module_id != 0) {
            tls_addr = tls_records[obj->tls_module_id - 1].tls_image;
        }
        struct dl_phdr_info info = {
            .dlpi_addr = obj->relocation_offset,
            .dlpi_name = obj == dynamic_object_head ? "" : obj->full_path,
            .dlpi_phdr = obj->phdr_start,
            .dlpi_phnum = obj->phdr_count,
            .dlpi_tls_modid = obj->tls_module_id,
            .dlpi_tls_data = tls_addr,
        };
        ret = iter(&info, sizeof(info), closure);
    }
    return ret;
}
LOADER_HIDDEN_EXPORT(iter_phdr, __loader_iter_phdr);
