#include <sys/iros.h>
#include <sys/syscall.h>

int create_task(struct create_task_args *create_task_args) {
    return syscall(SYS_create_task, create_task_args);
}
