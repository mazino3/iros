#include <di/prelude.h>
#include <dius/prelude.h>
#include <test/test.h>

static void meta() {
    auto sender = di::execution::just(5);
    auto sender2 = di::execution::just(5, 10);
    auto sender3 = di::execution::just_error(5);
    auto sender4 = di::execution::just_stopped();

    static_assert(di::SameAs<di::meta::ValueTypesOf<decltype(sender)>, di::Variant<di::Tuple<int>>>);
    static_assert(di::concepts::SenderOf<decltype(sender), int>);
    static_assert(di::SameAs<di::meta::ValueTypesOf<decltype(sender2)>, di::Variant<di::Tuple<int, int>>>);
    static_assert(di::SameAs<di::meta::ValueTypesOf<decltype(sender3)>, di::meta::detail::EmptyVariant>);
    static_assert(di::SameAs<di::meta::ErrorTypesOf<decltype(sender3)>, di::Variant<int>>);
    static_assert(!di::SendsStopped<decltype(sender3)>);
    static_assert(di::SendsStopped<decltype(sender4)>);
}

TEST_CONSTEXPRX(execution, meta, meta)