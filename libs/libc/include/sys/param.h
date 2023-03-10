#ifndef _SYS_PARAM_H
#define _SYS_PARAM_H 1

#include <signal.h>

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

#define ALIGN_UP(size, align) \
    ((__typeof__(size)) ((size) == 0 ? 0 : ((align) == 0 ? (size) : ((((size) + (align) -1) / (align)) * (align)))))
#define ALIGN_DOWN(size, align) ((align) == 0 ? 0 : (((size) / (align)) * (align)))

#endif /* _SYS_PARAM_H */
