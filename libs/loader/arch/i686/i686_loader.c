#include "../../loader.h"

__attribute__((noreturn)) void loader_exec(struct initial_process_info *info, int argc, char **argv, char **envp) {
#ifdef LOADER_DEBUG
    loader_log("starting program: %p", (void *) info->program_entry);
#endif /* LOADER_DEBUG */
    ran_program = 1;
    asm volatile("and $(~15), %%esp\n"
                 "push %3\n"
                 "push %2\n"
                 "push %1\n"
                 "push %0\n"
                 "call *%4\n"
                 :
                 : "r"(info), "r"(argc), "r"(argv), "r"(envp), "r"(info->program_entry)
                 : "memory");
    _exit(99);
}
