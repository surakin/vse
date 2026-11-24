#pragma once
//! The instance resource: the chunks of an [[renderer::InstanceSet]] as minted bytes. [[asset::BuildInstanceBlob]]
//! writes an `INST` blob, [[asset::InstanceBlobView]] reads one in place, and [[asset::MakeInstanceSet]] makes a set
//! from it by copying the records chunk by chunk, with no `Add` per instance.
//!
//! Layout of the blob's sections (little-endian):
//!
//! | Section | Content |
//! |---|---|
//! | 16 header | 32 bytes: instance count `u32`, chunk count `u32`, flags `u32` (bit 0: every instance has data),
//! move threshold `f32`, Morton cell size `f64`, reserved `u64` (zero) |
//! | 17 chunks | 32 bytes a chunk: origin `f64` x3, record count `u32` (1 to 256), reserved `u32` (zero) |
//! | 18 records | every chunk's records one after another, 40 bytes each: position offset `f32` x3, rotation `f32` x4,
//! scale `f32` x3 (the layout of [[renderer::InstanceRecord]]) |
//! | 19 data | present when flag bit 0 is set: every instance's tint `f32` x4 and custom `u32`, 20 bytes each |
//!
//! The chunks are in Morton order of their origins, as `InstanceSet::Rebuild` leaves them; the blob is a deterministic
//! function of the instances and their order in the input.

#include "asset/blob.h"
#include "asset/resource_id.h"
#include "asset/resource_type.h"
#include "asset/status.h"
#include "containers/shared_bytes.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "renderer/instance_set.h"
#include "types.h"

namespace vse::asset
{

/// The size of the header section.
inline constexpr usize kInstanceBlobHeaderBytes = 32;
/// The size of one chunk table entry.
inline constexpr usize kInstanceBlobChunkBytes = 32;

/// The section ids of an instance blob.
enum InstanceBlobSection : u32
{
    kInstanceBlobSectionHeader = 16,   ///< The description (32 bytes).
    kInstanceBlobSectionChunks = 17,   ///< The chunk table.
    kInstanceBlobSectionRecords = 18,  ///< The records.
    kInstanceBlobSectionData = 19,     ///< The tints and custom values, when present.
};

/// What an instance blob says about its set.
///
/// - Thread-safety: thread-compatible plain data.
struct InstanceBlobHeader
{
    u32 instance_count = 0;     ///< Instances in all.
    u32 chunk_count = 0;        ///< Chunks.
    bool has_data = false;      ///< Whether every instance carries a tint and a custom value.
    f32 move_threshold = 8.0f;  ///< The set's move threshold.
    f64 key_cell_size = 4.0;    ///< The set's Morton cell size.
};

/// Writes the blob of an instance set made from a list: the instances are added to a set in the order given, the set is
/// rebuilt (sorted by Morton key, cut into full chunks) and its chunks are written.
///
/// - Param types: the registry; it must hold `kInstances`.
/// - Param id: the resource id.
/// - Param desc: the set's description (`has_data`, the cell size and the move threshold are stored; `max_instances`
///   limits the list).
/// - Param transforms: the instances.
/// - Param data: the data of each instance, or empty for the default (ignored without `has_data`; otherwise it must be
///   as long as `transforms` or empty).
/// - Param out: receives the blob bytes.
/// - Param target: the target platform code of the blob header.
/// - Fails: `Invalid` for a bad description, more instances than `max_instances`, a value an instance set refuses, or a
///   data list of another length; the errors of [[asset::BlobWriter::Finish]].
/// - Ownership: borrows its arguments for the call; `*out` receives a copy.
Status BuildInstanceBlob(const TypeRegistry& types, ResourceId id, const renderer::InstanceSetDesc& desc,
                         Span<const renderer::InstanceTransform> transforms, Span<const renderer::InstanceExtra> data,
                         Vector<byte>* out, u32 target = 0);

/// Writes the blob of a set as it is now (every instance, in chunk and slot order, rebuilt as for
/// [[asset::BuildInstanceBlob]]).
///
/// - Fails: as [[asset::BuildInstanceBlob]].
/// - Ownership: borrows `set`; `*out` receives a copy.
Status BuildInstanceBlobFromSet(const TypeRegistry& types, ResourceId id, const renderer::InstanceSet& set,
                                Vector<byte>* out, u32 target = 0);

/// An instance blob validated and read in place.
///
/// - Thread-safety: thread-compatible; immutable after `Open`.
/// - Ownership: shares the buffer it was opened on.
class InstanceBlobView
{
public:
    /// Creates an empty view.
    InstanceBlobView() = default;

    /// Validates `bytes` as an instance blob of the current version and keeps a share of them. The records are not
    /// scanned; their values are checked when a set is made.
    ///
    /// - Fails: the errors of [[asset::BlobReader::Open]]; `TypeMismatch` when the blob is not an `INST`; `Corrupt` for
    /// a
    ///   missing or mis-sized section, a chunk of 0 or more than 256 records, counts that do not add up, a field
    ///   outside its limits, or sections not aligned to 4 bytes.
    /// - Ownership: shares `bytes`.
    Status Open(const SharedBytesPtr& bytes, const TypeRegistry& types);

    /// The description.
    /// - Fails: never.
    const InstanceBlobHeader& Header() const { return header_; }
    /// The resource id recorded in the blob header.
    /// - Fails: never.
    ResourceId Id() const { return id_; }
    /// The buffer the view shares.
    /// - Fails: never.
    const SharedBytesPtr& Bytes() const { return bytes_; }
    /// Returns whether the view holds a blob.
    /// - Fails: never.
    bool Valid() const { return bool(bytes_); }

private:
    friend Status MakeInstanceSet(const InstanceBlobView& view, renderer::InstanceSetPtr* out);
    InstanceBlobHeader header_;
    ResourceId id_;
    SharedBytesPtr bytes_;
    usize chunks_ = 0;   // offset of the chunk table
    usize records_ = 0;  // offset of the records
    usize data_ = 0;     // offset of the data, 0 when absent
};

/// Makes the set a blob holds: the records are copied chunk by chunk (no per-instance `Add`; the set's `AddCalls()` is
/// 0).
///
/// - Param view: an open view.
/// - Param out: receives the set.
/// - Fails: `Invalid` for an empty view; `Corrupt` when a record or origin holds a value the set refuses (a hostile
///   blob), or the chunks are not in Morton order; `Other` when the set cannot be made.
/// - Ownership: copies out of the view's bytes; the set shares nothing with them.
Status MakeInstanceSet(const InstanceBlobView& view, renderer::InstanceSetPtr* out);

}  // namespace vse::asset
