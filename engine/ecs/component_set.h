#pragma once

//! A set of component ids as a 128-bit mask: the signature of an archetype and of a query.

#include "containers/hash_map.h"
#include "ecs/component.h"
#include "log/assert.h"
#include "types.h"

namespace engine_placeholder::ecs
{

/// A set of [[ecs::ComponentId]]s below [[ecs::kMaxComponentTypes]], stored as two 64-bit words.
///
/// - Thread-safety: thread-compatible value.
/// - Ownership: owns nothing.
struct ComponentSet
{
    /// The two words; bit `id % 64` of word `id / 64`.
    u64 words[2] = {0, 0};

    /// Adds `id`.
    /// - Aborts: `id` is not below [[ecs::kMaxComponentTypes]].
    void Add(ComponentId id)
    {
        ENGINE_ASSERT(id < kMaxComponentTypes, "ComponentSet::Add: the id is out of range");
        words[id >> 6] |= u64{1} << (id & 63u);
    }
    /// Removes `id` (no effect when absent).
    /// - Aborts: `id` is not below [[ecs::kMaxComponentTypes]].
    void Remove(ComponentId id)
    {
        ENGINE_ASSERT(id < kMaxComponentTypes, "ComponentSet::Remove: the id is out of range");
        words[id >> 6] &= ~(u64{1} << (id & 63u));
    }
    /// Returns whether `id` is in the set (false for an id out of range).
    /// - Fails: never.
    bool Has(ComponentId id) const { return id < kMaxComponentTypes && ((words[id >> 6] >> (id & 63u)) & 1u) != 0; }
    /// Returns whether the set is empty.
    /// - Fails: never.
    bool IsEmpty() const { return (words[0] | words[1]) == 0; }
    /// The number of ids in the set.
    /// - Fails: never.
    u32 Count() const { return Popcount(words[0]) + Popcount(words[1]); }
    /// Returns whether every id of this set is in `other`.
    /// - Fails: never.
    bool IsSubsetOf(const ComponentSet& other) const
    {
        return (words[0] & ~other.words[0]) == 0 && (words[1] & ~other.words[1]) == 0;
    }
    /// Returns whether the two sets share an id.
    /// - Fails: never.
    bool Intersects(const ComponentSet& other) const
    {
        return ((words[0] & other.words[0]) | (words[1] & other.words[1])) != 0;
    }
    /// A hash of the set.
    /// - Fails: never.
    u64 Hash() const
    {
        u64 h = words[0] * 0x9E3779B97F4A7C15ull;
        h ^= h >> 32;
        h += words[1] * 0xC2B2AE3D27D4EB4Full;
        h ^= h >> 29;
        return h;
    }
    /// The smallest id in the set that is `>= from`, or [[ecs::kInvalidComponent]] when none.
    ///
    /// Iterate with `for (id = set.NextFrom(0); id != kInvalidComponent; id = set.NextFrom(id + 1))` (ascending id
    /// order).
    /// - Fails: never.
    ComponentId NextFrom(u32 from) const
    {
        for (u32 word = from >> 6; word < 2; ++word)
        {
            u64 bits = words[word];
            if (word == (from >> 6))
            {
                bits &= ~u64{0} << (from & 63u);
            }
            if (bits != 0)
            {
                return static_cast<ComponentId>(word * 64 + CountTrailingZeros(bits));
            }
        }
        return kInvalidComponent;
    }

    friend bool operator==(const ComponentSet& a, const ComponentSet& b)
    {
        return a.words[0] == b.words[0] && a.words[1] == b.words[1];
    }
    friend bool operator!=(const ComponentSet& a, const ComponentSet& b) { return !(a == b); }

private:
    static u32 Popcount(u64 v)
    {
        u32 n = 0;
        for (; v != 0; v &= v - 1)
        {
            ++n;
        }
        return n;
    }
    static u32 CountTrailingZeros(u64 v)
    {
        u32 n = 0;
        while ((v & 1u) == 0)
        {
            v >>= 1;
            ++n;
        }
        return n;
    }
};

}  // namespace engine_placeholder::ecs

namespace engine_placeholder
{

/// The hash of a component set, for `HashMap<ecs::ComponentSet, ...>`.
///
/// - Thread-safety: thread-safe (stateless).
/// - Fails: never.
template <> struct Hash<ecs::ComponentSet>
{
    /// Returns [[ecs::ComponentSet::Hash]].
    usize operator()(const ecs::ComponentSet& set) const { return static_cast<usize>(set.Hash()); }
};

}  // namespace engine_placeholder
