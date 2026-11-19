#pragma once

#include "containers/array.h"
#include "containers/hash.h"
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

template <typename K, typename V, typename Allocator> class HashMap;

namespace detail
{

// The range-for iterator of a HashMap: walks the slots, skipping empty ones and tombstones. A debug build
// (ENGINE_SLOW_CHECKS) checks that the map was not changed while it walks.
template <typename Map, typename Entry> class HashMapIterator
{
public:
    HashMapIterator(Map* map, usize index) : map_(map), index_(index), changes_(map->changes_) { Settle(); }

    Entry& operator*() const { return map_->EntryAt(index_); }
    Entry* operator->() const { return &map_->EntryAt(index_); }

    HashMapIterator& operator++()
    {
        ENGINE_CHECK_SLOW(map_->changes_ == changes_, "HashMap: changed while a range-for walked it");
        ++index_;
        Settle();
        return *this;
    }

    bool operator==(const HashMapIterator& other) const { return index_ == other.index_; }
    bool operator!=(const HashMapIterator& other) const { return index_ != other.index_; }

private:
    void Settle()
    {
        while (index_ < map_->slots_.Size() && !map_->OccupiedAt(index_))
        {
            ++index_;
        }
    }

    Map* map_;
    usize index_;
    u32 changes_;
};

}  // namespace detail

/// Hash map from `K` to `V` using open addressing with linear probing.
///
/// `K` needs `operator==` and a `Hash<K>` specialization. Keys and values are stored by value, constructed when they
/// are inserted (copied or moved in) and destroyed when they are removed, when the table is rehashed into a new one
/// (they are moved across) and with the map; neither needs a default constructor, and either may be move-only (a
/// [[String]] key, Phase 27 Day 474). A key can also be looked up by a type `Q` it compares equal to (`K == Q`) whose
/// `Hash<Q>` agrees with `Hash<K>` for equal keys: a [[StringView]] for a [[String]] key.
///
/// The table is a power of two in size (a probe step is a mask, not a division) and is made by the first insert, not
/// by the constructor; it rehashes when the load factor, counting tombstones, reaches 0.7: into twice the slots if the
/// live entries alone fill at least half of that, otherwise into the same number (which clears the tombstones left by
/// removals). Growth invalidates pointers returned by `Find`.
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

public:
    /// Creates an empty map. No table is made until the first insert.
    ///
    /// - Param allocator: allocator for table storage; held by pointer and must
    ///   outlive the map.
    /// - Param initial_capacity: slots of the first table; rounded up to a power of two, at least 8.
    explicit HashMap(Allocator& allocator, usize initial_capacity = 16)
        : allocator_(&allocator), slots_(allocator), first_capacity_(RoundCapacity(initial_capacity))
    {
    }

    /// Destroys every key and value.
    ~HashMap() { DestroyAll(); }

    HashMap(const HashMap&) = delete;
    HashMap& operator=(const HashMap&) = delete;

    /// Takes over `other`'s entries and table, leaving `other` empty (it may be used again: its next `Insert` makes a
    /// table).
    HashMap(HashMap&& other) noexcept
        : allocator_(other.allocator_), slots_(Move(other.slots_)), count_(other.count_),
          tombstone_count_(other.tombstone_count_), first_capacity_(other.first_capacity_)
    {
        other.count_ = 0;
        other.tombstone_count_ = 0;
        ++other.changes_;
    }

    /// Destroys this map's entries, then takes over `other`'s entries, table and allocator, leaving `other` empty (it
    /// may be used again).
    HashMap& operator=(HashMap&& other) noexcept
    {
        if (this != &other)
        {
            DestroyAll();
            allocator_ = other.allocator_;
            slots_ = Move(other.slots_);
            count_ = other.count_;
            tombstone_count_ = other.tombstone_count_;
            first_capacity_ = other.first_capacity_;
            other.count_ = 0;
            other.tombstone_count_ = 0;
            ++other.changes_;
            ++changes_;
        }
        return *this;
    }

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
        else if (NeedsGrow())
        {
            usize index = 0;
            if (FindSlotForLookup(key, index))
            {
                EntryAt(index).value = V(Forward<VV>(value));  // a replacement needs no new slot: no rehash
                return;
            }
            // The arguments may refer to entries of this table, which the rehash moves and frees: take them first.
            K held_key(Forward<KK>(key));
            V held_value(Forward<VV>(value));
            Grow();
            Put(Move(held_key), Move(held_value));
        }
        else
        {
            Put(Forward<KK>(key), Forward<VV>(value));
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
            return EmplaceAs(Forward<KK>(key), Forward<Args>(args)...);
        }
    }

private:
    template <typename KK, typename... Args> HashMapEmplaced<V> EmplaceAs(KK&& key, Args&&... args)
    {
        usize index = 0;
        if (!NeedsGrow())
        {
            index = FindSlotForInsert(key);  // one probe finds the key or the slot for it
            if (slots_[index].state == SlotState::Occupied)
            {
                return HashMapEmplaced<V>{&EntryAt(index).value, false};
            }
            return ConstructEntry(index, K(Forward<KK>(key)), Forward<Args>(args)...);
        }
        if (FindSlotForLookup(key, index))
        {
            return HashMapEmplaced<V>{&EntryAt(index).value, false};  // present: no growth
        }
        K held_key(Forward<KK>(key));  // before a growth that may move what `key` refers to
        Grow();
        index = FindSlotForInsert(held_key);
        return ConstructEntry(index, Move(held_key), Forward<Args>(args)...);
    }

    // Makes the entry in the free (empty or tombstone) slot `index`.
    template <typename... Args> HashMapEmplaced<V> ConstructEntry(usize index, K&& key, Args&&... args)
    {
        if (slots_[index].state == SlotState::Tombstone)
        {
            --tombstone_count_;
        }
        ConstructAt<Entry>(slots_[index].entry.bytes.Data(), Entry{Move(key), V(Forward<Args>(args)...)});
        slots_[index].state = SlotState::Occupied;
        ++count_;
        ++changes_;
        return HashMapEmplaced<V>{&EntryAt(index).value, true};
    }

public:
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
    bool Contains(const K& key) const { return FindBy(key) != nullptr; }

    /// Returns true when a key equal to `key`, a value of another type, is present (as the heterogeneous `Find`).
    ///
    /// - Param key: what to search for.
    template <HeterogeneousKey<K> Q> bool Contains(const Q& key) const { return FindBy(key) != nullptr; }

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
    void Clear()
    {
        DestroyAll();
        ++changes_;
    }

    /// Makes room for `count` live entries without a rehash (the next inserts up to that count do not grow the table).
    ///
    /// - Param count: the number of entries to make room for.
    /// - Aborts: the allocator is out of memory.
    void Reserve(usize count)
    {
        // Growth starts when (live + tombstones + 1) * 10 >= slots * 7: the table must keep that false at `count`.
        const usize needed = RoundCapacity((count + 1) * 10 / 7 + 1);
        if (needed > slots_.Size())
        {
            Rehash(needed);
        }
    }

    /// Calls `visit(key, value)` for every entry, in no particular order. `visit` must not insert into or remove from
    /// the map.
    ///
    /// - Param visit: called as `visit(const K&, V&)`.
    template <typename Visit> void ForEach(Visit visit)
    {
        for (usize i = 0; i < slots_.Size(); ++i)
        {
            if (OccupiedAt(i))
            {
                visit(static_cast<const K&>(EntryAt(i).key), EntryAt(i).value);
            }
        }
    }

    /// Calls `visit(key, value)` for every entry of a const map, in no particular order.
    ///
    /// - Param visit: called as `visit(const K&, const V&)`.
    template <typename Visit> void ForEach(Visit visit) const
    {
        for (usize i = 0; i < slots_.Size(); ++i)
        {
            if (OccupiedAt(i))
            {
                visit(EntryAt(i).key, EntryAt(i).value);
            }
        }
    }

    /// The first entry, for a range-for (`for (auto& [key, value] : map)`); in no particular order.
    ///
    /// - Internal: the range-for protocol; use the loop, not the iterator.
    detail::HashMapIterator<HashMap, Entry> begin() { return detail::HashMapIterator<HashMap, Entry>(this, 0); }
    /// The end of a range-for.
    ///
    /// - Internal: the range-for protocol.
    detail::HashMapIterator<HashMap, Entry> end()
    {
        return detail::HashMapIterator<HashMap, Entry>(this, slots_.Size());
    }
    /// The first entry of a const map, for a range-for.
    ///
    /// - Internal: the range-for protocol.
    detail::HashMapIterator<const HashMap, const Entry> begin() const
    {
        return detail::HashMapIterator<const HashMap, const Entry>(this, 0);
    }
    /// The end of a range-for over a const map.
    ///
    /// - Internal: the range-for protocol.
    detail::HashMapIterator<const HashMap, const Entry> end() const
    {
        return detail::HashMapIterator<const HashMap, const Entry>(this, slots_.Size());
    }

    /// Returns the number of keys currently stored.
    usize Size() const { return count_; }
    /// Returns true when no key is stored.
    bool Empty() const { return count_ == 0; }

    /// Returns the number of slots in the table (capacity, not element count); 0 before the first insert.
    ///
    /// Exposed so tests can observe growth; production code should not need it.
    usize SlotCapacity() const { return slots_.Size(); }

    /// Returns the number of slots holding a removed entry's tombstone, which the next rehash clears.
    ///
    /// - Returns: the tombstone count.
    /// - Thread-safety: thread-compatible.
    usize TombstoneCount() const { return tombstone_count_; }

private:
    template <typename Map, typename E> friend class detail::HashMapIterator;

    // Whether an argument of type KK can be hashed and compared as it is: a K, or a key type Hash agrees on.
    template <typename KK>
    static constexpr bool kSearchesAs = IsSame<RemoveCVRef<KK>, K> || HeterogeneousKey<RemoveCVRef<KK>, K>;

    enum class SlotState : u8
    {
        Empty,
        Occupied,
        Tombstone
    };

    // Raw, aligned room for one T; the map constructs and destroys the T inside it. Left uninitialized: it is never
    // read before the T is constructed.
    template <typename T> struct Room
    {
        alignas(T) Array<byte, sizeof(T)> bytes;

        T& Get() { return *Launder(reinterpret_cast<T*>(bytes.Data())); }
        const T& Get() const { return *Launder(reinterpret_cast<const T*>(bytes.Data())); }
    };

    struct Slot
    {
        Slot() {}  // leaves the room uninitialized
        SlotState state = SlotState::Empty;
        Room<Entry> entry;
    };

    // The smallest power of two of at least `n` slots, and at least 8.
    static usize RoundCapacity(usize n)
    {
        usize cap = 8;
        while (cap < n)
        {
            cap *= 2;
        }
        return cap;
    }

    bool OccupiedAt(usize i) const { return slots_[i].state == SlotState::Occupied; }
    Entry& EntryAt(usize i) { return slots_[i].entry.Get(); }
    const Entry& EntryAt(usize i) const { return slots_[i].entry.Get(); }

    void DestroyAll()
    {
        for (usize i = 0; i < slots_.Size(); ++i)
        {
            if (slots_[i].state == SlotState::Occupied)
            {
                EntryAt(i).~Entry();
            }
            slots_[i].state = SlotState::Empty;
        }
        count_ = 0;
        tombstone_count_ = 0;
    }

    bool NeedsGrow() const
    {
        // Load factor >= 0.7 triggers growth, counting tombstones along with occupied slots: a tombstone still costs
        // a probe step (it isn't Empty, so lookups/inserts keep scanning past it) and is only reclaimed by a rehash,
        // so insert/remove churn that never grows `count_` must still eventually trigger one. An empty table (none
        // made yet) always needs one.
        return (count_ + tombstone_count_ + 1) * 10 >= slots_.Size() * 7;
    }

    // Grows the table: double only when the live entries need the room; when the load is mostly tombstones
    // (insert/remove churn), rehash at the same size, which clears them, so churn does not grow the table without
    // bound. A map with no table gets its first one.
    void Grow()
    {
        const usize cap = slots_.Size();
        if (cap == 0)
        {
            Rehash(first_capacity_);
            return;
        }
        const bool live_needs_room = (count_ + 1) * 20 >= cap * 7;  // live load >= 0.35
        Rehash(live_needs_room ? cap * 2 : cap);
    }

    // Stores the entry; the table has room.
    template <typename KK, typename VV> void Put(KK&& key, VV&& value)
    {
        const usize index = FindSlotForInsert(key);
        if (slots_[index].state == SlotState::Occupied)
        {
            EntryAt(index).value = V(Forward<VV>(value));
            return;
        }
        if (slots_[index].state == SlotState::Tombstone)
        {
            --tombstone_count_;
        }
        ConstructAt<Entry>(slots_[index].entry.bytes.Data(), Entry{K(Forward<KK>(key)), V(Forward<VV>(value))});
        slots_[index].state = SlotState::Occupied;
        ++count_;
        ++changes_;
    }

    template <typename Q> V* FindBy(const Q& key)
    {
        usize index;
        if (!FindSlotForLookup(key, index))
        {
            return nullptr;
        }
        return &EntryAt(index).value;
    }

    template <typename Q> const V* FindBy(const Q& key) const
    {
        usize index;
        if (!FindSlotForLookup(key, index))
        {
            return nullptr;
        }
        return &EntryAt(index).value;
    }

    template <typename Q> bool RemoveBy(const Q& key)
    {
        usize index;
        if (!FindSlotForLookup(key, index))
        {
            return false;
        }
        EntryAt(index).~Entry();
        slots_[index].state = SlotState::Tombstone;
        --count_;
        ++tombstone_count_;
        ++changes_;
        return true;
    }

    // The slot to store `key` in: its slot if present, else the first tombstone or empty slot of its probe chain.
    // The table has at least one empty slot (NeedsGrow keeps it so).
    template <typename Q> usize FindSlotForInsert(const Q& key) const
    {
        const usize mask = slots_.Size() - 1;
        usize i = Hash<Q>{}(key)&mask;
        usize first_tombstone = slots_.Size();  // the size means "none found yet"
        for (usize probe = 0; probe < slots_.Size(); ++probe, i = (i + 1) & mask)
        {
            if (slots_[i].state == SlotState::Empty)
            {
                return first_tombstone != slots_.Size() ? first_tombstone : i;
            }
            if (slots_[i].state == SlotState::Occupied && EntryAt(i).key == key)
            {
                return i;
            }
            if (slots_[i].state == SlotState::Tombstone && first_tombstone == slots_.Size())
            {
                first_tombstone = i;
            }
        }
        // Not expected to run: NeedsGrow() keeps every Insert() from letting the table fill completely.
        return first_tombstone != slots_.Size() ? first_tombstone : 0;
    }

    template <typename Q> bool FindSlotForLookup(const Q& key, usize& out_index) const
    {
        if (slots_.Size() == 0)
        {
            return false;  // no table yet, or a moved-from map
        }
        const usize mask = slots_.Size() - 1;
        usize i = Hash<Q>{}(key)&mask;
        for (usize probe = 0; probe < slots_.Size(); ++probe, i = (i + 1) & mask)
        {
            if (slots_[i].state == SlotState::Empty)
            {
                return false;  // probe chain ended: definitely absent.
            }
            if (slots_[i].state == SlotState::Occupied && EntryAt(i).key == key)
            {
                out_index = i;
                return true;
            }
            // Tombstone (or occupied-but-different-key): keep probing.
        }
        return false;
    }

    // Moves every entry into a fresh table of `new_capacity` slots (a power of two); the old table's entries are
    // destroyed after their move. The slot vector is never grown in place, so no live key is relocated bytewise.
    void Rehash(usize new_capacity)
    {
        Vector<Slot, Allocator> old(Move(slots_));
        slots_ = Vector<Slot, Allocator>(*allocator_);
        slots_.Reserve(new_capacity);
        for (usize i = 0; i < new_capacity; ++i)
        {
            slots_.Push(Slot());
        }
        count_ = 0;
        tombstone_count_ = 0;
        ++changes_;
        for (usize i = 0; i < old.Size(); ++i)
        {
            if (old[i].state != SlotState::Occupied)
            {
                continue;
            }
            Entry& from = old[i].entry.Get();
            const usize index = FindSlotForInsert(from.key);
            ConstructAt<Entry>(slots_[index].entry.bytes.Data(), Entry{Move(from.key), Move(from.value)});
            slots_[index].state = SlotState::Occupied;
            ++count_;
            from.~Entry();
            old[i].state = SlotState::Empty;
        }
    }

    Allocator* allocator_ = nullptr;
    Vector<Slot, Allocator> slots_;
    usize count_ = 0;
    usize tombstone_count_ = 0;
    usize first_capacity_ = 16;  // the slots of the first table, a power of two
    u32 changes_ = 0;            // bumped by every insert, removal, clear and rehash; checked by the iterator
};

}  // namespace engine_placeholder
