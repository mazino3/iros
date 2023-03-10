#include <eventloop/event.h>
#include <eventloop/event_loop.h>
#include <eventloop/object.h>
#include <eventloop/timer.h>
#include <test/test.h>

using namespace App;

APP_EVENT(App, CustomEvent, Event, (), (), ())

APP_EVENT(App, CustomEvent2, Event, (), (), ())

APP_EVENT_REQUIRES_HANDLING(App, ConsumableEvent, Event, (), (), ())

TEST(object, events_basic) {
    auto object = Object::create(nullptr);

    int count = 0;
    object->on_unchecked<CustomEvent>({}, [&](const auto&) {
        count++;
    });

    EXPECT(!object->emit<CustomEvent>());
    EXPECT(!object->emit<CustomEvent>());
    EXPECT(!object->emit<CustomEvent>());

    EXPECT(!object->emit<CustomEvent2>());

    EXPECT_EQ(count, 3);
}

TEST(object, events_deregistration) {
    auto object = Object::create(nullptr);
    auto listener = Object::create(nullptr);

    int count = 0;
    object->on_unchecked<CustomEvent>(*listener, [&](const auto&) {
        count++;
    });

    EXPECT(!object->emit<CustomEvent>());
    EXPECT(!object->emit<CustomEvent>());

    listener = nullptr;

    EXPECT(!object->emit<CustomEvent>());

    EXPECT_EQ(count, 2);

    listener = Object::create(nullptr);

    count = 0;
    object->on_unchecked<CustomEvent>(*listener, [&](const auto&) {
        count++;
    });

    EXPECT(!object->emit<CustomEvent>());
    EXPECT(!object->emit<CustomEvent>());

    object->remove_listener(*listener);

    EXPECT(!object->emit<CustomEvent>());

    EXPECT_EQ(count, 2);

    count = 0;
    auto listener_token = object->on_unchecked<CustomEvent>(*listener, [&](auto&) {
        count++;
    });

    EXPECT(!object->emit<CustomEvent>());
    EXPECT(!object->emit<CustomEvent>());

    object->remove_listener(listener_token);

    EXPECT(!object->emit<CustomEvent>());

    EXPECT_EQ(count, 2);
}

TEST(object, event_consuming) {
    auto object = Object::create(nullptr);

    int count = 0;
    object->on_unchecked<CustomEvent>({}, [&](const auto&) {
        count++;
    });
    object->on_unchecked<CustomEvent>({}, [&](const auto&) {
        count++;
    });
    object->on_unchecked<CustomEvent>({}, [&](const auto&) {
        count++;
    });

    object->on_unchecked<ConsumableEvent>({}, [&](const auto&) {
        count++;
        return false;
    });
    object->on_unchecked<ConsumableEvent>({}, [&](const auto&) {
        count++;
        return true;
    });
    object->on_unchecked<ConsumableEvent>({}, [&](const auto&) {
        count++;
        return true;
    });

    EXPECT(!object->emit<CustomEvent>());
    EXPECT_EQ(count, 3);

    EXPECT(object->emit<ConsumableEvent>());
    EXPECT_EQ(count, 5);
}

TEST(object, event_multiple) {
    auto object = Object::create(nullptr);

    int count = 0;

    object->on_unchecked<CustomEvent, CustomEvent2>({}, [&](auto&& event) {
        using Type = LIIM::decay_t<decltype(event)>;

        if constexpr (LIIM::IsSame<CustomEvent, Type>::value) {
            EXPECT_EQ(event.name(), CustomEvent::static_event_name());
            count++;
        } else {
            EXPECT_EQ(event.name(), CustomEvent2::static_event_name());
            count += 2;
        }
    });

    EXPECT(!object->emit<CustomEvent>());
    EXPECT(!object->emit<CustomEvent2>());

    EXPECT_EQ(count, 3);
}

static Task<int> do_thing() {
    co_return 1;
}

static App::ObjectBoundCoroutine coroutine_test(int& count) {
    count = co_await do_thing();
    EventLoop::the().set_should_exit(true);
    co_return;
}

TEST(object, coroutine) {
    auto loop = App::EventLoop {};

    auto object = Object::create(nullptr);

    int count = 0;
    object->start_coroutine(coroutine_test(count));

    loop.enter();

    EXPECT_EQ(count, 1);
}

static App::ObjectBoundCoroutine block_until_event_test(App::Timer* timer, App::Object* object, int& count) {
    auto event = co_await timer->block_until_event<App::TimerEvent>(*object);
    count = event.times_expired();
    EventLoop::the().set_should_exit(true);
    co_return;
}

TEST(object, block_until_event) {
    auto loop = App::EventLoop {};

    auto object = Object::create(nullptr);
    auto timer = Timer::create_single_shot_timer(nullptr, 100);

    int count = 0;

    object->start_coroutine(block_until_event_test(timer.get(), object.get(), count));

    loop.enter();

    EXPECT_EQ(count, 1);
}

TEST(object, intercept) {
    auto loop = App::EventLoop {};

    int value = 0;

    auto object = Object::create(nullptr);
    object->intercept_unchecked<CustomEvent>({}, [&](auto&) {
        value = 1;
    });
    object->intercept_unchecked<CustomEvent>({}, [&](auto&) {
        value = 2;
    });
    object->intercept_unchecked<CustomEvent>({}, [&](auto&) {
        value = 3;
    });

    EXPECT(!object->emit<CustomEvent>());
    EXPECT_EQ(value, 1);
}
