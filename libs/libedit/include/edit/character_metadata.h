#pragma once

namespace Edit {
class CharacterMetadata {
public:
    enum Flags {
        Highlighted = 1,
        Selected = 2,
        SyntaxString = 4,
        SyntaxOperator = 8,
        SyntaxIdentifier = 16,
        SyntaxNumber = 32,
        SyntaxKeyword = 64,
        SyntaxComment = 128,
        SyntaxImportant = 256,
        AutoCompletePreview = 512,
        MainCursor = 1024,
        SecondaryCursor = 2048,
    };

    CharacterMetadata() {}
    CharacterMetadata(int flags) : m_flags(flags) {}

    bool operator==(const CharacterMetadata& other) const { return this->m_flags == other.m_flags; }
    bool operator!=(const CharacterMetadata& other) const { return !(*this == other); }

    CharacterMetadata combine(CharacterMetadata other) const { return { this->m_flags | other.m_flags }; }

    bool highlighted() const { return m_flags & Flags::Highlighted; }
    void set_highlighted(bool b) {
        if (b) {
            m_flags |= Flags::Highlighted;
        } else {
            m_flags &= ~Flags::Highlighted;
        }
    }
    void invert_highlighted() { m_flags ^= Flags::Highlighted; }

    bool selected() const { return m_flags & Flags::Selected; }
    void set_selected(bool b) {
        if (b) {
            m_flags |= Flags::Selected;
        } else {
            m_flags &= ~Flags::Selected;
        }
    }
    void invert_selected() { m_flags ^= Flags::Selected; }

    void clear_syntax_highlighting() { m_flags &= (Flags::Highlighted | Flags::Selected); }
    void set_syntax_highlighting(int flags) {
        clear_syntax_highlighting();
        m_flags |= flags;
    }
    int syntax_highlighting() const { return m_flags & (~(Flags::Highlighted | Flags::Selected | Flags::AutoCompletePreview)); }

    int auto_complete_preview() const { return m_flags & Flags::AutoCompletePreview; }

    int main_cursor() const { return m_flags & Flags::MainCursor; }
    int secondary_cursor() const { return m_flags & Flags::SecondaryCursor; }

private:
    int m_flags { 0 };
};
}
