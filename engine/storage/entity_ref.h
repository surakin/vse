#pragma once

//! The stored identity of an entity, as a field type: [[storage::EntityKey]] (128 bits) and [[storage::EntityRef]] (a reference to another entity by key).
//! Day 256 declares the types and their encoding (16 bytes); Day 258 adds key generation, the key index and the resolution of references on load.

#include "types.h"

namespace engine_placeholder::storage {

/// A 128-bit entity identity that survives processes and machines.
///
/// Thread-safety: thread-compatible plain data.
struct EntityKey {
    u64 high = 0; ///< The high 64 bits.
    u64 low = 0;  ///< The low 64 bits.

    /// Returns true when both halves are zero: the null key, which refers to no entity.
    /// Fails: never.
    constexpr bool IsNull() const { return high == 0 && low == 0; }
    /// Returns true when both keys are equal.
    friend constexpr bool operator==(const EntityKey& a, const EntityKey& b) { return a.high == b.high && a.low == b.low; }
    /// Returns true when the keys differ.
    friend constexpr bool operator!=(const EntityKey& a, const EntityKey& b) { return !(a == b); }
};

/// A component field that refers to another entity. Stored as the target's key (the null key for no target); resolved to an entity after a load.
///
/// Thread-safety: thread-compatible plain data.
struct EntityRef {
    EntityKey key; ///< The target's key, or the null key.
};

} // namespace engine_placeholder::storage
