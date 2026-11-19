#pragma once

#include "containers/array.h"
#include "containers/vector.h"
#include "lang/new.h"
#include "lang/traits.h"
#include "lang/utility.h"
#include "string_view.h"
#include "types.h"

//! Open-addressing hash map (linear probing, tombstone deletion) whose slot
//! storage is a [[Vector]], plus the `Hash` function-object templates it uses.
namespace engine_placeholder
{

namespace detail
{

// 64-bit finalizer mix (Murmur3-style fmix64) — decent avalanche for
// small integer/pointer keys without needing a full hash algorithm.
inline usize MixHash64(u64 x)
{
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return static_cast<usize>(x);
}

}  // namespace detail

/// Hash function object used by [[HashMap]]: `Hash<K>{}(key)` returns a `usize` hash.
///
/// The primary template is intentionally undefined; specialize it for each key
/// type used. Specializations exist for the fixed-width integer types, for
/// pointers, for [[StringView]] and (in `string/string.h`) for [[String]].
template <typename K> struct Hash;

/// Hash function for `i8` keys.
template <> struct Hash<i8>
{
    /// Returns a well-mixed hash of the integer value.
    usize operator()(i8 k) const { return detail::MixHash64(static_cast<u64>(static_cast<u8>(k))); }
};
/// Hash function for `i16` keys.
template <> struct Hash<i16>
{
    /// Returns a well-mixed hash of the integer value.
    usize operator()(i16 k) const { return detail::MixHash64(static_cast<u64>(static_cast<u16>(k))); }
};
/// Hash function for `i32` keys.
template <> struct Hash<i32>
{
    /// Returns a well-mixed hash of the integer value.
    usize operator()(i32 k) const { return detail::MixHash64(static_cast<u64>(static_cast<u32>(k))); }
};
/// Hash function for `i64` keys.
template <> struct Hash<i64>
{
    /// Returns a well-mixed hash of the integer value.
    usize operator()(i64 k) const { return detail::MixHash64(static_cast<u64>(k)); }
};
/// Hash function for `u8` keys.
template <> struct Hash<u8>
{
    /// Returns a well-mixed hash of the integer value.
    usize operator()(u8 k) const { return detail::MixHash64(static_cast<u64>(k)); }
};
/// Hash function for `u16` keys.
template <> struct Hash<u16>
{
    /// Returns a well-mixed hash of the integer value.
    usize operator()(u16 k) const { return detail::MixHash64(static_cast<u64>(k)); }
};
/// Hash function for `u32` keys.
template <> struct Hash<u32>
{
    /// Returns a well-mixed hash of the integer value.
    usize operator()(u32 k) const { return detail::MixHash64(static_cast<u64>(k)); }
};
/// Hash function for `u64` keys.
template <> struct Hash<u64>
{
    /// Returns a well-mixed hash of the integer value.
    usize operator()(u64 k) const { return detail::MixHash64(k); }
};

/// Hash function for `usize` and `isize` keys where they are types of their own: on macOS `usize` is `unsigned long`
/// and `u64` is `unsigned long long` (Linux and Windows make them the same type, so there the `u64` and `i64` hashes
/// serve). Phase 21 Day 285.
template <typename K>
    requires((IsSame<K, usize> && !IsSame<usize, u64>) || (IsSame<K, isize> && !IsSame<isize, i64>))
struct Hash<K>
{
    // Returns a well-mixed hash of the integer value (a plain comment: docgen does not attach a doc comment inside a
    // constrained specialization).
    usize operator()(K k) const { return detail::MixHash64(static_cast<u64>(k)); }
};

/// Hash function for pointer keys; hashes the address, not the pointee.
template <typename T> struct Hash<T*>
{
    /// Returns a well-mixed hash of the pointer's address.
    usize operator()(T* k) const { return detail::MixHash64(reinterpret_cast<u64>(k)); }
};

/// Hash function for [[StringView]] keys (Phase 27 Day 474): FNV-1a over the bytes, then mixed. A string type that
/// hashes as a key gives the same value as the view of its contents, so a map keyed by strings can be searched with a
/// view (`String<A>`'s hash is in `string/string.h`).
template <> struct Hash<StringView>
{
    /// Returns a well-mixed hash of the bytes.
    usize operator()(StringView k) const
    {
        u64 h = 0xcbf29ce484222325ULL;
        for (usize i = 0; i < k.Length(); ++i)
        {
            h ^= static_cast<u8>(k[i]);
            h *= 0x100000001b3ULL;
        }
        return detail::MixHash64(h);
    }
};

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

/// Hash map from `K` to `V` using open addressing with linear probing.
///
/// `K` needs `operator==` and a `Hash<K>` specialization. Keys and values are stored by value, constructed when they
/// are inserted (copied or moved in) and destroyed when they are removed, when the table is rehashed into a new one
/// (they are moved across) and with the map; neither needs a default constructor, and either may be move-only (a
/// [[String]] key, Phase 27 Day 474). A key can also be looked up by a type `Q` it compares equal to (`K == Q`) whose
/// `Hash<Q>` agrees with `Hash<K>` for equal keys: a [[StringView]] for a [[String]] key. The table starts with at
/// least 8 slots and rehashes when the load factor, counting tombstones, reaches 0.7: into twice the slots if the live
/// entries alone fill at least half of that, otherwise into the same number (which clears the tombstones left by
/// removals). Growth invalidates pointers returned by `Find`. `ForEach` visits the entries in no particular
/// order.
///
/// - Thread-safety: not-thread-safe. Synchronize externally if shared, including
///   concurrent `Find` calls with a writer.
/// - Ownership: the map owns its keys and values; its storage comes from the allocator, which must outlive the map, and
///   is released as a [[Vector]] releases it (freed when the allocator has `Free`, else abandoned inside it).
template <typename K, typename V, typename Allocator> class HashMap
{
public:
    /// Creates an empty map and allocates its initial table.
    ///
    /// - Param allocator: allocator for table storage; held by pointer and must
    ///   outlive the map.
    /// - Param initial_capacity: initial slot count; values below 8 are raised
    ///   to 8.
    /// - Aborts: the allocator is out of memory.
    explicit HashMap(Allocator& allocator, usize initial_capacity = 16) : allocator_(&allocator), slots_(allocator)
    {
        Rehash(initial_capacity < 8 ? 8 : initial_capacity);
    }

    /// Destroys every key and value.
    ~HashMap() { DestroyAll(); }

    HashMap(const HashMap&) = delete;
    HashMap& operator=(const HashMap&) = delete;

    /// Takes over `other`'s entries and table, leaving `other` empty (it may be used again: its next `Insert` makes a
    /// table).
    HashMap(HashMap&& other) noexcept
        : allocator_(other.allocator_), slots_(Move(other.slots_)), count_(other.count_),
          tombstone_count_(other.tombstone_count_)
    {
        other.count_ = 0;
        other.tombstone_count_ = 0;
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
            other.count_ = 0;
            other.tombstone_count_ = 0;
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
        if (NeedsGrow())
        {
            // The arguments may refer to entries of this table, which the rehash moves and frees: take them first.
            K held_key(Forward<KK>(key));
            V held_value(Forward<VV>(value));
            Grow();
            Put(Move(held_key), Move(held_value));
            return;
        }
        Put(Forward<KK>(key), Forward<VV>(value));
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

    /// Calls `visit(key, value)` for every entry, in no particular order. `visit` must not insert into or remove from
    /// the map.
    ///
    /// - Param visit: called as `visit(const K&, V&)`.
    template <typename Visit> void ForEach(Visit visit)
    {
        for (usize i = 0; i < slots_.Size(); ++i)
        {
            if (slots_[i].state == SlotState::Occupied)
            {
                visit(KeyAt(i), ValueAt(i));
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
            if (slots_[i].state == SlotState::Occupied)
            {
                visit(KeyAt(i), ValueAt(i));
            }
        }
    }

    /// Returns the number of keys currently stored.
    usize Size() const { return count_; }

    /// Returns the number of slots in the table (capacity, not element count).
    ///
    /// Exposed so tests can observe growth; production code should not need it.
    usize SlotCapacity() const { return slots_.Size(); }

    /// Returns the number of slots holding a removed entry's tombstone, which the next rehash clears.
    ///
    /// - Returns: the tombstone count.
    /// - Thread-safety: thread-compatible.
    usize TombstoneCount() const { return tombstone_count_; }

private:
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
        Slot() {}  // leaves the rooms uninitialized
        SlotState state = SlotState::Empty;
        Room<K> key;
        Room<V> value;
    };

    K& KeyAt(usize i) { return slots_[i].key.Get(); }
    const K& KeyAt(usize i) const { return slots_[i].key.Get(); }
    V& ValueAt(usize i) { return slots_[i].value.Get(); }
    const V& ValueAt(usize i) const { return slots_[i].value.Get(); }

    void DestroyAt(usize i)
    {
        KeyAt(i).~K();
        ValueAt(i).~V();
    }

    void DestroyAll()
    {
        for (usize i = 0; i < slots_.Size(); ++i)
        {
            if (slots_[i].state == SlotState::Occupied)
            {
                DestroyAt(i);
                slots_[i].state = SlotState::Empty;
            }
        }
        count_ = 0;
        tombstone_count_ = 0;
    }

    bool NeedsGrow() const
    {
        // Load factor >= 0.7 triggers growth, counting tombstones along
        // with occupied slots: a tombstone still costs a probe step (it
        // isn't Empty, so lookups/inserts keep scanning past it) and is
        // only reclaimed by a rehash, so insert/remove churn that never
        // grows `count_` must still eventually trigger one — otherwise
        // repeated insert-then-remove cycles fill the table with
        // tombstones forever without ever rehashing them away.
        return (count_ + tombstone_count_ + 1) * 10 >= slots_.Size() * 7;
    }

    // Grows the table: double only when the live entries need the room; when the load is mostly tombstones
    // (insert/remove churn), rehash at the same size, which clears them, so churn does not grow the table without
    // bound. A moved-from map has no table: it gets the smallest one.
    void Grow()
    {
        const usize cap = slots_.Size();
        const bool live_needs_room = (count_ + 1) * 20 >= cap * 7;  // live load >= 0.35
        const usize next = live_needs_room ? cap * 2 : cap;
        Rehash(next < 8 ? 8 : next);
    }

    // Stores the entry; the table has room.
    template <typename KK, typename VV> void Put(KK&& key, VV&& value)
    {
        const usize index = FindSlotForInsert(key);
        if (slots_[index].state == SlotState::Occupied)
        {
            ValueAt(index) = V(Forward<VV>(value));
            return;
        }
        ConstructAt<K>(slots_[index].key.bytes.Data(), Forward<KK>(key));
        ConstructAt<V>(slots_[index].value.bytes.Data(), Forward<VV>(value));
        slots_[index].state = SlotState::Occupied;
        ++count_;
    }

    template <typename Q> V* FindBy(const Q& key)
    {
        usize index;
        if (!FindSlotForLookup(key, index))
        {
            return nullptr;
        }
        return &ValueAt(index);
    }

    template <typename Q> const V* FindBy(const Q& key) const
    {
        usize index;
        if (!FindSlotForLookup(key, index))
        {
            return nullptr;
        }
        return &ValueAt(index);
    }

    template <typename Q> bool RemoveBy(const Q& key)
    {
        usize index;
        if (!FindSlotForLookup(key, index))
        {
            return false;
        }
        DestroyAt(index);
        slots_[index].state = SlotState::Tombstone;
        --count_;
        ++tombstone_count_;
        return true;
    }

    template <typename Q> usize FindSlotForInsert(const Q& key) const
    {
        usize cap = slots_.Size();
        usize start = Hash<Q>{}(key) % cap;
        usize first_tombstone = cap;  // cap == "none found yet"
        for (usize probe = 0; probe < cap; ++probe)
        {
            usize i = (start + probe) % cap;
            if (slots_[i].state == SlotState::Empty)
            {
                return first_tombstone != cap ? first_tombstone : i;
            }
            if (slots_[i].state == SlotState::Occupied && KeyAt(i) == key)
            {
                return i;
            }
            if (slots_[i].state == SlotState::Tombstone && first_tombstone == cap)
            {
                first_tombstone = i;
            }
        }
        // Not expected to run — NeedsGrow() (which now counts
        // tombstones, not just occupied slots) keeps every Insert() from
        // letting the table fill completely — but fall back safely
        // rather than assume it can never happen.
        return first_tombstone != cap ? first_tombstone : 0;
    }

    template <typename Q> bool FindSlotForLookup(const Q& key, usize& out_index) const
    {
        usize cap = slots_.Size();
        if (cap == 0)
        {
            return false;  // a moved-from map
        }
        usize start = Hash<Q>{}(key) % cap;
        for (usize probe = 0; probe < cap; ++probe)
        {
            usize i = (start + probe) % cap;
            if (slots_[i].state == SlotState::Empty)
            {
                return false;  // probe chain ended: definitely absent.
            }
            if (slots_[i].state == SlotState::Occupied && KeyAt(i) == key)
            {
                out_index = i;
                return true;
            }
            // Tombstone (or occupied-but-different-key): keep probing.
        }
        return false;
    }

    // Moves every entry into a fresh table of `new_capacity` slots; the old table's keys and values are destroyed
    // after their move. The slot vector is never grown in place, so no live key is relocated bytewise.
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
        for (usize i = 0; i < old.Size(); ++i)
        {
            if (old[i].state != SlotState::Occupied)
            {
                continue;
            }
            K& key = old[i].key.Get();
            V& value = old[i].value.Get();
            Put(Move(key), Move(value));
            key.~K();
            value.~V();
        }
    }

    Allocator* allocator_ = nullptr;
    Vector<Slot, Allocator> slots_;
    usize count_ = 0;
    usize tombstone_count_ = 0;
};

}  // namespace engine_placeholder
