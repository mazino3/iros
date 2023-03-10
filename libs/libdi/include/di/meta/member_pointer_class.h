#pragma once

#include <di/concepts/member_pointer.h>
#include <di/meta/remove_cv.h>
#include <di/meta/type_constant.h>

namespace di::meta {
namespace detail {
    template<typename T>
    struct MemberPointerClassHelper {};

    template<typename Value, typename Class>
    struct MemberPointerClassHelper<Value Class::*> : TypeConstant<Class> {};
}

template<concepts::MemberPointer T>
using MemberPointerClass = detail::MemberPointerClassHelper<RemoveCV<T>>::Type;
}
