#pragma once

#include "containers/array.h"
#include "types.h"

// Open-addressing hash map (linear probing + tombstones for deletion),
// backed by Day 6's `Array<T, Allocator>` for slot storage. PLAN.md
// assumption #2: hand-rolled rather than std::unordered_map.
namespace engine_placeholder {

namespace detail {

// 64-bit finalizer mix (Murmur3-style fmix64) — decent avalanche for
// small integer/pointer keys without needing a full hash algorithm.
inline usize MixHash64(u64 x) {
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return static_cast<usize>(x);
}

} // namespace detail

// Primary template intentionally undefined — specialize for each key
// type actually used. Integer and pointer keys are covered here;
// string keys are added once Day 8's string type exists (not needed
// yet, see this day's plan notes).
template <typename K>
struct Hash;

template <>
struct Hash<i8> {
    usize operator()(i8 k) const { return detail::MixHash64(static_cast<u64>(static_cast<u8>(k))); }
};
template <>
struct Hash<i16> {
    usize operator()(i16 k) const { return detail::MixHash64(static_cast<u64>(static_cast<u16>(k))); }
};
template <>
struct Hash<i32> {
    usize operator()(i32 k) const { return detail::MixHash64(static_cast<u64>(static_cast<u32>(k))); }
};
template <>
struct Hash<i64> {
    usize operator()(i64 k) const { return detail::MixHash64(static_cast<u64>(k)); }
};
template <>
struct Hash<u8> {
    usize operator()(u8 k) const { return detail::MixHash64(static_cast<u64>(k)); }
};
template <>
struct Hash<u16> {
    usize operator()(u16 k) const { return detail::MixHash64(static_cast<u64>(k)); }
};
template <>
struct Hash<u32> {
    usize operator()(u32 k) const { return detail::MixHash64(static_cast<u64>(k)); }
};
template <>
struct Hash<u64> {
    usize operator()(u64 k) const { return detail::MixHash64(k); }
};

template <typename T>
struct Hash<T*> {
    usize operator()(T* k) const {
        return detail::MixHash64(reinterpret_cast<u64>(k));
    }
};

template <typename K, typename V, typename Allocator>
class HashMap {
public:
    explicit HashMap(Allocator& allocator, usize initial_capacity = 16)
        : allocator_(&allocator), slots_(allocator) {
        Rehash(initial_capacity < 8 ? 8 : initial_capacity);
    }

    HashMap(const HashMap&) = delete;
    HashMap& operator=(const HashMap&) = delete;

    void Insert(const K& key, const V& value) {
        if (NeedsGrow()) {
            Rehash(slots_.Capacity() * 2);
        }
        InsertNoRehashCheck(key, value);
    }

    // Returns nullptr if `key` isn't present.
    V* Find(const K& key) {
        usize index;
        if (!FindSlotForLookup(key, index)) {
            return nullptr;
        }
        return &slots_[index].value;
    }

    const V* Find(const K& key) const {
        usize index;
        if (!FindSlotForLookup(key, index)) {
            return nullptr;
        }
        return &slots_[index].value;
    }

    // Returns true if `key` was present and is now removed.
    bool Remove(const K& key) {
        usize index;
        if (!FindSlotForLookup(key, index)) {
            return false;
        }
        slots_[index].state = SlotState::Tombstone;
        --count_;
        return true;
    }

    usize Size() const { return count_; }

private:
    enum class SlotState : u8 { Empty, Occupied, Tombstone };

    struct Slot {
        SlotState state = SlotState::Empty;
        K key{};
        V value{};
    };

    bool NeedsGrow() const {
        // Load factor >= 0.7 triggers growth.
        return (count_ + 1) * 10 >= slots_.Capacity() * 7;
    }

    usize FindSlotForInsert(const K& key) const {
        usize cap = slots_.Capacity();
        usize start = Hash<K>{}(key) % cap;
        usize first_tombstone = cap; // cap == "none found yet"
        for (usize probe = 0; probe < cap; ++probe) {
            usize i = (start + probe) % cap;
            const Slot& s = slots_[i];
            if (s.state == SlotState::Empty) {
                return first_tombstone != cap ? first_tombstone : i;
            }
            if (s.state == SlotState::Occupied && s.key == key) {
                return i;
            }
            if (s.state == SlotState::Tombstone && first_tombstone == cap) {
                first_tombstone = i;
            }
        }
        // Unreachable in practice — NeedsGrow() keeps the table from
        // ever filling completely — but fall back safely if it happens.
        return first_tombstone != cap ? first_tombstone : 0;
    }

    bool FindSlotForLookup(const K& key, usize& out_index) const {
        usize cap = slots_.Capacity();
        usize start = Hash<K>{}(key) % cap;
        for (usize probe = 0; probe < cap; ++probe) {
            usize i = (start + probe) % cap;
            const Slot& s = slots_[i];
            if (s.state == SlotState::Empty) {
                return false; // probe chain ended: definitely absent.
            }
            if (s.state == SlotState::Occupied && s.key == key) {
                out_index = i;
                return true;
            }
            // Tombstone (or occupied-but-different-key): keep probing.
        }
        return false;
    }

    void InsertNoRehashCheck(const K& key, const V& value) {
        usize index = FindSlotForInsert(key);
        Slot& slot = slots_[index];
        bool was_new = slot.state != SlotState::Occupied;
        slot.state = SlotState::Occupied;
        slot.key = key;
        slot.value = value;
        if (was_new) {
            ++count_;
        }
    }

    void Rehash(usize new_capacity) {
        // Snapshot every currently-occupied pair.
        Array<Slot, Allocator> occupied(*allocator_);
        for (usize i = 0; i < slots_.Size(); ++i) {
            if (slots_[i].state == SlotState::Occupied) {
                occupied.Push(slots_[i]);
            }
        }

        // Reset slots_ to `new_capacity` fresh Empty slots, reusing
        // Array's own Push/Pop rather than needing Array to support
        // move-assignment (it doesn't, by Day 6 design).
        while (slots_.Size() > 0) {
            slots_.Pop();
        }
        slots_.Reserve(new_capacity);
        for (usize i = 0; i < new_capacity; ++i) {
            slots_.Push(Slot{});
        }

        count_ = 0;
        for (usize i = 0; i < occupied.Size(); ++i) {
            InsertNoRehashCheck(occupied[i].key, occupied[i].value);
        }
    }

    Allocator* allocator_ = nullptr;
    Array<Slot, Allocator> slots_;
    usize count_ = 0;
};

} // namespace engine_placeholder
