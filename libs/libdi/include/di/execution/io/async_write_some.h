#pragma once

#include <di/execution/concepts/scheduler.h>
#include <di/execution/concepts/sender_of.h>
#include <di/execution/types/prelude.h>
#include <di/function/tag_invoke.h>
#include <di/types/prelude.h>

namespace di::execution {
namespace async_write_some_ns {
    struct Function {
        template<typename File>
        requires(concepts::TagInvocable<Function, File, Span<Byte const>, Optional<u64>>)
        concepts::SenderOf<NoEnv, size_t> auto operator()(File&& handle, Span<Byte const> buffer, Optional<u64> offset = {}) const {
            return function::tag_invoke(*this, util::forward<File>(handle), buffer, offset);
        }
    };
}

constexpr inline auto async_write_some = async_write_some_ns::Function {};
}