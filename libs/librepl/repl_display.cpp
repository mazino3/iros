#include <clipboard/connection.h>
#include <edit/document.h>
#include <edit/document_type.h>
#include <edit/keyboard_action.h>
#include <edit/line_renderer.h>
#include <eventloop/event.h>
#include <graphics/point.h>
#include <liim/utf8_view.h>
#include <repl/repl_base.h>
#include <stdlib.h>
#include <tinput/io_terminal.h>
#include <tinput/terminal_glyph.h>
#include <tinput/terminal_renderer.h>
#include <tui/application.h>

#include "repl_display.h"
#include "suggestions_panel.h"

namespace Repl {
ReplDisplay::ReplDisplay(ReplBase& repl) : m_repl(repl) {

    m_main_prompt = repl.get_main_prompt();
    m_secondary_prompt = repl.get_secondary_prompt();

    m_history_index = m_repl.history().size();
}

void ReplDisplay::did_attach() {
    set_accepts_focus(true);

    set_key_bindings(Edit::get_key_bindings(base()));

    on<App::KeyDownEvent>([this](const App::KeyEvent& event) {
        if (!document()) {
            return false;
        }

        if (m_suggestions_panel) {
            switch (event.key()) {
                case App::Key::Enter:
                case App::Key::Tab:
                case App::Key::UpArrow:
                case App::Key::DownArrow:
                case App::Key::Escape:
                    return forward_to(m_suggestions_panel->base(), event);
                default:
                    break;
            }
        }

        if (event.key() == App::Key::R && event.control_down()) {
            m_suggest_based_on_history = true;
            compute_suggestions();
            show_suggestions_panel();
            return true;
        }

        if (event.key() == App::Key::C && event.control_down()) {
            deferred_invoke([] {
                printf("^C\r\n");
            });
            set_quit_by_interrupt();
            start_coroutine(quit());
            return true;
        }

        if (event.key() == App::Key::D && event.control_down()) {
            if (document()->line_count() == 1 && document()->content_string().empty()) {
                set_quit_by_eof();
                start_coroutine(quit());
            }
            return true;
        }

        if (event.key() == App::Key::UpArrow && main_cursor().at_first_line(*document())) {
            move_history_up();
            return true;
        }

        if (event.key() == App::Key::DownArrow && main_cursor().at_last_line(*document())) {
            move_history_down();
            return true;
        }

        if (event.key() != App::Key::Tab) {
            m_consecutive_tabs = 0;
        }

        return false;
    });

    Panel::did_attach();
}

ReplDisplay::~ReplDisplay() {}

void ReplDisplay::document_did_change() {
    if (document()) {
        listen<Edit::Submit>(*document(), [this](auto&) {
            auto input_text = document()->content_string();
            auto input_status = m_repl.get_input_status(input_text);

            if (input_status == Repl::InputStatus::Finished) {
                cursors().remove_secondary_cursors();
                document()->move_cursor_to_document_end(base(), main_cursor());
                set_preview_auto_complete(false);
                invalidate();
                start_coroutine(quit());
                deferred_invoke([] {
                    printf("\r\n");
                });
                return;
            }

            document()->insert_line(Edit::Line(""), document()->line_count());
            cursors().remove_secondary_cursors();
            document()->move_cursor_to_document_end(base(), main_cursor());
            scroll_cursor_into_view(main_cursor());
        });
    }
}

App::ObjectBoundCoroutine ReplDisplay::quit() {
    TUI::Application::the().main_event_loop().set_should_exit(true);
    co_return;
}

App::ObjectBoundCoroutine ReplDisplay::do_open_prompt() {
    co_return;
}

Task<Option<String>> ReplDisplay::prompt(String, String) {
    co_return {};
}

int ReplDisplay::enter() {
    make_focused();
    return 0;
}

Option<Point> ReplDisplay::cursor_position() {
    if (!document()) {
        return {};
    }

    auto position = display_position_of_index(main_cursor().index());
    return Point { position.col(), position.row() };
}

void ReplDisplay::move_up_rows(int count) {
    auto terminal_rect = TUI::Application::the().io_terminal().terminal_rect();
    auto new_height = sized_rect().height() + count;
    set_positioned_rect({ 0, terminal_rect.height() - new_height, sized_rect().width(), new_height });

    TUI::Application::the().io_terminal().scroll_up(count);
}

void ReplDisplay::render() {
    if (!document()) {
        return;
    }

    auto rendered_line_count = document()->num_rendered_lines(base());
    if (positioned_rect().top() != 0 && rendered_line_count > sized_rect().height()) {
        auto terminal_rect = TUI::Application::the().io_terminal().terminal_rect();
        auto new_height = min(rendered_line_count, terminal_rect.height());

        auto delta_height = new_height - sized_rect().height();
        move_up_rows(delta_height);
        scroll_up(delta_height);
    }

    m_last_rendered_row = 0;
    render_lines();

    auto empty_rows = rows() - m_last_rendered_row - 1;
    auto renderer = get_renderer();
    renderer.clear_rect({ 0, rows() - empty_rows, sized_rect().width(), empty_rows });

    return Panel::render();
}

Edit::RenderedLine ReplDisplay::compose_line(const Edit::Line& line) {
    if (!document()) {
        return {};
    }

    auto renderer = Edit::LineRenderer { cols(), word_wrap_enabled() };

    auto& prompt = &line == &document()->first_line() ? m_main_prompt : m_secondary_prompt;
    renderer.begin_segment(0, 0, Edit::PositionRangeType::InlineBeforeCursor);
    renderer.add_to_segment(prompt.view(), TInput::convert_to_glyphs(prompt.view()).total_width());
    renderer.end_segment();

    auto glyphs = TInput::convert_to_glyphs(line.contents().view());
    auto index_into_line = 0;

    auto maybe_insert_auto_complete_text = [&] {
        if (cursors().should_show_auto_complete_text_at(*document(), line, index_into_line)) {
            auto maybe_suggestion_text = cursors().preview_auto_complete_text();
            if (maybe_suggestion_text) {
                renderer.begin_segment(index_into_line, Edit::CharacterMetadata::Flags::AutoCompletePreview,
                                       Edit::PositionRangeType::InlineAfterCursor);
                renderer.add_to_segment(maybe_suggestion_text->view(), maybe_suggestion_text->size());
                renderer.end_segment();
            }
        }
    };

    for (auto& glyph : glyphs) {
        maybe_insert_auto_complete_text();

        renderer.begin_segment(index_into_line, 0, Edit::PositionRangeType::Normal);
        if (glyph.text() == "\t") {
            auto spaces = String::repeat(' ', Edit::tab_width - (renderer.absolute_col_position() % Edit::tab_width));
            renderer.add_to_segment(spaces.view(), spaces.size());
        } else {
            renderer.add_to_segment(glyph.text().view(), glyph.width());
        }
        renderer.end_segment();

        index_into_line += glyph.text().size();
    }
    maybe_insert_auto_complete_text();

    return renderer.finish(line, 0);
}

void ReplDisplay::send_status_message(String) {}

void ReplDisplay::output_line(int row, int col_offset, const Edit::RenderedLine& line, int line_index) {
    m_last_rendered_row = row;

    auto renderer = get_renderer();

    auto visible_line_rect = Rect { 0, row, sized_rect().width(), 1 };
    renderer.set_clip_rect(visible_line_rect);

    auto text_width = line.position_ranges()[line_index].last().end.col();

    auto text_rect = visible_line_rect.translated({ -col_offset, 0 }).with_width(text_width);
    for (auto& range : line.position_ranges()[line_index]) {
        auto rendering_info = rendering_info_for_metadata(range.metadata);
        auto style = TInput::TerminalTextStyle {
            .foreground = rendering_info.fg,
            .background = rendering_info.bg,
            .bold = rendering_info.bold,
            .invert = rendering_info.secondary_cursor,
        };

        auto glyph = TInput::TerminalGlyph { line.rendered_lines()[line_index].substring(range.byte_offset_in_rendered_string,
                                                                                         range.byte_count_in_rendered_string),
                                             static_cast<size_t>(range.end.col() - range.start.col()) };
        renderer.put_glyph(text_rect.top_left().translated(range.start.col(), 0), glyph, style);
    }

    auto clear_rect = Rect { text_rect.right(), row, max(visible_line_rect.right() - text_rect.right(), 0), 1 };
    renderer.clear_rect(clear_rect);
}

void ReplDisplay::suggestions_did_change(const Option<Edit::TextRange>& old_text_range) {
    if (m_suggestions_panel) {
        if (m_suggest_based_on_history) {
            m_suggestions_panel->did_update_suggestions();
            return;
        }

        auto& current_text_range = suggestions().current_text_range();
        if ((current_text_range.has_value() && !old_text_range.has_value()) ||
            (!current_text_range.has_value() && old_text_range.has_value())) {
            hide_suggestions_panel();
            return;
        }
        if (!current_text_range && !old_text_range) {
            m_suggestions_panel->did_update_suggestions();
            return;
        }
        if (current_text_range->start() != old_text_range->start()) {
            hide_suggestions_panel();
            return;
        }
        m_suggestions_panel->did_update_suggestions();
    }
}

void ReplDisplay::do_compute_suggestions() {
    if (m_suggest_based_on_history) {
        auto& history = m_repl.history();
        auto suggestions = Vector<Edit::Suggestion> {};
        for (int i = history.size() - 1; i >= 0; i--) {
            auto& item = m_repl.history().item(i);
            suggestions.add({ item, {} });
        }
        return set_suggestions(move(suggestions));
    }

    return set_suggestions(m_repl.get_suggestions(*document(), main_cursor().index()));
}

void ReplDisplay::show_suggestions_panel() {
    if (m_suggestions_panel) {
        return;
    }

    auto cursor_position = display_position_of_index(main_cursor().index());

    m_suggestions_panel = create_widget_owned<SuggestionsPanel>(*this);

    auto suggestions_rect = Rect { positioned_rect().x(), positioned_rect().y() + cursor_position.row() + 1, sized_rect().width(),
                                   m_suggestions_panel->layout_constraint().height() };
    if (suggestions_rect.bottom() > positioned_rect().bottom()) {
        auto ideal_rows_to_move_up = suggestions_rect.bottom() - positioned_rect().bottom();
        auto rows_to_move_up = min(positioned_rect().top(), ideal_rows_to_move_up);
        if (rows_to_move_up != 0) {
            move_up_rows(rows_to_move_up);
            suggestions_rect.set_y(suggestions_rect.y() - rows_to_move_up);
        }
    }

    m_suggestions_panel->set_positioned_rect(suggestions_rect);
}

void ReplDisplay::hide_suggestions_panel() {
    if (!m_suggestions_panel) {
        return;
    }

    m_suggest_based_on_history = false;

    m_suggestions_panel->remove();
    m_suggestions_panel = nullptr;

    make_focused();
}

void ReplDisplay::complete_suggestion(const Edit::MatchedSuggestion& suggestion) {
    document()->insert_suggestion(base(), suggestion);
    hide_suggestions_panel();
}

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
    new_document->move_cursor_to_document_end(base(), main_cursor());
    scroll_cursor_into_view(main_cursor());
    invalidate_line(main_cursor().line_index());
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
    new_document->move_cursor_to_document_end(base(), main_cursor());
    scroll_cursor_into_view(main_cursor());
    invalidate_line(main_cursor().line_index());
    invalidate();

    m_history_index++;
}

Edit::TextIndex ReplDisplay::text_index_at_mouse_position(const Point& point) {
    return text_index_at_display_position({ point.y(), point.x() });
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
