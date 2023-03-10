#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/param.h>

#include <kernel/fs/vfs.h>
#include <kernel/hal/processor.h>
#include <kernel/mem/anon_vm_object.h>
#include <kernel/mem/inode_vm_object.h>
#include <kernel/mem/page.h>
#include <kernel/mem/vm_allocator.h>
#include <kernel/mem/vm_region.h>
#include <kernel/proc/elf64.h>
#include <kernel/proc/task.h>
#include <kernel/util/validators.h>

// #define ELF64_DEBUG

static ElfW(Sym) * kernel_symbols;
static char *kernel_string_table;
static size_t kernel_symbols_size;
static void *kernel_buffer;

static void try_load_symbols(void *buffer, ElfW(Sym) * *symbols, size_t *symbols_size, char **string_table) {
    ElfW(Ehdr) *elf_header = (ElfW(Ehdr) *) buffer;
    ElfW(Shdr) *section_headers = (ElfW(Shdr) *) (((uintptr_t) buffer) + elf_header->e_shoff);

    char *section_string_table = (char *) (((uintptr_t) buffer) + section_headers[elf_header->e_shstrndx].sh_offset);
    for (int i = 0; i < elf_header->e_shnum; i++) {
        if (section_headers[i].sh_type == SHT_SYMTAB) {
            *symbols = (ElfW(Sym) *) (((uintptr_t) buffer) + section_headers[i].sh_offset);
            *symbols_size = section_headers[i].sh_size;
        } else if (section_headers[i].sh_type == SHT_STRTAB && strcmp(".strtab", section_string_table + section_headers[i].sh_name) == 0) {
            *string_table = (char *) (((uintptr_t) buffer) + section_headers[i].sh_offset);
        }
    }
}

void init_kernel_symbols(void) {
    int ret = fs_read_all_path("/boot/kernel", &kernel_buffer, NULL, NULL);
    if (ret) {
        debug_log("failed to read kernel object file: [ %s ]\n", strerror(-ret));
        return;
    }

    if (!elf64_is_valid(kernel_buffer)) {
        debug_log("kernel object is file is not elf64?\n");
        free(kernel_buffer);
        kernel_buffer = NULL;
        return;
    }

    try_load_symbols(kernel_buffer, &kernel_symbols, &kernel_symbols_size, &kernel_string_table);
    if (!kernel_symbols || !kernel_string_table) {
        debug_log("failed to load kernel symbols\n");
        free(kernel_buffer);
        kernel_buffer = NULL;
    }
}

bool elf64_is_valid(void *buffer) {
    /* Should Probably Also Check Sections And Architecture */

    ElfW(Ehdr) *elf_header = buffer;
    if (elf_header->e_ident[EI_MAG0] != 0x7F || elf_header->e_ident[EI_MAG1] != 'E' || elf_header->e_ident[EI_MAG2] != 'L' ||
        elf_header->e_ident[EI_MAG3] != 'F') {
        return false;
    }

#ifdef __x86_64__
    uint8_t expected_class = ELFCLASS64;
#else
    uint8_t expected_class = ELFCLASS32;
#endif
    if (elf_header->e_ident[EI_CLASS] != expected_class || elf_header->e_ident[EI_DATA] != ELFDATA2LSB ||
        elf_header->e_ident[EI_VERSION] != EV_CURRENT || elf_header->e_ident[EI_OSABI] != ELFOSABI_SYSV ||
        elf_header->e_ident[EI_ABIVERSION] != 0) {
        return false;
    }

    return elf_header->e_type == ET_EXEC;
}

uintptr_t elf64_get_start(void *buffer) {
    ElfW(Ehdr) *elf_header = buffer;
    ElfW(Phdr) *program_header = (ElfW(Phdr) *) ((uintptr_t) buffer + elf_header->e_phoff);
    for (int i = 0; i < elf_header->e_phnum; i++) {
        if (program_header[i].p_type == PT_LOAD) {
            return program_header[i].p_vaddr;
        }
    }
    return 0;
}

uintptr_t elf64_load_program(void *buffer, size_t length, struct file *execuatable, struct initial_process_info *info) {
    (void) length;

    ElfW(Ehdr) *elf_header = buffer;
    ElfW(Phdr) *program_headers = (ElfW(Phdr) *) (((uintptr_t) buffer) + elf_header->e_phoff);

    uintptr_t offset = 0;
    const char *interpreter = NULL;
    size_t tls_size = 0;
    uintptr_t text_start = -1;
    uintptr_t text_end = 0;
    uintptr_t data_start = -1;
    uintptr_t data_end = 0;
    for (int i = 0; i < elf_header->e_phnum; i++) {
        switch (program_headers[i].p_type) {
            case PT_DYNAMIC:
            case PT_PHDR:
                continue;
            case PT_INTERP:
                interpreter = ((const char *) buffer) + program_headers[i].p_offset;
                continue;
            case PT_TLS:
                tls_size = program_headers[i].p_memsz;
                continue;
            case PT_LOAD:
                if (program_headers[i].p_flags & PF_X) {
                    text_start = program_headers[i].p_vaddr & ~(PAGE_SIZE - 1);
                    text_end = ((program_headers[i].p_vaddr + program_headers[i].p_memsz + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE;
                } else {
                    data_start = MIN(data_start, program_headers[i].p_vaddr & ~(PAGE_SIZE - 1));
                    data_end =
                        MAX(data_end, ((program_headers[i].p_vaddr + program_headers[i].p_memsz + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE);
                }
                continue;
            default:
                debug_log("Unkown program header type: [ %d ]\n", program_headers[i].p_type);
                continue;
        }
    }

    size_t phdr_region_size = ALIGN_UP(elf_header->e_phnum * elf_header->e_phentsize, PAGE_SIZE);

    // Loading an interpreter
    if (elf_header->e_type == ET_DYN) {
        size_t size = (text_start != (uintptr_t) -1 ? (text_end - text_start) : 0) +
                      (data_end >= data_start ? (data_end - data_start) : 0) + phdr_region_size;
        struct vm_region *first = get_current_task()->process->process_memory;
        assert(first->start - PAGE_SIZE >= size);
        offset = first->start - size;
    }

    assert(offset % PAGE_SIZE == 0);

    uintptr_t entry = elf_header->e_entry;
    if (elf_header->e_type == ET_EXEC) {
        info->program_entry = entry + offset;
        info->program_offset = elf64_get_start(buffer);
        info->program_size = data_end - info->program_offset + tls_size;
        info->program_size = ((info->program_size + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE;
    }

    if (data_end >= data_start) {
#ifdef ELF64_DEBUG
        debug_log("Creating data region: [ %p, %" PRIuPTR " ]\n", (void *) data_start + offset, data_end - data_start);
#endif /* ELF64_DEBUG */
        struct vm_region *data_region = map_region((void *) (data_start + offset), data_end - data_start, PROT_READ | PROT_WRITE,
                                                   MAP_ANON | MAP_PRIVATE, VM_PROCESS_DATA);
        struct vm_object *object = vm_create_anon_object(data_end - data_start);
        data_region->vm_object = object;
        data_region->vm_object_offset = 0;
        vm_map_region_with_object(data_region);
    }

    if (tls_size != 0) {
#ifdef ELF64_DEBUG
        debug_log("Creating tls region: [ %p, %lu ]\n", (void *) data_end + offset, tls_size);
#endif /* ELF64_DEBUG */
        struct vm_region *tls_region =
            map_region((void *) (data_end + offset), tls_size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, VM_PROCESS_TLS_MASTER_COPY);
        struct vm_object *object = vm_create_anon_object(tls_size);
        tls_region->vm_object = object;
        tls_region->vm_object_offset = 0;
        vm_map_region_with_object(tls_region);
    }

    size_t phdr_region_start = data_end + offset + tls_size;

#ifdef ELF64_DEBUG
    debug_log("Creating PHDR region: [ %#.16lX, %lu ]\n", phdr_region_start, phdr_region_size);
#endif /* ELF64_DEBUG */

    struct vm_region *phdr_region =
        map_region((void *) phdr_region_start, phdr_region_size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, VM_PROCESS_ROD);
    struct vm_object *object = vm_create_anon_object(phdr_region_size);
    phdr_region->vm_object = object;
    phdr_region->vm_object_offset = 0;
    vm_map_region_with_object(phdr_region);
    memcpy((void *) phdr_region_start, program_headers, elf_header->e_phnum * elf_header->e_phentsize);

    if (elf_header->e_type == ET_EXEC) {
        info->program_phdr_start = (void *) phdr_region_start;
        info->program_phdr_count = elf_header->e_phnum;
    } else {
        info->loader_phdr_start = (void *) phdr_region_start;
        info->loader_phdr_count = elf_header->e_phnum;
    }

    for (int i = 0; i < elf_header->e_phnum; i++) {
        switch (program_headers[i].p_type) {
            case PT_PHDR:
            case PT_INTERP:
                continue;
            case PT_DYNAMIC:
                if (elf_header->e_type == ET_EXEC) {
                    info->program_dynamic_start = program_headers[i].p_vaddr + offset;
                    info->program_dynamic_size = program_headers[i].p_memsz;
                } else if (elf_header->e_type == ET_DYN) {
                    info->loader_dynamic_start = program_headers[i].p_vaddr + offset;
                    info->loader_dynamic_size = program_headers[i].p_memsz;
                    info->loader_offset = offset;
                    info->loader_size = data_end - offset + tls_size;
                }
                continue;
            case PT_TLS:
                assert(info);
                info->tls_alignment = program_headers[i].p_align;
                info->tls_size = tls_size;
                info->tls_start = (void *) (offset + data_end);
                memcpy(info->tls_start, buffer + program_headers[i].p_offset, program_headers[i].p_filesz);
                continue;
            case PT_LOAD:
                if (program_headers[i].p_flags & PF_X) {
#ifdef ELF64_DEBUG
                    debug_log("Creating text region: [ %p, %" PRIuPTR " ]\n", (void *) (program_headers[i].p_vaddr + offset),
                              ((program_headers[i].p_filesz + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE);
#endif /* ELF64_DEBUG */
                    assert(fs_mmap((void *) (program_headers[i].p_vaddr + offset), program_headers[i].p_filesz, PROT_READ | PROT_EXEC,
                                   MAP_SHARED, execuatable, program_headers[i].p_offset) != (intptr_t) MAP_FAILED);
                    if (!offset) {
                        find_user_vm_region_by_addr(program_headers[i].p_vaddr + offset)->type = VM_PROCESS_TEXT;
                    }
                } else {
                    memcpy((void *) (program_headers[i].p_vaddr + offset), ((char *) buffer) + program_headers[i].p_offset,
                           program_headers[i].p_filesz);
                }
                continue;
            default:
                debug_log("Unkown program header type: [ %d ]\n", program_headers[i].p_type);
                continue;
        }
    }

    if (interpreter) {
        assert(info);
#ifdef ELF64_DEBUG
        debug_log("loading interpreter: [ %s ]\n", interpreter);
#endif /* ELF64_DEBUG */
        int error = 0;
        struct file *file = fs_openat(fs_root(), interpreter, O_RDONLY, 0, &error);
        assert(error == 0);
        size_t size = fs_file_size(file);
        void *interp = (void *) fs_mmap(NULL, size, PROT_READ, MAP_SHARED, file, 0);
        assert(interp != MAP_FAILED);
        entry = elf64_load_program(interp, size, file, info);
        unmap_range((uintptr_t) interp, ((size + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE);
        fs_close(file);
    }

    return entry + offset;
}

void elf64_map_heap(struct task *task, struct initial_process_info *info) {
    struct vm_region *task_heap = calloc(1, sizeof(struct vm_region));
    task_heap->flags = VM_USER | VM_WRITE | VM_NO_EXEC;
    task_heap->type = VM_PROCESS_HEAP;
    task_heap->start = ((info->program_offset + info->program_size) & ~0xFFF) + 100 * PAGE_SIZE;
#ifdef ELF64_DEBUG
    debug_log("Heap start: [ %p ]\n", (void *) task_heap->start);
#endif /* ELF64_DEBUG */
    task_heap->end = task_heap->start;
    task_heap->vm_object = vm_create_anon_object(0);
    task_heap->vm_object_offset = 0;
    task->process->process_memory = add_vm_region(task->process->process_memory, task_heap);
}

struct stack_frame {
    struct stack_frame *next;
    uintptr_t rip;
};

static size_t print_symbol_at(uintptr_t rip, ElfW(Sym) * symbols, uintptr_t symbols_size, const char *string_table,
                              size_t (*output)(void *closure, const char *string, ...), void *closure1, bool kernel) {
    if (kernel) {
        for (int i = 0; symbols && string_table && (uintptr_t) (symbols + i) < ((uintptr_t) symbols) + symbols_size; i++) {
            if (symbols[i].st_name != 0 && symbols[i].st_size != 0) {
                if (rip >= symbols[i].st_value && rip <= symbols[i].st_value + symbols[i].st_size) {
                    return output(closure1, "<%s> [ %#.16lX, %#.16lX, %s ]\n", kernel ? "K" : "U", rip, symbols[i].st_value,
                                  string_table + symbols[i].st_name);
                }
            }
        }

        // We didn't find a matching symbol
        return output(closure1, "<%s> [ %#.16lX, %#.16lX, %s ]\n", "K", rip, 0UL, "??");
    }

    // Each instruction pointer may be in a different shared object.
    // Find the vm_region it is associated with, and then use it to get the desired offset.
    struct vm_region *region = find_user_vm_region_by_addr(rip);
    if (!region || !region->vm_object || region->vm_object->type != VM_INODE) {
        return output(closure1, "<%s> [ %#.16lX, %#.16lX, %s, %ld, %ld ]\n", "U", rip, 0UL, "??", 0, 0);
    }

    if (region->type != VM_PROCESS_TEXT) {
        rip -= region->start;
    }
    struct inode *inode = ((struct inode_vm_object_data *) region->vm_object->private_data)->inode;
    ino_t id = inode->index;
    dev_t dev = inode->fsid;
    return output(closure1, "<%s> [ %#.16lX, %#.16lX, %s, %ld, %ld ]\n", "U", rip, 0UL, "??", dev, id);
}

static size_t do_stack_trace(uintptr_t rip, uintptr_t rbp, ElfW(Sym) * symbols, size_t symbols_size, char *string_table,
                             size_t (*output)(void *closure, const char *string, ...), void *closure1, bool kernel) {
    size_t ret = print_symbol_at(rip, symbols, symbols_size, string_table, output, closure1, kernel);

    struct stack_frame *frame = (struct stack_frame *) rbp;
    int (*validate)(const void *addr, size_t size) = kernel ? &validate_kernel_read : &validate_read;
    while (!validate(frame, sizeof(struct stack_frame)) && frame) {
        struct stack_frame *old_frame = frame;
        frame = frame->next;
        if (!old_frame->rip) {
            break;
        }
        ret += print_symbol_at(old_frame->rip, symbols, symbols_size, string_table, output, closure1, kernel);
    }

    return ret;
}

// NOTE: this must be called from within a task's address space
size_t do_elf64_stack_trace(struct task *task, bool extra_info, size_t (*output)(void *closure, const char *string, ...), void *closure) {
    uintptr_t rsp = task->in_kernel ? task_get_stack_pointer(task->user_task_state) : task_get_stack_pointer(&task->arch_task.task_state);
    uintptr_t rbp = task->in_kernel ? task_get_base_pointer(task->user_task_state) : task_get_base_pointer(&task->arch_task.task_state);
    uintptr_t rip =
        task->in_kernel ? task_get_instruction_pointer(task->user_task_state) : task_get_instruction_pointer(&task->arch_task.task_state);

    if (extra_info) {
        dump_process_regions(task->process);
    }

    if (extra_info) {
        debug_log("Dumping core: [ %p, %p ]\n", (void *) rip, (void *) rsp);
    }

    size_t ret = do_stack_trace(rip, rbp, NULL, 0, NULL, output, closure, false);

    return ret;
}

static size_t debug_log_wrapper(void *closure __attribute__((unused)), const char *string, ...) {
    va_list parameters;
    va_start(parameters, string);

    size_t ret = vdebug_log(string, parameters);

    va_end(parameters);
    return ret;
}

void elf64_stack_trace(struct task *task, bool extra_info) {
    do_elf64_stack_trace(task, extra_info, debug_log_wrapper, NULL);
}

void kernel_stack_trace(uintptr_t instruction_pointer, uintptr_t frame_base) {
    do_stack_trace(instruction_pointer, frame_base, kernel_symbols, kernel_symbols_size, kernel_string_table, debug_log_wrapper, NULL,
                   true);

    struct task *current = get_current_task();
    if (current->in_kernel && !current->kernel_task) {
        elf64_stack_trace(current, false);
    }
}

struct snprintf_object {
    char *buffer;
    size_t max;
};

static size_t snprintf_wrapper(void *closure, const char *string, ...) {
    va_list parameters;
    va_start(parameters, string);

    struct snprintf_object *cls = closure;
    size_t ret = vsnprintf(cls->buffer, cls->max, string, parameters);
    cls->max = cls->max - ret > cls->max ? 0 : cls->max - ret;
    cls->buffer += ret;

    va_end(parameters);
    return ret;
}

size_t kernel_stack_trace_for_procfs(struct task *main_task, void *buffer, size_t buffer_max) {
    // Can't stack trace the idle task
    if (main_task->tid == 1) {
        return 0;
    }

    struct snprintf_object obj = { .buffer = buffer, .max = buffer_max };
    if (main_task->kernel_task || main_task->in_kernel) {
        struct task *current = get_current_task();

        uintptr_t rip =
            current == main_task ? (uintptr_t) (&kernel_stack_trace_for_procfs) : task_get_instruction_pointer(main_task->user_task_state);

        uintptr_t current_bp = get_base_pointer();
        uintptr_t rbp = current == main_task ? current_bp : task_get_base_pointer(main_task->user_task_state);
        return do_stack_trace(rip, rbp, kernel_symbols, kernel_symbols_size, kernel_string_table, snprintf_wrapper, &obj, true);
    }

    return 0;
}
