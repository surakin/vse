#pragma once

//! The record store (Phase 19 Day 259): [[storage::Store]] keeps one [[storage::EntityRecord]] per [[storage::EntityKey]], in key order, with all-or-nothing
//! batches and small named metadata. It knows nothing of components: a record is a key and the bytes the entity container (Day 260) built. Backends:
//! [[storage::MemoryStore]] (the reference) and, from Day 262, the SQL store.

#include "bytes/byte_writer.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "mem/heap_allocator.h"
#include "storage/entity_ref.h"
#include "storage/status.h"
#include "string/string.h"
#include "string_view.h"
#include "types.h"

namespace engine_placeholder::storage {

/// The content hash of a byte string: 64-bit FNV-1a. Stored with each record so a save can tell an unchanged entity without comparing bytes, and a load can
/// notice damage. Not cryptographic.
///
/// Param bytes: any bytes, also none.
/// Returns: the hash.
/// Ownership: `bytes` is read during the call only.
/// Fails: never.
u64 HashBytes(ByteSpan bytes);

/// One entity as stored: its key, the container bytes and the hash of those bytes.
///
/// Thread-safety: thread-compatible plain data.
/// Ownership: owns its bytes; move-only, copy with [[storage::EntityRecord::Clone]].
struct EntityRecord {
    EntityKey key;      ///< The entity's key; never null in a store.
    Vector<byte> blob;  ///< The encoded components (the framing is the entity container's, Day 260).
    u64 hash = 0;       ///< [[storage::HashBytes]] of `blob`, as the writer computed it.

    /// Returns a record with this key and a copy of the bytes and hash.
    /// Fails: never.
    EntityRecord Clone() const;
    /// Makes a record from `key` and a copy of `bytes`, with the hash computed.
    ///
    /// Ownership: `bytes` is read during the call only.
    /// Fails: never.
    static EntityRecord Make(EntityKey key, ByteSpan bytes);
    /// Returns true when `hash` is the hash of `blob`.
    /// Fails: never.
    bool HashMatches() const;
};

/// A half-open range of keys, `[first, end)`, in key order. The default range is every key.
///
/// Thread-safety: thread-compatible plain data.
struct KeyRange {
    EntityKey first;          ///< The smallest key in the range.
    EntityKey end;            ///< The first key past the range; ignored when `unbounded` is true.
    bool unbounded = true;    ///< When true the range runs to the largest key.

    /// Returns true when `key` is in the range.
    /// Fails: never.
    bool Contains(EntityKey key) const { return !(key < first) && (unbounded || key < end); }
};

/// Called by [[storage::Store::Scan]] for each record, in key order.
///
/// Thread-safety: thread-compatible; called on the thread that called `Scan`.
class RecordVisitor {
public:
    virtual ~RecordVisitor() = default;
    /// Receives one record.
    ///
    /// Param record: valid during the call only.
    /// Returns: true to continue, false to stop the scan (not an error).
    /// Ownership: the record is borrowed; copy it with `Clone` to keep it.
    /// Fails: never.
    virtual bool Visit(const EntityRecord& record) = 0;
};

/// Well-known metadata names.
namespace metadata {
inline constexpr const char* kFormatVersion = "format_version";     ///< The layout of the store's records; written by the first save.
inline constexpr const char* kSchemaVersion = "schema_version";     ///< The version of the store's own schema (a backend's).
inline constexpr const char* kLastSaveLabel = "last_save_label";    ///< The label of the last save (`SaveOptions::label`).
} // namespace metadata

/// A record store: entity records in key order, batches, and metadata.
///
/// **Batches.** `Begin` opens an all-or-nothing batch: `Put`, `Remove` and `SetMetadata` made after it become visible together at `Commit` and not at all after
/// `Rollback`. Outside a batch each mutation is its own batch. A mutation that fails inside a batch leaves the batch open; the caller rolls it back.
///
/// **Threads.** One writer at a time: mutations, `Begin`, `Commit` and `Rollback` come from one thread (the one that called `Begin` while a batch is open; a
/// mutation from another thread while a batch is open is `Busy`). Readers (`Get`, `Scan`, `Count`, `GetMetadata`) may run on any thread at any time and see
/// the last committed state, except that the thread that owns the open batch sees its own uncommitted changes.
///
/// Thread-safety: thread-safe as described above; the implementation locks internally.
/// Ownership: a store owns its records; results are copied out.
class Store {
public:
    virtual ~Store() = default;

    /// Returns the backend's name (`"memory"`).
    /// Ownership: the string is static.
    /// Fails: never.
    virtual const char* Name() const = 0;

    /// Stores `record`, replacing the record with its key.
    ///
    /// Param record: a non-null key, a blob within the record limit.
    /// Ownership: `record` is copied; it is read during the call only.
    /// Fails: `Invalid` (null key); `Busy` (a batch belongs to another thread); `Io`; `LimitExceeded`.
    virtual Status Put(const EntityRecord& record) = 0;
    /// Copies the record with `key` into `out`.
    ///
    /// Param out: receives the record on success; unchanged on failure.
    /// Ownership: `out` owns the copy.
    /// Fails: `NotFound`; `Corrupt` (the stored bytes do not match their hash); `Io`.
    virtual Status Get(EntityKey key, EntityRecord* out) const = 0;
    /// Removes the record with `key`.
    ///
    /// Fails: `NotFound`; `Busy` (a batch belongs to another thread); `Io`.
    virtual Status Remove(EntityKey key) = 0;
    /// Calls `visitor` for each record in `range`, in key order, until it returns false.
    ///
    /// Param range: the keys to visit.
    /// Param visitor: receives the records; must not call the store (the scan holds its lock).
    /// Ownership: both are borrowed for the call.
    /// Fails: `Corrupt` (a record does not match its hash; the scan stops there); `Io`.
    virtual Status Scan(const KeyRange& range, RecordVisitor& visitor) const = 0;
    /// Counts the records.
    ///
    /// Param out: receives the count.
    /// Ownership: `out` is borrowed for the call.
    /// Fails: `Io`.
    virtual Status Count(usize* out) const = 0;

    /// Opens a batch.
    /// Fails: `Busy` (a batch is open); `Io`.
    virtual Status Begin() = 0;
    /// Makes the open batch visible, all of it.
    /// Fails: `Invalid` (no batch, or another thread's); `Io` (the batch stays open; roll it back).
    virtual Status Commit() = 0;
    /// Drops the open batch.
    /// Fails: `Invalid` (no batch, or another thread's).
    virtual Status Rollback() = 0;

    /// Reads a metadata value.
    ///
    /// Param name: a non-empty name; the well-known names are `metadata::kFormatVersion`, `metadata::kSchemaVersion` and `metadata::kLastSaveLabel`.
    /// Param out: receives the text on success.
    /// Ownership: `name` is read during the call; `out` owns the copy.
    /// Fails: `NotFound`; `Io`.
    virtual Status GetMetadata(StringView name, String<HeapAllocator>* out) const = 0;
    /// Writes a metadata value, replacing the old one.
    ///
    /// Param name: a non-empty name.
    /// Param value: any text.
    /// Ownership: both are copied.
    /// Fails: `Invalid` (empty name); `Busy`; `Io`.
    virtual Status SetMetadata(StringView name, StringView value) = 0;
};

/// Lists the keys in `range` in key order, at most `limit` of them.
///
/// Param store: the store to read.
/// Param range: the keys to list.
/// Param limit: the most keys to return.
/// Param out: receives the keys (cleared first).
/// Ownership: all borrowed for the call.
/// Fails: the errors of [[storage::Store::Scan]].
Status ListKeys(const Store& store, const KeyRange& range, usize limit, Vector<EntityKey>* out);

} // namespace engine_placeholder::storage
