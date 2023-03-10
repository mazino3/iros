#pragma once

#include <cli/error.h>
#include <cli/meta.h>
#include <liim/function.h>
#include <liim/option.h>
#include <liim/result.h>
#include <liim/string_view.h>
#include <liim/try.h>

namespace Cli {
class Flag {
private:
    using ParserCallback = Result<void, Error> (*)(Option<StringView>, void*);

    template<auto member>
    class FlagBuilder {};

    template<typename StructType, typename ValueType, ValueType StructType::*member>
    class FlagBuilder<member> {
    private:
        static constexpr bool is_option = IsOption<ValueType>::value;
        static constexpr bool is_bool = LIIM::IsSame<ValueType, bool>::value;
        using ParserType = IsOption<ValueType>::Type;

        constexpr FlagBuilder(ParserCallback parser, bool required = false) : m_parser(parser), m_required(required) {}

    public:
        constexpr static FlagBuilder boolean(bool) requires(is_bool) {
            return FlagBuilder([](auto, void* output_ptr) -> Result<void, Error> {
                StructType& output = *static_cast<StructType*>(output_ptr);
                output.*member = true;
                return {};
            });
        }

        constexpr static FlagBuilder optional() requires(is_option) {
            return FlagBuilder([](auto input, void* output_ptr) -> Result<void, Error> {
                StructType& output = *static_cast<StructType*>(output_ptr);
                auto value = TRY(Ext::parse<ParserType>(*input));
                output.*member = move(value);
                return {};
            });
        }

        constexpr static FlagBuilder defaulted() requires(!is_option && !is_bool) {
            return FlagBuilder([](auto input, void* output_ptr) -> Result<void, Error> {
                StructType& output = *static_cast<StructType*>(output_ptr);
                auto value = TRY(Ext::parse<ParserType>(*input));
                output.*member = move(value);
                return {};
            });
        }

        constexpr static FlagBuilder required() requires(!is_option && !is_bool) {
            return FlagBuilder(
                [](auto input, void* output_ptr) -> Result<void, Error> {
                    StructType& output = *static_cast<StructType*>(output_ptr);
                    auto value = TRY(Ext::parse<ParserType>(*input));
                    output.*member = move(value);
                    return {};
                },
                true);
        }

    public:
        constexpr FlagBuilder& short_name(char name) {
            m_short_name = name;
            return *this;
        }

        constexpr FlagBuilder& long_name(StringView name) {
            m_long_name = name;
            return *this;
        }

        constexpr FlagBuilder& description(StringView description) {
            m_description = description;
            return *this;
        }

        constexpr Flag flag() const {
            auto requires_value = !is_bool;
            return Flag(m_parser, move(m_short_name), move(m_long_name), move(m_description), requires_value, m_required);
        }

        constexpr operator Flag() const { return flag(); }

    private:
        ParserCallback m_parser;
        Option<char> m_short_name;
        Option<StringView> m_long_name;
        Option<StringView> m_description;
        bool m_required { false };
    };

public:
    template<auto member>
    static constexpr FlagBuilder<member> boolean(bool default_value = false) {
        return FlagBuilder<member>::boolean(default_value);
    }

    template<auto member>
    static constexpr FlagBuilder<member> optional() {
        return FlagBuilder<member>::optional();
    }

    template<auto member>
    static constexpr FlagBuilder<member> required() {
        return FlagBuilder<member>::required();
    }

    template<auto member>
    static constexpr FlagBuilder<member> defaulted() {
        return FlagBuilder<member>::defaulted();
    }

    constexpr Flag() = default;

    constexpr Option<char> short_name() const { return m_short_name; }
    constexpr Option<StringView> long_name() const { return m_long_name; }
    constexpr Option<StringView> description() const { return m_description; }

    constexpr bool requires_value() const { return m_requires_value; }
    constexpr bool required() const { return m_required; }

    constexpr Result<void, Error> validate(Option<StringView> value, void* output) const { return m_parser(move(value), output); }

private:
    constexpr Flag(ParserCallback parser, Option<char> short_name, Option<StringView> long_name, Option<StringView> description,
                   bool requires_value, bool required)
        : m_parser(parser)
        , m_short_name(move(short_name))
        , m_long_name(move(long_name))
        , m_description(move(description))
        , m_requires_value(requires_value)
        , m_required(required) {}

    ParserCallback m_parser;
    Option<char> m_short_name;
    Option<StringView> m_long_name;
    Option<StringView> m_description;
    bool m_requires_value { false };
    bool m_required { false };
};
}
