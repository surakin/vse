#pragma once

#include "containers/array.h"
#include "containers/hash.h"
#include "containers/open_table.h"
#include "containers/vector.h"
#include "lang/new.h"
#include "lang/traits.h"
#include "lang/utility.h"
#include "log/assert.h"
#include "string_view.h"
#include "types.h"

//! Open-addressing hash map (linear probing, tombstone deletion) whose slot storage is a [[Vector]]. The hash
//! functions are in `containers/hash.h`; the set of keys alone is [[HashSet]].
namespace engine_placeholder
{

/// A type `Q` a [[HashMap]] keyed by `K` can be searched with, without making a `K`: not `K` itself, not a pointer, an
/// array or an arithmetic or enum type (a C string would hash its address and an integer of another width hashes
/// differently: both convert to `K` and take the ordinary overload instead), comparable with `K`, and hashed by
/// `Hash<Q>` the way `Hash<K>` hashes an equal key (a [[StringView]] for a [[String]] key).
template <typename Q, typename K>
concept HeterogeneousKey = !IsSame<Q, K> && !IsPointer<Q> && !IsArray<Q> && !IsIntegral<Q> && !IsFloatingPoint<Q> &&
                           !IsEnum<Q> && requires(const K& k, const Q& q) {
                               k == q;
                               Hash<Q>{}(q);
                           };

/// One entry of a [[HashMap]], as a range-for over the map yields it (`for (auto& [key, value] : map)`).
///
/// The key is a plain member so that structured bindings work; never assign to it: the entry would sit in the slot of
/// its old hash and could no longer be found.
///
/// - Thread-safety: thread-compatible, as the map.
template <typename K, typename V> struct HashMapEntry
{
    K key;    ///< The key. Read it; never assign to it.
    V value;  ///< The value.
};

/// What [[HashMap::Emplace]] did.
///
/// - Thread-safety: thread-compatible plain data.
template <typename V> struct HashMapEmplaced
{
    V* value = nullptr;     ///< The stored value: the new one, or the one that was already there. Valid until the next
                            ///< change of the map.
    bool inserted = false;  ///< True when the key was absent and the value was made.
};

/// Hash map from `K` to `V` using open addressing with linear probing.
///
/// `K` needs `operator==` and a `Hash<K>` specialization. Keys and values are stored by value, constructed when they
/// are inserted (copied or moved in) and destroyed when they are removed, when the table is rehashed into a new one
/// (they are moved across) and with the map; neither needs a default constructor, and either may be move-only (a
/// [[String]] key, Phase 27 Day 474). A key can also be looked up by a type `Q` it compares equal to (`K == Q`) whose
/// `Hash<Q>` agrees with `Hash<K>` for equal keys: a [[StringView]] for a [[String]] key.
///
/// The table (shared with [[HashSet]], container audit Day 7) is a power of two in size (a probe step is a mask, not a
/// division) and is made by the first insert, not by the constructor; it rehashes when the load factor, counting
/// tombstones, reaches 0.7: into twice the slots if the live entries alone fill at least half of that, otherwise into
/// the same number (which clears the tombstones left by removals). Growth invalidates pointers returned by `Find`.
///
/// Iteration (`ForEach`, or a range-for: `for (auto& [key, value] : map)`) visits the entries in no particular order:
/// no deterministic output may depend on it. The map must not be changed while it is walked.
///
/// - Thread-safety: not-thread-safe. Synchronize externally if shared, including
///   concurrent `Find` calls with a writer.
/// - Ownership: the map owns its keys and values; its storage comes from the allocator, which must outlive the map, and
///   is released as a [[Vector]] releases it (freed when the allocator has `Free`, else abandoned inside it).
template <typename K, typename V, typename Allocator> class HashMap
{
    using Entry = HashMapEntry<K, V>;
    using Table = detail::OpenTable<Entry, detail::MapKeyOf, Allocator>;

public:
    /// Creates an empty map. No table is made until the first insert.
    ///
    /// - Param allocator: allocator for table storage; held by pointer and must
    ///   outlive the map.
    /// - Param initial_capacity: slots of the first table; rounded up to a power of two, at least 8.
    explicit HashMap(Allocator& allocator, usize initial_capacity = 16) : table_(allocator, initial_capacity) {}

    HashMap(const HashMap&) = delete;
    HashMap& operator=(const HashMap&) = delete;
    /// Takes over `other`'s entries and table, leaving `other` empty (it may be used again: its next `Insert` makes a
    /// table).
    HashMap(HashMap&& other) noexcept = default;
    /// Destroys this map's entries, then takes over `other`'s entries, table and allocator, leaving `other` empty (it
    /// may be used again).
    HashMap& operator=(HashMap&& other) noexcept = default;

    /// Inserts `key` mapped to `value`, replacing the value if the key is already present. Each argument is copied or
    /// moved in as it is passed (a `const K&` copies, a `K&&` moves); an argument may refer to an entry of this map.
    ///
    /// May rehash, which invalidates pointers returned by `Find`.
    ///
    /// - Param key: the key; `K` must be constructible from it.
    /// - Param value: the value; `V` must be constructible from it.
    /// - Aborts: growth is needed and the allocator is out of memory.
    template <typename KK, typename VV>
        requires(IsConstructible<K, KK&&> && IsConstructible<V, VV&&>)
    void Insert(KK&& key, VV&& value)
    {
        if constexpr (!kSearchesAs<KK>)
        {
            // Another type (a C string for a String key, an integer of another width): hashed as itself it would land
            // in the wrong slot, so it becomes a K first.
            Insert(K(Forward<KK>(key)), Forward<VV>(value));
        }
        else
        {
            if (!table_.NeedsGrow())
            {
                const usize i = table_.SlotForInsert(key);  // one probe finds the key or the slot for it
                if (table_.OccupiedAt(i))
                {
                    table_.At(i).value = V(Forward<VV>(value));
                    return;
                }
                table_.Make(i, Entry{K(Forward<KK>(key)), V(Forward<VV>(value))});
                return;
            }
            const usize found = table_.Find(key);
            if (found != Table::kNone)
            {
                table_.At(found).value = V(Forward<VV>(value));  // a replacement needs no new slot: no rehash
                return;
            }
            {
                // The arguments may refer to entries of this table, which the rehash moves and frees: take them first.
                K held_key(Forward<KK>(key));
                V held_value(Forward<VV>(value));
                table_.Grow();
                table_.Make(table_.SlotForInsert(held_key), Entry{Move(held_key), Move(held_value)});
            }
        }
    }

    /// Returns the value of `key`, making it from `args` first if the key is absent; an existing value is left as it
    /// is (unlike `Insert`, which replaces it).
    ///
    /// - Param key: the key; `K` must be constructible from it (a [[StringView]] for a [[String]] key). Only copied
    ///   or moved in when the key is absent.
    /// - Param args: the arguments of `V`'s constructor; used only when the key is absent.
    /// - Returns: the stored value and whether it was made now; the pointer is valid until the next change of the map.
    /// - Aborts: growth is needed and the allocator is out of memory.
    template <typename KK, typename... Args>
        requires(IsConstructible<K, KK&&> && IsConstructible<V, Args&&...>)
    HashMapEmplaced<V> Emplace(KK&& key, Args&&... args)
    {
        if constexpr (!kSearchesAs<KK>)
        {
            return Emplace(K(Forward<KK>(key)), Forward<Args>(args)...);  // as in Insert
        }
        else
        {
            if (!table_.NeedsGrow())
            {
                const usize i = table_.SlotForInsert(key);  // one probe finds the key or the slot for it
                if (table_.OccupiedAt(i))
                {
                    return HashMapEmplaced<V>{&table_.At(i).value, false};
                }
                return HashMapEmplaced<V>{&table_.Make(i, Entry{K(Forward<KK>(key)), V(Forward<Args>(args)...)}).value,
                                          true};
            }
            const usize found = table_.Find(key);
            if (found != Table::kNone)
            {
                return HashMapEmplaced<V>{&table_.At(found).value, false};  // present: no growth
            }
            K held_key(Forward<KK>(key));  // before a growth that may move what `key` refers to
            table_.Grow();
            const usize i = table_.SlotForInsert(held_key);
            return HashMapEmplaced<V>{&table_.Make(i, Entry{Move(held_key), V(Forward<Args>(args)...)}).value, true};
        }
    }

    /// Looks up the value stored for `key`.
    ///
    /// - Param key: key to search for.
    /// - Returns: a pointer to the stored value, valid until the next `Insert`,
    ///   or nullptr if `key` is absent.
    /// - Fails: reports an absent key by returning nullptr.
    V* Find(const K& key) { return FindBy(key); }

    /// Looks up the value stored for `key` in a const map.
    ///
    /// - Param key: key to search for.
    /// - Returns: a pointer to the stored value, valid until the next `Insert`,
    ///   or nullptr if `key` is absent.
    /// - Fails: reports an absent key by returning nullptr.
    const V* Find(const K& key) const { return FindBy(key); }

    /// Looks up the value stored for the key equal to `key`, a value of another type (a [[StringView]] for a
    /// [[String]] key), without making a `K`.
    ///
    /// - Param key: what to search for; `Hash<Q>` must agree with `Hash<K>` for equal keys.
    /// - Returns: a pointer to the stored value, valid until the next `Insert`, or nullptr if absent.
    /// - Fails: reports an absent key by returning nullptr.
    template <HeterogeneousKey<K> Q> V* Find(const Q& key) { return FindBy(key); }

    /// Looks up the value stored for the key equal to `key` in a const map (as the non-const overload).
    ///
    /// - Param key: what to search for.
    /// - Returns: a pointer to the stored value, valid until the next `Insert`, or nullptr if absent.
    /// - Fails: reports an absent key by returning nullptr.
    template <HeterogeneousKey<K> Q> const V* Find(const Q& key) const { return FindBy(key); }

    /// Returns true when `key` is present.
    ///
    /// - Param key: key to search for.
    bool Contains(const K& key) const { return table_.Find(key) != Table::kNone; }

    /// Returns true when a key equal to `key`, a value of another type, is present (as the heterogeneous `Find`).
    ///
    /// - Param key: what to search for.
    template <HeterogeneousKey<K> Q> bool Contains(const Q& key) const { return table_.Find(key) != Table::kNone; }

    /// Removes `key` if present, destroying its key and value and leaving a tombstone until the next rehash.
    ///
    /// - Param key: key to remove.
    /// - Returns: true if the key was present and has been removed, false if it
    ///   was absent.
    bool Remove(const K& key) { return RemoveBy(key); }

    /// Removes the key equal to `key`, a value of another type (as the heterogeneous `Find`).
    ///
    /// - Param key: what to remove.
    /// - Returns: true if a key was present and has been removed.
    template <HeterogeneousKey<K> Q> bool Remove(const Q& key) { return RemoveBy(key); }

    /// Destroys every entry and keeps the table, so filling the map again to the same size does not allocate.
    void Clear() { table_.Clear(); }

    /// Makes room for `count` live entries without a rehash (the next inserts up to that count do not grow the table).
    ///
    /// - Param count: the number of entries to make room for.
    /// - Aborts: the allocator is out of memory.
    void Reserve(usize count) { table_.Reserve(count); }

    /// Calls `visit(key, value)` for every entry, in no particular order. `visit` must not insert into or remove from
    /// the map.
    ///
    /// - Param visit: called as `visit(const K&, V&)`.
    template <typename Visit> void ForEach(Visit visit)
    {
        table_.ForEach([&](Entry& e) { visit(static_cast<const K&>(e.key), e.value); });
    }

    /// Calls `visit(key, value)` for every entry of a const map, in no particular order.
    ///
    /// - Param visit: called as `visit(const K&, const V&)`.
    template <typename Visit> void ForEach(Visit visit) const
    {
        table_.ForEach([&](const Entry& e) { visit(e.key, e.value); });
    }

    /// The first entry, for a range-for (`for (auto& [key, value] : map)`); in no particular order.
    ///
    /// - Internal: the range-for protocol; use the loop, not the iterator.
    auto begin() { return table_.begin(); }
    /// The end of a range-for.
    ///
    /// - Internal: the range-for protocol.
    auto end() { return table_.end(); }
    /// The first entry of a const map, for a range-for.
    ///
    /// - Internal: the range-for protocol.
    auto begin() const { return table_.begin(); }
    /// The end of a range-for over a const map.
    ///
    /// - Internal: the range-for protocol.
    auto end() const { return table_.end(); }

    /// Returns the number of keys currently stored.
    usize Size() const { return table_.Size(); }
    /// Returns true when no key is stored.
    bool Empty() const { return table_.Size() == 0; }

    /// Returns the number of slots in the table (capacity, not element count); 0 before the first insert.
    ///
    /// Exposed so tests can observe growth; production code should not need it.
    usize SlotCapacity() const { return table_.SlotCapacity(); }

    /// Returns the number of slots holding a removed entry's tombstone, which the next rehash clears.
    ///
    /// - Returns: the tombstone count.
    /// - Thread-safety: thread-compatible.
    usize TombstoneCount() const { return table_.TombstoneCount(); }

private:
    // Whether an argument of type KK can be hashed and compared as it is: a K, or a key type Hash agrees on.
    template <typename KK>
    static constexpr bool kSearchesAs = IsSame<RemoveCVRef<KK>, K> || HeterogeneousKey<RemoveCVRef<KK>, K>;

    template <typename Q> V* FindBy(const Q& key)
    {
        const usize i = table_.Find(key);
        return i != Table::kNone ? &table_.At(i).value : nullptr;
    }

    template <typename Q> const V* FindBy(const Q& key) const
    {
        const usize i = table_.Find(key);
        return i != Table::kNone ? &table_.At(i).value : nullptr;
    }

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

}  // namespace engine_placeholder
