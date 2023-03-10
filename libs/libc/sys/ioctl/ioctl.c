#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>

int ioctl(int fd, unsigned long request, ...) {
    va_list parameters;
    va_start(parameters, request);
    void *argp;
    switch (request) {
        case TISATTY:
        case TIOCNOTTY:
        case TIOCSCTTY:
        case TCIOFFI:
        case TCOOFFI:
        case TCIONI:
        case TCOONI:
        case SECURSOR:
        case SDCURSOR:
        case FIOCLEX:
        case FIONCLEX:
            argp = NULL;
            break;
        default:
            argp = va_arg(parameters, void *);
            break;
    }

    int ret = (int) syscall(SYS_ioctl, fd, request, argp);
    va_end(parameters);
    __SYSCALL_TO_ERRNO(ret);
}
