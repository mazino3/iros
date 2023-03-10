#include <edit/command.h>
#include <edit/display.h>
#include <edit/document.h>
#include <unistd.h>

namespace Edit {
Command::Command(Document& document) : m_document(document) {}

Command::~Command() {}

DeltaBackedCommand::DeltaBackedCommand(Document& document) : Command(document) {}

DeltaBackedCommand::~DeltaBackedCommand() {}

bool DeltaBackedCommand::execute(Display& display) {
    m_start_snapshot = document().snapshot_state(display);
    for (auto& cursor : m_start_snapshot.cursors) {
        m_selection_texts.add(cursor.selection_text(document()));
    }
    bool was_modified = do_execute(display, display.cursors());
    m_end_snapshot = document().snapshot_state(display);
    return was_modified;
}

void DeltaBackedCommand::undo(Display& display) {
    document().restore_state(display.cursors(), m_end_snapshot);
    do_undo(display, display.cursors());
    document().restore_state(display.cursors(), m_start_snapshot);
}

void DeltaBackedCommand::redo(Display& display) {
    document().restore_state(display.cursors(), start_snapshot());
    do_execute(display, display.cursors());
}

bool CommandGroup::merge(Command& other_command) {
    if (m_should_merge == ShouldMerge::No) {
        return false;
    }

    if (other_command.name() != name()) {
        return false;
    }

    auto& other = static_cast<CommandGroup&>(other_command);
    if (other.start_snapshot().cursors != this->end_snapshot().cursors) {
        return false;
    }

    m_commands.add(move(other.m_commands));
    set_end_snapshot(other.end_snapshot());
    return true;
}

bool CommandGroup::do_execute(Display& display, MultiCursor&) {
    bool modified = false;
    for (auto& command : m_commands) {
        if (command->execute(display)) {
            modified = true;
        }
    }
    return modified;
}

void CommandGroup::do_undo(Display& display, MultiCursor&) {
    m_commands.for_each_reverse([&](auto& command) {
        const_cast<Command&>(*command).undo(display);
    });
}

void CommandGroup::redo(Display& display) {
    for (auto& command : m_commands) {
        command->redo(display);
    }
}

void MovementCommand::redo(Display& display) {
    document().restore_state(display.cursors(), end_snapshot());
}

InsertCommand::InsertCommand(Document& document, String text) : DeltaBackedCommand(document), m_text_to_insert(move(text)) {}

InsertCommand::InsertCommand(Document& document, Vector<String> text_per_cursor)
    : DeltaBackedCommand(document), m_text_to_insert(move(text_per_cursor)) {}

InsertCommand::~InsertCommand() {}

bool InsertCommand::do_execute(Display&, MultiCursor& cursors) {
    m_insertion_start_indices.clear();

    bool modified = false;
    for (int i = 0; i < cursors.size(); i++) {
        auto& cursor = cursors[i];
        m_insertion_start_indices.add(cursor.index());

        auto text = m_text_to_insert.is<String>() ? m_text_to_insert.as<String>().view() : m_text_to_insert.as<Vector<String>>()[i].view();
        if (!text.empty()) {
            document().insert_text_at_index(cursor.index(), text);
            modified = true;
        }
    }
    return modified;
}

void InsertCommand::do_undo(Display&, MultiCursor& cursors) {
    for (int cursor_index = cursors.size() - 1; cursor_index >= 0; cursor_index--) {
        document().delete_text_in_range({ m_insertion_start_indices[cursor_index], end_snapshot().cursors[cursor_index].index() });
    }
}

DeleteCommand::DeleteCommand(Document& document) : DeltaBackedCommand(document) {}

DeleteCommand::~DeleteCommand() {}

bool DeleteCommand::do_execute(Display&, MultiCursor& cursors) {
    bool modified = false;
    for (int i = cursors.size() - 1; i >= 0; i--) {
        auto& cursor = cursors[i];
        if (!cursor.selection().empty()) {
            document().delete_text_in_range(cursor.selection());
            modified = true;
        }
    }

    return modified;
}

void DeleteCommand::do_undo(Display&, MultiCursor& cursors) {
    for (int i = 0; i < cursors.size(); i++) {
        document().insert_text_at_index(start_snapshot().cursors[i].selection().start(), selection_text(i).view());
    }
}

SwapLinesCommand::SwapLinesCommand(Document& document, SwapDirection direction) : DeltaBackedCommand(document), m_direction(direction) {}

SwapLinesCommand::~SwapLinesCommand() {}

bool SwapLinesCommand::do_execute(Display&, MultiCursor& cursors) {
    cursors.remove_secondary_cursors();
    auto& cursor = cursors.main_cursor();
    bool ret = do_swap(cursor, m_direction);
    return ret;
}

bool SwapLinesCommand::do_swap(Cursor& cursor, SwapDirection direction) {
    auto selection_start = cursor.selection().start();
    auto selection_end = cursor.selection().end();

    if ((selection_start.line_index() == 0 && direction == SwapDirection::Up) ||
        (selection_end.line_index() == document().last_line_index() && direction == SwapDirection::Down)) {
        return false;
    }

    if (direction == SwapDirection::Up) {
        document().move_line_to(selection_start.line_index() - 1, selection_end.line_index());
    } else {
        document().move_line_to(selection_end.line_index() + 1, selection_start.line_index());
    }

    return true;
}

void SwapLinesCommand::do_undo(Display&, MultiCursor& cursors) {
    auto& cursor = cursors.main_cursor();
    do_swap(cursor, m_direction == SwapDirection::Up ? SwapDirection::Down : SwapDirection::Up);
}
}
