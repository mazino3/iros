#pragma once

#ifdef USERLAND_NATIVE
#include "../include/regex.h"
#else
#include <regex.h>
#endif /* USERLAND_NATIVE */

#include "generic_regex_parser.h"
#include "regex_lexer.h"
#include "regex_value.h"

class RegexParser final : public GenericRegexParser<RegexValue> {
public:
    using Token = GenericToken<RegexTokenType, RegexValue>;

    RegexParser(RegexLexer& lexer, int cflags) : GenericRegexParser<RegexValue>(lexer), m_flags(cflags) {}

    RegexLexer& lexer() { return static_cast<RegexLexer&>(this->GenericParser::lexer()); }
    const RegexLexer& lexer() const { return const_cast<RegexParser&>(*this).lexer(); }

    int error_code() const { return m_error_code; }

    const RegexValue& result() const { return peek_value_stack(); };

    virtual RegexValue reduce_duplicate_symbol$asterisk(RegexValue& v) override {
        assert(v.is<TokenInfo>());
        return { DuplicateCount { DuplicateCount::Type::AtLeast, 0, __INT_MAX__ } };
    }

private:
    virtual void on_error(RegexTokenType) override { m_error_code = REG_BADPAT; }

    virtual RegexValue reduce_duplicate_symbol$plus(RegexValue&) override {
        return { DuplicateCount { DuplicateCount::Type::AtLeast, 1, __INT_MAX__ } };
    }

    virtual RegexValue reduce_duplicate_symbol$questionmark(RegexValue&) override {
        return { DuplicateCount { DuplicateCount::Type::Between, 0, 1 } };
    }

    virtual RegexValue reduce_duplicate_symbol$leftcurlybrace_duplicatecount_rightcurlybrace(RegexValue&, RegexValue& c,
                                                                                             RegexValue&) override {
        assert(c.is<TokenInfo>());
        int min;
        // NOTE: sscanf is safe, it will stop reading once ints stop and this must be terminated
        //       eventually by either a '\0', ',', or '}' in all cases.
        if (sscanf(c.as<TokenInfo>().text.data(), "%i", &min) != 1) {
            this->set_error();
            m_error_code = REG_BADBR;
            return {};
        }
        return { DuplicateCount { DuplicateCount::Type::Exact, min, min } };
    }

    virtual RegexValue reduce_duplicate_symbol$leftcurlybrace_duplicatecount_comma_rightcurlybrace(RegexValue&, RegexValue& c, RegexValue&,
                                                                                                   RegexValue&) override {
        assert(c.is<TokenInfo>());
        int min;
        // NOTE: sscanf is safe, it will stop reading once ints stop and this must be terminated
        //       eventually by either a '\0', ',', or '}' in all cases.
        if (sscanf(c.as<TokenInfo>().text.data(), "%i", &min) != 1) {
            this->set_error();
            m_error_code = REG_BADBR;
            return {};
        }
        return { DuplicateCount { DuplicateCount::Type::AtLeast, min, __INT_MAX__ } };
    }

    virtual RegexValue
    reduce_duplicate_symbol$leftcurlybrace_duplicatecount_comma_duplicatecount_rightcurlybrace(RegexValue&, RegexValue& c, RegexValue&,
                                                                                               RegexValue& d, RegexValue&) override {
        assert(c.is<TokenInfo>());
        assert(d.is<TokenInfo>());
        int min;
        int max;
        // NOTE: sscanf is safe, it will stop reading once ints stop and this must be terminated
        //       eventually by either a '\0', ',', or '}' in all cases.
        if (sscanf(c.as<TokenInfo>().text.data(), "%d", &min) != 1) {
            this->set_error();
            m_error_code = REG_BADBR;
            return {};
        }
        if (sscanf(d.as<TokenInfo>().text.data(), "%d", &max) != 1) {
            this->set_error();
            m_error_code = REG_BADBR;
            return {};
        }
        if (max < min) {
            this->set_error();
            m_error_code = REG_BADBR;
            return {};
        }
        return { DuplicateCount { DuplicateCount::Type::Between, min, max } };
    }

    virtual RegexValue reduce_regex_one_char$ordinarycharacter(RegexValue& ch) override {
        assert(ch.is<TokenInfo>());
        return { SharedPtr<RegexSingleExpression>(
            new RegexSingleExpression { RegexSingleExpression::Type::OrdinaryCharacter, { *ch.as<TokenInfo>().text.data() }, {} }) };
    }

    virtual RegexValue reduce_regex_one_char$quotedcharacter(RegexValue& ch) override {
        assert(ch.is<TokenInfo>());
        return { SharedPtr<RegexSingleExpression>(
            new RegexSingleExpression { RegexSingleExpression::Type::QuotedCharacter, { *ch.as<TokenInfo>().text.data() }, {} }) };
    }

    virtual RegexValue reduce_regex_one_char$period(RegexValue&) override {
        return { SharedPtr<RegexSingleExpression>(new RegexSingleExpression { RegexSingleExpression::Type::Any, { '.' }, {} }) };
    }

    virtual RegexValue reduce_regex_one_char$bracket_expression(RegexValue& v) override {
        assert(v.is<BracketExpression>());
        return { SharedPtr<RegexSingleExpression>(
            new RegexSingleExpression { RegexSingleExpression::Type::BracketExpression, { move(v.as<BracketExpression>()) }, {} }) };
    }

    virtual RegexValue reduce_expression$regex_one_char(RegexValue& se) override {
        assert(se.is<SharedPtr<RegexSingleExpression>>());
        return se;
    }

    virtual RegexValue reduce_expression$backreference(RegexValue& v) override {
        assert(v.is<TokenInfo>());
        int group_index = static_cast<int>(v.as<TokenInfo>().text.first() - '0');
        if (group_index > lexer().group_at_position(v.as<TokenInfo>().position)) {
            m_error_code = REG_ESUBREG;
            set_error();
            return {};
        }
        return { SharedPtr<RegexSingleExpression>(
            new RegexSingleExpression { RegexSingleExpression::Type::Backreference, group_index, {} }) };
    }

    virtual RegexValue reduce_expression$carrot(RegexValue&) override {
        return { SharedPtr<RegexSingleExpression>(new RegexSingleExpression { RegexSingleExpression::Type::LeftAnchor, {}, {} }) };
    }

    virtual RegexValue reduce_expression$dollar(RegexValue&) override {
        return { SharedPtr<RegexSingleExpression>(new RegexSingleExpression { RegexSingleExpression::Type::RightAnchor, {}, {} }) };
    }

    virtual RegexValue reduce_expression$leftparenthesis_regex_rightparenthesis(RegexValue& a, RegexValue& v, RegexValue&) override {
        assert(a.is<TokenInfo>());
        assert(v.is<ParsedRegex>());
        v.as<ParsedRegex>().index = lexer().group_at_position(a.as<TokenInfo>().position);
        return { SharedPtr<RegexSingleExpression>(
            new RegexSingleExpression { RegexSingleExpression::Type::Group, move(v.as<ParsedRegex>()), {} }) };
    }

    virtual RegexValue reduce_expression$expression_duplicate_symbol(RegexValue& v, RegexValue& c) override {
        assert(v.is<SharedPtr<RegexSingleExpression>>());
        assert(c.is<DuplicateCount>());
        if (v.as<SharedPtr<RegexSingleExpression>>()->duplicate.has_value()) {
            m_error_code = REG_BADPAT;
            set_error();
            return {};
        }
        v.as<SharedPtr<RegexSingleExpression>>()->duplicate = { move(c.as<DuplicateCount>()) };
        return v;
    }

    virtual RegexValue reduce_regex_branch$expression(RegexValue& v) override {
        assert(v.is<SharedPtr<RegexSingleExpression>>());
        Vector<SharedPtr<RegexSingleExpression>> exps;
        exps.add(v.as<SharedPtr<RegexSingleExpression>>());
        return { RegexExpression { move(exps) } };
    }

    virtual RegexValue reduce_regex_branch$regex_branch_expression(RegexValue& exps, RegexValue& v) override {
        assert(exps.is<RegexExpression>());
        assert(v.is<SharedPtr<RegexSingleExpression>>());
        exps.as<RegexExpression>().parts.add(v.as<SharedPtr<RegexSingleExpression>>());
        return exps;
    }

    virtual RegexValue reduce_regex$regex_branch(RegexValue& v) override {
        assert(v.is<RegexExpression>());
        Vector<RegexExpression> alts;
        alts.add(move(v.as<RegexExpression>()));
        return { ParsedRegex { move(alts), 0 } };
    }

    virtual RegexValue reduce_regex$regex_pipe_regex_branch(RegexValue& exps, RegexValue&, RegexValue& v) override {
        assert(exps.is<ParsedRegex>());
        assert(v.is<RegexExpression>());
        exps.as<ParsedRegex>().alternatives.add(move(v.as<RegexExpression>()));
        return exps;
    }

    virtual RegexValue reduce_bracket_expression$leftsquarebracket_matching_list_rightsquarebracket(RegexValue&, RegexValue& v,
                                                                                                    RegexValue&) override {
        assert(v.is<BracketExpression>());
        return move(v);
    }

    virtual RegexValue reduce_bracket_expression$leftsquarebracket_nonmatching_list_rightsquarebracket(RegexValue&, RegexValue& v,
                                                                                                       RegexValue&) override {
        assert(v.is<BracketExpression>());
        return move(v);
    }

    virtual RegexValue reduce_matching_list$bracket_list(RegexValue& v) override {
        assert(v.is<BracketExpression>());
        return move(v);
    }

    virtual RegexValue reduce_nonmatching_list$carrot_bracket_list(RegexValue&, RegexValue& v) override {
        assert(v.is<BracketExpression>());
        v.as<BracketExpression>().inverted = true;
        return move(v);
    }

    virtual RegexValue reduce_bracket_list$follow_list(RegexValue& v) override {
        assert(v.is<BracketExpression>());
        return v;
    }

    virtual RegexValue reduce_bracket_list$follow_list_minus(RegexValue& v, RegexValue&) override {
        assert(v.is<BracketExpression>());
        v.as<BracketExpression>().list.add(
            BracketItem { BracketItem::Type::SingleExpression,
                          { BracketSingleExpression { BracketSingleExpression::Type::SingleCollatingSymbol, "." } } });
        return v;
    }

    virtual RegexValue reduce_follow_list$expression_term(RegexValue& v) override {
        assert(v.is<BracketItem>());
        Vector<BracketItem> items;
        items.add(move(v.as<BracketItem>()));
        return { BracketExpression { move(items), false } };
    }

    virtual RegexValue reduce_follow_list$follow_list_expression_term(RegexValue& vs, RegexValue& v) override {
        assert(vs.is<BracketExpression>());
        assert(v.is<BracketItem>());
        vs.as<BracketExpression>().list.add(move(v.as<BracketItem>()));
        return move(vs);
    }

    virtual RegexValue reduce_expression_term$single_expression(RegexValue& v) override {
        assert(v.is<BracketSingleExpression>());
        return { BracketItem { BracketItem::Type::SingleExpression, { move(v.as<BracketSingleExpression>()) } } };
    }

    virtual RegexValue reduce_expression_term$range_expression(RegexValue& v) override {
        assert(v.is<BracketRangeExpression>());
        return { BracketItem { BracketItem::Type::RangeExpression, { move(v.as<BracketRangeExpression>()) } } };
    }

    virtual RegexValue reduce_single_expression$end_range(RegexValue& v) override {
        assert(v.is<BracketSingleExpression>());
        return move(v);
    }

    virtual RegexValue reduce_single_expression$character_class(RegexValue& v) override {
        assert(v.is<BracketSingleExpression>());
        return move(v);
    }

    virtual RegexValue reduce_single_expression$equivalence_class(RegexValue& v) override {
        assert(v.is<BracketSingleExpression>());
        return move(v);
    }

    virtual RegexValue reduce_range_expression$start_range_end_range(RegexValue& s, RegexValue& e) override {
        assert(s.is<BracketSingleExpression>());
        assert(e.is<BracketSingleExpression>());
        return { BracketRangeExpression { s.as<BracketSingleExpression>().expression, e.as<BracketSingleExpression>().expression } };
    }

    virtual RegexValue reduce_range_expression$start_range_minus(RegexValue& s, RegexValue& e) override {
        assert(s.is<BracketSingleExpression>());
        assert(e.is<TokenInfo>());
        return { BracketRangeExpression { s.as<BracketSingleExpression>().expression, e.as<TokenInfo>().text } };
    }

    virtual RegexValue reduce_start_range$end_range_minus(RegexValue& v, RegexValue&) override {
        assert(v.is<BracketSingleExpression>());
        return move(v);
    }

    virtual RegexValue reduce_end_range$collatesingleelement(RegexValue& v) override {
        assert(v.is<TokenInfo>());
        return { BracketSingleExpression { BracketSingleExpression::Type::SingleCollatingSymbol, v.as<TokenInfo>().text } };
    }

    virtual RegexValue reduce_end_range$collating_symbol(RegexValue& v) override {
        assert(v.is<BracketSingleExpression>());
        return move(v);
    }

    virtual RegexValue reduce_collating_symbol$leftsquarebracketperiod_collatesingleelement_periodrightsquarebracket(RegexValue&,
                                                                                                                     RegexValue& v,
                                                                                                                     RegexValue&) override {
        assert(v.is<TokenInfo>());
        return { BracketSingleExpression { BracketSingleExpression::Type::GroupedCollatingSymbol, v.as<TokenInfo>().text } };
    }

    virtual RegexValue
    reduce_collating_symbol$leftsquarebracketperiod_collatemultipleelements_periodrightsquarebracket(RegexValue&, RegexValue& v,
                                                                                                     RegexValue&) override {
        assert(v.is<TokenInfo>());
        return { BracketSingleExpression { BracketSingleExpression::Type::GroupedCollatingSymbol, v.as<TokenInfo>().text } };
    }

    virtual RegexValue reduce_collating_symbol$leftsquarebracketperiod_metacharacter_periodrightsquarebracket(RegexValue&, RegexValue& v,
                                                                                                              RegexValue&) override {
        assert(v.is<TokenInfo>());
        return { BracketSingleExpression { BracketSingleExpression::Type::GroupedCollatingSymbol, v.as<TokenInfo>().text } };
    }

    virtual RegexValue reduce_equivalence_class$leftsquarebracketequal_collatesingleelement_equalrightsquarebracket(RegexValue&,
                                                                                                                    RegexValue& v,
                                                                                                                    RegexValue&) override {
        assert(v.is<TokenInfo>());
        return { BracketSingleExpression { BracketSingleExpression::Type::EquivalenceClass, v.as<TokenInfo>().text } };
    }

    virtual RegexValue
    reduce_equivalence_class$leftsquarebracketequal_collatemultipleelements_equalrightsquarebracket(RegexValue&, RegexValue& v,
                                                                                                    RegexValue&) override {
        assert(v.is<TokenInfo>());
        return { BracketSingleExpression { BracketSingleExpression::Type::EquivalenceClass, v.as<TokenInfo>().text } };
    }

    virtual RegexValue reduce_character_class$leftsquarebracketcolon_classname_colonrightsquarebracket(RegexValue&, RegexValue& v,
                                                                                                       RegexValue&) override {
        assert(v.is<TokenInfo>());
        return { BracketSingleExpression { BracketSingleExpression::Type::CharacterClass, v.as<TokenInfo>().text } };
    }

private:
    int m_error_code { 0 };
    int m_flags { 0 };
};
