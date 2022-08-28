#pragma once

#include <di/concepts/lvalue_reference.h>
#include <di/meta/remove_cvref.h>
#include <di/types/in_place_type.h>
#include <di/util/tag_invoke.h>

namespace di::container {
constexpr inline struct EnableBorrowedContainer {
    // This is a helper to prevent the construction of views that would produce dangling references.
    // As such, only rvalue references which reference explicitly borrowable type are allowed through.
    // In particular, containers which "own" their data, like di::Vector and di::String, are not borrowable.
    // However, di::Span and di::StringView are.
    template<typename T>
    constexpr auto operator()(types::InPlaceType<T>) const {
        if constexpr (concepts::LValueReference<T>) {
            return true;
        } else if constexpr (concepts::TagInvocableTo<EnableBorrowedContainer, bool, types::InPlaceType<meta::RemoveCVRef<T>>>) {
            return util::tag_invoke(*this, types::in_place_type<meta::RemoveCVRef<T>>);
        } else {
            return false;
        }
    }
} enable_borrowed_container;
}
