#include <clipboard/connection.h>
#include <edit/document.h>
#include <edit/document_type.h>
#include <edit/line_renderer.h>
#include <edit/position.h>
#include <eventloop/event.h>
#include <graphics/point.h>
#include <repl/repl_base.h>
#include <stdlib.h>
#include <tinput/io_terminal.h>
#include <tinput/terminal_renderer.h>
#include <tui/application.h>

#include "repl_display.h"

namespace Repl {
ReplDisplay::ReplDisplay(ReplBase& repl) : m_repl(repl) {
    m_main_prompt = repl.get_main_prompt();
    m_secondary_prompt = repl.get_secondary_prompt();

    m_history_index = m_repl.history().size();
}

ReplDisplay::~ReplDisplay() {}

void ReplDisplay::document_did_change() {
    if (document()) {
        document()->on_submit = [this] {
            auto input_text = document()->content_string();
            auto input_status = m_repl.get_input_status(input_text);

            if (input_status == Repl::InputStatus::Finished) {
                cursors().remove_secondary_cursors();
                document()->move_cursor_to_document_end(*this, cursors().main_cursor());
                document()->set_preview_auto_complete(false);
                invalidate();
                quit();
                deferred_invoke([] {
                    printf("\r\n");
                });
                return;
            }

            document()->insert_line(Edit::Line(""), document()->num_lines());
            cursors().remove_secondary_cursors();
            document()->move_cursor_to_document_end(*this, cursors().main_cursor());
            document()->scroll_cursor_into_view(*this, cursors().main_cursor());
        };

        notify_line_count_changed();
        schedule_update();
    }
}

void ReplDisplay::quit() {
    TUI::Application::the().event_loop().set_should_exit(true);
}

int ReplDisplay::enter() {
    TUI::Application::the().set_active_panel(this);
    return 0;
}

Maybe<Point> ReplDisplay::cursor_position() {
    if (!document()) {
        return {};
    }

    auto position = document()->cursor_position_on_display(*this, cursors().main_cursor());
    return Point { position.col, position.row };
}

void ReplDisplay::render() {
    if (!document()) {
        return;
    }

    if (positioned_rect().top() != 0 && document()->num_rendered_lines(*this) > sized_rect().height()) {
        auto terminal_rect = TUI::Application::the().io_terminal().terminal_rect();
        auto new_height = min(document()->num_rendered_lines(*this), terminal_rect.height());

        auto delta_height = new_height - sized_rect().height();
        set_positioned_rect({ 0, terminal_rect.height() - new_height, sized_rect().width(), new_height });

        TUI::Application::the().io_terminal().scroll_up(delta_height);
        scroll_up(delta_height);
    }

    document()->display(*this);

    auto empty_rows = scroll_row_offset() + rows() - document()->num_rendered_lines(*this);
    auto renderer = get_renderer();
    renderer.clear_rect({ 0, rows() - empty_rows, sized_rect().width(), empty_rows });
}

void ReplDisplay::on_resize() {
    if (document()) {
        document()->notify_display_size_changed();
    }

    return Panel::on_resize();
}

void ReplDisplay::on_key_event(const App::KeyEvent& event) {
    if (!document()) {
        return;
    }

    if (event.key() == App::Key::C && event.control_down()) {
        deferred_invoke([] {
            printf("^C\r\n");
        });
        set_quit_by_interrupt();
        quit();
        return;
    }

    if (event.key() == App::Key::D && event.control_down()) {
        if (document()->num_lines() == 1 && document()->content_string().empty()) {
            set_quit_by_eof();
            quit();
        }
        return;
    }

    if (event.key() == App::Key::UpArrow && cursors().main_cursor().line_index() == 0) {
        move_history_up();
        return;
    }

    if (event.key() == App::Key::DownArrow && cursors().main_cursor().line_index() == document()->num_lines() - 1) {
        move_history_down();
        return;
    }

    if (event.key() != App::Key::Tab) {
        m_consecutive_tabs = 0;
    }

    document()->notify_key_pressed(*this, event);
}

void ReplDisplay::on_mouse_event(const App::MouseEvent& event) {
    if (!document()) {
        return;
    }

    if (document()->notify_mouse_event(*this, event)) {
        return;
    }

    return Panel::on_mouse_event(event);
}

static int string_print_width(const StringView& string) {
    // NOTE: naively handle TTY escape sequences as matching the regex \033(.*)[:alpha:]
    //       also UTF-8 characters are ignored.

    int count = 0;
    for (size_t i = 0; i < string.size(); i++) {
        if (string[i] != '\033') {
            count++;
            continue;
        }

        for (i = i + 1; i < string.size(); i++) {
            if (isalpha(string[i])) {
                break;
            }
        }
    }

    return count;
}

Edit::RenderedLine ReplDisplay::compose_line(const Edit::Line& line) const {
    if (!document()) {
        return {};
    }

    auto renderer = Edit::LineRenderer { cols(), document()->word_wrap_enabled() };
    auto& prompt = &line == &document()->first_line() ? m_main_prompt : m_secondary_prompt;
    renderer.begin_segment(0, 0, Edit::PositionRangeType::InlineBeforeCursor);
    renderer.add_to_segment(prompt.view(), string_print_width(prompt.view()));
    renderer.end_segment();

    for (int index_into_line = 0; index_into_line <= line.length(); index_into_line++) {
        if (cursors().should_show_auto_complete_text_at(*document(), line, index_into_line)) {
            auto maybe_suggestion_text = cursors().preview_auto_complete_text(*this);
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
    return renderer.finish(line);
}

void ReplDisplay::send_status_message(String) {}

void ReplDisplay::output_line(int row, int col_offset, const StringView& text, const Vector<Edit::CharacterMetadata>& metadata) {
    auto renderer = get_renderer();

    auto visible_line_rect = Rect { 0, row, sized_rect().width(), 1 };
    renderer.set_clip_rect(visible_line_rect);

    // FIXME: this computation is more complicated.
    auto text_width = text.size();

    auto text_rect = visible_line_rect.translated({ -col_offset, 0 }).with_width(text_width);
    renderer.render_complex_styled_text(text_rect, text, [&](size_t index) -> TInput::TerminalTextStyle {
        auto rendering_info = rendering_info_for_metadata(metadata[index]);
        return TInput::TerminalTextStyle {
            .foreground = rendering_info.fg.map([](vga_color color) {
                return Color { color };
            }),
            .background = rendering_info.bg.map([](vga_color color) {
                return Color { color };
            }),
            .bold = rendering_info.bold,
            .invert = rendering_info.secondary_cursor,
        };
    });

    auto clear_rect = Rect { text_rect.right(), row, max(visible_line_rect.right() - text_rect.right(), 0), 1 };
    renderer.clear_rect(clear_rect);
}

void ReplDisplay::do_open_prompt() {}

Edit::Suggestions ReplDisplay::get_suggestions() const {
    auto content_string = document()->content_string();
    auto cursor_index = document()->cursor_index_in_content_string(cursors().main_cursor());
    auto suggestions_object = m_repl.get_suggestions(content_string, cursor_index);

    auto& suggestions = suggestions_object.suggestion_list();
    if (suggestions_object.suggestion_count() <= 1) {
        return suggestions_object;
    }

    ::qsort(suggestions.vector(), suggestions.size(), sizeof(suggestions[0]), [](const void* p1, const void* p2) {
        const auto* s1 = reinterpret_cast<const String*>(p1);
        const auto* s2 = reinterpret_cast<const String*>(p2);
        return strcmp(s1->string(), s2->string());
    });

    size_t i;
    for (i = suggestions_object.suggestion_offset(); i < suggestions.first().size() && i < suggestions.last().size(); i++) {
        if (suggestions.first()[i] != suggestions.last()[i]) {
            break;
        }
    }

    if (i == suggestions_object.suggestion_offset()) {
        return suggestions_object;
    }

    auto new_suggestions = Vector<String>::create_from_single_element({ suggestions.first().string(), i });
    return Edit::Suggestions { suggestions_object.suggestion_offset(), move(new_suggestions) };
}

void ReplDisplay::handle_suggestions(const Edit::Suggestions&) {}

Vector<SharedPtr<Edit::Document>>& ReplDisplay::ensure_history_documents() {
    if (m_history_documents.empty()) {
        m_history_documents.resize(m_repl.history().size() + 1);
    }
    return m_history_documents;
}

void ReplDisplay::put_history_document(SharedPtr<Edit::Document> document, int index) {
    ensure_history_documents()[index] = move(document);
}

SharedPtr<Edit::Document> ReplDisplay::history_document(int index) {
    auto& documents = ensure_history_documents();
    if (documents[index]) {
        return move(documents[index]);
    }

    auto& new_document_text = m_repl.history().item(index);
    return Edit::Document::create_from_text(new_document_text);
}

void ReplDisplay::move_history_up() {
    if (m_history_index == 0) {
        return;
    }

    auto current_document = document_as_shared();
    auto new_document = history_document(m_history_index - 1);
    new_document->copy_settings_from(*current_document);
    put_history_document(move(current_document), m_history_index);

    set_document(new_document);
    new_document->move_cursor_to_document_end(*this, cursors().main_cursor());
    new_document->scroll_cursor_into_view(*this, cursors().main_cursor());
    new_document->invalidate_rendered_contents(cursors().main_cursor().referenced_line(*new_document));
    invalidate();

    m_history_index--;
}

void ReplDisplay::move_history_down() {
    if (m_history_index == m_repl.history().size()) {
        return;
    }

    auto current_document = document_as_shared();
    auto new_document = history_document(m_history_index + 1);
    new_document->copy_settings_from(*current_document);
    put_history_document(move(current_document), m_history_index);

    set_document(new_document);
    new_document->move_cursor_to_document_end(*this, cursors().main_cursor());
    new_document->scroll_cursor_into_view(*this, cursors().main_cursor());
    new_document->invalidate_rendered_contents(cursors().main_cursor().referenced_line(*new_document));
    invalidate();

    m_history_index++;
}

Edit::TextIndex ReplDisplay::text_index_at_mouse_position(const Point& point) {
    return document()->text_index_at_scrolled_position(*this, { point.y(), point.x() });
}

Maybe<String> ReplDisplay::enter_prompt(const String&, String) {
    return {};
}

Maybe<String> ReplDisplay::prompt(const String&) {
    return {};
}

void ReplDisplay::enter_search(String) {}

void ReplDisplay::set_clipboard_contents(String text, bool is_whole_line) {
    m_prev_clipboard_contents = move(text);
    m_prev_clipboard_contents_were_whole_line = is_whole_line;
    Clipboard::Connection::the().set_clipboard_contents_to_text(m_prev_clipboard_contents);
}

String ReplDisplay::clipboard_contents(bool& is_whole_line) const {
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
}