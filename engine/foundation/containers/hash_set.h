#pragma once

#include "containers/hash_map.h"
#include "containers/open_table.h"
#include "types.h"

//! A set of keys over the open-addressing table [[HashMap]] uses (container audit series, Days 2 and 7), for the
//! membership tests that were written as `HashMap<K, u8>`. The set stores its keys alone.
namespace vse
{

/// A set of `K` using open addressing with linear probing: the table of [[HashMap]], holding keys alone.
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
    using Table = detail::OpenTable<K, detail::SetKeyOf, Allocator>;

public:
    /// Creates an empty set. No table is made until the first insert.
    ///
    /// - Param allocator: allocator for table storage; held by pointer and must outlive the set.
    /// - Param initial_capacity: slots of the first table; rounded up to a power of two, at least 8.
    explicit HashSet(Allocator& allocator, usize initial_capacity = 16) : table_(allocator, initial_capacity) {}

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
        if constexpr (!kSearchesAs<KK>)
        {
            return Insert(K(Forward<KK>(key)));  // hashed as K, never as another type
        }
        else
        {
            if (!table_.NeedsGrow())
            {
                const usize i = table_.SlotForInsert(key);
                if (table_.OccupiedAt(i))
                {
                    return false;
                }
                table_.Make(i, K(Forward<KK>(key)));
                return true;
            }
            if (table_.Find(key) != Table::kNone)
            {
                return false;  // present: no growth
            }
            K held(Forward<KK>(key));  // before a growth that may move what `key` refers to
            table_.Grow();
            const usize i = table_.SlotForInsert(held);  // before `held` is moved: argument order is unspecified
            table_.Make(i, Move(held));
            return true;
        }
    }

    /// Returns true when `key` is present.
    ///
    /// - Param key: the key to search for.
    bool Contains(const K& key) const { return table_.Find(key) != Table::kNone; }

    /// Returns true when a key equal to `key`, a value of another type, is present.
    ///
    /// - Param key: what to search for.
    template <HeterogeneousKey<K> Q> bool Contains(const Q& key) const { return table_.Find(key) != Table::kNone; }

    /// Removes `key` if present.
    ///
    /// - Param key: the key to remove.
    /// - Returns: true when the key was present and has been removed.
    bool Remove(const K& key) { return RemoveBy(key); }

    /// Removes the key equal to `key`, a value of another type.
    ///
    /// - Param key: what to remove.
    /// - Returns: true when a key was present and has been removed.
    template <HeterogeneousKey<K> Q> bool Remove(const Q& key) { return RemoveBy(key); }

    /// Removes every key and keeps the table.
    void Clear() { table_.Clear(); }

    /// Makes room for `count` keys without a rehash.
    ///
    /// - Param count: the number of keys to make room for.
    /// - Aborts: the allocator is out of memory.
    void Reserve(usize count) { table_.Reserve(count); }

    /// Calls `visit(key)` for every key, in no particular order; `visit` must not change the set.
    ///
    /// - Param visit: called as `visit(const K&)`.
    template <typename Visit> void ForEach(Visit visit) const
    {
        table_.ForEach([&](const K& key) { visit(key); });
    }

    /// The first key, for a range-for (`for (const K& key : set)`); in no particular order.
    ///
    /// - Internal: the range-for protocol; use the loop, not the iterator.
    auto begin() const { return table_.begin(); }
    /// The end of a range-for.
    ///
    /// - Internal: the range-for protocol.
    auto end() const { return table_.end(); }

    /// Returns the number of keys.
    usize Size() const { return table_.Size(); }
    /// Returns true when the set holds no key.
    bool Empty() const { return table_.Size() == 0; }
    /// Returns the number of slots in the table; for tests that observe growth.
    usize SlotCapacity() const { return table_.SlotCapacity(); }
    /// Returns the number of tombstones the next rehash clears.
    usize TombstoneCount() const { return table_.TombstoneCount(); }

private:
    template <typename KK>
    static constexpr bool kSearchesAs = IsSame<RemoveCVRef<KK>, K> || HeterogeneousKey<RemoveCVRef<KK>, K>;

    template <typename Q> bool RemoveBy(const Q& key)
    {
        const usize i = table_.Find(key);
        if (i == Table::kNone)
        {
            return false;
        }
        table_.Erase(i);
        return true;
    }

    Table table_;
};

}  // namespace vse
