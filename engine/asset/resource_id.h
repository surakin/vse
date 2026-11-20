#pragma once

//! [[asset::ResourceId]]: a resource's stable identity, and the rules for the virtual paths that name it. An id is a
//! permanent 64-bit number assigned once by `asset_mint adopt`; it is not derived from the path, so a rename or a
//! move never changes it. The runtime refers to resources by id only; a path is an alias for tools and sources.

#include "asset/status.h"
#include "mem/heap_allocator.h"
#include "string/string.h"
#include "string_view.h"
#include "types.h"

namespace vse::asset
{

/// A stable 64-bit resource id. `ResourceId::None()` (value 0) is the explicit null reference.
///
/// - Thread-safety: immutable value.
class ResourceId
{
public:
    /// Creates the null id.
    constexpr ResourceId() = default;

    /// Returns the null id: a reference that names nothing.
    /// - Fails: never.
    static constexpr ResourceId None() { return ResourceId(); }
    /// Returns the id with the raw value `value`.
    /// - Fails: never.
    static constexpr ResourceId FromValue(u64 value)
    {
        ResourceId id;
        id.value_ = value;
        return id;
    }

    /// The raw value.
    /// - Fails: never.
    constexpr u64 Value() const { return value_; }
    /// Returns true for the null id.
    /// - Fails: never.
    constexpr bool IsNone() const { return value_ == 0; }

    /// Compares values.
    /// - Fails: never.
    friend constexpr bool operator==(ResourceId a, ResourceId b) { return a.value_ == b.value_; }
    /// Compares values.
    /// - Fails: never.
    friend constexpr bool operator!=(ResourceId a, ResourceId b) { return a.value_ != b.value_; }
    /// Orders by value (the order of every sorted table and dependency section).
    /// - Fails: never.
    friend constexpr bool operator<(ResourceId a, ResourceId b) { return a.value_ < b.value_; }

private:
    u64 value_ = 0;
};

/// Draws a new, non-null id from the operating system's secure random source. Only `asset_mint adopt` calls this; the
/// result is written into the source file and never regenerated.
///
/// - Thread-safety: thread-safe.
/// - Aborts: the operating system cannot supply random bytes.
ResourceId GenerateResourceId();

/// Writes `id` as 16 lower-case hex digits and a terminating zero.
///
/// - Fails: never.
void FormatResourceId(ResourceId id, char (&out)[17]);

/// Parses exactly 16 hex digits (either case).
///
/// - Param out: receives the id on success; untouched on failure.
/// - Fails: `Invalid` when the text is not 16 hex digits; `Invalid` for the null id (a source never records it).
/// - Ownership: `out` is written on success, never retained; `text` is not kept.
Status ParseResourceId(StringView text, ResourceId* out);

/// The longest virtual path, in bytes.
inline constexpr usize kMaxPathBytes = 1024;
/// The longest single path segment, in bytes.
inline constexpr usize kMaxPathSegmentBytes = 255;

/// Normalizes a virtual path into its one canonical spelling: UTF-8 in Unicode NFC, a leading `/`, segments separated
/// by single `/`, no `.` or `..` segment, no trailing slash, no empty segment, no backslash and no control
/// character, at most [[asset::kMaxPathBytes]] bytes (and [[asset::kMaxPathSegmentBytes]] per segment). Case is
/// significant. The root `/` alone is not a resource path. A path that cannot be made canonical is an error, never
/// repaired by guesswork.
///
/// - Param path: the path as written.
/// - Param out: receives the canonical spelling on success; may be null to only check.
/// - Fails: `Invalid` with a message naming the broken rule; `TooLarge` above the length limits.
/// - Ownership: `out` receives a new string from the default heap allocator; `path` is not kept.
Status NormalizePath(StringView path, String<HeapAllocator>* out);

}  // namespace vse::asset
