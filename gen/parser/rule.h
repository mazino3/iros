#pragma once

#include <liim/string.h>
#include <liim/string_view.h>
#include <liim/traits.h>
#include <liim/vector.h>

class Rule {
public:
    Rule() : m_name("null") {}
    Rule(StringView name, const Vector<StringView>& components) : m_name(name), m_components(components) {}
    Rule(const Rule& other) : m_name(other.name()), m_components(other.components()), m_number(other.number()) {}
    ~Rule() {}

    StringView& name() { return m_name; }
    const StringView& name() const { return m_name; }

    int number() const { return m_number; }
    void set_number(int n) { m_number = n; }

    Vector<StringView>& components() { return m_components; }
    const Vector<StringView>& components() const { return m_components; }

    String stringify(int position = -1) const {
        String ret = "";
        char buf[50];
        snprintf(buf, 50, "[%d] ", number());
        ret += buf;
        ret += m_name;
        ret += " -> ";

        int i = 0;
        m_components.for_each([&](const StringView& part) {
            if (i++ == position) {
                ret += ". ";
            }
            ret += part;
            ret += " ";
        });

        if (i == position) {
            ret += ". ";
        }

        return ret;
    }

    bool operator==(const Rule& other) const { return this->number() == other.number(); }

private:
    StringView m_name;
    Vector<StringView> m_components;
    int m_number { 0 };
};

namespace LIIM {

template<> struct Traits<Rule> {
    static constexpr bool is_simple() { return false; }
    static unsigned int hash(const Rule& rule) { return static_cast<unsigned int>(rule.number()); }
};

}