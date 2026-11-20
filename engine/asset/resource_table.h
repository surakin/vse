#pragma once

//! [[asset::ResourceTable]]: the id <-> path map of the engine (the user's decision of 2026-11-13: one table where the
//! map lives). It is a value type: each backend supplies its own (a stored `NAMES` blob, a pak section, a server's
//! reply, or one built on demand by scanning a folder), and the mount table merges them by priority (Day 315). The
//! load path never uses it: the runtime refers to resources by id, and paths are resolved at the edges.

#include "asset/blob.h"
#include "asset/resource_id.h"
#include "asset/resource_type.h"
#include "asset/status.h"
#include "containers/dense_map.h"
#include "containers/hash_map.h"
#include "containers/vector.h"
#include "mem/heap_allocator.h"
#include "string/string.h"
#include "string_view.h"
#include "types.h"

namespace vse::asset
{

/// The most entries a table (and a serialized `NAMES` blob) may hold.
inline constexpr usize kMaxTableEntries = usize(1) << 22;

/// One path of one resource, as a view into the table.
///
/// - Thread-safety: thread-compatible; the view is valid while the table is unchanged.
struct TableEntryView
{
    StringView path;    ///< The normalized path (valid until the table changes).
    ResourceId id;      ///< The resource.
    ResourceType type;  ///< Its type.
};

/// An entry of a lower-priority table that a merge did not take because a higher-priority table already maps its path.
///
/// - Thread-safety: thread-compatible.
struct ShadowedEntry
{
    String<HeapAllocator> path;  ///< The path.
    ResourceId shadowed_id;      ///< The id the lower-priority table gave it.
    ResourceId winning_id;       ///< The id the higher-priority table gave it.
};

/// A bidirectional map between normalized virtual paths and resource ids. One path names one id; one id may have any
/// number of paths; one id has one type.
///
/// - Thread-safety: thread-compatible; reads are safe from many threads while nothing mutates the table.
/// - Ownership: owns its paths; views it returns are invalidated by any mutation.
class ResourceTable
{
public:
    /// Creates an empty table.
    ResourceTable();

    /// Adds `path` for `id`, normalizing the path. Adding the same (path, id) again is accepted and changes nothing.
    ///
    /// - Fails: `Invalid` for a bad path or a null id or type; `DuplicateId` when the path already names another id;
    ///   `TypeMismatch` when the id is known with another type; `LimitExceeded` above [[asset::kMaxTableEntries]].
    Status Add(StringView path, ResourceId id, ResourceType type);

    /// Removes the entry for `path` (as written; it is normalized first). The cost is linear in the table size (the
    /// indexes are rebuilt), which suits tools and unmounting, not bulk removal in a loop.
    ///
    /// - Returns: true when an entry was removed.
    /// - Fails: never.
    bool Remove(StringView path);

    /// Finds the id of `path` (as written; it is normalized first).
    ///
    /// - Param id: receives the id; may be null.
    /// - Returns: false when the path is unknown or malformed.
    /// - Ownership: `id` is written, never retained; `path` is not kept.
    /// - Fails: never.
    bool Resolve(StringView path, ResourceId* id) const;

    /// Finds the type of `id`.
    ///
    /// - Param type: receives the type; may be null.
    /// - Returns: false when no entry names the id.
    /// - Ownership: `type` is written, never retained.
    /// - Fails: never.
    bool TypeOf(ResourceId id, ResourceType* type) const;

    /// Appends every path of `id` to `out`, sorted.
    ///
    /// - Returns: the number appended.
    /// - Ownership: the appended views borrow the table's strings until the table changes.
    /// - Fails: never.
    usize PathsOf(ResourceId id, Vector<StringView>* out) const;

    /// Appends every entry whose path starts with `prefix` to `out`, sorted by path.
    ///
    /// - Returns: the number appended.
    /// - Ownership: the appended views borrow the table's strings until the table changes.
    /// - Fails: never.
    usize ListPrefix(StringView prefix, Vector<TableEntryView>* out) const;

    /// The number of entries (paths).
    /// - Fails: never.
    usize Size() const { return entries_.Size(); }
    /// Returns entry `index` in insertion order.
    ///
    /// - Aborts: `index` is not below `Size()`.
    TableEntryView At(usize index) const;

    /// Merges a lower-priority table into this one: a path this table does not map is added; a path both map to the
    /// same id is kept; a path both map to different ids stays as this table has it and the other's entry is
    /// reported in `shadowed` (never dropped silently).
    ///
    /// - Param lower: the lower-priority table.
    /// - Param shadowed: receives the entries that were not taken; may be null.
    /// - Fails: `TypeMismatch` when an id has one type here and another there (nothing is merged); `LimitExceeded`
    ///   above [[asset::kMaxTableEntries]] (nothing is merged).
    /// - Ownership: `shadowed` receives copies of the paths; `lower` is only read and not kept.
    Status Merge(const ResourceTable& lower, Vector<ShadowedEntry>* shadowed);

    /// Writes the table as a `NAMES` blob: one section of entries sorted by id then path, so the bytes depend only on
    /// the content.
    ///
    /// - Fails: whatever `BlobWriter::Finish` reports.
    /// - Ownership: `out` receives the bytes; the table keeps no pointer to it.
    Status WriteNamesBlob(const TypeRegistry& types, Vector<byte>* out) const;

    /// Replaces this table with the content of a `NAMES` blob.
    ///
    /// - Fails: `TypeMismatch` when the blob is not a `NAMES` blob; `Corrupt` for a damaged section or an entry that
    ///   breaks the table's rules; the table is empty after a failure.
    Status ReadNamesBlob(const BlobReader& blob);

private:
    struct Entry
    {
        ResourceId id;
        ResourceType type;
        u32 next_by_id = 0;  // the slot of the next entry of the same id, or none
    };

    void Clear();
    void RelinkId(u64 id, u32 from, u32 to);
    const Entry* FindByNormalized(StringView normalized) const;

    // The entries by normalized path, dense in slots (container audit Day 7; the path is the key, stored once).
    DenseMap<String<HeapAllocator>, Entry> entries_;
    HashMap<u64, u32, HeapAllocator> by_id_;  // id -> the slot of the first entry of its chain
};

}  // namespace vse::asset
