#pragma once

#include "containers/hash_map.h"
#include "types.h"

//! A set of keys: [[HashMap]]'s table with no value (container audit series, Day 2), for membership tests that were
//! written as `HashMap<K, u8>`.
namespace engine_placeholder
{

namespace detail
{

// The value a HashSet stores beside each key: nothing.
struct HashSetNoValue
{
};

// The range-for iterator of a HashSet: the map's, yielding the key alone.
template <typename MapIterator, typename K> class HashSetIterator
{
public:
    explicit HashSetIterator(MapIterator it) : it_(it) {}

    const K& operator*() const { return (*it_).key; }
    const K* operator->() const { return &(*it_).key; }

    HashSetIterator& operator++()
    {
        ++it_;
        return *this;
    }

    bool operator==(const HashSetIterator& other) const { return it_ == other.it_; }
    bool operator!=(const HashSetIterator& other) const { return it_ != other.it_; }

private:
    MapIterator it_;
};

}  // namespace detail

/// A set of `K` using open addressing with linear probing: the table of [[HashMap]] with no value.
///
/// The rules are the map's: `K` needs `operator==` and a `Hash<K>` specialization, may be move-only, and can be looked
/// up by a [[HeterogeneousKey]] (a [[StringView]] for a [[String]] key); the table is a power of two, made by the first
/// insert. Iteration (`ForEach`, or `for (const K& key : set)`) visits the keys in no particular order: no
/// deterministic output may depend on it, and the set must not be changed while it is walked.
///
/// - Thread-safety: not-thread-safe. Synchronize externally if shared, including concurrent `Contains` calls with a
///   writer.
/// - Ownership: the set owns its keys; the allocator must outlive it.
template <typename K, typename Allocator> class HashSet
{
    using Map = HashMap<K, detail::HashSetNoValue, Allocator>;

public:
    /// Creates an empty set. No table is made until the first insert.
    ///
    /// - Param allocator: allocator for table storage; held by pointer and must outlive the set.
    /// - Param initial_capacity: slots of the first table; rounded up to a power of two, at least 8.
    explicit HashSet(Allocator& allocator, usize initial_capacity = 16) : map_(allocator, initial_capacity) {}

    HashSet(const HashSet&) = delete;
    HashSet& operator=(const HashSet&) = delete;
    /// Takes over `other`'s keys, leaving it empty.
    HashSet(HashSet&& other) noexcept = default;
    /// Destroys this set's keys, then takes over `other`'s, leaving it empty.
    HashSet& operator=(HashSet&& other) noexcept = default;

    /// Adds `key` if absent.
    ///
    /// - Param key: the key; `K` must be constructible from it. Copied or moved in only when absent.
    /// - Returns: true when the key was absent and has been added.
    /// - Aborts: growth is needed and the allocator is out of memory.
    template <typename KK>
        requires(IsConstructible<K, KK&&>)
    bool Insert(KK&& key)
    {
        return map_.Emplace(Forward<KK>(key)).inserted;
    }

    /// Returns true when `key` is present.
    ///
    /// - Param key: the key to search for.
    bool Contains(const K& key) const { return map_.Contains(key); }

    /// Returns true when a key equal to `key`, a value of another type, is present.
    ///
    /// - Param key: what to search for.
    template <HeterogeneousKey<K> Q> bool Contains(const Q& key) const { return map_.Contains(key); }

    /// Removes `key` if present.
    ///
    /// - Param key: the key to remove.
    /// - Returns: true when the key was present and has been removed.
    bool Remove(const K& key) { return map_.Remove(key); }

    /// Removes the key equal to `key`, a value of another type.
    ///
    /// - Param key: what to remove.
    /// - Returns: true when a key was present and has been removed.
    template <HeterogeneousKey<K> Q> bool Remove(const Q& key) { return map_.Remove(key); }

    /// Removes every key and keeps the table.
    void Clear() { map_.Clear(); }

    /// Makes room for `count` keys without a rehash.
    ///
    /// - Param count: the number of keys to make room for.
    /// - Aborts: the allocator is out of memory.
    void Reserve(usize count) { map_.Reserve(count); }

    /// Calls `visit(key)` for every key, in no particular order; `visit` must not change the set.
    ///
    /// - Param visit: called as `visit(const K&)`.
    template <typename Visit> void ForEach(Visit visit) const
    {
        map_.ForEach([&](const K& key, const detail::HashSetNoValue&) { visit(key); });
    }

    /// The first key, for a range-for (`for (const K& key : set)`); in no particular order.
    ///
    /// - Internal: the range-for protocol; use the loop, not the iterator.
    auto begin() const { return detail::HashSetIterator<decltype(map_.begin()), K>(map_.begin()); }
    /// The end of a range-for.
    ///
    /// - Internal: the range-for protocol.
    auto end() const { return detail::HashSetIterator<decltype(map_.end()), K>(map_.end()); }

    /// Returns the number of keys.
    usize Size() const { return map_.Size(); }
    /// Returns true when the set holds no key.
    bool Empty() const { return map_.Empty(); }
    /// Returns the number of slots in the table; for tests that observe growth.
    usize SlotCapacity() const { return map_.SlotCapacity(); }
    /// Returns the number of tombstones the next rehash clears.
    usize TombstoneCount() const { return map_.TombstoneCount(); }

private:
    Map map_;
};

}  // namespace engine_placeholder
