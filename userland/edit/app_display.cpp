#include <app/flex_layout_engine.h>
#include <clipboard/connection.h>
#include <edit/document.h>
#include <edit/keyboard_action.h>
#include <edit/line_renderer.h>
#include <eventloop/event.h>
#include <graphics/bitmap.h>
#include <graphics/renderer.h>
#include <gui/context_menu.h>
#include <gui/text_label.h>
#include <gui/window.h>

#include "app_display.h"

SearchWidget::SearchWidget() {}

SearchWidget::~SearchWidget() {}

void SearchWidget::render() {
    Widget::render();

    auto renderer = get_renderer();
    renderer.draw_rect(sized_rect(), ColorValue::White);
}

AppDisplay& SearchWidget::display() {
    if (!m_display) {
        auto& layout = set_layout_engine<App::HorizontalFlexLayoutEngine>();
        auto& label = layout.add<GUI::TextLabel>("Find:");
        label.set_layout_constraint({ 46, App::LayoutConstraint::AutoSize });

        m_display = layout.add_owned<AppDisplay>(false);
    }
    return *m_display;
}

AppDisplay::AppDisplay(bool main_display) : m_main_display(main_display) {}

void AppDisplay::did_attach() {
    set_key_bindings(Edit::get_key_bindings(base()));

    auto window = this->parent_window()->shared_from_this();
    auto context_menu = GUI::ContextMenu::create(window.get(), window);
    context_menu->add_menu_item("Copy", [this] {
        if (auto* doc = document()) {
            doc->copy(base(), cursors());
        }
    });
    context_menu->add_menu_item("Cut", [this] {
        if (auto* doc = document()) {
            doc->cut(base(), cursors());
        }
    });
    context_menu->add_menu_item("Paste", [this] {
        if (auto* doc = document()) {
            doc->paste(base(), cursors());
        }
    });
    set_context_menu(context_menu);

    on<App::ResizeEvent>([this](const App::ResizeEvent&) {
        m_rows = positioned_rect().height() / row_height();
        m_cols = positioned_rect().width() / col_width();

        if (m_main_display) {
            ensure_search_display();
            constexpr int display_height = 28;
            m_search_widget->set_positioned_rect({ positioned_rect().x(),
                                                   positioned_rect().y() + positioned_rect().height() - display_height,
                                                   positioned_rect().width(), display_height });
        }
    });

    on<App::FocusedEvent>([this](const App::FocusedEvent&) {
        invalidate();
    });

    Widget::did_attach();
}

AppDisplay::~AppDisplay() {}

AppDisplay& AppDisplay::ensure_search_display() {
    assert(m_main_display);
    if (!m_search_widget) {
        m_search_widget = create_widget_owned<SearchWidget>();
        m_search_widget->set_hidden(true);
    }
    return m_search_widget->display();
}

Edit::RenderedLine AppDisplay::compose_line(const Edit::Line& line) {
    auto renderer = Edit::LineRenderer { cols(), word_wrap_enabled() };
    for (int index_into_line = 0; index_into_line <= line.length(); index_into_line++) {
        if (cursors().should_show_auto_complete_text_at(*document(), line, index_into_line)) {
            auto maybe_suggestion_text = cursors().preview_auto_complete_text();
            if (maybe_suggestion_text) {
                renderer.begin_segment(index_into_line, Edit::CharacterMetadata::Flags::AutoCompletePreview,
                                       Edit::PositionRangeType::InlineAfterCursor);
                renderer.add_to_segment(maybe_suggestion_text->view(), maybe_suggestion_text->size());
                renderer.end_segment();
            }
        }

        if (index_into_line == line.length()) {
            break;
        }

        renderer.begin_segment(index_into_line, 0, Edit::PositionRangeType::Normal);
        char c = line.char_at(index_into_line);
        if (c == '\t') {
            auto spaces = String::repeat(' ', Edit::tab_width - (renderer.absolute_col_position() % Edit::tab_width));
            renderer.add_to_segment(spaces.view(), spaces.size());
        } else {
            renderer.add_to_segment(StringView { &c, 1 }, 1);
        }
        renderer.end_segment();
    }
    return renderer.finish(line, {});
}

Edit::TextIndex AppDisplay::text_index_at_mouse_position(const Point& point) {
    return text_index_at_display_position({ point.y() / row_height(), point.x() / col_width() });
}

void AppDisplay::output_line(int, int, const Edit::RenderedLine&, int) {}

int AppDisplay::enter() {
    this->make_focused();
    return 0;
}

App::ObjectBoundCoroutine AppDisplay::quit() {
    if (on_quit) {
        on_quit();
    }
    co_return;
}

App::ObjectBoundCoroutine AppDisplay::do_open_prompt() {
    co_return;
}

void AppDisplay::send_status_message(String) {}

Task<Option<String>> AppDisplay::prompt(String, String) {
    co_return {};
}

void AppDisplay::enter_search(String starting_text) {
    if (!m_main_display) {
        return;
    }

    ensure_search_display().set_document(Edit::Document::create_from_text(move(starting_text)));

    auto& search_document = *ensure_search_display().document();
    search_document.set_submittable(true);
    listen<Edit::Change>(search_document, [this](auto&) {
        auto contents = ensure_search_display().document()->content_string();
        set_search_text(move(contents));
    });
    listen<Edit::Submit>(search_document, [this](auto&) {
        cursors().remove_secondary_cursors();
        move_cursor_to_next_search_match();
    });
    ensure_search_display().on_quit = [this] {
        set_search_text("");
        this->make_focused();
    };

    m_search_widget->set_hidden(false);
    ensure_search_display().make_focused();
}

void AppDisplay::set_clipboard_contents(String text, bool is_whole_line) {
    m_prev_clipboard_contents = move(text);
    m_prev_clipboard_contents_were_whole_line = is_whole_line;
    Clipboard::Connection::the().set_clipboard_contents_to_text(m_prev_clipboard_contents);
}

String AppDisplay::clipboard_contents(bool& is_whole_line) const {
    auto contents = Clipboard::Connection::the().get_clipboard_contents_as_text();
    if (!contents.has_value()) {
        is_whole_line = m_prev_clipboard_contents_were_whole_line;
        return m_prev_clipboard_contents;
    }

    auto& ret = contents.value();
    if (ret == m_prev_clipboard_contents) {
        is_whole_line = m_prev_clipboard_contents_were_whole_line;
    } else {
        m_prev_clipboard_contents = "";
        is_whole_line = m_prev_clipboard_contents_were_whole_line = false;
    }
    return move(ret);
}

void AppDisplay::render_cursor(Renderer& renderer) {
    if (focused()) {
        return;
    }

    auto cursor_pos = display_position_of_index(main_cursor().index());

    int cursor_x = cursor_pos.col() * col_width();
    int cursor_y = cursor_pos.row() * row_height();
    for (int y = cursor_y; y < cursor_y + row_height(); y++) {
        renderer.pixels().put_pixel(cursor_x, y, ColorValue::White);
    }

    m_last_drawn_cursor_col = cursor_pos.col();
    m_last_drawn_cursor_row = cursor_pos.row();
}

void AppDisplay::render_cell(Renderer& renderer, int x, int y, char c, Edit::CharacterMetadata metadata) {
    auto info = rendering_info_for_metadata(metadata);

    Color fg = info.fg.has_value() ? info.fg.value() : Color(VGA_COLOR_LIGHT_GREY);
    Color bg = info.bg.has_value() ? info.bg.value() : ColorValue::Black;

    auto cell_rect = Rect { x, y, col_width(), row_height() };
    renderer.fill_rect(cell_rect, bg);
    renderer.render_text(String(c), cell_rect, fg, TextAlign::Center, info.bold ? *Font::bold_font() : *Font::default_font());
}

void AppDisplay::render() {
    auto renderer = get_renderer();

    auto total_width = cols() * col_width();
    auto total_height = rows() * row_height();
    Rect bottom_extra_rect = { 0, total_height, total_width, sized_rect().height() - total_height };
    Rect right_extra_rect = { total_width, 0, sized_rect().width() - total_width, sized_rect().height() };
    renderer.fill_rect(bottom_extra_rect, ColorValue::Black);
    renderer.fill_rect(right_extra_rect, ColorValue::Black);

    renderer.fill_rect(sized_rect(), ColorValue::Black);

    render_lines();

    render_cursor(renderer);
    GUI::Widget::render();
}
