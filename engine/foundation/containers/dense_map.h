#pragma once

#include "containers/hash.h"
#include "containers/hash_map.h"
#include "containers/vector.h"
#include "lang/traits.h"
#include "lang/utility.h"
#include "log/assert.h"
#include "mem/heap_allocator.h"
#include "types.h"

//! [[DenseMap]]: a map whose entries sit densely in slots, with a hash index from key to slot: for the caches and
//! tables that iterate in a stable order or hand out slot numbers, which were written as a `Vector` plus a `HashMap` of
//! positions with the swap-remove fix-up repeated at every site.
namespace vse
{

/// One entry of a [[DenseMap]], as a range-for yields it (`for (auto& [key, value] : map)`).
///
/// - Thread-safety: thread-compatible, as the map.
template <typename K, typename V> struct DenseMapEntry
{
    K key;    ///< The key. Read it; never assign to it.
    V value;  ///< The value.
};

/// What [[DenseMap::Emplace]] did.
///
/// - Thread-safety: thread-compatible plain data.
template <typename V> struct DenseMapEmplaced
{
    V* value = nullptr;     ///< The stored value; valid until the next insert or removal.
    u32 slot = 0;           ///< Its slot.
    bool inserted = false;  ///< True when the key was absent and the value was made.
};

/// A map from `K` to `V` whose entries are dense in slots `0..Size()-1`, found by key through a hash index.
///
/// - **Slots** are stable until a removal: `Emplace` appends, and a removal moves the last entry into the hole
///   (O(1)) and reports where it came from, so a caller that keeps slot numbers can follow the move.
/// - **Order:** a range-for (`for (auto& [key, value] : map)`) visits the slots in order. For a given sequence of
///   calls the order is always the same (unlike [[HashMap]]'s), so it may reach a deterministic output.
/// - **Keys** need `operator==` and `Hash<K>`, may be move-only (a [[String]]) and are stored once; a lookup may use
///   a [[HeterogeneousKey]] (a [[StringView]] for a [[String]] key). The index holds 4-byte slot numbers and each
///   entry keeps its hash, so growth never hashes a key again.
///
/// Use it where entries are walked in a stable order or named by slot; use [[HashMap]] otherwise.
///
/// - Thread-safety: not-thread-safe. Synchronize externally if shared, including concurrent `Find` calls with a
///   writer.
/// - Ownership: owns its keys and values; the allocator must outlive the map.
template <typename K, typename V, typename Allocator = HeapAllocator> class DenseMap
{
    using Entry = DenseMapEntry<K, V>;

public:
    /// Creates an empty map on `allocator`; nothing is allocated until the first insert.
    ///
    /// - Param allocator: must outlive the map.
    explicit DenseMap(Allocator& allocator) : entries_(allocator), hashes_(allocator), index_(allocator) {}

    /// Creates an empty map on the shared default allocator.
    ///
    /// Only available when the allocator has a static `Default()`, as [[HeapAllocator]] does.
    DenseMap()
        requires requires { Allocator::Default(); }
        : DenseMap(Allocator::Default())
    {
    }

    DenseMap(const DenseMap&) = delete;
    DenseMap& operator=(const DenseMap&) = delete;
    /// Takes over `other`'s entries, leaving it empty.
    DenseMap(DenseMap&& other) noexcept = default;
    /// Destroys this map's entries, then takes over `other`'s.
    DenseMap& operator=(DenseMap&& other) noexcept = default;

    /// Returns the value of `key`, making it from `args` at the end if the key is absent; an existing value is left as
    /// it is.
    ///
    /// - Param key: the key; `K` must be constructible from it. Only copied or moved in when absent.
    /// - Param args: the arguments of `V`'s constructor; used only when the key is absent.
    /// - Returns: the value, its slot and whether it was made.
    /// - Aborts: the allocator is out of memory.
    template <typename KK, typename... Args>
        requires(IsConstructible<K, KK&&> && IsConstructible<V, Args&&...>)
    DenseMapEmplaced<V> Emplace(KK&& key, Args&&... args)
    {
        if constexpr (!kSearchesAs<KK>)
        {
            return Emplace(K(Forward<KK>(key)), Forward<Args>(args)...);  // hashed as K, never as another type
        }
        else
        {
            const usize hash = Hash<RemoveCVRef<KK>>{}(key);
            const u32 found = Lookup(key, hash);
            if (found != kNone)
            {
                return DenseMapEmplaced<V>{&entries_[found].value, found, false};
            }
            const u32 slot = static_cast<u32>(entries_.Size());
            entries_.Emplace(Entry{K(Forward<KK>(key)), V(Forward<Args>(args)...)});
            hashes_.Push(hash);
            IndexInsert(slot);
            return DenseMapEmplaced<V>{&entries_[slot].value, slot, true};
        }
    }

    /// Looks up the value of `key`.
    ///
    /// - Returns: the value, valid until the next insert or removal, or null when absent.
    /// - Fails: reports an absent key by returning null.
    V* Find(const K& key) { return FindBy(key); }
    /// Looks up the value of `key` in a const map.
    ///
    /// - Returns: the value, or null when absent.
    /// - Fails: reports an absent key by returning null.
    const V* Find(const K& key) const { return FindBy(key); }
    /// Looks up the value of the key equal to `key`, a value of another type.
    ///
    /// - Returns: the value, or null when absent.
    /// - Fails: reports an absent key by returning null.
    template <HeterogeneousKey<K> Q> V* Find(const Q& key) { return FindBy(key); }
    /// Looks up the value of the key equal to `key` in a const map.
    ///
    /// - Returns: the value, or null when absent.
    /// - Fails: reports an absent key by returning null.
    template <HeterogeneousKey<K> Q> const V* Find(const Q& key) const { return FindBy(key); }

    /// Returns true when `key` is present.
    bool Contains(const K& key) const { return SlotOf(key) != kNone; }
    /// Returns true when a key equal to `key` is present.
    template <HeterogeneousKey<K> Q> bool Contains(const Q& key) const { return SlotOf(key) != kNone; }

    /// Returns the slot of `key`, or `kNone`.
    u32 SlotOf(const K& key) const { return Lookup(key, Hash<K>{}(key)); }
    /// Returns the slot of the key equal to `key`, or `kNone`.
    template <HeterogeneousKey<K> Q> u32 SlotOf(const Q& key) const { return Lookup(key, Hash<Q>{}(key)); }

    /// Removes `key` if present; the last entry moves into its slot.
    ///
    /// - Param key: the key.
    /// - Param moved_from: if not null, receives the slot the moved entry came from (`kNone` when nothing moved).
    /// - Returns: true when the key was present.
    bool Remove(const K& key, u32* moved_from = nullptr) { return RemoveBy(key, moved_from); }
    /// Removes the key equal to `key`, a value of another type.
    ///
    /// - Returns: true when a key was present.
    template <HeterogeneousKey<K> Q> bool Remove(const Q& key, u32* moved_from = nullptr)
    {
        return RemoveBy(key, moved_from);
    }

    /// Removes the entry in `slot`; the last entry moves into it.
    ///
    /// - Param slot: below `Size()`.
    /// - Returns: the slot the moved entry came from, or `kNone` when the removed entry was the last.
    /// - Aborts: `slot` is not below `Size()`.
    u32 RemoveAt(u32 slot)
    {
        ENGINE_ASSERT(slot < entries_.Size(), "DenseMap::RemoveAt: the slot is past the end");
        IndexErase(slot);
        const u32 last = static_cast<u32>(entries_.Size() - 1);
        if (slot != last)
        {
            IndexRetarget(last, slot);
            hashes_[slot] = hashes_[last];
        }
        entries_.SwapRemove(slot);
        hashes_.Pop();
        return slot != last ? last : kNone;
    }

    /// Removes every entry for which `drop(key, value)` is true, in one pass from the last slot down (so each entry
    /// is visited once, whatever moves).
    ///
    /// - Param drop: called as `drop(const K&, V&)`.
    /// - Returns: how many were removed.
    template <typename Pred> usize RemoveIf(Pred drop)
    {
        usize removed = 0;
        for (usize i = entries_.Size(); i > 0; --i)
        {
            if (drop(static_cast<const K&>(entries_[i - 1].key), entries_[i - 1].value))
            {
                RemoveAt(static_cast<u32>(i - 1));
                ++removed;
            }
        }
        return removed;
    }

    /// Returns the value in `slot`.
    ///
    /// - Aborts: `slot` is not below `Size()`.
    V& At(u32 slot)
    {
        ENGINE_ASSERT(slot < entries_.Size(), "DenseMap::At: the slot is past the end");
        return entries_[slot].value;
    }
    /// Returns the value in `slot` of a const map.
    ///
    /// - Aborts: `slot` is not below `Size()`.
    const V& At(u32 slot) const
    {
        ENGINE_ASSERT(slot < entries_.Size(), "DenseMap::At: the slot is past the end");
        return entries_[slot].value;
    }
    /// Returns the key in `slot`.
    ///
    /// - Aborts: `slot` is not below `Size()`.
    const K& KeyAt(u32 slot) const
    {
        ENGINE_ASSERT(slot < entries_.Size(), "DenseMap::KeyAt: the slot is past the end");
        return entries_[slot].key;
    }

    /// Destroys every entry and keeps the storage.
    void Clear()
    {
        entries_.Clear();
        hashes_.Clear();
        for (usize i = 0; i < index_.Size(); ++i)
        {
            index_[i] = kEmpty;
        }
        tombstones_ = 0;
    }

    /// Makes room for `count` entries without growing.
    ///
    /// - Aborts: the allocator is out of memory.
    void Reserve(usize count)
    {
        entries_.Reserve(count);
        hashes_.Reserve(count);
        usize cap = 8;
        while (count * 10 >= cap * 7)
        {
            cap *= 2;
        }
        if (cap > index_.Size())
        {
            Reindex(cap);
        }
    }

    /// Returns the number of entries.
    usize Size() const { return entries_.Size(); }
    /// Returns true when there are no entries.
    bool Empty() const { return entries_.Empty(); }

    /// The first entry, for a range-for in slot order.
    ///
    /// - Internal: the range-for protocol.
    Entry* begin() { return entries_.begin(); }
    /// The end of a range-for.
    ///
    /// - Internal: the range-for protocol.
    Entry* end() { return entries_.end(); }
    /// The first entry of a const map, for a range-for.
    ///
    /// - Internal: the range-for protocol.
    const Entry* begin() const { return entries_.begin(); }
    /// The end of a range-for over a const map.
    ///
    /// - Internal: the range-for protocol.
    const Entry* end() const { return entries_.end(); }

    /// The slot number that means "none".
    static constexpr u32 kNone = 0xFFFFFFFFu;

private:
    static constexpr u32 kEmpty = 0;           // an index cell that ends a probe chain
    static constexpr u32 kTomb = 0xFFFFFFFFu;  // a removed cell: probing goes on past it
    // Any other cell holds slot + 1.

    template <typename KK>
    static constexpr bool kSearchesAs = IsSame<RemoveCVRef<KK>, K> || HeterogeneousKey<RemoveCVRef<KK>, K>;

    template <typename Q> u32 Lookup(const Q& key, usize hash) const
    {
        if (index_.Size() == 0)
        {
            return kNone;
        }
        const usize mask = index_.Size() - 1;
        for (usize i = hash & mask, probe = 0; probe < index_.Size(); ++probe, i = (i + 1) & mask)
        {
            const u32 cell = index_[i];
            if (cell == kEmpty)
            {
                return kNone;
            }
            if (cell != kTomb && hashes_[cell - 1] == hash && entries_[cell - 1].key == key)
            {
                return cell - 1;
            }
        }
        return kNone;
    }

    // The index cell holding `slot` (it is there: the slot is live).
    usize CellOf(u32 slot) const
    {
        const usize mask = index_.Size() - 1;
        usize i = hashes_[slot] & mask;
        while (index_[i] != slot + 1)
        {
            i = (i + 1) & mask;
        }
        return i;
    }

    void IndexInsert(u32 slot)
    {
        if ((entries_.Size() + tombstones_) * 10 >= index_.Size() * 7)
        {
            // Double only when the live entries need it; otherwise the same size, which clears the tombstones.
            const usize cap = index_.Size() == 0
                                  ? 8
                                  : (entries_.Size() * 20 >= index_.Size() * 7 ? index_.Size() * 2 : index_.Size());
            Reindex(cap);  // indexes every entry, `slot` included
            return;
        }
        const usize mask = index_.Size() - 1;
        usize i = hashes_[slot] & mask;
        while (index_[i] != kEmpty && index_[i] != kTomb)
        {
            i = (i + 1) & mask;
        }
        if (index_[i] == kTomb)
        {
            --tombstones_;
        }
        index_[i] = slot + 1;
    }

    void IndexErase(u32 slot)
    {
        index_[CellOf(slot)] = kTomb;
        ++tombstones_;
    }

    void IndexRetarget(u32 from, u32 to) { index_[CellOf(from)] = to + 1; }

    void Reindex(usize capacity)
    {
        index_.Clear();
        index_.Resize(capacity);  // zero: every cell empty
        tombstones_ = 0;
        const usize mask = capacity - 1;
        for (usize slot = 0; slot < entries_.Size(); ++slot)
        {
            usize i = hashes_[slot] & mask;
            while (index_[i] != kEmpty)
            {
                i = (i + 1) & mask;
            }
            index_[i] = static_cast<u32>(slot + 1);
        }
    }

    template <typename Q> V* FindBy(const Q& key)
    {
        const u32 slot = Lookup(key, Hash<Q>{}(key));
        return slot != kNone ? &entries_[slot].value : nullptr;
    }

    template <typename Q> const V* FindBy(const Q& key) const
    {
        const u32 slot = Lookup(key, Hash<Q>{}(key));
        return slot != kNone ? &entries_[slot].value : nullptr;
    }

    template <typename Q> bool RemoveBy(const Q& key, u32* moved_from)
    {
        const u32 slot = Lookup(key, Hash<Q>{}(key));
        if (slot == kNone)
        {
            if (moved_from != nullptr)
            {
                *moved_from = kNone;
            }
            return false;
        }
        const u32 moved = RemoveAt(slot);
        if (moved_from != nullptr)
        {
            *moved_from = moved;
        }
        return true;
    }

    Vector<Entry, Allocator> entries_;
    Vector<usize, Allocator> hashes_;  // the hash of each entry's key, by slot
    Vector<u32, Allocator> index_;     // power of two; kEmpty, kTomb or slot + 1
    usize tombstones_ = 0;
};

}  // namespace vse
