#include <ext/json.h>

int main() {
    using namespace Ext::Json;
    using Ext::Json::Array;

    Value v = Number(10);
    assert(v.is<Number>());

    auto o = Object();
    o.put<String>("hello", "world");
    o.put<Number>("x", 4);

    assert(!o.get_as<Number>("w"));
    assert(!o.get_as<Number>("hello"));
    assert(*o.get_as<String>("hello") == "world");
    assert(o.get_or<Number>("q", 5) == 5);

    auto a = Array();
    a.add(Value(String("asdf")));
    a.add(Value(String("qwer")));
    o.put<Array>("a", move(a));

    Value x = move(o);
    assert(x.is<Object>());

    auto s = stringify(x);
    printf("%s\n", s.string());

    auto xx = parse(s.view());
    assert(xx.has_value());
    printf("%s\n", stringify(xx.value(), 0).string());

    return 0;
}
