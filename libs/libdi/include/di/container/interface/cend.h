#pragma once

#include <di/container/concepts/iterator.h>
#include <di/container/interface/enable_borrowed_container.h>
#include <di/container/interface/end.h>
#include <di/container/interface/possibly_const_container.h>
#include <di/container/meta/const_sentinel.h>
#include <di/function/tag_invoke.h>
#include <di/meta/decay.h>
#include <di/meta/remove_reference.h>
#include <di/util/forward.h>

namespace di::container {
struct CEndFunction {
    template<concepts::InputContainer T>
    requires(enable_borrowed_container(types::in_place_type<meta::RemoveCV<T>>))
    constexpr auto operator()(T&& container) const {
        return meta::ConstSentinel<decltype(container::end(detail::possibly_const_container(container)))>(
            container::end(detail::possibly_const_container(container)));
    }
};

constexpr inline auto cend = CEndFunction {};
}
