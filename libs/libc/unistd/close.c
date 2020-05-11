#include <errno.h>
#include <sys/syscall.h>
#include <unistd.h>

int close(int fd) {
    int ret = (int) syscall(SC_CLOSE, fd);
    __SYSCALL_TO_ERRNO(ret);
}
