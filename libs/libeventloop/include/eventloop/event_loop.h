#pragma once

#include <eventloop/forward.h>
#include <liim/function.h>
#include <liim/pointers.h>
#include <liim/vector.h>
#include <pthread.h>
#include <time.h>

namespace App {
class EventLoop {
public:
    EventLoop();
    ~EventLoop();

    EventLoop(const EventLoop&) = delete;

    static EventLoop& the();
    static void register_selectable(Selectable& selectable);
    static void unregister_selectable(Selectable& selectable);
    static void register_signal_handler(int signum, Function<void()> callback);
    static void unregister_signal_handler(int signum);
    static void register_timer_callback(timer_t id, WeakPtr<Object> target);
    static void unregister_timer_callback(timer_t id);
    static void queue_event(WeakPtr<Object> target, UniquePtr<Event> event);

    static void setup_timer_sigevent(sigevent& ev, timer_t* id);

    void enter();
    void set_should_exit(bool b) { m_should_exit = b; }

    void pump();

private:
    void do_queue_event(WeakPtr<Object> target, UniquePtr<Event> event);

    void do_select(bool block);
    void do_event_dispatch();
    void setup_signal_handlers();

    struct QueuedEvent {
        WeakPtr<Object> target;
        UniquePtr<Event> event;
    };

    Vector<QueuedEvent> m_events;
    pthread_mutex_t m_lock;
    pthread_t m_waiting_thread { 0 };
    bool m_should_exit { false };
};
}
