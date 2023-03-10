#pragma once

#include <di/sync/atomic.h>
#include <di/sync/concepts/lock.h>

namespace di::sync {
class DumbSpinlock {
public:
    DumbSpinlock() = default;

    DumbSpinlock(DumbSpinlock const&) = delete;
    DumbSpinlock& operator=(DumbSpinlock const&) = delete;

    void lock() {
        for (;;) {
            if (try_lock()) {
                return;
            }
            while (m_state.load(MemoryOrder::Relaxed)) {
#ifdef __x86_64__
                asm volatile("pause" ::: "memory");
#endif
            }
        }
    }
    bool try_lock() { return !m_state.exchange(true, MemoryOrder::Acquire); }
    void unlock() { m_state.store(false, MemoryOrder::Release); }

private:
    Atomic<bool> m_state;
};

static_assert(concepts::Lock<DumbSpinlock>);
}