#pragma once

#include <ctype.h>
#include <liim/stack.h>
#include <liim/string_view.h>
#include <liim/vector.h>
#include <parser/generic_lexer.h>
#include <sh/sh_token.h>
#include <stddef.h>
#include <stdio.h>

#include "sh_token_type.h"

enum class LexComments { Yes, No };

class ShLexer final : public GenericLexer<ShTokenType, ShValue> {
public:
    using Token = GenericToken<ShTokenType, ShValue>;

    ShLexer(const char* input_stream, size_t length)
        : GenericLexer<ShTokenType, ShValue>(), m_input_stream(input_stream), m_input_length(length) {}
    virtual ~ShLexer();

    bool lex(LexComments lex_comments = LexComments::No);

    const Vector<Token>& tokens() const { return m_tokens; }

    virtual ShTokenType peek_next_token_type() const override {
        if (m_current_pos >= (size_t) m_tokens.size()) {
            return ShTokenType::End;
        }

        ShTokenType type = m_tokens[m_current_pos].type();

        if (type == ShTokenType::WORD) {
            const StringView& text = m_tokens[m_current_pos].value().text();

            if (type == ShTokenType::WORD && would_be_first_word_of_command(m_current_pos) &&
                !(m_current_pos == 0 && text.first() == '=')) {
                bool in_s_quotes = false;
                bool in_d_quotes = false;
                bool in_b_quotes = false;
                bool prev_was_backslash = false;
                bool prev_was_dollar = false;
                bool found_equal = false;
                int param_expansion_count = 0;
                for (size_t i = 0; !found_equal && i < text.size(); i++) {
                    char current = text[i];
                    switch (current) {
                        case '\\':
                            if (!in_s_quotes) {
                                prev_was_backslash = !prev_was_backslash;
                                prev_was_dollar = false;
                                continue;
                            }
                            break;
                        case '$':
                            if (!prev_was_dollar && !prev_was_backslash && !in_d_quotes && !in_b_quotes && !in_s_quotes) {
                                prev_was_dollar = true;
                                continue;
                            }
                            break;
                        case '\'':
                            in_s_quotes = !in_d_quotes && !in_b_quotes ? !in_s_quotes : in_s_quotes;
                            break;
                        case '"':
                            in_d_quotes = !prev_was_backslash && !in_s_quotes && !in_b_quotes ? !in_d_quotes : in_d_quotes;
                            break;
                        case '`':
                            in_b_quotes = !prev_was_backslash && !in_d_quotes && !in_s_quotes ? !in_b_quotes : in_b_quotes;
                            break;
                        case '{':
                            if (prev_was_dollar && !prev_was_backslash && !in_b_quotes && !in_s_quotes && !in_d_quotes) {
                                param_expansion_count++;
                            }
                            break;
                        case '(':
                            if (!prev_was_backslash && !in_b_quotes && !in_s_quotes && !in_d_quotes) {
                                param_expansion_count++;
                            }
                            break;
                        case '}':
                        case ')':
                            if (!prev_was_backslash && !in_b_quotes && !in_s_quotes && !in_d_quotes) {
                                param_expansion_count--;
                            }
                            break;
                        case '=':
                            if (!prev_was_backslash && !in_b_quotes && !in_s_quotes && !in_d_quotes && param_expansion_count == 0) {
                                found_equal = true;
                            }
                            break;
                        default:
                            break;
                    }

                    prev_was_dollar = false;
                    prev_was_backslash = false;
                }

                if (found_equal) {
                    type = ShTokenType::ASSIGNMENT_WORD;
                }
            }

            if (type != ShTokenType::WORD) {
                const_cast<ShLexer&>(*this).m_tokens[m_current_pos].set_type(type);
            }
        }

        return type;
    }

    virtual const ShValue& peek_next_token_value() const override {
        assert(m_current_pos < (size_t) m_tokens.size());
        return m_tokens[m_current_pos].value();
    }

    virtual void advance() override { m_current_pos++; }

    template<typename TypeGetter>
    static bool static_would_be_first_word_of_command(int tokens_size, TypeGetter type_getter, int start_index = -1) {
        if (start_index == -1) {
            start_index = tokens_size;
        }

        if (start_index > 0 && is_io_redirect(type_getter(start_index - 1))) {
            return false;
        }

        for (int i = start_index - 1; i >= 0; i--) {
            if (type_getter(i) == ShTokenType::In) {
                return false;
            }

            if (type_getter(i) != ShTokenType::WORD) {
                return true;
            } else if (i > 0 && is_io_redirect(type_getter(i - 1))) {
                i--;
                continue;
            } else {
                return false;
            }
        }

        return true;
    }

    bool would_be_first_word_of_command(int start_index = -1) const {
        return static_would_be_first_word_of_command(
            m_tokens.size(),
            [&](int index) -> ShTokenType {
                return m_tokens[index].type();
            },
            start_index);
    }

private:
    static bool is_io_redirect(ShTokenType type) {
        return type == ShTokenType::LESSGREAT || type == ShTokenType::LessThan || type == ShTokenType::GreaterThan ||
               type == ShTokenType::GREATAND || type == ShTokenType::LESSGREAT || type == ShTokenType::DGREAT ||
               type == ShTokenType::DLESS || type == ShTokenType::DLESSDASH || type == ShTokenType::TLESS || type == ShTokenType::LESSAND ||
               type == ShTokenType::CLOBBER;
    }

    int peek() const {
        if (m_position >= m_input_length) {
            return EOF;
        }
        return m_input_stream[m_position];
    }

    void consume() {
        if (peek() == '\n') {
            m_current_row++;
            m_current_col = -1;
        }

        m_position++;
        m_current_col++;
    }

    void begin_token() {
        m_current_token_start = m_input_stream + m_position;
        m_current_token_row = m_current_row;
        m_current_token_col = m_current_col;
    }

    void commit_token(ShTokenType type) {
        if (!m_current_token_start) {
            if (type == ShTokenType::NAME && m_tokens.size() && m_tokens.last().type() == ShTokenType::WORD) {
                m_tokens.last().set_type(ShTokenType::NAME);
            }
            return;
        }

        auto text = StringView { m_current_token_start, m_input_stream + m_position };
        if (m_expecting_name) {
            m_expecting_name = false;

            if (type == ShTokenType::WORD) {
                type = ShTokenType::NAME;
            }
        }

        if (type == ShTokenType::IO_NUMBER) {
            for (size_t i = 0; i < text.size() - 1; i++) {
                if (!isdigit(text[i])) {
                    type = ShTokenType::WORD;
                    break;
                }
            }
        } else if (type == ShTokenType::WORD) {
            if (would_be_first_word_of_command() || m_allow_reserved_word_next) {
                m_allow_reserved_word_next = true;
                if (text == "if") {
                    type = ShTokenType::If;
                } else if (text == "then") {
                    type = ShTokenType::Then;
                } else if (text == "else") {
                    type = ShTokenType::Else;
                } else if (text == "elif") {
                    type = ShTokenType::Elif;
                } else if (text == "fi") {
                    type = ShTokenType::Fi;
                } else if (text == "do") {
                    type = ShTokenType::Do;
                } else if (text == "done") {
                    type = ShTokenType::Done;
                } else if (text == "case") {
                    m_allow_reserved_word_next = false;
                    type = ShTokenType::Case;
                } else if (text == "esac") {
                    type = ShTokenType::Esac;
                } else if (text == "while") {
                    type = ShTokenType::While;
                } else if (text == "until") {
                    type = ShTokenType::Until;
                } else if (text == "for") {
                    m_expecting_name = true;
                    m_allow_reserved_word_next = false;
                    type = ShTokenType::For;
                } else if (text == "{") {
                    type = ShTokenType::Lbrace;
                } else if (text == "}") {
                    type = ShTokenType::Rbrace;
                } else if (text == "!") {
                    type = ShTokenType::Bang;
                } else if (text == "in") {
                    m_allow_reserved_word_next = false;
                    type = ShTokenType::In;
                } else {
                    m_allow_reserved_word_next = false;
                }
            }

            if (m_tokens.size() >= 2 && m_tokens[m_tokens.size() - 2].type() == ShTokenType::Case && text == "in") {
                m_allow_reserved_word_next = false;
                type = ShTokenType::In;
            }
        }

        m_tokens.add({ type, { text, m_current_token_row, m_current_token_col, m_current_row, m_current_col } });
        m_current_token_start = nullptr;
        m_current_token_row = 0;
        m_current_token_col = 0;
    }

    void set_resume_position() {
        m_resume_pos = m_position;
        m_resume_row = m_current_row;
        m_resume_col = m_current_col;
    }

    void resume_position_if_needed() {
        if (m_resume_pos != 0) {
            m_position = m_resume_pos;
            m_current_row = m_resume_row;
            m_current_col = m_resume_col;
            m_resume_pos = 0;
            m_resume_row = 0;
            m_resume_col = 0;
        }
    }

    const char* m_input_stream { nullptr };
    size_t m_input_length { 0 };
    size_t m_position { 0 };
    size_t m_current_row { 0 };
    size_t m_current_col { 0 };
    size_t m_current_token_row { 0 };
    size_t m_current_token_col { 0 };
    const char* m_current_token_start { nullptr };
    size_t m_resume_row { 0 };
    size_t m_resume_col { 0 };
    size_t m_resume_pos { 0 };
    bool m_expecting_name { false };
    bool m_allow_reserved_word_next { false };
    size_t m_current_pos { 0 };
    Vector<Token> m_tokens;
};
