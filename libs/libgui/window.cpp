#include <eventloop/event.h>
#include <gui/application.h>
#include <gui/context_menu.h>
#include <gui/widget.h>
#include <gui/window.h>
#include <pthread.h>

namespace GUI {
static HashMap<wid_t, Window*> s_windows;
static pthread_mutex_t s_windows_lock = PTHREAD_MUTEX_INITIALIZER;

void Window::for_each_window(Function<void(Window&)> callback) {
    pthread_mutex_lock(&s_windows_lock);
    s_windows.for_each([&](auto* window) {
        callback(*window);
    });
    pthread_mutex_unlock(&s_windows_lock);
}

void Window::register_window(Window& window) {
    pthread_mutex_lock(&s_windows_lock);
    s_windows.put(window.wid(), &window);
    pthread_mutex_unlock(&s_windows_lock);
}

void Window::unregister_window(wid_t wid) {
    pthread_mutex_lock(&s_windows_lock);
    s_windows.remove(wid);
    pthread_mutex_unlock(&s_windows_lock);
}

Option<SharedPtr<Window>> Window::find_by_wid(wid_t wid) {
    pthread_mutex_lock(&s_windows_lock);
    auto result = s_windows.get(wid);
    pthread_mutex_unlock(&s_windows_lock);

    if (!result) {
        return {};
    }
    return { (*result)->shared_from_this() };
}

Window::~Window() {
    unregister_window(wid());
}

Window::Window(int x, int y, int width, int height, String name, bool has_alpha, WindowServer::WindowType type, wid_t parent_id)
    : m_parent_wid(parent_id), m_visible(type != WindowServer::WindowType::Frameless), m_has_alpha(has_alpha) {
    m_platform_window = Application::the().create_window(*this, x, y, width, height, move(name), has_alpha, type, parent_id);
    register_window(*this);
}

void Window::initialize() {
    on<App::WindowCloseEvent>([this](auto&) {
        m_removed = true;
        Application::the().main_event_loop().set_should_exit(true);
    });

    on<App::WindowDidResizeEvent>([this](auto&) {
        m_platform_window->did_resize();
        if (auto* main_widget = &this->main_widget()) {
            main_widget->set_positioned_rect(rect());
        }
        pixels()->clear(Application::the().palette()->color(Palette::Background));
        invalidate_rect(rect());
    });

    on<App::WindowForceRedrawEvent>([this](auto&) {
        invalidate_rect(rect());
    });

    on<App::WindowStateEvent>([this](const App::WindowStateEvent& event) {
        auto& state_event = static_cast<const App::WindowStateEvent&>(event);
        if (state_event.active() == active()) {
            return;
        }

        if (!state_event.active()) {
            did_become_inactive();
        } else {
            did_become_active();
        }
        m_active = state_event.active();
    });

    on_unchecked<App::ThemeChangeEvent>([this](const App::ThemeChangeEvent&) {
        pixels()->clear(Application::the().palette()->color(Palette::Background));
        invalidate_rect(rect());
    });

    App::Window::initialize();
}

void Window::hide() {
    if (!m_visible) {
        return;
    }

    m_visible = false;
    m_platform_window->do_set_visibility(0, 0, false);
}

void Window::show(int x, int y) {
    if (m_visible) {
        return;
    }

    m_visible = true;
    m_platform_window->do_set_visibility(x, y, true);
}

void Window::hide_current_context_menu() {
    auto maybe_context_menu = m_current_context_menu.lock();
    if (maybe_context_menu) {
        maybe_context_menu->hide();
    }
}

void Window::clear_current_context_menu() {
    m_current_context_menu.reset();
}

void Window::set_current_context_menu(ContextMenu* menu) {
    m_current_context_menu = menu->weak_from_this();
}

void Window::schedule_render() {
    deferred_invoke_batched(m_render_scheduled, [this] {
        flush_layout();
        do_render();
    });
}

void Window::do_render() {
    if (!main_widget().hidden()) {
        main_widget().render_including_children();
        m_platform_window->flush_pixels();
        clear_dirty_rects();
    }
}
}
