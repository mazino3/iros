#include <sys/os_2.h>
#include <sys/syscall.h>

int getcpuclockid(int tgid, int tid, clockid_t *clock_id) {
    return (int) syscall(SYS_getcpuclockid, tgid, tid, clock_id);
}
