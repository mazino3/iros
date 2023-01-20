#include <di/prelude.h>
#include <iris/boot/cxx_init.h>
#include <iris/core/log.h>

extern "C" {
typedef void (*init_function_t)(void);

extern init_function_t __iris_init_array_start[];
extern init_function_t __iris_init_array_end[];
}

namespace iris::arch {
void cxx_init() {
    ssize_t init_size = __iris_init_array_end - __iris_init_array_start;
    for (ssize_t i = 0; i < init_size; i++) {
        iris::debug_log("exec: {}"_sv, __iris_init_array_start[i]);
        (*__iris_init_array_start[i])();
    }
}
}