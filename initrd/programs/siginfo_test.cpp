#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

int main() {
    auto on_int = [](int sig, siginfo_t* info, void* _context [[maybe_unused]]) {
        assert(sig == SIGINT);

#ifdef __os_2__
        ucontext_t* context = reinterpret_cast<ucontext_t*>(_context);
        fprintf(stderr, "rax: %#.16lX\n", context->uc_mcontext.__cpu_state.rax);
#endif /* __os_2__ */

        fprintf(stderr, "val: %d\n", info->si_value.sival_int);
    };

    struct sigaction act;
    act.sa_flags = SA_RESTART;
    sigemptyset(&act.sa_mask);
    act.sa_sigaction = on_int;
    sigaction(SIGINT, &act, nullptr);

    pause();

    write(2, "returned\n", 9);
    return 0;
}