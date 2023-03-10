#pragma once

#include <errno.h>
#include <ext/forward.h>
#include <liim/container/new_vector.h>
#include <liim/error.h>
#include <liim/format.h>
#include <liim/forward.h>
#include <liim/result.h>
#include <liim/source_location.h>
#include <liim/string.h>
#include <liim/try.h>
#include <liim/utilities.h>
#include <liim/variant.h>
#include <liim/vector.h>

namespace Ext {
template<typename T>
concept ClassErrorType = requires(const T& x) {
    { x.to_message() } -> LIIM::SameAs<String>;
};

template<typename T>
concept FreeErrorType = requires(const T& x) {
    { to_message(x) } -> LIIM::SameAs<String>;
};

template<ClassErrorType T>
String to_message_impl(const T& error) {
    return error.to_message();
}

template<FreeErrorType T>
String to_message_impl(const T& error) {
    return to_message(error);
}

template<typename T>
concept ErrorType = ClassErrorType<T> || FreeErrorType<T>;

template<typename T, typename C, typename E = LIIM::InvokeResult<C, const T&>::type::ErrorType>
Result<void, Vector<E>> collect_errors(const Vector<T>& input, C mapper) {
    Vector<E> output;
    for (auto& t : input) {
        auto result = mapper(t);
        if (result.is_error()) {
            output.add(move(result.error()));
        }
    }
    if (!output.empty()) {
        return Err(move(output));
    }
    return {};
}

template<typename Collection, typename C,
         typename R = LIIM::InvokeResult<C, decltype(*LIIM::declval<const Collection&>().begin())>::type::ErrorType>
Result<void, R> stop_on_error(const Collection& input, C mapper) {
    for (auto& t : input) {
        TRY(mapper(t));
    }
    return {};
}
}

namespace LIIM {
template<Ext::ErrorType T>
String to_message(const Vector<T>& errors) {
    Vector<String> messages;
    for (auto& error : errors) {
        messages.add(Ext::to_message_impl(error));
    }
    return String::join(move(messages), '\n');
}

template<typename... Types>
String to_message(const Variant<Types...>& error) {
    return error.visit([](auto&& error) {
        return Ext::to_message_impl(error);
    });
}
}

namespace LIIM::Error {
template<typename T, typename Domain>
String to_message(const Error<T, Domain>& error) {
    return String(error.message());
}
}

namespace LIIM::Format {
template<Ext::ErrorType T>
struct Formatter<T> : Formatter<StringView> {
    void format(const T& error, FormatContext& context) { return format_string_view(Ext::to_message_impl(error).view(), context); }
};
}
