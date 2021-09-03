#include <app/flex_layout_engine.h>
#include <edit/document.h>
#include <tui/label.h>

#include "terminal_display.h"
#include "terminal_search.h"

TerminalSearch::TerminalSearch(TerminalDisplay& host_display, String initial_text)
    : m_host_display(host_display), m_initial_text(move(initial_text)) {}

void TerminalSearch::initialize() {
    auto& layout = set_layout_engine<App::HorizontalFlexLayoutEngine>();
    layout.set_margins({ 1, 1, 1, 1 });

    auto& label = layout.add<TUI::Label>("Find: ");
    label.set_shrink_to_fit(true);

    auto search_document = Edit::Document::create_from_text(m_initial_text);
    search_document->set_submittable(true);
    search_document->on<Edit::Submit>(*this, [this](auto&) {
        m_host_display.document()->move_cursor_to_next_search_match(m_host_display, m_host_display.cursors().main_cursor());
    });

    search_document->on<Edit::Change>(*this, [this, document = search_document.get()](auto&) {
        auto to_find = document->content_string();
        m_host_display.document()->set_search_text(move(to_find));
    });

    auto& text_box = layout.add<TerminalDisplay>();
    text_box.on<App::KeyDownEvent>(*this, [this](const App::KeyDownEvent& event) {
        if (event.key() == App::Key::Escape) {
            m_host_display.hide_search_panel();
            return true;
        }
        return false;
    });
    text_box.set_document(search_document);
    text_box.enter();

    set_focus_proxy(&text_box);

    search_document->select_all(text_box, text_box.cursors().main_cursor());

    Panel::initialize();
}

TerminalSearch::~TerminalSearch() {}
