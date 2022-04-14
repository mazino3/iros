#include <errno.h>
#include <sys/iros.h>
#include <time.h>

int clock_getcpuclockid(pid_t pid, clockid_t *id) {
    int ret = getcpuclockid(pid, 0, id);
    __SYSCALL_TO_ERRNO(ret);
}
