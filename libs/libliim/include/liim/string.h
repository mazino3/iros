#pragma once

#include <assert.h>
#include <ctype.h>
#include <liim/container/erased_string.h>
#include <liim/container/hash.h>
#include <liim/option.h>
#include <liim/pointers.h>
#include <liim/string_view.h>
#include <liim/traits.h>
#include <liim/utilities.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace LIIM {

#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"

enum class JoinPrependDelimiter { No, Yes };

template<>
struct IsTriviallyRelocatable<String> {
    constexpr static bool value = true;
};

class String {
public:
    static SharedPtr<String> wrap_malloced_chars(char* chars);
    static __attribute__((format(printf, 1, 2))) String format(const char* format, ...);
    static String repeat(char value, size_t count);

    template<typename StringType>
    static String join(const Vector<StringType>& strings, char delimiter,
                       JoinPrependDelimiter prepend_delimiter = JoinPrependDelimiter::No) {
        String result;
        if (prepend_delimiter == JoinPrependDelimiter::Yes) {
            result += String(delimiter);
        }

        for (int i = 0; i < strings.size(); i++) {
            if (i != 0) {
                result += String(delimiter);
            }
            result += String(strings[i]);
        }
        return result;
    }

    explicit String(char c);
    explicit String(const StringView& view);
    String(const char* chars = "");
    String(const char* chars, size_t length);
    String(const String& other);
    String(String&& other);

    operator ErasedString() const {
        return ErasedString(
            LIIM::Container::Detail::ErasedStringStorage<UniquePtr<String>>(make_unique<String>(*this)),
            [](auto storage) -> const char* {
                return LIIM::Container::Detail::erased_string_storage_cast<UniquePtr<String>>(storage).value->string();
            },
            [](auto storage) -> size_t {
                return LIIM::Container::Detail::erased_string_storage_cast<UniquePtr<String>>(storage).value->size();
            },
            [](auto storage) {
                LIIM::Container::Detail::erased_string_storage_cast<UniquePtr<String>>(storage).value.~UniquePtr<String>();
            });
    }

    ~String() { clear(); }

    String& operator=(const String& other);
    String& operator=(String&& other);

    String& operator+=(const String& other) {
        insert(other, size());
        return *this;
    }

    bool operator==(const String& other) const { return this->view() == other.view(); }
    bool operator==(StringView other) const { return this->view() == other; }
    bool operator==(const char* other) const { return this->view() == other; }

    auto operator<=>(const String& other) const { return this->view() <=> other.view(); }
    auto operator<=>(StringView other) const { return this->view() <=> other; }
    auto operator<=>(const char* other) const { return this->view() <=> other; }

    char& operator[](size_t index) {
        assert(index <= size());
        return string()[index];
    }
    const char& operator[](size_t index) const {
        assert(index <= size());
        return string()[index];
    };

    size_t size() const { return is_small() ? (m_string.small.size_and_flag >> 1) : m_string.large.size; }
    bool empty() const { return size() == 0; }

    char& first() { return string()[0]; }
    const char& first() const { return string()[0]; }

    char& last() { return string()[size() - 1]; }
    const char& last() const { return string()[size() - 1]; }

    size_t capacity() const { return is_small() ? sizeof(m_string.small.data) : (m_string.large.capacity & ~1); };

    char* begin() { return string(); }
    char* end() { return string() + size(); }

    const char* begin() const { return string(); }
    const char* end() const { return string() + size(); }

    const char* cbegin() const { return string(); }
    const char* cend() const { return string() + size(); }

    char* string() { return is_small() ? m_string.small.data : m_string.large.data; }
    const char* string() const { return is_small() ? m_string.small.data : m_string.large.data; }

    StringView view() const { return StringView(string(), size()); }
    Vector<StringView> split_view(char c, SplitMethod split_method = SplitMethod::RemoveEmpty) const {
        return view().split(c, split_method);
    }
    Vector<StringView> split_view(StringView characters, SplitMethod split_method = SplitMethod::RemoveEmpty) const {
        return view().split(characters, split_method);
    }

    String first(size_t count) const { return substring(0, count); }
    String last(size_t count) const {
        assert(count <= size());
        return substring(size() - count, count);
    }
    String substring(size_t start) const { return substring(start, size() - start); }
    String substring(size_t start, size_t length) const { return String(string() + start, length); }

    bool starts_with(const StringView& needle) const { return view().starts_with(needle); }
    bool ends_with(const StringView& needle) const { return view().ends_with(needle); }

    Vector<String> split(char c, SplitMethod split_method = SplitMethod::RemoveEmpty) const { return split({ &c, 1 }, split_method); }
    Vector<String> split(StringView characters, SplitMethod split_method = SplitMethod::RemoveEmpty) const {
        auto views = view().split(characters, split_method);

        Vector<String> result;
        for (auto& view : views) {
            result.add(String(view));
        }
        return result;
    }

    void insert(const StringView& view, size_t position);
    void insert(const String& string, size_t position) { insert(string.view(), position); }
    void insert(char c, size_t position) { insert(String(c), position); }

    void clear();
    void remove_index(size_t index);
    void remove_count(size_t index, size_t count);

    void reverse();

    String& to_upper_case();
    String& to_lower_case();
    String& to_title_case();

    Option<size_t> index_of(char c) const { return view().index_of(c); }
    Option<size_t> last_index_of(char c) const { return view().last_index_of(c); }

    void swap(String& other);

private:
    bool is_small() const { return m_string.small.size_and_flag & 1; }

    char* allocate_string(size_t capacity) { return static_cast<char*>(malloc(capacity)); }
    size_t round_up_capacity(size_t requested_capacity);
    void set_capacity(size_t capacity) { m_string.large.capacity = capacity; }
    void set_size(size_t size);

    union {
        struct {
            size_t capacity;
            size_t size;
            char* data;
        } large;
        struct {
            unsigned char size_and_flag;
            char data[sizeof(size_t) + sizeof(size_t) + sizeof(char*) - 1];
        } small;
    } m_string;
};

inline SharedPtr<String> String::wrap_malloced_chars(char* chars) {
    auto ret = make_shared<String>();

    size_t len = strlen(chars);
    ret->set_capacity(ret->round_up_capacity(len + 1));
    ret->set_size(len);
    ret->m_string.large.data = chars;
    return ret;
}

inline String String::format(const char* format, ...) {
    char buffer[2048];
    va_list args;
    va_start(args, format);
    int size = vsnprintf(buffer, 2047, format, args);
    va_end(args);
    return String(buffer, size);
}

inline String String::repeat(char value, size_t count) {
    String ret;
    for (size_t i = 0; i < count; i++) {
        ret += String(value);
    }
    return ret;
}

inline String::String(char c) {
    m_string.small.size_and_flag = 0b11;
    string()[0] = c;
    string()[1] = '\0';
}

inline String::String(const StringView& view) {
    size_t needed_capacity = view.size() + 1;
    if (needed_capacity >= sizeof(m_string.small.data)) {
        set_capacity(round_up_capacity(needed_capacity));
        m_string.large.data = allocate_string(capacity());
    } else {
        m_string.small.size_and_flag = 1;
    }

    memcpy(string(), view.data(), view.size());
    string()[view.size()] = '\0';

    set_size(needed_capacity - 1);
}

inline String::String(const char* chars) : String(StringView(chars)) {}

inline String::String(const char* chars, size_t size) : String(StringView(chars, size)) {}

inline String::String(const String& other) : String(other.string()) {}

inline String::String(String&& other) {
    if (other.is_small()) {
        this->m_string.small.size_and_flag = other.m_string.small.size_and_flag;
        memcpy(this->string(), other.string(), other.size() + 1);
    } else {
        this->m_string.large = other.m_string.large;
        other.m_string.large.data = nullptr;
    }
    other.clear();
}

inline String& String::operator=(const String& other) {
    if (this != &other) {
        String temp(other);
        swap(temp);
    }
    return *this;
}

inline String& String::operator=(String&& other) {
    if (this != &other) {
        String temp(move(other));
        swap(temp);
    }
    return *this;
}

inline void String::insert(const StringView& to_insert, size_t position) {
    assert(position <= size());
    size_t new_size = size() + to_insert.size();
    if (new_size + 1 > capacity()) {
        const bool was_small = is_small();
        size_t new_capacity = round_up_capacity(new_size + 1);
        char* allocated_string = allocate_string(new_capacity);

        memcpy(allocated_string, string(), position);
        memcpy(allocated_string + position, to_insert.data(), to_insert.size());
        memcpy(allocated_string + position + to_insert.size(), string() + position, size() + 1 - position);

        set_capacity(new_capacity);
        set_size(new_size);
        if (!was_small) {
            free(m_string.large.data);
        }
        m_string.large.data = allocated_string;
        return;
    }

    memmove(string() + position + to_insert.size(), string() + position, size() + 1 - position);
    memcpy(string() + position, to_insert.data(), to_insert.size());
    set_size(new_size);
}

inline void String::clear() {
    if (!is_small()) {
        free(m_string.large.data);
    }

    m_string.small.size_and_flag = 1;
    m_string.small.data[0] = '\0';
}

inline void String::remove_index(size_t index) {
    assert(index < size());
    memmove(string() + index, string() + index + 1, size() - index);
    set_size(size() - 1);
}

inline void String::remove_count(size_t index, size_t count) {
    assert(index + count <= size());
    memmove(string() + index, string() + index + count, size() - index - count + 1);
    set_size(size() - count);
}

inline void String::reverse() {
    for (size_t i = 0; i < size() / 2; i++) {
        LIIM::swap(string()[i], string()[size() - i - 1]);
    }
}

inline String& String::to_upper_case() {
    for (size_t i = 0; i < size(); i++) {
        if (islower(string()[i])) {
            string()[i] = _toupper(string()[i]);
        }
    }

    return *this;
}

inline String& String::to_lower_case() {
    for (size_t i = 0; i < size(); i++) {
        if (isupper(string()[i])) {
            string()[i] = _tolower(string()[i]);
        }
    }

    return *this;
}

inline String& String::to_title_case() {
    for (size_t i = 0; i < size(); i++) {
        if (i == 0) {
            string()[i] = toupper(string()[i]);
            continue;
        }

        if (string()[i] == '_') {
            string()[i + 1] = toupper(string()[i + 1]);
            memmove(string() + i, string() + i + 1, size() - i);
            set_size(size() - 1);
        } else {
            string()[i] = tolower(string()[i]);
        }
    }

    return *this;
}

inline void String::swap(String& other) {
    if (this == &other) {
        return;
    }

    if (this->is_small() && other.is_small()) {
        char temp_chars[sizeof(m_string.small.data)];
        memcpy(temp_chars, other.string(), other.size() + 1);
        unsigned char other_size_and_flags = other.m_string.small.size_and_flag;

        other.m_string.small.size_and_flag = this->m_string.small.size_and_flag;
        memcpy(other.string(), this->string(), this->size() + 1);

        this->m_string.small.size_and_flag = other_size_and_flags;
        memcpy(this->string(), temp_chars, this->size() + 1);
        return;
    }

    if (this->is_small() && !other.is_small()) {
        other.swap(*this);
        return;
    }

    if (!this->is_small() && other.is_small()) {
        char temp_chars[sizeof(m_string.small.data)];
        memcpy(temp_chars, other.string(), other.size() + 1);
        unsigned char other_size_and_flags = other.m_string.small.size_and_flag;

        other.m_string.large = this->m_string.large;

        this->m_string.small.size_and_flag = other_size_and_flags;
        memcpy(this->string(), temp_chars, this->size() + 1);
        return;
    }

    LIIM::swap(this->m_string.large.capacity, other.m_string.large.capacity);
    LIIM::swap(this->m_string.large.size, other.m_string.large.size);
    LIIM::swap(this->m_string.large.data, other.m_string.large.data);
}

inline size_t String::round_up_capacity(size_t requested_capacity) {
    size_t capacity = 64;
    while (capacity < requested_capacity) {
        capacity <<= 1;
    }
    return capacity;
}

inline void String::set_size(size_t size) {
    assert(size < capacity());
    if (is_small()) {
        m_string.small.size_and_flag = (size << 1) | 1;
    } else {
        m_string.large.size = size;
    }
}

#pragma GCC diagnostic pop

template<typename T>
void swap(String& a, String& b) {
    a.swap(b);
}

template<>
struct Traits<String> {
    static constexpr bool is_simple() { return false; }
    static unsigned int hash(const String& s) {
        unsigned int v = 0;
        for (size_t i = 0; i < s.size(); i++) {
            v += ~s[i];
            v ^= s[i];
        }

        return v;
    }
};

namespace Container::Hash {
    template<>
    struct HashFunction<String> {
        static void hash(Hasher& hasher, const String& string) { hasher.add({ string.string(), string.size() }); }

        using Matches = Tuple<const char*, StringView>;
    };
}
}

inline LIIM::String operator""s(const char* string, size_t size) {
    return { string, size };
}

using LIIM::JoinPrependDelimiter;
using LIIM::String;
