#pragma once

//! The pak: many resources in one file, found by id ([[asset::PakWriter]], [[asset::PakReader]]). Phase 22 Day 316.
//!
//! All integers are little-endian. The layout, in file order:
//!
//! | Part | Size | Content |
//! |---|---|---|
//! | header | 128 | magic `APAK`, format version, flags, target tag, alignment, type count, entry count, type table
//! offset, names offset and size, base pak identity (16 bytes, zero for none), reserved zero | | type table | 8 each |
//! `(tag u32, version u32)` for each resource type the pak holds, sorted by tag | | names | any | the id <-> path table
//! as a `NAMES` blob (optional, flag bit 0), 64-byte aligned | | entries | any | the blobs in id order, each 64-byte
//! aligned (4096 from 1 MiB), zero padding between | | index | 48 each | `(id u64, offset u64, size u64, content hash
//! 16, type u32, reserved u32)` sorted by id | | footer | 48 | index offset, entry count, index hash (16), file size,
//! magic `KPAK`, format version |
//!
//! The pak's identity (what a patch pak names as its base) is its index hash. Version 1 stores blobs uncompressed.

#include "asset/backend.h"
#include "asset/blob.h"
#include "asset/content_hash.h"
#include "asset/resource_id.h"
#include "asset/resource_table.h"
#include "asset/resource_type.h"
#include "asset/status.h"
#include "containers/array.h"
#include "containers/hash_map.h"
#include "containers/span.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "io/file.h"
#include "mem/heap_allocator.h"
#include "types.h"

namespace vse::asset
{

/// The pak format version the runtime reads and the writer writes.
inline constexpr u32 kPakFormatVersion = 1;
/// The size of the pak header.
inline constexpr usize kPakHeaderBytes = 128;
/// The size of one index entry.
inline constexpr usize kPakIndexEntryBytes = 48;
/// The size of the footer.
inline constexpr usize kPakFooterBytes = 48;
/// The most entries a pak holds.
inline constexpr usize kMaxPakEntries = usize(1) << 22;
/// Header flag: the pak holds a names section.
inline constexpr u32 kPakFlagNames = 1;

/// One index entry.
///
/// - Thread-safety: immutable value.
struct PakEntry
{
    ResourceId id;      ///< The resource.
    u64 offset = 0;     ///< Where its blob starts in the file.
    u64 size = 0;       ///< The blob's size in bytes.
    ContentHash hash;   ///< The blob's content hash (from its header).
    ResourceType type;  ///< The blob's type.
};

/// What a pak says about itself.
///
/// - Thread-safety: immutable value.
struct PakInfo
{
    u32 format_version = 0;  ///< The pak format version.
    u32 target = 0;          ///< The target tag the pak was packed for.
    bool has_names = false;  ///< True when the pak holds its id <-> path table.
    bool has_base = false;   ///< True when the pak is a patch pak and names a base pak.
    ContentHash base;        ///< The identity of the base pak (when `has_base`).
    ContentHash identity;    ///< This pak's identity: its index hash.
    u64 file_size = 0;       ///< The size of the file.
    usize entry_count = 0;   ///< The number of entries.
};

/// Options of [[asset::PakWriter]].
///
/// - Thread-safety: immutable value.
struct PakWriterOptions
{
    u32 target = 0;              ///< The target tag; every blob added must have been laid out for it.
    bool include_names = true;   ///< Write the id <-> path table; a shipping pak may leave it out.
    u64 first_entry_offset = 0;  ///< Entry data starts at or after this offset (for tests of 64-bit offsets).
};

/// Builds a pak. Adds borrow the blobs; `Finish` writes the file deterministically (entries in id order, zero padding,
/// no time stamps), so the same inputs give the same bytes.
///
/// The writer is an enforcement point: a blob that is not the current version of its type, whose own id differs from
/// the id it is added under, whose hash does not verify or that was laid out for another target is refused, and
/// `Finish` refuses a pak whose closure is incomplete.
/// - Thread-safety: thread-compatible.
/// - Ownership: borrows every added blob and the registry until `Finish` returns.
class PakWriter
{
public:
    /// Makes a writer that checks blobs against `types`.
    PakWriter(const TypeRegistry& types, const PakWriterOptions& options);

    /// Names a base pak (a patch pak): dependencies may be satisfied by it, and its identity goes into the header.
    ///
    /// - Ownership: borrows `base` until `Finish` returns.
    void SetBase(const class PakReader* base) { base_ = base; }

    /// Adds a blob, optionally with its path.
    ///
    /// - Fails: `Corrupt`, `UnsupportedVersion`, `TypeMismatch` (as `BlobReader::Open` and `Verify`); `Corrupt` when
    /// the
    ///   blob's own id is not `id`; `WrongTarget`; `DuplicateId`; `Invalid` or `DuplicateId` for a path; `Invalid` for
    ///   the null id; `LimitExceeded` past `kMaxPakEntries`.
    /// - Ownership: borrows `blob` until `Finish` returns.
    Status Add(ResourceId id, ByteSpan blob, StringView path = StringView());

    /// Checks the closure, then writes the pak to `out` from offset 0.
    ///
    /// - Fails: `MissingDependency` or `DependencyTypeMismatch` (the pairs are in `Problems`), `Io` and the file's
    ///   status when the write fails.
    /// - Ownership: borrows `out` for the call.
    Status Finish(io::File& out);

    /// One broken dependency found by `Finish`.
    ///
    /// - Thread-safety: immutable value.
    struct Problem
    {
        ResourceId blob;                                ///< The blob that declares the dependency.
        ResourceId dependency;                          ///< The dependency.
        ErrorCode code = ErrorCode::MissingDependency;  ///< `MissingDependency` or `DependencyTypeMismatch`.
    };

    /// The (blob, dependency) pairs the last `Finish` refused.
    const Vector<Problem>& Problems() const { return problems_; }

private:
    struct Item
    {
        ResourceId id;
        ByteSpan bytes;
        ResourceType type;
        u32 type_version = 0;
        ContentHash hash;
    };

    Status CheckClosure();

    const TypeRegistry* types_;
    PakWriterOptions options_;
    const PakReader* base_ = nullptr;
    Vector<Item> items_;
    HashMap<u64, u32, HeapAllocator> id_index_;
    ResourceTable names_;
    Vector<Problem> problems_;
};

/// A pak opened for reading. The index is validated against the file size when it is opened and a hash table over it is
/// built, so a lookup is a few probes; nothing is read from an entry until it is asked for.
///
/// - Thread-safety: thread-compatible to open; once open, every const member and `ReadEntry` may be called from any
///   thread.
/// - Ownership: owns the file.
class PakReader
{
public:
    PakReader() = default;
    PakReader(const PakReader&) = delete;
    PakReader& operator=(const PakReader&) = delete;
    PakReader(PakReader&&) noexcept = default;
    PakReader& operator=(PakReader&&) noexcept = default;

    /// Opens a pak: validates the header, the footer and the index and builds the lookup table.
    ///
    /// The index hash and the entries' hashes are not checked here (see `Verify`).
    /// - Param expected_target: the target tag of the running program.
    /// - Fails: `Corrupt` (any structural damage, truncation included), `UnsupportedVersion` (the pak format or a
    ///   resource type version is not the current one; the message names the tool), `WrongTarget` (the pak was packed
    ///   for another target; `Detail` names both), `TypeMismatch` (an unregistered type), `TooLarge`, `Io`.
    /// - Ownership: takes `file`.
    static Status Open(io::FilePtr file, const TypeRegistry& types, u32 expected_target, PakReader* out);

    /// A sentence for the last failed `Open` that names the numbers the static message cannot (empty otherwise).
    ///
    /// - Fails: never.
    /// - Ownership: returns a string owned by the reader, valid until the reader is changed or destroyed.
    const char* Detail() const { return detail_.Data(); }

    /// What the pak says about itself (valid after a successful `Open`, and after a `WrongTarget` failure).
    const PakInfo& Info() const { return info_; }

    /// The number of entries.
    usize Count() const { return entries_.Size(); }

    /// The entry at `index` (in id order).
    ///
    /// - Aborts: `index` is not below `Count()`.
    const PakEntry& At(usize index) const;

    /// Finds an entry by id.
    ///
    /// - Returns: false when the pak does not hold `id`.
    /// - Ownership: borrows `out` for the call only; nothing is retained.
    bool Find(ResourceId id, PakEntry* out) const;

    /// The version of `type` the pak was packed with (from its type table).
    ///
    /// - Returns: false when the pak holds no resource of `type`.
    /// - Fails: never.
    /// - Ownership: borrows `version` for the call only; nothing is retained.
    bool TypeVersionOf(ResourceType type, u32* version) const;

    /// Reads `length` bytes at `offset` inside `entry`'s blob.
    ///
    /// - Fails: `Invalid` when the range is not inside the entry; `Io`.
    /// - Ownership: borrows `buffer` for the call only; nothing is retained.
    Status ReadEntry(const PakEntry& entry, u64 offset, void* buffer, usize length) const;

    /// Reads the names section into `out` (which must be empty).
    ///
    /// - Fails: `NotFound` when the pak has none; `Corrupt`; `Io`.
    /// - Ownership: borrows its pointer arguments for the call only; nothing is retained.
    Status ReadNames(const TypeRegistry& types, ResourceTable* out) const;

    /// Checks the index hash and every blob's hash (reads the whole file; a tool's job, never the load path).
    ///
    /// - Fails: `Corrupt` naming what differs.
    Status Verify(const TypeRegistry& types) const;

private:
    struct TypeRecord
    {
        u32 tag = 0;
        u32 version = 0;
    };

    io::FilePtr file_;
    PakInfo info_;
    Vector<PakEntry> entries_;
    Vector<u32> slots_;  // open-addressing table of entry index + 1 (0 is empty)
    Vector<TypeRecord> types_;
    u64 names_offset_ = 0;
    u64 names_size_ = 0;
    u64 index_offset_ = 0;
    Array<char, 160> detail_ = {};
};

}  // namespace vse::asset
