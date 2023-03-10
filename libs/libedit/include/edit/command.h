#pragma once

#include <edit/document.h>
#include <edit/text_index.h>
#include <liim/variant.h>

namespace Edit {
class Command {
public:
    Command(Document& document);
    virtual ~Command();

    Document& document() { return m_document; }
    const Document& document() const { return m_document; }

    virtual bool execute(Display& display) = 0;
    virtual void undo(Display& display) = 0;
    virtual void redo(Display& display) = 0;

    virtual bool merge(Command&) { return false; }

    virtual StringView name() const = 0;

private:
    Document& m_document;
};

class DeltaBackedCommand : public Command {
public:
    DeltaBackedCommand(Document& document);
    virtual ~DeltaBackedCommand() override;

    virtual void redo(Display& display) override;
    virtual bool execute(Display& display) final;
    virtual void undo(Display& display) final;

    virtual bool do_execute(Display& display, MultiCursor& cursors) = 0;
    virtual void do_undo(Display& display, MultiCursor& cursors) = 0;

    const Document::StateSnapshot& start_snapshot() const { return m_start_snapshot; }
    const Document::StateSnapshot& end_snapshot() const { return m_end_snapshot; }

    const String& selection_text(int index) const { return m_selection_texts[index]; }

protected:
    void set_start_snapshot(Document::StateSnapshot snapshot) { m_start_snapshot = move(snapshot); }
    void set_end_snapshot(Document::StateSnapshot snapshot) { m_end_snapshot = move(snapshot); }

private:
    Document::StateSnapshot m_start_snapshot;
    Document::StateSnapshot m_end_snapshot;
    Vector<String> m_selection_texts;
};

class CommandGroup : public DeltaBackedCommand {
public:
    enum class ShouldMerge { Yes, No };

    explicit CommandGroup(Document& document, StringView name, ShouldMerge should_merge = ShouldMerge::No)
        : DeltaBackedCommand(document), m_name(name), m_should_merge(should_merge) {}

    template<typename C, typename... Args>
    void add(Args&&... args) {
        m_commands.add(make_unique<C>(forward<Args>(args)...));
    }

    virtual bool do_execute(Display& display, MultiCursor& cursors) override;
    virtual void do_undo(Display& display, MultiCursor& cursors) override;
    virtual void redo(Display& display) override;

    virtual bool merge(Command& other) override;

private:
    virtual StringView name() const override { return m_name; };

    Vector<UniquePtr<Command>> m_commands;
    StringView m_name;
    ShouldMerge m_should_merge { ShouldMerge::No };
};

class MovementCommand : public DeltaBackedCommand {
public:
    explicit MovementCommand(Document& document, Function<void(Display&, MultiCursor&)> do_movement)
        : DeltaBackedCommand(document), m_do_movement(move(do_movement)) {}

    virtual bool do_execute(Display& display, MultiCursor& cursors) override {
        m_do_movement.safe_call(display, cursors);
        return false;
    }

    virtual void do_undo(Display&, MultiCursor&) override {}

    virtual void redo(Display& display) override;

private:
    virtual StringView name() const override { return "MovementCommand"; }

    Function<void(Display&, MultiCursor&)> m_do_movement;
};

class InsertCommand final : public DeltaBackedCommand {
public:
    InsertCommand(Document& document, String string);
    InsertCommand(Document& document, Vector<String> text_per_cursor);
    virtual ~InsertCommand();

    virtual bool do_execute(Display& display, MultiCursor& cursor) override;
    virtual void do_undo(Display& display, MultiCursor& cursor) override;

private:
    virtual StringView name() const override { return "InsertCommand"; }

    Variant<String, Vector<String>> m_text_to_insert;
    Vector<TextIndex> m_insertion_start_indices;
};

class DeleteCommand final : public DeltaBackedCommand {
public:
    explicit DeleteCommand(Document& document);
    virtual ~DeleteCommand();

    virtual bool do_execute(Display& display, MultiCursor& cursor) override;
    virtual void do_undo(Display& display, MultiCursor& cursor) override;

private:
    virtual StringView name() const override { return "DeleteCommand"; }
};

class SwapLinesCommand final : public DeltaBackedCommand {
public:
    SwapLinesCommand(Document& document, SwapDirection direction);
    virtual ~SwapLinesCommand() override;

    virtual bool do_execute(Display& display, MultiCursor& cursor) override;
    virtual void do_undo(Display& display, MultiCursor& cursor) override;

private:
    virtual StringView name() const override { return "SwapLinesCommand"; }

    bool do_swap(Cursor& cursor, SwapDirection direction);

    SwapDirection m_direction { SwapDirection::Down };
};
}
