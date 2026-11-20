#pragma once

//! The versioned blob container every minted resource is stored in: [[asset::BlobWriter]] writes it,
//! [[asset::BlobReader]] validates and reads it.
//!
//! Layout (all integers little-endian, no padding other than the alignment gaps, which are zero):
//!
//! | offset | size | field |
//! |---|---|---|
//! | 0 | 4 | magic `ABLB` |
//! | 4 | 4 | container version ([[asset::kContainerVersion]]) |
//! | 8 | 4 | resource type tag |
//! | 12 | 4 | type version (the registry's current version, nothing else is read) |
//! | 16 | 8 | the blob's own [[asset::ResourceId]] (null only for a `NAMES` blob) |
//! | 24 | 4 | target tag (the platform family the layout was made for; 0 is the development target) |
//! | 28 | 4 | flags (0) |
//! | 32 | 4 | section count |
//! | 36 | 4 | reserved (0) |
//! | 40 | 8 | total size of the blob in bytes |
//! | 48 | 16 | content hash of the whole blob with these 16 bytes zero |
//! | 64 | 24 each | section table: id (u32), reserved (u32, 0), offset (u64), size (u64), ids strictly increasing |
//!
//! Every section starts at a multiple of 64 bytes (of 4096 when it is 1 MiB or larger) so a loader can hand a span of
//! the file buffer straight to the GPU upload. Section 1 is the standard dependency section (a `u32` count, then
//! `(id u64, type u32, type version u32, role u32)` records strictly sorted by id); ids below 16 are reserved.

#include "asset/asset_module.h"
#include "asset/content_hash.h"
#include "asset/resource_id.h"
#include "asset/resource_type.h"
#include "asset/status.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "types.h"

namespace vse::asset
{

/// The size of the fixed header in bytes.
inline constexpr usize kBlobHeaderBytes = 64;
/// The size of one section table entry in bytes.
inline constexpr usize kSectionEntryBytes = 24;
/// The alignment of every section start.
inline constexpr usize kSectionAlignment = 64;
/// Sections of at least this size start on a page boundary.
inline constexpr usize kPageAlignedSectionBytes = 1u << 20;
/// The page alignment of a large section.
inline constexpr usize kPageAlignment = 4096;
/// The most sections a blob may hold besides the dependency section.
inline constexpr u32 kMaxUserSections = 64;
/// The most dependencies a blob may declare.
inline constexpr u32 kMaxDependencies = 4096;
/// The standard dependency section.
inline constexpr u32 kSectionDependencies = 1;
/// The first section id a type may use for its own data.
inline constexpr u32 kFirstUserSection = 16;
/// The largest blob the writer produces, in bytes (the sizes in the format are 64-bit; this is the bound the loader's
/// buffer pool and the minters work within).
inline constexpr u64 kMaxBlobBytes = static_cast<u64>(1) << 32;
/// The size of one serialized dependency record.
inline constexpr usize kDependencyRecordBytes = 20;

/// One declared dependency: the loader follows only these, and every one is required (there are no optional
/// dependencies).
///
/// - Thread-safety: immutable value.
struct Dependency
{
    ResourceId id;         ///< The resource depended on (never null, never the blob's own id).
    ResourceType type;     ///< The type the reference declared.
    u32 type_version = 0;  ///< The version of that type the reference was minted against (above 0).
    u32 role = 0;          ///< What the reference is for (a slot or field tag the type defines).
};

/// Builds a blob.
///
/// - Thread-safety: not-thread-safe.
/// - Ownership: copies the bytes it is given; the caller's buffers need not outlive the call.
class BlobWriter
{
public:
    /// Starts a blob of `type` for the resource `id`, at the registry's current version of the type.
    ///
    /// - Param types: the registry; it must outlive the writer.
    /// - Param target: the target tag recorded in the header (0, the development target).
    BlobWriter(const TypeRegistry& types, ResourceType type, ResourceId id, u32 target = 0);

    /// Declares a dependency. The records are sorted by id when the blob is finished.
    /// - Fails: never (a bad record is reported by `Finish`).
    void AddDependency(const Dependency& dependency) { dependencies_.Push(dependency); }

    /// Appends a section (copied).
    ///
    /// - Param section_id: at least [[asset::kFirstUserSection]]; unique within the blob.
    /// - Fails: `Invalid` for a reserved or repeated id; `LimitExceeded` above [[asset::kMaxUserSections]]; `TooLarge`
    ///   when the copy does not fit in memory.
    Status AddSection(u32 section_id, ByteSpan bytes);

    /// Lays the blob out, fills the hash and writes the bytes.
    ///
    /// - Param out: receives the blob (replacing its contents).
    /// - Fails: `TypeMismatch` for an unregistered type; `Invalid` for a null id on a type that is a resource, or a
    ///   dependency that is null, repeated, the blob itself, of the null type or of version 0; `LimitExceeded` above
    ///   [[asset::kMaxDependencies]]; `TooLarge` when the blob would be larger than [[asset::kMaxBlobBytes]] or does
    ///   not fit in memory.
    /// - Ownership: `out` receives the bytes, the writer keeps no pointer to it; nothing is borrowed after the call.
    Status Finish(Vector<byte>* out) const;

private:
    struct PendingSection
    {
        u32 id = 0;
        Vector<byte> bytes;
    };

    const TypeRegistry* types_;
    ResourceType type_;
    ResourceId id_;
    u32 target_;
    Vector<Dependency> dependencies_;
    Vector<PendingSection> sections_;
};

/// Validates a blob and gives out its sections as spans of the one buffer. Everything is checked before use; the
/// content hash is checked only when [[asset::BlobReader::Verify]] is called, so the load path pays for bounds checks
/// and nothing else.
///
/// - Thread-safety: thread-compatible; a reader is immutable after `Open`.
/// - Ownership: borrows the buffer; the bytes must outlive the reader and every span it hands out.
class BlobReader
{
public:
    /// Creates a reader that holds no blob.
    BlobReader() = default;

    /// Validates `data` as a blob of the current container version and the current version of its type.
    ///
    /// - Param types: the registry that names the current version of each type.
    /// - Fails: `Corrupt` for a bad magic, size, section table, overlap, misalignment, out-of-range section, a flag bit
    ///   set (none is defined in this version) or
    ///   dependency section; `UnsupportedVersion` (the message names `asset_mint remint`) when the container or the
    ///   type version is not the current one; `TypeMismatch` for an unregistered type.
    Status Open(ByteSpan data, const TypeRegistry& types);

    /// The blob's type.
    /// - Fails: never.
    ResourceType Type() const { return type_; }
    /// The blob's own id (null for a `NAMES` blob).
    /// - Fails: never.
    ResourceId Id() const { return id_; }
    /// The type version (always the current one after a successful `Open`).
    /// - Fails: never.
    u32 TypeVersion() const { return type_version_; }
    /// The target tag.
    /// - Fails: never.
    u32 Target() const { return target_; }
    /// The flags.
    /// - Fails: never.
    u32 Flags() const { return flags_; }
    /// The content hash recorded in the header (not verified).
    /// - Fails: never.
    const ContentHash& RecordedHash() const { return hash_; }
    /// The total size of the blob.
    /// - Fails: never.
    usize Size() const { return data_.Size(); }

    /// The number of sections, the dependency section included.
    /// - Fails: never.
    usize SectionCount() const { return sections_.Size(); }
    /// Returns the bytes of section `section_id`.
    ///
    /// - Param out: receives a span of the buffer (aligned as the layout says, relative to the buffer start).
    /// - Returns: false when the blob has no such section.
    /// - Ownership: the span borrows the buffer given to `Open`; it is valid as long as that buffer is.
    /// - Fails: never.
    bool FindSection(u32 section_id, ByteSpan* out) const;

    /// The number of declared dependencies.
    /// - Fails: never.
    usize DependencyCount() const { return dependency_count_; }
    /// Decodes dependency `index` (they are sorted by id).
    ///
    /// - Aborts: `index` is not below `DependencyCount()`.
    Dependency DependencyAt(usize index) const;

    /// Recomputes the content hash and compares it with the recorded one.
    ///
    /// - Fails: `Corrupt` when they differ.
    Status Verify() const;

private:
    struct SectionEntry
    {
        u32 id = 0;
        u64 offset = 0;
        u64 size = 0;
    };

    ByteSpan data_;
    ResourceType type_;
    ResourceId id_;
    u32 type_version_ = 0;
    u32 target_ = 0;
    u32 flags_ = 0;
    ContentHash hash_;
    Vector<SectionEntry> sections_;
    usize dependency_count_ = 0;
    usize dependency_offset_ = 0;
};

/// The fixed fields of a blob's 64-byte header, read without opening the blob.
///
/// - Thread-safety: immutable value.
struct BlobHeaderInfo
{
    ResourceType type;         ///< The resource type tag.
    u32 type_version = 0;      ///< The type version the blob was minted at.
    ResourceId id;             ///< The blob's own id (null only for a `NAMES` blob).
    u32 target = 0;            ///< The target tag.
    u64 total_size = 0;        ///< The size the header records for the whole blob.
    ContentHash content_hash;  ///< The recorded content hash.
};

/// The content hash a blob of these bytes should record: the hash of the whole blob with the recorded hash field taken
/// as zeros (what [[asset::BlobReader::Verify]] compares with).
///
/// - Param blob: the whole blob; at least [[asset::kBlobHeaderBytes]] bytes (anything shorter hashes as is).
/// - Thread-safety: thread-safe.
/// - Ownership: borrows `blob` for the call only.
/// - Fails: never.
ContentHash ComputeBlobHash(ByteSpan blob);

/// Copies a blob with another target tag in its header and the content hash recomputed (the bytes after the header are
/// the same). The layout of the blob is not changed: a type whose layout depends on the target (a texture) is laid out
/// again by its own function (`RetargetTextureBlob`).
///
/// - Param blob: the blob.
/// - Param target: the new target tag.
/// - Param out: replaced by the copy.
/// - Fails: `Corrupt` for bytes that are not a blob header; `UnsupportedVersion` for another container version.
/// - Ownership: borrows `blob` for the call only; `*out` is replaced.
/// - Thread-safety: thread-safe.
Status SetBlobTarget(ByteSpan blob, u32 target, Vector<byte>* out);

/// Reads the header fields of a blob from its first `kBlobHeaderBytes` bytes, so a backend can learn a file's id and
/// type without reading the file.
///
/// Checks the magic and the container version only; the type, the type version and the rest are checked by
/// [[asset::BlobReader::Open]].
/// - Param header: at least `kBlobHeaderBytes` bytes from the start of the blob.
/// - Fails: `Corrupt` when the bytes are shorter than a header or do not start with the blob magic;
/// `UnsupportedVersion`
///   when the container version is not the current one.
/// - Ownership: borrows `header` for the call only.
/// - Thread-safety: thread-safe.
Status PeekBlobHeader(ByteSpan header, BlobHeaderInfo* out);

}  // namespace vse::asset
