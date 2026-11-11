#pragma once

//! The identity of a thing in an ECS world: a generation-checked 64-bit id.

#include "containers/hash_map.h"
#include "types.h"

namespace engine_placeholder::ecs {

/// A reference to an entity: a slot index and the slot's generation.
///
/// The all-zero value is invalid; an id whose entity was destroyed no longer matches even if its slot is reused.
///
/// - Thread-safety: thread-safe plain value.
/// - Ownership: owns nothing.
struct Entity {
    /// The slot in the entity table.
    u32 index = 0;
    /// The slot's generation when the id was made; 0 means invalid.
    u32 generation = 0;

    /// Returns whether this is not the invalid (all-zero) id; a valid-looking id can still be stale.
    /// - Fails: never.
    constexpr bool IsValid() const { return generation != 0; }
    /// The generation that marks a command-buffer placeholder; the entity table never issues it (it wraps past it to 1).
    static constexpr u32 kPlaceholderGeneration = 0xFFFFFFFFu;
    /// Returns whether this is a placeholder from a [[ecs::CommandBuffer]] (an entity that is created when the buffer is applied): `index` is its number in the buffer.
    /// - Fails: never.
    constexpr bool IsPlaceholder() const { return generation == kPlaceholderGeneration; }
    /// Packs the id into 64 bits (generation in the high half), for hashing, logging and storage.
    /// - Fails: never.
    constexpr u64 ToU64() const { return (static_cast<u64>(generation) << 32) | index; }
    /// The id packed by [[ecs::Entity::ToU64]].
    /// - Fails: never.
    static constexpr Entity FromU64(u64 value) { return Entity{static_cast<u32>(value), static_cast<u32>(value >> 32)}; }
    /// Returns whether both ids name the same entity.
    constexpr bool operator==(const Entity& other) const { return index == other.index && generation == other.generation; }
    /// Returns whether the ids differ.
    constexpr bool operator!=(const Entity& other) const { return !(*this == other); }
    /// Orders ids by their packed value (generation first); a stable order for sorting, with no other meaning.
    constexpr bool operator<(const Entity& other) const { return ToU64() < other.ToU64(); }
};

} // namespace engine_placeholder::ecs

namespace engine_placeholder {

/// Hash function for [[ecs::Entity]] keys of a [[HashMap]].
///
/// - Thread-safety: thread-safe, a pure function.
/// - Ownership: owns nothing.
/// - Fails: never.
template <>
struct Hash<ecs::Entity> {
    /// Returns a well-mixed hash of the packed id.
    usize operator()(const ecs::Entity& e) const { return Hash<u64>{}(e.ToU64()); }
};

} // namespace engine_placeholder
