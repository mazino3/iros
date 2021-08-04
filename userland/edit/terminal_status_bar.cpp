#include <edit/document.h>
#include <edit/document_type.h>
#include <tinput/terminal_renderer.h>

#include "terminal_display.h"
#include "terminal_status_bar.h"

static TerminalStatusBar* s_the;

TerminalStatusBar& TerminalStatusBar::the() {
    assert(s_the);
    return *s_the;
}

TerminalStatusBar::TerminalStatusBar() {
    s_the = this;

    set_layout_constraint({ TUI::LayoutConstraint::AutoSize, 1 });
}

TerminalStatusBar::~TerminalStatusBar() {}

void TerminalStatusBar::set_active_display(TerminalDisplay* display) {
    if (!display) {
        m_active_display.reset();
        invalidate();
        return;
    }

    m_active_display = display->weak_from_this();
    invalidate();
}

void TerminalStatusBar::set_status_message(String message) {
    m_status_message = move(message);
    m_status_message_timer = App::Timer::create_single_shot_timer(
        shared_from_this(),
        [this](auto) {
            m_status_message = {};
            invalidate();
        },
        3000);
    invalidate();
}

void TerminalStatusBar::render() {
    auto display = m_active_display.lock();
    if (!display) {
        return;
    }

    auto document = display->document();
    if (!document) {
        return;
    }

    auto& name = document->name().empty() ? "[Unamed File]"s : document->name();

    auto cursor_col = display->cursors().main_cursor().referenced_line(*document).absoulte_col_offset_of_index(
        *document, *display, display->cursors().main_cursor().index_into_line());
    auto position_string = String::format("%d,%d", display->cursors().main_cursor().line_index() + 1, cursor_col + 1);
    auto status_rhs = String::format("%s%s [%s] %9s", name.string(), document->modified() ? "*" : "",
                                     Edit::document_type_to_string(document->type()).string(), position_string.string());

    auto renderer = get_renderer();

    renderer.render_text(sized_rect(), status_rhs.view(), {}, TextAlign::CenterRight);

    auto left = 0;
    if (m_status_message) {
        auto padded_message = String::format("%s    ", m_status_message->string());
        renderer.render_text(sized_rect(), padded_message.view());
        left = padded_message.size();
    }

    auto right = sized_rect().width() - static_cast<int>(status_rhs.size());
    if (right >= left) {
        renderer.clear_rect(sized_rect().with_x(left).with_width(right - left));
    }
}
