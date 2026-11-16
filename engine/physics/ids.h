// The ids of the physics world (Phase 25 Day 376): an index and a generation, so a stale id is refused instead of
// reaching another body.
#pragma once

#include "types.h"

namespace engine_placeholder::physics
{

/// The id of a body in a [[physics::PhysicsWorld]]: a slot index and a generation (zero is invalid).
///
/// Ids are reused in a fixed order (the last freed slot first), so two worlds built by the same calls hand out the same
/// ids.
///
/// - Thread-safety: thread-compatible plain data.
struct BodyId
{
    /// The slot.
    u32 index = 0;
    /// The slot's generation when the id was made; 0 means invalid.
    u32 generation = 0;

    /// Returns whether this is not the null id (it may still be stale).
    ///
    /// - Returns: true unless the generation is zero.
    /// - Fails: never.
    constexpr bool IsValid() const { return generation != 0; }

    /// Packs the id into 64 bits (generation in the high half), for hashing, logging and the C API.
    ///
    /// - Returns: the packed value.
    /// - Fails: never.
    constexpr u64 ToU64() const { return (static_cast<u64>(generation) << 32) | index; }

    /// Returns whether two ids are the same.
    ///
    /// - Param other: the other id.
    /// - Returns: true if the slot and the generation are equal.
    /// - Fails: never.
    constexpr bool operator==(const BodyId& other) const
    {
        return index == other.index && generation == other.generation;
    }
};

/// The id of a collider in a [[physics::PhysicsWorld]]; made like [[physics::BodyId]].
///
/// - Thread-safety: thread-compatible plain data.
struct ColliderId
{
    /// The slot.
    u32 index = 0;
    /// The slot's generation when the id was made; 0 means invalid.
    u32 generation = 0;

    /// Returns whether this is not the null id (it may still be stale).
    ///
    /// - Returns: true unless the generation is zero.
    /// - Fails: never.
    constexpr bool IsValid() const { return generation != 0; }

    /// Packs the id into the key the spatial module orders by: the slot in the high half, so keys sort by slot.
    ///
    /// - Returns: the key.
    /// - Fails: never.
    constexpr u64 ToKey() const { return (static_cast<u64>(index) << 32) | generation; }

    /// Unpacks a key made by `ToKey`.
    ///
    /// - Param key: the key.
    /// - Returns: the id.
    /// - Fails: never.
    static constexpr ColliderId FromKey(u64 key)
    {
        return ColliderId{static_cast<u32>(key >> 32), static_cast<u32>(key & 0xFFFFFFFFu)};
    }

    /// Returns whether two ids are the same.
    ///
    /// - Param other: the other id.
    /// - Returns: true if the slot and the generation are equal.
    /// - Fails: never.
    constexpr bool operator==(const ColliderId& other) const
    {
        return index == other.index && generation == other.generation;
    }
};

}  // namespace engine_placeholder::physics
