#include <app/flex_layout_engine.h>
#include <edit/document.h>
#include <tui/label.h>

#include "terminal_display.h"
#include "terminal_prompt.h"

TerminalPrompt::TerminalPrompt(TerminalDisplay& host_display, String prompt, String initial_value)
    : m_host_display(host_display), m_prompt(move(prompt)), m_initial_value(move(initial_value)) {}

void TerminalPrompt::did_attach() {
    auto& layout = set_layout_engine<App::HorizontalFlexLayoutEngine>();
    layout.set_margins({ 1, 1, 1, 1 });

    auto& label = layout.add<TUI::Label>(m_prompt);
    label.set_shrink_to_fit(true);

    auto document = Edit::Document::create_from_text(m_initial_value);
    document->set_submittable(true);
    listen<Edit::Submit>(*document, [this, document = document.get()](auto&) {
        emit<Edit::PromptResult>(document->content_string());
    });

    m_display = layout.add_owned<TerminalDisplay>();
    listen_intercept<App::KeyDownEvent>(*m_display, [this](const App::KeyEvent& event) {
        if (event.key() == App::Key::Escape) {
            m_host_display.hide_prompt_panel();
            m_host_display.make_focused();
            return true;
        }
        return false;
    });
    m_display->set_steals_focus(true);
    m_display->set_document(document);
    m_display->enter();

    set_focus_proxy(&m_display->base());

    document->move_cursor_to_document_end(m_display->base(), m_display->main_cursor());

    Panel::did_attach();
}

Task<Option<String>> TerminalPrompt::block_until_result(App::Object& coroutine_owner) {
    auto event = co_await block_until_event<Edit::PromptResult>(coroutine_owner);
    co_return event.result();
}

TerminalPrompt::~TerminalPrompt() {}
