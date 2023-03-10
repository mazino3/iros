#include <assert.h>
#include <errno.h>
#include <eventloop/event_loop.h>
#include <graphics/bitmap.h>
#include <graphics/renderer.h>
#include <liim/pointers.h>
#include <stdio.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/umessage.h>
#include <sys/un.h>
#include <unistd.h>
#include <window_server/message.h>

#include "server.h"
#include "window.h"
#include "window_manager.h"

namespace WindowServer {

constexpr time_t draw_timer_rate = 1000 / 60;

ServerImpl::ServerImpl(int fb, SharedPtr<Bitmap> front_buffer, SharedPtr<Bitmap> back_buffer)
    : m_manager(make_unique<WindowManager>(fb, front_buffer, back_buffer)) {
    m_manager->on_window_close_button_pressed = [this](auto window) {
        m_manager->remove_window(window);

        send<Server::WindowClosedEventMessage>(window->client(), { .wid = window->id() });
    };

    m_manager->on_window_resize_start = [this](auto window) {
        send<Server::WindowDidResizeMessage>(window->client(), { .wid = window->id() });
    };

    m_manager->on_window_state_change = [this](auto window, bool active) {
        send<Server::WindowStateChangeMessage>(window->client(), { .wid = window->id(), .active = active });
        if (active) {
            notify_listeners_did_make_window_active(*window);
        }
    };

    m_manager->on_window_removed = [this](auto wid) {
        notify_listeners_did_close_window(wid);
    };

    m_manager->on_rect_invaliadted = [this] {
        update_draw_timer();
    };

    m_manager->on_palette_changed = [this] {
        broadcast<Server::ThemeChangeMessage>(*m_server, {});
    };
}

void ServerImpl::initialize() {
    m_input_socket = App::FdWrapper::create(this, socket(AF_UMESSAGE, SOCK_DGRAM | SOCK_NONBLOCK, UMESSAGE_INPUT));
    assert(m_input_socket->valid());
    m_input_socket->set_selected_events(App::NotifyWhen::Readable);
    m_input_socket->enable_notifications();
    m_input_socket->on<App::ReadableEvent>(*this, [this](auto&) {
        char buffer[400];
        ssize_t ret;
        while ((ret = read(m_input_socket->fd(), buffer, sizeof(buffer))) > 0) {
            auto* message = (umessage*) buffer;
            switch (message->type) {
                case UMESSAGE_INPUT_KEY_EVENT: {
                    auto& event = ((umessage_input_key_event*) message)->event;
                    auto* active_window = m_manager->active_window();
                    if (active_window) {
                        auto key_event = m_input_tracker.notify_os_key_event(event.ascii, event.key, event.flags);
                        if (!key_event->generates_text()) {
                            send<Server::KeyEventMessage>(active_window->client(), {
                                                                                       .wid = active_window->id(),
                                                                                       .key = static_cast<int>(key_event->key()),
                                                                                       .modifiers = key_event->modifiers(),
                                                                                       .key_down = key_event->key_down(),
                                                                                       .generates_text = key_event->generates_text(),
                                                                                   });
                        } else if (event.ascii) {
                            send<Server::TextEventMessage>(active_window->client(), {
                                                                                        .wid = active_window->id(),
                                                                                        .text = String { event.ascii },
                                                                                    });
                        }
                    }
                    break;
                }
                case UMESSAGE_INPUT_MOUSE_EVENT: {
                    auto& event = ((umessage_input_mouse_event*) message)->event;
                    auto events =
                        m_input_tracker.notify_os_mouse_event(event.scale_mode, event.dx, event.dy, event.dz, event.buttons,
                                                              m_manager->screen_rect().width(), m_manager->screen_rect().height());
                    for (auto& mouse_event : events) {
                        m_manager->notify_mouse_input(*mouse_event);
                    }

                    auto* active_window = m_manager->active_window();
                    if (active_window && m_manager->should_send_mouse_events(*active_window)) {
                        Point relative_mouse = m_manager->mouse_position_relative_to_window(*active_window);
                        send<Server::MouseEventMessage>(active_window->client(), {
                                                                                     .wid = active_window->id(),
                                                                                     .x = relative_mouse.x(),
                                                                                     .y = relative_mouse.y(),
                                                                                     .z = event.dz,
                                                                                     .buttons = event.buttons,
                                                                                     .modifiers = 0,
                                                                                 });
                    }
                    break;
                }
            }
        }
    });

    m_server = IPC::Server::create(this, "/tmp/.window_server.socket", shared_from_this());

    m_draw_timer = App::Timer::create_single_shot_timer(this, draw_timer_rate);
    m_draw_timer->on<App::TimerEvent>(*this, [this](auto&) {
        m_manager->draw();
    });

    Client::MessageDispatcher::initialize();
}

void ServerImpl::update_draw_timer() {
    if (!m_manager->drawing() && m_draw_timer->expired()) {
        m_draw_timer->set_timeout(draw_timer_rate);
    }
}

void ServerImpl::kill_client(IPC::Endpoint& client) {
    m_manager->remove_windows_of_client(client.shared_from_this());
    m_window_server_listeners.remove(&client);
    m_server->kill_client(client);
}

void ServerImpl::handle(IPC::Endpoint& client, const Client::CreateWindowRequest& data) {
    SharedPtr<Window> parent;
    if (data.parent_id) {
        parent = m_manager->find_by_wid(data.parent_id);
        if (!parent) {
            kill_client(client);
            return;
        }
    }

    Rect rect(data.x, data.y, data.width, data.height);
    if (parent) {
        rect.set_x(rect.x() + parent->content_rect().x());
        rect.set_y(rect.y() + parent->content_rect().y());
    }

    if (data.type == WindowServer::WindowType::Taskbar) {
        rect.set_x(0);
        rect.set_y(m_manager->screen_rect().height() - data.height);
        rect.set_width(m_manager->screen_rect().width());
    }

    while (m_manager->window_exactly_at(rect.top_left())) {
        rect.set_x(rect.x() + 64);
        rect.set_y(rect.y() + 64);
    }

    auto window = make_shared<Window>(rect, String(data.name), client, data.type, data.has_alpha);
    if (parent) {
        Window::set_parent(window, parent);
    }

    send<Server::CreateWindowResponse>(client, {
                                                   .wid = window->id(),
                                                   .size = (size_t) window->buffer()->size_in_bytes(),
                                                   .path = window->shm_path(),
                                                   .width = rect.width(),
                                                   .height = rect.height(),
                                               });

    notify_listeners_did_create_window(*window);

    m_manager->add_window(window);
}

void ServerImpl::handle(IPC::Endpoint& client, const Client::RemoveWindowRequest& data) {
    wid_t wid = data.wid;
    auto window = m_manager->find_by_wid(wid);
    if (!window) {
        kill_client(client);
        return;
    }

    m_manager->remove_window(window);

    send<Server::RemoveWindowResponse>(client, { .success = true });
}

void ServerImpl::handle(IPC::Endpoint& client, const Client::ChangeWindowVisibilityRequest& data) {
    wid_t wid = data.wid;
    auto window = m_manager->find_by_wid(wid);
    if (!window) {
        kill_client(client);
        return;
    }

    bool visible = data.visible;
    if (visible) {
        window->set_position_relative_to_parent(data.x, data.y);
    }
    m_manager->set_window_visibility(window, visible);

    send<Server::ChangeWindowVisibilityResponse>(client, { .wid = wid, .success = true });
}

void ServerImpl::handle(IPC::Endpoint& client, const Client::SwapBufferRequest& data) {
    auto window = m_manager->find_by_wid(data.wid);
    if (!window) {
        kill_client(client);
        return;
    }

    window->swap();
}

void ServerImpl::handle(IPC::Endpoint& client, const Client::WindowReadyToResizeMessage& data) {
    wid_t wid = data.wid;
    auto window_ptr = m_manager->find_by_wid(wid);
    if (!window_ptr) {
        kill_client(client);
        return;
    }

    auto& window = *window_ptr;
    if (!window.in_resize()) {
        kill_client(client);
        return;
    }

    auto dw = window.resize_rect().width() - window.rect().width();
    auto dh = window.resize_rect().height() - window.rect().height();
    window.relative_resize(dw, dh);
    window.set_position(window.resize_rect().x(), window.resize_rect().y());
    window.set_in_resize(false);

    send<Server::WindowReadyToResizeResponse>(client, {
                                                          .wid = wid,
                                                          .new_width = window.content_rect().width(),
                                                          .new_height = window.content_rect().height(),
                                                      });
}

void ServerImpl::handle(IPC::Endpoint& client, const Client::WindowRenameRequest& data) {
    auto window = m_manager->find_by_wid(data.wid);
    if (!window) {
        kill_client(client);
        return;
    }
    window->set_title(String(data.name));
    notify_listeners_did_change_window_title(*window);
}

void ServerImpl::handle(IPC::Endpoint& client, const Client::ChangeThemeRequest& data) {
    bool result = m_manager->load_palette(data.path);
    send<Server::ChangeThemeResponse>(client, { .success = result });
}

void ServerImpl::handle(IPC::Endpoint& client, const Client::RegisterAsWindowServerListener&) {
    m_window_server_listeners.put(&client);
}

void ServerImpl::handle(IPC::Endpoint& client, const Client::UnregisterAsWindowServerListener&) {
    m_window_server_listeners.remove(&client);
}

void ServerImpl::handle(IPC::Endpoint& client, const Client::SetActiveWindow& data) {
    if (!m_window_server_listeners.get(&client)) {
        kill_client(client);
        return;
    }

    auto window = m_manager->find_by_wid(data.wid);
    if (!window) {
        return;
    }

    m_manager->move_to_front_and_make_active(move(window));
}

void ServerImpl::notify_listeners_did_create_window(const Window& window) {
    auto message = Server::ServerDidCreatedWindow {
        .wid = window.id(),
        .title = window.title(),
        .type = window.type(),
    };

    m_window_server_listeners.for_each([&](IPC::Endpoint* client) {
        client->send(message);
    });
}

void ServerImpl::notify_listeners_did_change_window_title(const Window& window) {
    auto message = Server::ServerDidChangeWindowTitle {
        .wid = window.id(),
        .new_title = window.title(),
    };

    m_window_server_listeners.for_each([&](IPC::Endpoint* client) {
        client->send(message);
    });
}

void ServerImpl::notify_listeners_did_close_window(wid_t wid) {
    auto message = Server::ServerDidCloseWindow {
        .wid = wid,
    };

    m_window_server_listeners.for_each([&](IPC::Endpoint* client) {
        client->send(message);
    });
}

void ServerImpl::notify_listeners_did_make_window_active(const Window& window) {
    auto message = Server::ServerDidMakeWindowActive {
        .wid = window.id(),
    };

    m_window_server_listeners.for_each([&](IPC::Endpoint* client) {
        client->send(message);
    });
}

void ServerImpl::start() {
    char* synchronize = getenv("__SYNCHRONIZE");
    if (synchronize) {
        int fd = atoi(synchronize);
        char c = '\0';
        write(fd, &c, 1);
        close(fd);
    }

    m_manager->invalidate_rect(m_manager->screen_rect());

    App::EventLoop loop;
    loop.enter();
}

ServerImpl::~ServerImpl() {
    if (m_input_socket && m_input_socket->fd() >= 0) {
        close(m_input_socket->fd());
    }
}

}
