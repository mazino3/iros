#pragma once

#include <app/mouse_press_tracker.h>
#include <edit/mouse_event.h>
#include <edit/panel.h>
#include <liim/hash_map.h>
#include <liim/maybe.h>
#include <liim/variant.h>
#include <liim/vector.h>
#include <time.h>

namespace TInput {

class ReplPanel final : public Panel {
public:
    ReplPanel(Repl& repl);
    virtual ~ReplPanel() override;

    virtual int rows() const override { return m_rows; }
    virtual int cols_at_row(int row) const override;

    virtual void clear() override;
    virtual void set_text_at(int row, int col, char c, CharacterMetadata metadata) override;
    virtual void flush() override;
    virtual int enter() override;
    virtual void send_status_message(String message) override;
    virtual Maybe<String> prompt(const String& message) override;
    virtual void enter_search(String starting_text) override;
    virtual void notify_line_count_changed() override;
    virtual void quit() override;

    virtual void set_clipboard_contents(String text, bool is_whole_line) override;
    virtual String clipboard_contents(bool& is_whole_line) const override;

    virtual void set_cursor(int row, int col) override;

    virtual Suggestions get_suggestions() const override;
    virtual void handle_suggestions(const Suggestions& suggestions) override;

    virtual int cursor_col() const { return m_cursor_col; }
    virtual int cursor_row() const { return m_cursor_row; }

    void set_coordinates(int rows, int max_rows, int max_cols);
    bool quit_by_interrupt() const { return m_quit_by_interrupt; }
    bool quit_by_eof() const { return m_quit_by_eof; }

    virtual void do_open_prompt() override;
    virtual void notify_now_is_a_good_time_to_draw_cursor() override;

private:
    struct Info {
        char ch;
        CharacterMetadata metadata;
    };

    virtual void document_did_change() override;

    Vector<Variant<KeyPress, MouseEvent>> read_input();

    void draw_cursor();

    String string_for_metadata(CharacterMetadata metadata) const;

    void print_char(char c, CharacterMetadata metadata);
    void flush_row(int line);

    void set_quit_by_interrupt() { m_quit_by_interrupt = true; }
    void set_quit_by_eof() { m_quit_by_eof = true; }

    Maybe<String> enter_prompt(const String& message, String starting_text = "");

    int index(int row, int col) const;
    int max_cols() const { return m_max_cols; }
    int max_rows() const { return m_max_rows; }
    int prompt_cols_at_row(int row) const;
    const String& prompt_at_row(int row) const;

    Vector<UniquePtr<Document>>& ensure_history_documents();
    void put_history_document(UniquePtr<Document> document, int history_index);
    UniquePtr<Document> take_history_document(int history_index);

    void move_history_up();
    void move_history_down();

    void get_absolute_row_position();

    Repl& m_repl;
    String m_main_prompt;
    String m_secondary_prompt;
    Vector<UniquePtr<Document>> m_history_documents;
    int m_history_index { -1 };

    Vector<Info> m_screen_info;
    Vector<bool> m_dirty_rows;
    mutable String m_prev_clipboard_contents;
    mutable bool m_prev_clipboard_contents_were_whole_line { false };
    App::MousePressTracker m_mouse_press_tracker;
    CharacterMetadata m_last_metadata_rendered;
    int m_rows { 0 };
    int m_max_rows { 0 };
    int m_max_cols { 0 };
    int m_absolute_row_position { -1 };
    int m_cursor_row { 0 };
    int m_cursor_col { 0 };
    int m_visible_cursor_row { 0 };
    int m_visible_cursor_col { 0 };
    int m_exit_code { 0 };
    int m_consecutive_tabs { 0 };
    bool m_should_exit { false };
    bool m_quit_by_interrupt { false };
    bool m_quit_by_eof { false };
};

}
