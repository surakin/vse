#pragma once

#include "containers/vector.h"
#include "lang/traits.h"
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
/// type used. Specializations exist for the fixed-width integer types and for
/// pointers.
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
/// serve). Phase 26 Day 301.
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

/// Hash map from `K` to `V` using open addressing with linear probing.
///
/// `K` needs `operator==`, a `Hash<K>` specialization, and a default
/// constructor; `V` needs a default constructor. Keys and values are copied in
/// and stored by value. The table starts with at least 8 slots and rehashes when
/// the load factor, counting tombstones, reaches 0.7: into twice the slots if the
/// live entries alone fill at least half of that, otherwise into the same number
/// (which clears the tombstones left by removals). Growth rehashes into a new
/// block and abandons the old one inside the allocator, as with [[Vector]].
/// Growth invalidates pointers returned by `Find`. There is no iteration
/// support.
///
/// - Thread-safety: not-thread-safe. Synchronize externally if shared, including
///   concurrent `Find` calls with a writer.
/// - Ownership: the allocator owns all storage and must outlive the map; the map
///   never frees memory.
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

    HashMap(const HashMap&) = delete;
    HashMap& operator=(const HashMap&) = delete;

    /// Inserts `key` mapped to `value`, replacing the value if the key is
    /// already present.
    ///
    /// May rehash, which invalidates pointers returned by `Find`.
    ///
    /// - Param key: key to copy in.
    /// - Param value: value to copy in.
    /// - Aborts: growth is needed and the allocator is out of memory.
    void Insert(const K& key, const V& value)
    {
        if (NeedsGrow())
        {
            // Double only when the live entries need the room; when the load is mostly tombstones (insert/remove
            // churn), rehash at the same size, which clears them, so churn does not grow the table without bound.
            const bool live_needs_room = (count_ + 1) * 20 >= slots_.Capacity() * 7;  // live load >= 0.35
            Rehash(live_needs_room ? slots_.Capacity() * 2 : slots_.Capacity());
        }
        InsertNoRehashCheck(key, value);
    }

    /// Looks up the value stored for `key`.
    ///
    /// - Param key: key to search for.
    /// - Returns: a pointer to the stored value, valid until the next `Insert`,
    ///   or nullptr if `key` is absent.
    /// - Fails: reports an absent key by returning nullptr.
    V* Find(const K& key)
    {
        usize index;
        if (!FindSlotForLookup(key, index))
        {
            return nullptr;
        }
        return &slots_[index].value;
    }

    /// Looks up the value stored for `key` in a const map.
    ///
    /// - Param key: key to search for.
    /// - Returns: a pointer to the stored value, valid until the next `Insert`,
    ///   or nullptr if `key` is absent.
    /// - Fails: reports an absent key by returning nullptr.
    const V* Find(const K& key) const
    {
        usize index;
        if (!FindSlotForLookup(key, index))
        {
            return nullptr;
        }
        return &slots_[index].value;
    }

    /// Removes `key` if present, leaving a tombstone until the next rehash.
    ///
    /// - Param key: key to remove.
    /// - Returns: true if the key was present and has been removed, false if it
    ///   was absent.
    bool Remove(const K& key)
    {
        usize index;
        if (!FindSlotForLookup(key, index))
        {
            return false;
        }
        slots_[index].state = SlotState::Tombstone;
        --count_;
        ++tombstone_count_;
        return true;
    }

    /// Returns the number of keys currently stored.
    usize Size() const { return count_; }

    /// Returns the number of slots in the table (capacity, not element count).
    ///
    /// Exposed so tests can observe growth; production code should not need it.
    usize SlotCapacity() const { return slots_.Capacity(); }

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

    struct Slot
    {
        SlotState state = SlotState::Empty;
        K key{};
        V value{};
    };

    bool NeedsGrow() const
    {
        // Load factor >= 0.7 triggers growth, counting tombstones along
        // with occupied slots: a tombstone still costs a probe step (it
        // isn't Empty, so lookups/inserts keep scanning past it) and is
        // only reclaimed by a rehash, so insert/remove churn that never
        // grows `count_` must still eventually trigger one — otherwise
        // repeated insert-then-remove cycles fill the table with
        // tombstones forever without ever rehashing them away.
        return (count_ + tombstone_count_ + 1) * 10 >= slots_.Capacity() * 7;
    }

    usize FindSlotForInsert(const K& key) const
    {
        usize cap = slots_.Capacity();
        usize start = Hash<K>{}(key) % cap;
        usize first_tombstone = cap;  // cap == "none found yet"
        for (usize probe = 0; probe < cap; ++probe)
        {
            usize i = (start + probe) % cap;
            const Slot& s = slots_[i];
            if (s.state == SlotState::Empty)
            {
                return first_tombstone != cap ? first_tombstone : i;
            }
            if (s.state == SlotState::Occupied && s.key == key)
            {
                return i;
            }
            if (s.state == SlotState::Tombstone && first_tombstone == cap)
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

    bool FindSlotForLookup(const K& key, usize& out_index) const
    {
        usize cap = slots_.Capacity();
        usize start = Hash<K>{}(key) % cap;
        for (usize probe = 0; probe < cap; ++probe)
        {
            usize i = (start + probe) % cap;
            const Slot& s = slots_[i];
            if (s.state == SlotState::Empty)
            {
                return false;  // probe chain ended: definitely absent.
            }
            if (s.state == SlotState::Occupied && s.key == key)
            {
                out_index = i;
                return true;
            }
            // Tombstone (or occupied-but-different-key): keep probing.
        }
        return false;
    }

    void InsertNoRehashCheck(const K& key, const V& value)
    {
        usize index = FindSlotForInsert(key);
        Slot& slot = slots_[index];
        bool was_new = slot.state != SlotState::Occupied;
        slot.state = SlotState::Occupied;
        slot.key = key;
        slot.value = value;
        if (was_new)
        {
            ++count_;
        }
    }

    void Rehash(usize new_capacity)
    {
        // Snapshot every currently-occupied pair.
        Vector<Slot, Allocator> occupied(*allocator_);
        for (usize i = 0; i < slots_.Size(); ++i)
        {
            if (slots_[i].state == SlotState::Occupied)
            {
                occupied.Push(slots_[i]);
            }
        }

        // Reset slots_ to `new_capacity` fresh Empty slots, reusing
        // Vector's own Push/Pop rather than needing Vector to support
        // move-assignment (it doesn't, by Day 6 design).
        while (slots_.Size() > 0)
        {
            slots_.Pop();
        }
        slots_.Reserve(new_capacity);
        for (usize i = 0; i < new_capacity; ++i)
        {
            slots_.Push(Slot{});
        }

        count_ = 0;
        tombstone_count_ = 0;
        for (usize i = 0; i < occupied.Size(); ++i)
        {
            InsertNoRehashCheck(occupied[i].key, occupied[i].value);
        }
    }

    Allocator* allocator_ = nullptr;
    Vector<Slot, Allocator> slots_;
    usize count_ = 0;
    usize tombstone_count_ = 0;
};

}  // namespace engine_placeholder
