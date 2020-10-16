#include <app/app.h>
#include <app/window.h>
#include <assert.h>
#include <eventloop/event.h>
#include <eventloop/selectable.h>
#include <liim/function.h>
#include <window_server/message.h>

namespace App {

static App* s_app;

void App::setup_ws_connection_notifier() {
    static SharedPtr<FdWrapper> fd_wrapper = FdWrapper::create(nullptr, ws_connection().fd());
    fd_wrapper->set_selected_events(NotifyWhen::Readable);
    fd_wrapper->on_readable = [this] {
        m_connection.read_from_server();

        UniquePtr<WindowServer::Message> message;
        while ((message = m_connection.recieve_message())) {
            process_ws_message(move(message));
        }
    };

    EventLoop::register_selectable(*fd_wrapper);
}

void App::process_ws_message(UniquePtr<WindowServer::Message> message) {
    switch (message->type) {
        case WindowServer::Message::Type::WindowClosedEventMessage: {
            auto maybe_window = Window::find_by_wid(message->data.window_closed_event_messasge.wid);
            assert(maybe_window.has_value());
            EventLoop::queue_event(maybe_window.value(), make_unique<WindowEvent>(WindowEvent::Type::Close));
            break;
        }
        case WindowServer::Message::Type::MouseEventMessage: {
            auto& mouse_event = message->data.mouse_event_message;
            auto mouse_event_type =
                m_mouse_tracker.notify_mouse_event(mouse_event.left, mouse_event.right, mouse_event.x, mouse_event.y, mouse_event.scroll);
            auto maybe_window = Window::find_by_wid(mouse_event.wid);
            assert(maybe_window.has_value());
            EventLoop::queue_event(maybe_window.value(),
                                   make_unique<MouseEvent>(mouse_event_type, m_mouse_tracker.buttons_down(), mouse_event.x, mouse_event.y,
                                                           mouse_event.scroll, mouse_event.left, mouse_event.right));
            break;
        }
        case WindowServer::Message::Type::KeyEventMessage: {
            auto& key_event = message->data.key_event_message;
            auto maybe_window = Window::find_by_wid(key_event.wid);
            assert(maybe_window.has_value());
            EventLoop::queue_event(maybe_window.value(),
                                   make_unique<KeyEvent>(key_event.event.ascii, key_event.event.key, key_event.event.flags));
            break;
        }
        case WindowServer::Message::Type::WindowDidResizeMessage: {
            auto& resize_event = message->data.window_did_resize_message;
            auto maybe_window = Window::find_by_wid(resize_event.wid);
            assert(maybe_window.has_value());
            EventLoop::queue_event(maybe_window.value(), make_unique<WindowEvent>(WindowEvent::Type::DidResize));
            break;
        }
        case WindowServer::Message::Type::WindowStateChangeMessage: {
            auto& state_event = message->data.window_state_change_message;
            auto maybe_window = Window::find_by_wid(state_event.wid);
            assert(maybe_window.has_value());
            EventLoop::queue_event(maybe_window.value(), make_unique<WindowStateEvent>(state_event.active));
            break;
        }
        default:
            break;
    }
}

App::App() {
    assert(!s_app);
    s_app = this;
    setup_ws_connection_notifier();
}

App& App::the() {
    return *s_app;
}
}
