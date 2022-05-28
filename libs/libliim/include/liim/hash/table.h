#pragma once

// Hash Table implementation modelled from https://www.youtube.com/watch?v=ncHmEUmJZf4.

#include <liim/compare.h>
#include <liim/container.h>
#include <liim/hash/entry.h>
#include <liim/hash/group_info.h>
#include <liim/hash/hashable.h>
#include <liim/hash/hasher.h>
#include <liim/hash/table_iterator.h>

namespace LIIM::Hash::Detail {
template<typename TransparentKey, typename Base>
concept CanLookup = HashableLike<TransparentKey, Base> && EqualComparable<TransparentKey, Base>;

template<typename TransparentKey, typename Base>
concept CanInsert = CanLookup<TransparentKey, Base> && ConstructibleFrom<Base, TransparentKey>;

template<Hashable T>
class Table {
public:
    using ValueType = T;

    constexpr Table() {}
    constexpr Table(const Table&);
    constexpr Table(Table&&);
    constexpr ~Table();

    constexpr Table& operator=(const Table&);
    constexpr Table& operator=(Table&&);

    constexpr bool empty() const { return size() == 0; }
    constexpr size_t size() const { return m_size; }

    using Iterator = TableIterator<Table>;
    using ConstIterator = TableIterator<const Table>;

    friend Iterator;
    friend ConstIterator;

    constexpr Iterator begin() { return Iterator(*this); }
    constexpr ConstIterator begin() const { return ConstIterator(*this); }
    constexpr ConstIterator cbegin() const { return ConstIterator(*this); }

    constexpr Iterator end() { return Iterator(*this, m_capacity, 0); }
    constexpr ConstIterator end() const { return ConstIterator(*this, m_capacity, 0); }
    constexpr ConstIterator cend() const { return ConstIterator(*this, m_capacity, 0); }

    constexpr void clear();

    template<CanLookup<T> U, typename Factory>
    constexpr Option<T&> insert_with_factory(U&& needle, Factory&& factory);

    template<CanInsert<T> U>
    constexpr Option<T&> insert(U&& to_insert);

    template<CanLookup<T> U>
    constexpr Option<T&> find(U&& needle);

    template<CanLookup<T> U>
    constexpr Option<const T&> find(U&& needle) const;

    constexpr Option<T> erase(ConstIterator iterator);

    template<CanLookup<T> U>
    constexpr Option<T> erase(U&& needle);

    constexpr void swap(Table& other);

private:
    explicit constexpr Table(size_t capacity);

    enum class FindType {
        PureFind,
        FindToInsert,
    };

    template<FindType find_type, EqualComparable<T> U>
    constexpr auto find_impl(uint64_t hash_high, uint8_t hash_low, U&& needle) const -> Option<Entry>;

    constexpr void grow_and_rehash();
    constexpr Option<T> erase(Entry entry);
    constexpr Entry entry_from_iterator(ConstIterator iterator) const;

    struct HashSplit {
        uint64_t hash_high;
        uint8_t hash_low;
    };

    template<HashableLike<T> U>
    constexpr HashSplit hash(U&& value) const;

    template<EqualComparable<T> U>
    constexpr bool equal(const T& value, U&& other) const;

    constexpr size_t group_count() const { return m_capacity; }

    constexpr size_t value_index(size_t group_index, uint8_t index_into_group) const {
        return GroupInfo::entry_count * group_index + index_into_group;
    }

    constexpr T& value(size_t index) { return m_values[index].value; }
    constexpr const T& value(size_t index) const { return m_values[index].value; }

    constexpr T* value_pointer_for_init(size_t index) { return &m_values[index].value; }

    GroupInfo* m_groups { nullptr };
    MaybeUninit<T>* m_values { nullptr };
    size_t m_size { 0 };
    size_t m_used_values { 0 };
    size_t m_capacity { 0 };
};

template<Hashable T>
constexpr Table<T>::Table(const Table& other) {
    for (auto& value : other) {
        insert(value);
    }
}

template<Hashable T>
constexpr Table<T>::Table(Table&& other)
    : m_groups(exchange(other.m_groups, nullptr))
    , m_values(exchange(other.m_values, nullptr))
    , m_size(exchange(other.m_size, 0))
    , m_used_values(exchange(other.m_used_values, 0))
    , m_capacity(exchange(other.m_capacity, 0)) {}

template<Hashable T>
constexpr Table<T>::Table(size_t capacity) : m_capacity(capacity) {
    m_groups = new GroupInfo[capacity];
    m_values = new MaybeUninit<T>[capacity * GroupInfo::entry_count];
}

template<Hashable T>
constexpr Table<T>::~Table() {
    clear();
}

template<Hashable T>
constexpr auto Table<T>::operator=(const Table& other) -> Table& {
    if (this != other) {
        auto new_table = Table(other);
        swap(new_table);
    }
    return *this;
}

template<Hashable T>
constexpr auto Table<T>::operator=(Table&& other) -> Table& {
    if (this != &other) {
        auto new_table = Table(move(other));
        swap(new_table);
    }
    return *this;
}

template<Hashable T>
constexpr void Table<T>::grow_and_rehash() {
    auto new_capacity = 2 * m_capacity ?: 8;
    auto new_table = Table(new_capacity);
    for (auto& value : *this) {
        new_table.insert(move(value));
    }
    this->swap(new_table);
}

template<Hashable T>
constexpr auto Table<T>::entry_from_iterator(ConstIterator iterator) const -> Entry {
    return Entry(m_groups[iterator.m_group_index].entry(iterator.m_index_into_group),
                 value_index(iterator.m_group_index, iterator.m_index_into_group));
}

template<Hashable T>
template<CanLookup<T> U, typename Factory>
constexpr Option<T&> Table<T>::insert_with_factory(U&& needle, Factory&& factory) {
    auto [hash_high, hash_low] = hash(forward<U>(needle));

    auto entry = find_impl<FindType::FindToInsert>(hash_high, hash_low, forward<U>(needle));
    if (!entry) {
        grow_and_rehash();
        entry = find_impl<FindType::FindToInsert>(hash_high, hash_low, forward<U>(needle));
        assert(entry);
    }

    if (entry->info().present()) {
        return value(entry->value_index());
    }

    forward<Factory>(factory)(value_pointer_for_init(entry->value_index()), forward<U>(needle));
    entry->info().set_present(hash_low);
    m_size++;
    m_used_values++;
    return None {};
}

template<Hashable T>
template<CanInsert<T> U>
constexpr Option<T&> Table<T>::insert(U&& to_insert) {
    return insert_with_factory(forward<U>(to_insert), [](T* pointer, U&& value) {
        construct_at(pointer, value);
    });
}

template<Hashable T>
template<CanLookup<T> U>
constexpr Option<T&> Table<T>::find(U&& needle) {
    return const_cast<const Table&>(*this).find(forward<U>(needle)).map([](const T& value) -> T& {
        return const_cast<T&>(value);
    });
}

template<Hashable T>
template<CanLookup<T> U>
constexpr Option<const T&> Table<T>::find(U&& needle) const {
    auto [hash_high, hash_low] = hash(needle);
    auto entry = find_impl<FindType::PureFind>(hash_high, hash_low, needle);
    return entry.map([&](auto entry) -> const T& {
        return value(entry.value_index());
    });
}

template<Hashable T>
constexpr Option<T> Table<T>::erase(ConstIterator iterator) {
    return erase(entry_from_iterator(iterator));
}

template<Hashable T>
template<CanLookup<T> U>
constexpr Option<T> Table<T>::erase(U&& needle) {
    auto [hash_high, hash_low] = hash(forward<U>(needle));
    return find_impl<FindType::PureFind>(hash_high, hash_low, forward<U>(needle)).and_then([&](auto& entry) {
        return erase(entry);
    });
}

template<Hashable T>
constexpr Option<T> Table<T>::erase(Entry entry) {
    if (!entry.info().present()) {
        return None {};
    }
    auto old_value = T(move(value(entry.value_index())));
    value(entry.value_index()).~T();
    entry.info().set_tombstone();
    m_size--;
    return old_value;
}

template<Hashable T>
constexpr void Table<T>::clear() {
    for (auto& value : *this) {
        value.~T();
    }
    delete[] m_groups;
    delete[] m_values;

    m_groups = nullptr;
    m_values = nullptr;
    m_size = m_used_values = m_capacity = 0;
}

template<Hashable T>
template<Table<T>::FindType find_type, EqualComparable<T> U>
constexpr Option<Entry> Table<T>::find_impl(uint64_t hash_high, uint8_t hash_low, U&& needle) const {
    if (group_count() == 0) {
        return None {};
    }

    auto start_group_index = hash_high % group_count();
    auto group_index = start_group_index;
    do {
        auto& group = m_groups[group_index];
        auto present = group.present_entries();
        for (uint8_t index_into_group = 0; index_into_group < GroupInfo::entry_count; index_into_group++) {
            if (present & (1 << index_into_group)) {
                auto& entry = group.entry(index_into_group);
                if (entry.hash_low() == hash_low) {
                    auto value_index = this->value_index(group_index, index_into_group);
                    auto& value = this->value(value_index);
                    if (value == forward<U>(needle)) {
                        return Entry(const_cast<EntryInfo&>(entry), value_index);
                    }
                }
            }
        }

        if ((present | group.tombstone_entries()) != 0xFF) {
            if constexpr (find_type == FindType::FindToInsert) {
                for (uint8_t index_into_group = 0; index_into_group < GroupInfo::entry_count; index_into_group++) {
                    auto& entry = group.entry(index_into_group);
                    if (entry.tombstone()) {
                        return Entry(const_cast<EntryInfo&>(entry), value_index(group_index, index_into_group));
                    }
                }
                for (uint8_t index_into_group = 0; index_into_group < GroupInfo::entry_count; index_into_group++) {
                    auto& entry = group.entry(index_into_group);
                    if (entry.vacant()) {
                        return Entry(const_cast<EntryInfo&>(entry), value_index(group_index, index_into_group));
                    }
                }

                assert(false);
            }
            break;
        }
        group_index++;
        group_index %= group_count();
    } while (start_group_index != group_index);

    return None {};
}

template<Hashable T>
template<HashableLike<T> U>
constexpr auto Table<T>::hash(U&& value) const -> HashSplit {
    auto hasher = Hasher {};
    HashForType<U>::hash(hasher, forward<U>(value));
    uint64_t hash = hasher.finish();
    return { hash & ~0x7F, static_cast<uint8_t>(hash & 0x7F) };
}

template<Hashable T>
template<EqualComparable<T> U>
constexpr bool Table<T>::equal(const T& value, U&& other) const {
    return value == other;
}

template<Hashable T>
constexpr void Table<T>::swap(Table& other) {
    ::swap(this->m_groups, other.m_groups);
    ::swap(this->m_values, other.m_values);
    ::swap(this->m_size, other.m_size);
    ::swap(this->m_used_values, other.m_used_values);
    ::swap(this->m_capacity, other.m_capacity);
}
}
