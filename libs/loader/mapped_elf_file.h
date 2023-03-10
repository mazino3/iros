#ifndef _MAPPED_ELF_FILE_H
#define _MAPPED_ELF_FILE_H 1

#include <bits/mapped_elf_file.h>
#include <elf.h>

#include "loader.h"

struct dynamic_elf_object;

struct mapped_elf_file build_mapped_elf_file(const char *file) LOADER_PRIVATE;
void destroy_mapped_elf_file(struct mapped_elf_file *self) LOADER_PRIVATE;
const ElfW(Ehdr) * elf_header(const struct mapped_elf_file *self) LOADER_PRIVATE;
const ElfW(Shdr) * section_table(const struct mapped_elf_file *self) LOADER_PRIVATE;
const ElfW(Shdr) * section_at(const struct mapped_elf_file *self, size_t index) LOADER_PRIVATE;
size_t section_count(const struct mapped_elf_file *self) LOADER_PRIVATE;
const ElfW(Phdr) * program_header_table(const struct mapped_elf_file *self) LOADER_PRIVATE;
const ElfW(Phdr) * program_header_at(const struct mapped_elf_file *self, size_t index) LOADER_PRIVATE;
size_t program_header_count(const struct mapped_elf_file *self) LOADER_PRIVATE;
const char *section_strings(const struct mapped_elf_file *self) LOADER_PRIVATE;
const char *section_string(const struct mapped_elf_file *self, size_t index) LOADER_PRIVATE;
const char *strings(const struct mapped_elf_file *self) LOADER_PRIVATE;
const char *string(const struct mapped_elf_file *self, size_t index) LOADER_PRIVATE;
const ElfW(Phdr) * dynamic_program_header(const struct mapped_elf_file *self) LOADER_PRIVATE;
uintptr_t dynamic_table_offset(const struct mapped_elf_file *self) LOADER_PRIVATE;
size_t dynamic_count(const struct mapped_elf_file *self) LOADER_PRIVATE;
struct dynamic_elf_object *load_mapped_elf_file(struct mapped_elf_file *file, const char *full_path, bool global,
                                                bool use_initial_tls) LOADER_PRIVATE;

#endif /* _MAPPED_ELF_FILE_H */
