#include <assert.h>
#include <sh/sh_lexer.h>
#include <stdio.h>
#include <unistd.h>

#ifndef USERLAND_NATIVE
#include <wordexp.h>
#else
#include "../../libs/libc/include/wordexp.h"
#endif /* USERLAND_NATIVE */

ShLexer::~ShLexer() {}

bool ShLexer::lex(LexComments lex_comments) {
    bool in_d_quotes = false;
    bool in_s_quotes = false;
    bool prev_was_backslash = false;
    bool prev_was_dollar = false;

    for (;;) {
        switch (peek()) {
            case EOF:
                commit_token(ShTokenType::WORD);
                return !in_d_quotes && !in_s_quotes;
            case '\\':
                if (!in_s_quotes) {
                    prev_was_backslash = !prev_was_backslash;
                    if (!m_current_token_start) {
                        begin_token();
                    }
                    consume();
                    prev_was_dollar = false;
                    continue;
                }
                goto process_regular_character;
            case '$':
                if (!prev_was_backslash && !prev_was_dollar) {
                    prev_was_dollar = true;
                    prev_was_backslash = false;
                    if (!m_current_token_start) {
                        begin_token();
                    }
                    consume();
                    continue;
                }
                goto process_regular_character;
            case '\t':
            case ' ':
                if (!prev_was_backslash && !in_d_quotes && !in_s_quotes) {
                    commit_token(ShTokenType::WORD);
                    consume();
                    break;
                }
                goto process_regular_character;
            case '\n':
                if (!prev_was_backslash && !in_d_quotes && !in_s_quotes) {
                    commit_token(ShTokenType::WORD);
                    begin_token();
                    consume();
                    commit_token(ShTokenType::NEWLINE);

                    // For Here documents
                    resume_position_if_needed();
                    break;
                }
                goto process_regular_character;
            case '\'':
                if (!in_d_quotes) {
                    in_s_quotes = !in_s_quotes;
                }
                goto process_regular_character;
            case '\"':
                if (!prev_was_backslash && !in_s_quotes) {
                    in_d_quotes = !in_d_quotes;
                }
                goto process_regular_character;
            case '#':
                if (!m_current_token_start && !prev_was_backslash && !in_s_quotes && !in_d_quotes) {
                    if (lex_comments == LexComments::Yes) {
                        begin_token();
                    }
                    while (peek() != '\n' && peek() != EOF) {
                        consume();
                    }
                    if (lex_comments == LexComments::Yes) {
                        commit_token(ShTokenType::COMMENT);
                    }
                    break;
                }
                goto process_regular_character;
            case '|':
                if (!prev_was_backslash && !in_s_quotes && !in_d_quotes) {
                    commit_token(ShTokenType::WORD);
                    begin_token();
                    consume();
                    if (peek() == '|') {
                        consume();
                        commit_token(ShTokenType::OR_IF);
                    } else {
                        commit_token(ShTokenType::Pipe);
                    }
                    break;
                }
                goto process_regular_character;
            case '&':
                if (!prev_was_backslash && !in_s_quotes && !in_d_quotes) {
                    commit_token(ShTokenType::WORD);
                    begin_token();
                    consume();
                    if (peek() == '&') {
                        consume();
                        commit_token(ShTokenType::AND_IF);
                    } else {
                        commit_token(ShTokenType::Ampersand);
                    }
                    break;
                }
                goto process_regular_character;
            case ';':
                if (!prev_was_backslash && !in_s_quotes && !in_d_quotes) {
                    commit_token(ShTokenType::WORD);
                    begin_token();
                    consume();
                    if (peek() == ';') {
                        consume();
                        commit_token(ShTokenType::DSEMI);
                    } else {
                        commit_token(ShTokenType::Semicolon);
                    }
                    break;
                }
                goto process_regular_character;
            case '>':
                if (!prev_was_backslash && !in_s_quotes && !in_d_quotes) {
                    commit_token(ShTokenType::IO_NUMBER);
                    begin_token();
                    consume();
                    if (peek() == '>') {
                        consume();
                        commit_token(ShTokenType::DGREAT);
                    } else if (peek() == '&') {
                        consume();
                        commit_token(ShTokenType::GREATAND);
                    } else if (peek() == '|') {
                        consume();
                        commit_token(ShTokenType::CLOBBER);
                    } else {
                        commit_token(ShTokenType::GreaterThan);
                    }
                    break;
                }
                goto process_regular_character;
            case '<':
                if (!prev_was_backslash && !in_s_quotes && !in_d_quotes) {
                    commit_token(ShTokenType::IO_NUMBER);
                    begin_token();
                    consume();
                    if (peek() == '<') {
                        consume();
                        bool strip_leading_tabs = false;
                        if (peek() == '-') {
                            consume();
                            commit_token(ShTokenType::DLESSDASH);
                            strip_leading_tabs = true;
                        } else if (peek() == '<') {
                            consume();
                            commit_token(ShTokenType::TLESS);
                            break;
                        } else {
                            commit_token(ShTokenType::DLESS);
                        }

                        while (peek() == '\t' || peek() == ' ') {
                            consume();
                        }

                        size_t word_start = m_position;
                        for (;;) {
                            switch (peek()) {
                                case ' ':
                                case '\t':
                                case '\n':
                                case '|':
                                case ';':
                                case '<':
                                case '>':
                                case '(':
                                case ')':
                                case '&':
                                    break;
                                case EOF:
                                    // Obviously need more characters in this case
                                    return false;
                                default:
                                    consume();
                                    continue;
                            }
                            break;
                        }

                        size_t pos_save = m_position;
                        size_t cont_row_save = m_current_row;
                        size_t cont_col_save = m_current_col;

                        auto here_end = StringView { m_input_stream + word_start, m_input_stream + m_position };
                        if (m_position == word_start) {
                            return false;
                        }

                        while (peek() != '\n') {
                            consume();
                            if (peek() == EOF) {
                                return false;
                            }
                        }

                        consume(); // Trailing `\n'

                        // Handle multipe here documents
                        resume_position_if_needed();

                        char* here_end_unescaped = strdup(String(here_end).string());
                        int ret = we_unescape(&here_end_unescaped);
                        assert(ret != WRDE_NOSPACE);

                        StringView new_here_end(const_cast<const char*>(here_end_unescaped));
                        ShValue::IoRedirect::HereDocumentQuoted here_document_quoted = new_here_end == here_end
                                                                                           ? ShValue::IoRedirect::HereDocumentQuoted::No
                                                                                           : ShValue::IoRedirect::HereDocumentQuoted::Yes;

                        size_t row_save = m_current_row;
                        size_t col_save = m_current_col;
                        size_t here_document_start = m_position;
                        size_t line_start = m_position;
                        for (;;) {
                            if (strip_leading_tabs) {
                                while (peek() == '\t') {
                                    consume();
                                }
                            }

                            if (peek() == EOF && new_here_end.size() != 0) {
                                free(here_end_unescaped);
                                return false;
                            }

                            line_start = m_position;
                            while (peek() != EOF && peek() != '\n') {
                                consume();
                            }

                            auto entire_line = StringView { m_input_stream + line_start, m_input_stream + m_position };

                            if (m_position - line_start == 0) {
                                if (new_here_end.size() == 0) {
                                    goto heredoc_line_matches;
                                }

                                consume();
                                continue; // Don't compare empty lines
                            }

                            if (entire_line == new_here_end) {
                            heredoc_line_matches:
                                free(here_end_unescaped);
                                consume();
                                break;
                            }

                            if (peek() == EOF) {
                                free(here_end_unescaped);
                                return false;
                            }

                            consume();
                        }

                        auto here_document = StringView { m_input_stream + here_document_start, m_input_stream + line_start };
                        m_tokens.add({ ShTokenType::HERE_END, { here_document, row_save, col_save, m_current_row, m_current_col } });
                        m_tokens.last().value().create_io_redirect(STDIN_FILENO, ShValue::IoRedirect::Type::HereDocument, here_document,
                                                                   here_document_quoted);
                        set_resume_position();

                        m_position = pos_save;
                        m_current_row = cont_row_save;
                        m_current_col = cont_col_save;
                    } else if (peek() == '>') {
                        consume();
                        commit_token(ShTokenType::LESSGREAT);
                    } else if (peek() == '&') {
                        consume();
                        commit_token(ShTokenType::LESSAND);
                    } else {
                        commit_token(ShTokenType::LessThan);
                    }
                    break;
                }
                goto process_regular_character;
            case '`':
                if (!prev_was_backslash && !in_s_quotes && !in_d_quotes) {
                    if (!m_current_token_start) {
                        begin_token();
                    }

                    size_t end_of_expansion = we_find_end_of_word_expansion(m_input_stream, m_position, m_input_length);
                    if (end_of_expansion == 0) {
                        return false;
                    }

                    while (m_position <= end_of_expansion) {
                        consume();
                    }
                    break;
                }
                goto process_regular_character;
            case '{':
                if (!prev_was_backslash && !in_s_quotes && !in_d_quotes && prev_was_dollar) {
                    size_t end_of_expansion = we_find_end_of_word_expansion(m_input_stream, m_position - 1, m_input_length);
                    if (end_of_expansion == 0) {
                        return false;
                    }

                    while (m_position <= end_of_expansion) {
                        consume();
                    }
                    break;
                }
                goto process_regular_character;
            case '(':
                if (!prev_was_backslash && !in_s_quotes && !in_d_quotes && !prev_was_dollar) {
                    commit_token(ShTokenType::NAME);
                    begin_token();
                    consume();
                    commit_token(ShTokenType::LeftParenthesis);
                    break;
                }

                if (!prev_was_backslash && !in_s_quotes && !in_d_quotes && prev_was_dollar) {
                    size_t end_of_expansion = we_find_end_of_word_expansion(m_input_stream, m_position - 1, m_input_length);
                    if (end_of_expansion == 0) {
                        return false;
                    }

                    while (m_position <= end_of_expansion) {
                        consume();
                    }
                    break;
                }
                goto process_regular_character;
            case ')':
                if (!prev_was_backslash && !in_s_quotes && !in_d_quotes) {
                    commit_token(ShTokenType::WORD);
                    begin_token();
                    consume();
                    commit_token(ShTokenType::RightParenthesis);
                    break;
                }
                goto process_regular_character;
            process_regular_character:
            default:
                if (!m_current_token_start) {
                    begin_token();
                }
                consume();
                break;
        }

        prev_was_backslash = false;
        prev_was_dollar = false;
    }

    assert(false);
    return false;
}
