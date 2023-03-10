#pragma once

#include <edit/absolute_position.h>
#include <edit/display_position.h>
#include <edit/document_type.h>
#include <edit/forward.h>
#include <edit/line.h>
#include <edit/multicursor.h>
#include <edit/relative_position.h>
#include <edit/suggestions.h>
#include <edit/text_index.h>
#include <edit/text_range_collection.h>
#include <eventloop/event.h>
#include <eventloop/object.h>
#include <liim/function.h>
#include <liim/pointers.h>
#include <liim/result.h>
#include <liim/vector.h>

APP_EVENT(Edit, DeleteLines, App::Event, (), ((int, line_index), (int, line_count)), ())
APP_EVENT(Edit, AddLines, App::Event, (), ((int, line_index), (int, line_count)), ())
APP_EVENT(Edit, SplitLines, App::Event, (), ((int, line_index), (int, index_into_line)), ())
APP_EVENT(Edit, MergeLines, App::Event, (), ((int, first_line_index), (int, first_line_length), (int, second_line_index)), ())
APP_EVENT(Edit, AddToLine, App::Event, (), ((int, line_index), (int, index_into_line), (int, bytes_added)), ())
APP_EVENT(Edit, DeleteFromLine, App::Event, (), ((int, line_index), (int, index_into_line), (int, bytes_deleted)), ())
APP_EVENT(Edit, MoveLineTo, App::Event, (), ((int, line), (int, destination)), ())

APP_EVENT(Edit, SyntaxHighlightingChanged, App::Event, (), (), ())
APP_EVENT(Edit, DocumentTypeChanged, App::Event, (), (), ())

APP_EVENT(Edit, Submit, App::Event, (), (), ())
APP_EVENT(Edit, Change, App::Event, (), (), ())

namespace Edit {
class DeleteCommand;

enum class MovementMode { Move, Select };

enum class DeleteCharMode { Backspace, Delete };

enum class SwapDirection { Up, Down };

enum class InputMode { Document, InputText };

class Document final : public App::Object {
    APP_OBJECT(Document)

    APP_EMITS(App::Object, DeleteLines, AddLines, SplitLines, MergeLines, AddToLine, DeleteFromLine, SyntaxHighlightingChanged, MoveLineTo,
              DocumentTypeChanged, Submit, Change)

public:
    static Result<SharedPtr<Document>, int> create_from_stdin(const String& path);
    static Result<SharedPtr<Document>, int> create_from_file(const String& path);
    static SharedPtr<Document> create_from_text(const String& text);
    static SharedPtr<Document> create_default(const String& path);
    static SharedPtr<Document> create_empty();

    struct StateSnapshot {
        MultiCursor::Snapshot cursors;
        bool document_was_modified { false };
    };

    struct Snapshot {
        Vector<Line> lines;
        StateSnapshot state;
    };

    virtual void initialize() override;
    virtual ~Document() override;

    void copy_settings_from(const Document& other);

    void invalidate_lines_in_range(Display& display, const TextRange& range);
    void invalidate_lines_in_range_collection(Display& display, const TextRangeCollection& collection);

    bool input_text_mode() const { return m_input_mode == InputMode::InputText; }
    bool submittable() const { return m_submittable; }

    void set_submittable(bool b) { m_submittable = b; }

    String content_string() const;
    size_t cursor_index_in_content_string(const Cursor& cursor) const;

    bool convert_tabs_to_spaces() const { return m_convert_tabs_to_spaces; }
    void set_convert_tabs_to_spaces(bool b) { m_convert_tabs_to_spaces = b; }

    bool modified() const { return m_document_was_modified; }

    const String& name() const { return m_name; }
    void set_name(String name) { m_name = move(name); }

    void move_cursor_left(Display& display, Cursor& cursor, MovementMode mode = MovementMode::Move);
    void move_cursor_right(Display& display, Cursor& cursor, MovementMode mode = MovementMode::Move);
    void move_cursor_down(Display& display, Cursor& cursor, MovementMode mode = MovementMode::Move);
    void move_cursor_up(Display& display, Cursor& cursor, MovementMode mode = MovementMode::Move);
    void move_cursor_to_line_start(Display& display, Cursor& cursor, MovementMode mode = MovementMode::Move);
    void move_cursor_to_line_end(Display& display, Cursor& cursor, MovementMode mode = MovementMode::Move);
    void move_cursor_left_by_word(Display& display, Cursor& cursor, MovementMode mode = MovementMode::Move);
    void move_cursor_right_by_word(Display& display, Cursor& cursor, MovementMode mode = MovementMode::Move);
    void move_cursor_to_document_start(Display& display, Cursor& cursor, MovementMode mode = MovementMode::Move);
    void move_cursor_to_document_end(Display& display, Cursor& cursor, MovementMode mode = MovementMode::Move);
    void move_cursor_page_up(Display& display, Cursor& cursor, MovementMode mode = MovementMode::Move);
    void move_cursor_page_down(Display& display, Cursor& cursor, MovementMode mode = MovementMode::Move);
    void move_cursor_to(Display& display, Cursor& cursor, const TextIndex& index, MovementMode mode = MovementMode::Move);

    int line_count() const { return m_lines.size(); }
    int num_rendered_lines(Display& display) const;

    void remove_line(int index);
    void remove_lines(int index, int count);
    void insert_line(Line&& line, int index);
    void insert_lines(int line_index, Span<StringView> lines);
    void move_line_to(int line, int destination);

    void split_line_at(const TextIndex& index);
    void merge_lines(int first_line_index, int second_line_index);

    void set_was_modified(bool b) { m_document_was_modified = b; }

    Snapshot snapshot(Display& display) const;
    void restore(MultiCursor& cursors, Snapshot snapshot);

    StateSnapshot snapshot_state(Display& display) const;
    void restore_state(MultiCursor& cursors, const StateSnapshot& state_snapshot);

    void insert_text_at_index(const TextIndex& index, StringView text);
    void delete_text_in_range(const TextRange& range);
    String text_in_range(const TextRange& range) const;

    void select_all_matches(Display& display, const TextRangeCollection& collection);
    void select_line_at_cursor(Display& display, Cursor& cursor);
    void select_word_at_cursor(Display& display, Cursor& cursor);
    void select_all(Display& display, Cursor& cursor);

    void redo(Display& display);
    void undo(Display& display);

    void copy(Display& display, MultiCursor& cursor);
    void paste(Display& display, MultiCursor& cursor);
    void cut(Display& display, MultiCursor& cursor);

    void insert_suggestion(Display& display, const MatchedSuggestion& suggestion);
    void insert_text_per_cursor(Display& display, Vector<String> strings, StringView command_name);
    void insert_text_at_cursor(Display& display, const String& string);
    void insert_line_at_cursor(Display& display, const String& line);
    void replace_next_search_match(Display& display, const String& replacement);
    void replace_all_search_matches(Display& display, const String& replacement);

    void set_input_mode(InputMode mode) { m_input_mode = mode; }

    DocumentType type() const { return m_type; }
    void set_type(DocumentType type);

    Line& line_at_index(int index) { return m_lines[index]; }
    const Line& line_at_index(int index) const { return m_lines[index]; }

    Line& first_line() { return m_lines.first(); }
    const Line& first_line() const { return m_lines.first(); }

    Line& last_line() { return m_lines.last(); }
    const Line& last_line() const { return m_lines.last(); }

    int first_line_index() const { return 0; }
    int last_line_index() const { return line_count() - 1; }

    bool execute_command(Display& display, Command& command);

    TextRangeCollection& syntax_highlighting_info() { return m_syntax_highlighting_info; }
    const TextRangeCollection& syntax_highlighting_info() const { return m_syntax_highlighting_info; }

    void swap_lines_at_cursor(Display& display, SwapDirection direction);
    void split_line_at_cursor(Display& display);
    void delete_char(Display& display, DeleteCharMode mode);
    void delete_word(Display& display, DeleteCharMode mode);
    void delete_line(Display& display);

private:
    Document(Vector<Line> lines, String name, InputMode mode);

    void move_cursor_to_max_col_position(Display& display, Cursor& cursor);
    void update_selection_state_for_mode(Cursor& cursor, MovementMode mode);

    void swap_selection_anchor_and_cursor(Display& display, Cursor& cursor);

    void guess_type_from_name();

    template<typename C, typename... Args>
    void push_command(Display& display, Args&&... args) {
        auto command = make_unique<C>(*this, forward<Args>(args)...);
        push_command(display, move(command));
    }

    void push_command(Display& display, UniquePtr<Command> command);

    Vector<Line> m_lines;
    String m_name;
    DocumentType m_type { DocumentType::Text };
    InputMode m_input_mode { InputMode::Document };
    bool m_submittable { false };
    bool m_convert_tabs_to_spaces { true };

    Vector<UniquePtr<Command>> m_command_stack;
    int m_command_stack_index { 0 };
    int m_max_undo_stack { 50 };
    bool m_document_was_modified { false };

    TextRangeCollection m_syntax_highlighting_info;
};
}
