#pragma once

//! The stored identity of an entity, as a field type: [[storage::EntityKey]] (128 bits) and [[storage::EntityRef]] (a
//! reference to another entity by key). Day 256 declares the types and their encoding (16 bytes); Day 258 adds key
//! generation, the key index and the resolution of references on load.

#include "containers/hash_map.h"
#include "mem/heap_allocator.h"
#include "string/string.h"
#include "string_view.h"
#include "types.h"

namespace engine_placeholder::storage
{

/// A 128-bit entity identity that survives processes and machines.
///
/// - Thread-safety: thread-compatible plain data.
struct EntityKey
{
    u64 high = 0;  ///< The high 64 bits.
    u64 low = 0;   ///< The low 64 bits.

    /// Returns true when both halves are zero: the null key, which refers to no entity.
    /// - Fails: never.
    constexpr bool IsNull() const { return high == 0 && low == 0; }
    /// Returns true when both keys are equal.
    friend constexpr bool operator==(const EntityKey& a, const EntityKey& b)
    {
        return a.high == b.high && a.low == b.low;
    }
    /// Returns true when the keys differ.
    friend constexpr bool operator!=(const EntityKey& a, const EntityKey& b) { return !(a == b); }
    /// Orders keys by `high`, then `low`: a stable order for sorting and ranges, with no other meaning.
    friend constexpr bool operator<(const EntityKey& a, const EntityKey& b)
    {
        return a.high != b.high ? a.high < b.high : a.low < b.low;
    }
    /// The key as 32 lower-case hexadecimal digits, `high` first.
    /// - Fails: never.
    String<HeapAllocator> ToText() const;
    /// Reads the text form of [[storage::EntityKey::ToText]] (32 hexadecimal digits, either case).
    ///
    /// - Param text: exactly 32 hexadecimal digits.
    /// - Param out: receives the key on success; unchanged on failure.
    /// - Returns: false when the text has another length or a character that is not a hexadecimal digit.
    /// - Ownership: `text` is read during the call only.
    /// - Fails: returns false as described.
    static bool FromText(StringView text, EntityKey* out);
};

/// A component field that refers to another entity. Stored as the target's key (the null key for no target); resolved
/// to an entity after a load.
///
/// - Thread-safety: thread-compatible plain data.
struct EntityRef
{
    EntityKey key;  ///< The target's key, or the null key.
};

/// A component field that names a resource (a mesh, a material, a clip) by its permanent 64-bit id, `0` for none.
/// Stored as 8 little-endian bytes, the same bytes as a `u64`. The storage module does not know what the id means: the
/// asset pipeline's level minter and loader read the declared resource type from [[storage::FieldOptions::Reference]].
///
/// - Thread-safety: thread-compatible plain data.
struct ResourceRef
{
    u64 id = 0;  ///< The resource id (`asset::ResourceId::Value()`); 0 names nothing.
};

}  // namespace engine_placeholder::storage

namespace engine_placeholder
{

/// Hash function for [[storage::EntityKey]] keys of a [[HashMap]].
///
/// - Thread-safety: thread-safe, a pure function.
/// - Ownership: owns nothing.
/// - Fails: never.
template <> struct Hash<storage::EntityKey>
{
    /// Returns a hash that mixes both halves.
    usize operator()(const storage::EntityKey& k) const
    {
        return Hash<u64>{}(k.high ^ (k.low * 0x9E3779B97F4A7C15ull));
    }
};

}  // namespace engine_placeholder
