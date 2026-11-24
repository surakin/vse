#pragma once

//! A set of instances of one mesh: many transforms with one owner, kept in chunks that culling and the upload work on.
//!
//! An instance is a position, a rotation (a unit quaternion) and a scale: a 40-byte record (`InstanceRecord`), with an
//! optional second record of a tint and a custom parameter (`InstanceExtra`) when the set was made with data. Instances
//! live in chunks of up to `kInstanceChunkSize` (256) records. A new instance goes to the chunk whose seed position is
//! nearest to it along a Morton curve, so a chunk holds instances that are near each other; `Rebuild` sorts everything
//! by that key and cuts the chunks again.
//!
//! Large worlds: a chunk keeps an `f64` origin and its records hold `f32` offsets from it, so a record is exact at any
//! distance from the world origin as long as the instances of a chunk are near each other. The public positions are
//! `f64` (a single-precision `Vec3` widens). A chunk's box is in offsets from its origin.
//!
//! Edits never abort on bad input: they return an [[InstanceStatus]]. An [[InstanceId]] stays valid when other
//! instances are removed, moved or when the set is rebuilt, and goes stale (`StaleId`) when its own instance is removed
//! or the set is cleared.

#include "containers/span.h"
#include "containers/shared_ptr.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "math/geometry.h"
#include "math/quaternion.h"
#include "math/vector.h"
#include "types.h"

namespace vse::renderer
{

/// Instances in a chunk, at most.
inline constexpr u32 kInstanceChunkSize = 256;
/// Instances in a set, at most (the index of an instance fits 24 bits, so it is exact in an `f32`).
inline constexpr u32 kMaxInstances = 1u << 24;
/// The largest absolute world coordinate an instance may have, in metres.
inline constexpr f64 kMaxInstanceCoordinate = 1.0e12;

/// What an edit of a set returned.
enum class InstanceStatus : u8
{
    Ok,            ///< Done.
    Full,          ///< The set holds its most instances, or its most chunks (`InstanceSetDesc::max_chunks`), or the
                   ///< memory for another chunk was refused.
    StaleId,       ///< The id names an instance that was removed, or was never made by this set.
    NoData,        ///< The set was made without per-instance data.
    InvalidValue,  ///< A value is not finite, a coordinate is beyond `kMaxInstanceCoordinate`, or a scale is not above
                   ///< 0.
};

/// A stable handle to one instance of one set.
///
/// - Thread-safety: thread-compatible plain data.
struct InstanceId
{
    u32 index = 0xFFFFFFFFu;  ///< Position in the set's id table.
    u32 generation = 0;       ///< Which use of that table entry; 0 is never a live instance.

    /// Returns whether two ids are the same handle.
    bool operator==(const InstanceId& o) const { return index == o.index && generation == o.generation; }
    /// Returns whether two ids differ.
    bool operator!=(const InstanceId& o) const { return !(*this == o); }
};

/// The placement of an instance in the world.
///
/// - Thread-safety: thread-compatible plain data.
struct InstanceTransform
{
    Vec3d position;                       ///< World position in metres.
    Quaternion rotation;                  ///< Unit rotation.
    Vec3 scale = Vec3(1.0f, 1.0f, 1.0f);  ///< Scale on each axis, above 0.
};

/// The record of one instance as stored and uploaded: position and rotation and scale, 40 bytes. The position is an
/// offset from its chunk's origin.
///
/// - Thread-safety: thread-compatible plain data.
struct InstanceRecord
{
    Vec3 position;        ///< Offset from the chunk's origin, in metres.
    Quaternion rotation;  ///< Unit rotation.
    Vec3 scale;           ///< Scale on each axis.
};
static_assert(sizeof(InstanceRecord) == 40, "the instance record is 40 bytes");

/// The optional data of one instance: a colour tint and one custom parameter.
///
/// - Thread-safety: thread-compatible plain data.
struct InstanceExtra
{
    Vec4 tint = Vec4(1.0f, 1.0f, 1.0f, 1.0f);  ///< Colour multiplier, red, green, blue, alpha.
    u32 custom = 0;                            ///< A value the material may read.
};
static_assert(sizeof(InstanceExtra) == 20, "the instance data is 20 bytes");

/// What an instance set is made with.
///
/// - Thread-safety: thread-compatible plain data.
struct InstanceSetDesc
{
    /// Whether every instance carries an `InstanceExtra`.
    bool has_data = false;
    /// The most instances the set holds; 1 to `kMaxInstances`.
    u32 max_instances = kMaxInstances;
    /// The side of a Morton cell in metres; finite, above 0. Instances closer than this share a key.
    f64 key_cell_size = 4.0;
    /// How far, in metres, an instance may move outside its chunk's box before it changes chunk; finite, not below 0.
    f32 move_threshold = 8.0f;
    /// The most chunks the set may hold; 0 means four times the chunks its `max_instances` fill. A chunk is 256
    /// slots whatever it holds, so instances scattered far apart (each opening a chunk) stop at this many chunks and
    /// the next `Add` is `Full`.
    u32 max_chunks = 0;
};

/// A chunk's summary, what culling and the upload read.
///
/// - Thread-safety: thread-compatible plain data.
struct InstanceChunkInfo
{
    Vec3d origin;  ///< The world position the chunk's offsets are from.
    Aabb box;      ///< The exact box of the instances' positions, as offsets from `origin`; empty when `count` is 0.
    f32 max_scale = 0.0f;  ///< The largest scale component of any instance in the chunk (0 when empty).
    u32 count = 0;         ///< Instances in the chunk, at most `kInstanceChunkSize`.
    u32 first = 0;    ///< Index of the chunk's first record in the set's record array (`chunk * kInstanceChunkSize`).
    u64 version = 0;  ///< Changes whenever the chunk's records, data or box change; unique across the set.
};

/// One chunk to adopt: its origin and its records (and data) as stored.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: the spans are borrowed for the call.
struct InstanceChunkSource
{
    Vec3d origin;                        ///< The world position the offsets are from.
    Span<const InstanceRecord> records;  ///< 1 to `kInstanceChunkSize` records.
    Span<const InstanceExtra> extras;    ///< The data of each record, or empty for a set without data.
};

class InstanceSet;

/// The shared pointer of an instance set: the scene, the entity that fills it and the renderer share one.
using InstanceSetPtr = SharedPtr<InstanceSet>;
/// The shared pointer of an instance set that is only read.
using InstanceSetConstPtr = SharedPtr<const InstanceSet>;

/// A set of instances, chunked in Morton order.
///
/// - Thread-safety: thread-compatible; concurrent readers are safe while nobody edits.
/// - Ownership: made by [[InstanceSet::Create]] and held by an `InstanceSetPtr` (a `SharedPtr`); every id belongs to
/// the set that made it.
class InstanceSet
{
public:
    /// Makes an empty set.
    ///
    /// - Returns: the set, or an empty pointer when `desc` is invalid or the heap is exhausted.
    /// - Fails: returns an empty pointer for a `max_instances` of 0 or above `kMaxInstances`, a `key_cell_size` that is
    ///   not finite and above 0, or a `move_threshold` that is not finite and not below 0.
    static InstanceSetPtr Create(const InstanceSetDesc& desc);

    /// Makes a set from chunks that were minted: the records are copied chunk by chunk, with no `Add` per instance
    /// (`AddCalls()` stays 0). The chunks keep their order and their contents; the ids are the positions in that order
    /// (chunk by chunk, slot by slot), each with generation 1.
    ///
    /// - Param desc: the description, as for [[InstanceSet::Create]].
    /// - Param chunks: the chunks, in the order they are to be listed (a Morton order, as `Rebuild` leaves it).
    /// - Returns: the set, or an empty pointer when `desc` is invalid, the keys of the chunk origins are not in
    ///   non-decreasing order, a chunk is empty or holds more than `kInstanceChunkSize` records, the total is above
    ///   `max_instances`, a record has a value `Add` would refuse (not finite, a scale not above 0), a tint is not
    ///   finite, a chunk origin is beyond `kMaxInstanceCoordinate`, or `extras` is not the size of `records` (for a set
    ///   with data; it must be empty for one without).
    /// - Fails: returns an empty pointer; nothing is logged.
    /// - Ownership: copies the spans.
    static InstanceSetPtr FromChunks(const InstanceSetDesc& desc, Span<const InstanceChunkSource> chunks);

    /// Adds an instance.
    ///
    /// - Param transform: the placement.
    /// - Param data: the instance's data, or null for the default (`InstanceExtra{}`); ignored when the set has none.
    /// - Param out: receives the id on success; may be null.
    /// - Fails: `Full`, or `InvalidValue` for a non-finite value, a coordinate beyond `kMaxInstanceCoordinate`, a scale
    ///   component not above 0 or a rotation that is not finite; nothing changes.
    /// - Ownership: `*out` is written on success only; `data` is copied.
    InstanceStatus Add(const InstanceTransform& transform, const InstanceExtra* data, InstanceId* out);

    /// Removes an instance. The chunk's last instance takes its place; other ids stay valid.
    ///
    /// - Fails: `StaleId`.
    InstanceStatus Remove(InstanceId id);

    /// Moves an instance, in its chunk or into another.
    ///
    /// It stays in its chunk, growing or refitting the box, unless the new position is beyond the box by more than
    /// `move_threshold`: then it joins the chunk nearest its new key.
    ///
    /// - Fails: `StaleId`, `InvalidValue`; nothing changes.
    InstanceStatus SetTransform(InstanceId id, const InstanceTransform& transform);

    /// Sets an instance's data.
    ///
    /// - Fails: `StaleId`, `NoData`, `InvalidValue` (a non-finite tint).
    InstanceStatus SetData(InstanceId id, const InstanceExtra& data);

    /// Reads an instance's placement back (the position as the chunk origin plus the stored offset).
    ///
    /// - Fails: `StaleId`; `out` is not written.
    /// - Ownership: `*out` is written on success only.
    InstanceStatus Get(InstanceId id, InstanceTransform* out) const;

    /// Reads an instance's data.
    ///
    /// - Fails: `StaleId`, `NoData`; `out` is not written.
    /// - Ownership: `*out` is written on success only.
    InstanceStatus GetData(InstanceId id, InstanceExtra* out) const;

    /// Returns whether the id names a live instance of this set.
    ///
    /// - Fails: never; a stale or foreign id gives false.
    bool Contains(InstanceId id) const;

    /// Removes every instance; every id goes stale. Keeps the storage.
    void Clear();

    /// Sorts every instance by (Morton key, id) and cuts new full chunks.
    ///
    /// The instances, their ids and their data are unchanged; every chunk's version changes. The layout is a function
    /// of the content.
    void Rebuild();

    /// Returns the number of instances.
    u32 Count() const { return count_; }
    /// Returns the most instances the set holds.
    u32 Capacity() const { return desc_.max_instances; }
    /// Returns whether instances carry data.
    ///
    /// - Fails: never.
    bool HasData() const { return desc_.has_data; }
    /// Returns the number of chunk slots, including empty ones awaiting reuse; chunk indices are below this.
    u32 ChunkCount() const { return static_cast<u32>(chunks_.Size()); }
    /// Returns the summary of chunk `index` (below `ChunkCount()`).
    ///
    /// - Aborts: `index` is not below `ChunkCount()`.
    InstanceChunkInfo Chunk(u32 index) const;
    /// Returns the records of chunk `index`: `Chunk(index).count` of them.
    ///
    /// - Aborts: `index` is not below `ChunkCount()`.
    Span<const InstanceRecord> Records(u32 index) const;
    /// Returns the data of chunk `index`, parallel to `Records`; empty for a set without data.
    ///
    /// - Aborts: `index` is not below `ChunkCount()`.
    Span<const InstanceExtra> Extras(u32 index) const;
    /// Returns the id of the instance in a slot of a chunk.
    ///
    /// - Returns: the id, or an invalid id (generation 0) when the chunk or slot is out of range.
    /// - Fails: never.
    InstanceId IdAt(u32 chunk, u32 slot) const;
    /// Returns the sum of the volumes of the non-empty chunks' boxes: the measure of how well the chunks fit.
    f64 ChunkVolumeSum() const;
    /// Returns the bytes the set's arrays hold, counting reserved space.
    usize MemoryBytes() const;
    /// Returns the counter the last edit stamped on a chunk; it grows with every edit.
    u64 Version() const { return counter_; }
    /// Returns how many times `Add` was called on this set (a set made by `FromChunks` starts at 0).
    u64 AddCalls() const { return add_calls_; }

private:
    struct Slot
    {
        u32 chunk = 0xFFFFFFFFu;
        u32 pos = 0;
        u32 generation = 1;
        u32 next_free = 0xFFFFFFFFu;
    };
    struct Chunk_
    {
        Vec3d origin;
        Aabb box = Aabb::Empty();
        f32 max_scale = 0.0f;
        u32 count = 0;
        u64 version = 0;
        u64 key = 0;
        bool listed = false;
    };

public:
    /// Used by [[InstanceSet::Create]]; do not call.
    explicit InstanceSet(const InstanceSetDesc& desc) : desc_(desc) {}

private:
    u64 KeyOf(const Vec3d& p) const;
    static bool ValidTransform(const InstanceTransform& t);
    bool Resolve(InstanceId id, u32* chunk, u32* pos) const;
    u32 OrderLowerBound(u64 key, u32 chunk) const;
    u32 ChunkCap() const;
    bool Near(u32 chunk, const Vec3d& position) const;
    u32 ChooseChunk(u64 key, const Vec3d& position);
    u32 NewChunk(u64 key, const Vec3d& origin);
    void FreeChunk(u32 chunk);
    void Refit(u32 chunk);
    void Attach(u32 chunk, u32 handle, const InstanceTransform& t, const InstanceExtra& extra);
    void Detach(u32 chunk, u32 pos);

    InstanceSetDesc desc_;
    u32 count_ = 0;
    u64 counter_ = 0;
    u64 add_calls_ = 0;
    Vector<Chunk_> chunks_;
    Vector<InstanceRecord> records_;
    Vector<u32> handles_;
    Vector<InstanceExtra> extras_;
    Vector<u32> order_;
    Vector<u32> free_chunks_;
    Vector<Slot> slots_;
    u32 free_slot_ = 0xFFFFFFFFu;
};

}  // namespace vse::renderer
