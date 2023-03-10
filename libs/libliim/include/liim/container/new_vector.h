#pragma once

#include <liim/compare.h>
#include <liim/container/allocator.h>
#include <liim/container/container.h>
#include <liim/container/iterator/continuous_iterator.h>
#include <liim/error/common_result.h>
#include <liim/error/error.h>
#include <liim/error/system_domain.h>
#include <liim/format.h>
#include <liim/initializer_list.h>
#include <liim/option.h>
#include <liim/result.h>
#include <liim/span.h>

namespace LIIM::Container {
template<typename T, AllocatorOf<T> Alloc = StandardAllocator<T>>
class NewVector;

template<typename VectorType>
class NewVectorIterator : public ContinuousIteratorAdapter<NewVectorIterator<VectorType>> {
public:
    using VectorValueType = VectorType::ValueType;
    using ValueType = Conditional<IsConst<VectorType>::value, const VectorValueType&, VectorValueType&>::type;

    constexpr operator NewVectorIterator<const VectorType>() requires(!IsConst<VectorType>::value) {
        return NewVectorIterator<const VectorType>(*m_vector, this->index());
    }

    constexpr decltype(auto) operator*() const { return (*m_vector)[this->index()]; }
    constexpr decltype(auto) operator->() const { return &(*m_vector)[this->index()]; }

private:
    explicit constexpr NewVectorIterator(VectorType& vector, size_t index)
        : ContinuousIteratorAdapter<NewVectorIterator>(index), m_vector(&vector) {}

    template<typename T>
    friend class NewVectorIterator;

    template<typename T, AllocatorOf<T> Alloc>
    friend class NewVector;

    VectorType* m_vector;
};

template<typename T, AllocatorOf<T> Alloc>
class NewVector {
public:
    using ValueType = T;
    using ReserveResult = void;

    using Iterator = NewVectorIterator<NewVector<T, Alloc>>;
    using ConstIterator = NewVectorIterator<const NewVector<T, Alloc>>;

    template<typename V, typename... Args>
    using InsertResult = CommonResult<V, CreateAtResultDefault<T, Args...>>;

    template<typename V, ::Iterator Iter>
    using IteratorInsertResult = InsertResult<V, IteratorValueType<Iter>>;

    constexpr NewVector() = default;
    constexpr NewVector(NewVector&&);

    constexpr static InsertResult<NewVector, const T&> create(std::initializer_list<T> list) requires(Copyable<T>) {
        auto result = NewVector {};
        result.insert(result.begin(), list);
        return result;
    }

    template<::Iterator Iter>
    constexpr static IteratorInsertResult<NewVector, Iter> create(Iter start, Iter end, Option<size_t> known_size = {});

    constexpr ~NewVector() { clear(); }

    constexpr auto clone() const requires(Cloneable<T> || FalliblyCloneable<T>) {
        return collect<NewVector<T, Alloc>>(transform(*this, [](const auto& v) {
            return ::clone(v);
        }));
    }

    constexpr NewVector& operator=(NewVector&&);

    constexpr decltype(auto) assign(std::initializer_list<T> list) requires(Copyable<T>) { return assign(list.begin(), list.end()); }

    template<::Iterator Iter>
    constexpr IteratorInsertResult<NewVector&, Iter> assign(Iter start, Iter end, Option<size_t> known_size = {});

    constexpr Option<T&> at(size_t index);
    constexpr Option<const T&> at(size_t index) const;

    constexpr T* data() { return m_data; }
    constexpr const T* data() const { return m_data; }

    constexpr Span<T> span() { return { data(), size() }; }
    constexpr Span<const T> span() const { return { data(), size() }; }

    constexpr T& operator[](size_t index) {
        assert(index < size());
        return m_data[index];
    }
    constexpr const T& operator[](size_t index) const {
        assert(index < size());
        return m_data[index];
    }

    constexpr T& front() { return (*this)[0]; }
    constexpr const T& front() const { return (*this)[0]; }

    constexpr T& back() { return (*this)[size() - 1]; }
    constexpr const T& back() const { return (*this)[size() - 1]; }

    constexpr bool empty() const { return size() == 0; }
    constexpr size_t size() const { return m_size; }
    constexpr size_t capacity() const { return m_capacity; }

    constexpr auto begin() { return Iterator(*this, 0lu); }
    constexpr auto end() { return Iterator(*this, size()); }
    constexpr auto begin() const { return ConstIterator(*this, 0lu); }
    constexpr auto end() const { return ConstIterator(*this, size()); }
    constexpr auto cbegin() const { return begin(); }
    constexpr auto cend() const { return end(); }
    constexpr auto rbegin() { return ReverseIterator(end()); }
    constexpr auto rend() { return ReverseIterator(Iterator(begin())); }
    constexpr auto rbegin() const { return ReverseIterator(ConstIterator(end())); }
    constexpr auto rend() const { return ReverseIterator(ConstIterator(begin())); }
    constexpr auto crbegin() const { return ReverseIterator(ConstIterator(end())); }
    constexpr auto crend() const { return ReverseIterator(ConstIterator(begin())); }

    constexpr auto iterator(size_t index) { return begin() + index; }
    constexpr auto iterator(size_t index) const { return begin() + index; }
    constexpr auto citerator(size_t index) const { return begin() + index; }

    constexpr size_t iterator_index(ConstIterator iterator) const { return iterator - begin(); }

    constexpr ReserveResult reserve(size_t capacity);

    constexpr void clear();

    template<typename U>
    constexpr auto insert(ConstIterator position, U&& value) {
        return emplace(position, forward<U>(value));
    }

    constexpr auto insert(ConstIterator position, std::initializer_list<T> list) requires(Copyable<T>) {
        return insert(iterator_index(position), list);
    }

    template<::Iterator Iter>
    constexpr auto insert(ConstIterator position, Iter begin, Iter end, Option<size_t> known_size = {}) {
        return insert(iterator_index(position), move(begin), move(end), known_size);
    }

    template<typename U>
    constexpr auto insert(size_t index, U&& value) {
        return emplace(index, forward<U>(value));
    }

    constexpr auto insert(size_t index, std::initializer_list<T> list) requires(Copyable<T>) {
        return insert(index, list.begin(), list.end());
    }

    template<::Iterator Iter>
    constexpr IteratorInsertResult<Iterator, Iter> insert(size_t index, Iter begin, Iter end, Option<size_t> known_size = {});

    constexpr Iterator erase(ConstIterator position) { return erase(position, position + 1); }
    constexpr Iterator erase(ConstIterator start, ConstIterator end) { return erase_count(iterator_index(start), end - start); }
    constexpr Iterator erase_count(size_t index, size_t count);
    constexpr Iterator erase_unstable(ConstIterator position) { return erase_unstable(iterator_index(position)); }
    constexpr Iterator erase_unstable(size_t index) {
        ::swap((*this)[index], back());
        pop_back();
        return iterator(index);
    }

    template<typename... Args>
    constexpr auto emplace(ConstIterator position, Args&&... args) {
        return emplace(iterator_index(position), forward<Args>(args)...);
    }
    template<typename... Args>
    constexpr InsertResult<Iterator, Args...> emplace(size_t index, Args&&... args);

    constexpr decltype(auto) push_back(const T& value) requires(Copyable<T>) { return emplace_back(value); }
    constexpr decltype(auto) push_back(T&& value) { return emplace_back(move(value)); }

    template<typename... Args>
    constexpr InsertResult<T&, Args...> emplace_back(Args&&... args) {
        if constexpr (CreateableFrom<T, Args...>) {
            return *emplace(end(), forward<Args>(args)...);
        } else {
            return result_and_then(emplace(end(), forward<Args>(args)...), [](auto it) -> T& {
                return *it;
            });
        }
    }

    constexpr Option<T> pop_back();

    constexpr InsertResult<void, const T&> resize(size_t count, const T& value = T()) requires(Copyable<T>);

    constexpr void assume_size(size_t size) { m_size = size; }

    constexpr void swap(NewVector&);

    template<EqualComparableWith<T> U>
    constexpr bool operator==(const NewVector<U>& other) const requires(EqualComparable<T>);
    template<ComparableWith<T> U>
    constexpr auto operator<=>(const NewVector<U>& other) const requires(Comparable<T>);

private:
    constexpr void move_objects(T* destination, T* source, size_t count);
    constexpr ReserveResult grow_to(size_t new_size);

    size_t m_size { 0 };
    size_t m_capacity { 0 };
    T* m_data { nullptr };
};

template<typename T, AllocatorOf<T> Alloc>
constexpr NewVector<T, Alloc>::NewVector(NewVector<T, Alloc>&& other)
    : m_size(exchange(other.m_size, 0)), m_capacity(exchange(other.m_capacity, 0)), m_data(exchange(other.m_data, nullptr)) {}

template<typename T, AllocatorOf<T> Alloc>
template<Iterator Iter>
constexpr auto NewVector<T, Alloc>::create(Iter start, Iter end, Option<size_t> known_size) -> IteratorInsertResult<NewVector, Iter> {
    auto result = NewVector {};
    result.insert(result.begin(), move(start), move(end), known_size);
    return result;
}

template<typename T, AllocatorOf<T> Alloc>
constexpr NewVector<T, Alloc>& NewVector<T, Alloc>::operator=(NewVector<T, Alloc>&& other) {
    if (this != &other) {
        NewVector<T, Alloc> temp(move(other));
        swap(temp);
    }
    return *this;
}

template<typename T, AllocatorOf<T> Alloc>
template<Iterator Iter>
constexpr auto NewVector<T, Alloc>::assign(Iter start, Iter end, Option<size_t> known_size) -> IteratorInsertResult<NewVector&, Iter> {
    clear();
    return result_and_then(insert(this->end(), move(start), move(end), known_size), [this](auto) -> NewVector& {
        return *this;
    });
}

template<typename T, AllocatorOf<T> Alloc>
constexpr void NewVector<T, Alloc>::reserve(size_t min_capacity) {
    if (min_capacity <= m_capacity) {
        return;
    }

    auto* old_buffer = m_data;
    auto old_capacity = m_capacity;
    auto [new_buffer, new_capacity] = Alloc().allocate(min_capacity);
    move_objects(new_buffer, old_buffer, size());
    m_capacity = new_capacity;
    m_data = new_buffer;

    if (old_buffer) {
        Alloc().deallocate(old_buffer, old_capacity);
    }
}

template<typename T, AllocatorOf<T> Alloc>
constexpr void NewVector<T, Alloc>::move_objects(T* destination, T* source, size_t count) {
    // NOTE: this should use memmove for trivial types when not in constant evaluated context.
    // Loop backwards, explitly use unsigned underflow in the loop.
    for (size_t i = count - 1; i < count; i--) {
        construct_at(&destination[i], move(source[i]));
        source[i].~T();
    }
}

template<typename T, AllocatorOf<T> Alloc>
constexpr void NewVector<T, Alloc>::grow_to(size_t new_size) {
    if (new_size <= m_capacity) {
        return;
    }

    if (m_capacity == 0 || 2 * m_capacity < new_size) {
        return reserve(max(20lu, new_size));
    } else {
        return reserve(3 * m_capacity / 2);
    }
}

template<typename T, AllocatorOf<T> Alloc>
constexpr Option<T&> NewVector<T, Alloc>::at(size_t index) {
    if (index >= size()) {
        return None {};
    }
    return (*this)[index];
}

template<typename T, AllocatorOf<T> Alloc>
constexpr Option<const T&> NewVector<T, Alloc>::at(size_t index) const {
    if (index >= size()) {
        return None {};
    }
    return (*this)[index];
}

template<typename T, AllocatorOf<T> Alloc>
constexpr void NewVector<T, Alloc>::clear() {
    erase(begin(), end());

    if (m_data) {
        Alloc().deallocate(m_data, m_capacity);
    }
    m_capacity = 0;
    m_data = nullptr;
}

template<typename T, AllocatorOf<T> Alloc>
template<Iterator Iter>
constexpr auto NewVector<T, Alloc>::insert(size_t index, Iter start, Iter end, Option<size_t>) -> IteratorInsertResult<Iterator, Iter> {
    auto result = index;
    if constexpr (CreateableFrom<T, IteratorValueType<Iter>>) {
        for (auto&& value : iterator_container(move(start), move(end))) {
            emplace(index++, static_cast<decltype(value)&&>(value));
        }
    } else if constexpr (FalliblyCreateableFrom<T, IteratorValueType<Iter>>) {
        auto old_end = this->end();
        for (auto it = move(start); it != end; ++it) {
            auto outcome = emplace_back(*it);
            if (!outcome) {
                erase(old_end, this->end());
                return move(outcome).try_did_fail();
            }
        }
        Alg::rotate(iterator_container(iterator(index), this->end()), old_end);
    }
    return iterator(result);
}

template<typename T, AllocatorOf<T> Alloc>
template<typename... Args>
constexpr auto NewVector<T, Alloc>::emplace(size_t index, Args&&... args) -> InsertResult<Iterator, Args...> {
    if constexpr (CreateableFrom<T, Args...>) {
        grow_to(size() + 1);

        move_objects(m_data + index + 1, m_data + index, size() - index);
        create_at(&m_data[index], forward<Args>(args)...);

        m_size++;
        return iterator(index);
    } else {
        return result_and_then(LIIM::create<T>(forward<Args>(args)...), [&](T&& value) {
            return emplace(index, move(value));
        });
    }
}

template<typename T, AllocatorOf<T> Alloc>
constexpr auto NewVector<T, Alloc>::erase_count(size_t index, size_t count) -> Iterator {
    if (count == 0) {
        return iterator(index);
    }

    auto leftover_elements = size() - index - count;
    for (size_t i = 0; i < leftover_elements; i++) {
        (*this)[index + i] = move((*this)[index + i + count]);
        m_data[index + i + count].~T();
    }
    for (size_t i = index + leftover_elements; i < m_size; i++) {
        m_data[i].~T();
    }
    m_size -= count;
    return iterator(min(index, size()));
}

template<typename T, AllocatorOf<T> Alloc>
constexpr Option<T> NewVector<T, Alloc>::pop_back() {
    if (empty()) {
        return None {};
    }

    auto& slot = m_data[--m_size];
    auto value = Option<T>(move(slot));
    slot.~T();
    return value;
}

template<typename T, AllocatorOf<T> Alloc>
constexpr auto NewVector<T, Alloc>::resize(size_t n, const T& value) -> InsertResult<void, const T&>
requires(Copyable<T>) {
    if (size() > n) {
        erase_count(n, size() - n);
    } else {
        ::insert(*this, end(), repeat(n - size(), value));
    }
}

template<typename T, AllocatorOf<T> Alloc>
template<EqualComparableWith<T> U>
constexpr bool NewVector<T, Alloc>::operator==(const NewVector<U>& other) const requires(EqualComparable<T>) {
    return Alg::equal(*this, other);
}

template<typename T, AllocatorOf<T> Alloc>
template<ComparableWith<T> U>
constexpr auto NewVector<T, Alloc>::operator<=>(const NewVector<U>& other) const requires(Comparable<T>) {
    return Alg::lexographic_compare(*this, other);
}

template<typename T, AllocatorOf<T> Alloc>
constexpr void NewVector<T, Alloc>::swap(NewVector<T, Alloc>& other) {
    ::swap(this->m_size, other.m_size);
    ::swap(this->m_capacity, other.m_capacity);
    ::swap(this->m_data, other.m_data);
}

template<Copyable T>
constexpr auto make_vector(std::initializer_list<T> list) {
    return NewVector<T>::create(list);
}

template<Iterator Iter>
constexpr auto make_vector(Iter start, Iter end, Option<size_t> known_size = {}) {
    using Type = decay_t<typename IteratorTraits<Iter>::ValueType>;
    return NewVector<Type>::create(move(start), move(end), known_size);
}

template<Container C>
constexpr auto collect_vector(C&& container) {
    using Iter = decltype(container.begin());
    using ValueType = IteratorTraits<Iter>::ValueType;
    return collect<NewVector<decay_t<ValueType>>>(forward<C>(container));
}

template<typename T, AllocatorOf<T> Alloc>
constexpr void swap(NewVector<T, Alloc>& a, NewVector<T, Alloc>& b) {
    a.swap(b);
}
}

namespace LIIM::Format {
template<Formattable T, LIIM::Container::AllocatorOf<T> Alloc>
struct Formatter<LIIM::Container::NewVector<T, Alloc>> {
    constexpr void parse(FormatParseContext& context) { m_formatter.parse(context); }

    void format(const LIIM::Container::NewVector<T, Alloc>& vector, FormatContext& context) {
        context.put("[ ");
        bool first = true;
        for (auto& item : vector) {
            if (!first) {
                context.put(", ");
            }
            m_formatter.format(item, context);
            first = false;
        }
        context.put(" ]");
    }

    Formatter<T> m_formatter;
};
}

using LIIM::Container::collect_vector;
using LIIM::Container::make_vector;
using LIIM::Container::NewVector;
