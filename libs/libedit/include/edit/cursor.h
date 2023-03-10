#pragma once

#include <assert.h>
#include <edit/forward.h>
#include <edit/text_index.h>
#include <stddef.h>

namespace Edit {
class Cursor {
public:
    Line& referenced_line(Document& document) const;
    const Line& referenced_line(const Document& document) const;
    char referenced_character(const Document& document) const;

    int line_index() const { return m_index.line_index(); }
    int index_into_line() const { return m_index.index_into_line(); }
    const TextIndex& index() const { return m_index; }

    void set_line_index(int line_index) { set({ line_index, index_into_line() }); }
    void set_index_into_line(int index_into_line) { set({ line_index(), index_into_line }); }
    void set(const TextIndex& index) { m_index = index; }

    void move_up_preserving_selection(int count) { move_preserving_selection(-count, 0); }
    void move_down_preserving_selection(int count) { move_preserving_selection(count, 0); }

    void move_left_preserving_selection(int count) { move_preserving_selection(0, -count); }
    void move_right_preserving_selection(int count) { move_preserving_selection(0, count); }

    void move_preserving_selection(int delta_line_index, int delta_index_into_line);

    RelativePosition relative_position(Display& display) const;
    AbsolutePosition absolute_position(Display& display) const;

    bool at_document_start(const Document&) const { return m_index == TextIndex { 0, 0 }; }
    bool at_document_end(const Document& document) const;

    bool at_line_start(const Document&) const { return index_into_line() == 0; }
    bool at_line_end(const Document& document) const;

    bool at_first_line(const Document&) const { return line_index() == 0; }
    bool at_last_line(const Document& document) const;

    void reset() {
        set({});
        clear_selection();
    }

    TextIndex selection_anchor() const { return m_selection_anchor.value_or(m_index); }
    void set_selection_anchor(const TextIndex& index) { m_selection_anchor = index; }
    void clear_selection() { m_selection_anchor = {}; }

    TextIndex normalized_selection_start() const;
    TextIndex normalized_selection_end() const;

    void merge_selections(const Cursor& other);
    TextRange selection() const;
    String selection_text(const Document& document) const;

    int max_col() const { return m_max_col; }
    void set_max_col(int max_col) { m_max_col = max_col; }
    void compute_max_col(Display& display);

    bool operator==(const Cursor& other) const {
        return this->index() == other.index() && this->selection_anchor() == other.selection_anchor();
    }

private:
    TextIndex m_index;
    Option<TextIndex> m_selection_anchor;
    int m_max_col { 0 };
};
};

namespace LIIM::Format {
template<>
struct Formatter<Edit::Cursor> : public BaseFormatter {
    void format(const Edit::Cursor& cursor, FormatContext& context) {
        return format_to_context(context, "Cursor <index={} selection_anchor={} max_col={}>", cursor.index(), cursor.selection_anchor(),
                                 cursor.max_col());
    }
};
}
