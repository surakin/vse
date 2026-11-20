#pragma once

//! The mesh blob (type `MESH`, version 1): geometry laid out for the GPU, built by [[asset::BuildMeshBlob]] and read
//! zero-copy through [[asset::MeshView]]. Phase 22 Day 319.
//!
//! Sections (ids from 16, little-endian, each 64-byte aligned by the container): 16 header (96 bytes), 17 attributes
//! (16 bytes each: location, format, offset, binding), 18 vertex stream 0 (the exact bytes of the vertex buffer), 19
//! indices (16 or 32 bit, a triangle list), 20 LODs (16 bytes each), 21 material slots (40 bytes each), and for a
//! skinned mesh 22 the skin stream (binding 1: `UInt8x4` joint indices at location 5, `UNorm8x4` weights at location 6,
//! summing to exactly 255 per vertex) and 23 the bind-pose box of each joint's influence set. A skinned mesh declares
//! its skeleton as a dependency (role [[asset::kMeshRoleSkinSkeleton]]) and records the skeleton hash and joint count.
//! Morph targets (glTF coverage Day 6) are sections 40 (the targets, 48 bytes each: a name of up to 31 bytes, the
//! first delta, the delta count) and 41 (the deltas, 40 bytes each: the vertex, then position, normal and tangent
//! deltas as `f32x3`; a target's deltas sorted by vertex, no repeats). An older reader refused section 40, so a blob
//! with targets is refused there and one without is unchanged: the version stays 1. The box holds every target at
//! weights from -1 to 1.
//!
//! Per-vertex colours (glTF coverage Day 4) are one more attribute of stream 0, `UNorm8x4` at location 4 after the
//! others (offset 24 packed, 48 full): rgb sRGB-encoded, alpha linear, as `gbuffer.vert`'s `VERTEX_COLOR` reads them.
//! The version-1 reader already took any attribute, so a coloured mesh is still version 1.

#include "asset/blob.h"
#include "asset/content_hash.h"
#include "asset/resource_id.h"
#include "asset/resource_type.h"
#include "asset/skeleton.h"
#include "asset/status.h"
#include "containers/array.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "rhi/enums.h"
#include "string_view.h"
#include "types.h"

namespace vse::asset
{

/// Most vertices of one mesh.
inline constexpr u32 kMaxMeshVertices = u32(1) << 24;
/// Most indices of one mesh.
inline constexpr u32 kMaxMeshIndices = u32(1) << 26;
/// Most LODs of one mesh.
inline constexpr u32 kMaxMeshLods = 8;
/// Most material slots of one mesh.
inline constexpr u32 kMaxMeshSlots = 64;
/// Most bytes of a slot name.
inline constexpr u32 kMaxSlotNameBytes = 31;
/// Most attributes of one mesh.
inline constexpr u32 kMaxMeshAttributes = 8;
/// Most morph targets of one mesh.
inline constexpr u32 kMaxMorphTargets = 64;
/// Most bytes of a morph target's name.
inline constexpr u32 kMaxMorphNameBytes = 31;
/// The dependency role of a skinned mesh's skeleton.
inline constexpr u32 kMeshRoleSkinSkeleton = 1;
/// The shader input location of the position.
inline constexpr u32 kMeshLocationPosition = 0;
/// The shader input location of the normal.
inline constexpr u32 kMeshLocationNormal = 1;
/// The shader input location of the tangent (w is the bitangent sign).
inline constexpr u32 kMeshLocationTangent = 2;
/// The shader input location of the texture coordinates.
inline constexpr u32 kMeshLocationUv = 3;
/// The shader input location of the per-vertex colour (`UNorm8x4`, rgb sRGB-encoded, alpha linear).
inline constexpr u32 kMeshLocationColor = 4;
/// The shader input location of the skin joint indices (vertex binding 1).
inline constexpr u32 kMeshLocationJoints = 5;
/// The shader input location of the skin weights (vertex binding 1).
inline constexpr u32 kMeshLocationWeights = 6;

/// The section ids of a mesh blob.
enum MeshSection : u32
{
    kMeshSectionHeader = 16,  ///< Counts, flags, bounds, texture-coordinate decode and the skeleton record (96 bytes).
    kMeshSectionAttributes = 17,    ///< The vertex attributes, 16 bytes each.
    kMeshSectionVertices = 18,      ///< Vertex stream 0.
    kMeshSectionIndices = 19,       ///< The indices.
    kMeshSectionLods = 20,          ///< The LODs, 16 bytes each.
    kMeshSectionSlots = 21,         ///< The material slots, 40 bytes each.
    kMeshSectionSkin = 22,          ///< The skin stream (vertex binding 1), 8 bytes per vertex.
    kMeshSectionJointBounds = 23,   ///< The bind-pose box of each joint's influence set, 32 bytes each.
    kMeshSectionMorphTargets = 40,  ///< The morph targets, 48 bytes each.
    kMeshSectionMorphDeltas = 41,   ///< The morph deltas, 40 bytes each.
};

/// How the minter stores the attributes.
enum class MeshQuality : u32
{
    Packed = 0,  ///< Position `Float3`, normal and tangent `SNorm8x4`, texture coordinates `UNorm16x2` (24 bytes).
    Full = 1,    ///< Position `Float3`, normal `Float3`, tangent `Float4`, texture coordinates `Float2` (48 bytes).
};

/// One vertex as the minter takes it (all in `f32`; the minter packs).
///
/// - Thread-safety: thread-compatible (a plain value).
struct MeshVertex
{
    Array<f32, 3> position = {{0, 0, 0}};    ///< Object-space position.
    Array<f32, 3> normal = {{0, 0, 1}};      ///< Normal (normalized by the minter).
    Array<f32, 4> tangent = {{1, 0, 0, 1}};  ///< Tangent xyz and the bitangent sign w (+1 or -1).
    Array<f32, 2> uv = {{0, 0}};             ///< Texture coordinates.
};

/// Up to four skin influences of one vertex, already quantized: `weight` sums to exactly 255.
///
/// - Thread-safety: thread-compatible (a plain value).
struct MeshSkinInfluence
{
    Array<u8, 4> joint = {{0, 0, 0, 0}};   ///< Joint indices, strongest first.
    Array<u8, 4> weight = {{0, 0, 0, 0}};  ///< Weights, summing to 255.
};

/// One LOD: a range of indices and the screen size (fraction of the screen height) below which the next LOD is used.
///
/// - Thread-safety: immutable value.
struct MeshLod
{
    u32 first_index = 0;  ///< First index of the range.
    u32 index_count = 0;  ///< Number of indices (a multiple of 3).
    f32 screen_size = 0;  ///< Non-increasing from one LOD to the next.
};

/// One material slot: a name and a range of indices.
///
/// - Thread-safety: thread-compatible (a plain value).
struct MeshSlot
{
    Array<char, kMaxSlotNameBytes + 1> name = {};  ///< NUL-terminated.
    u32 first_index = 0;                           ///< First index of the range.
    u32 index_count = 0;                           ///< Number of indices (a multiple of 3).
};

/// One vertex's change in a morph target: added to the vertex times the target's weight.
///
/// - Thread-safety: thread-compatible (a plain value).
struct MeshMorphDelta
{
    u32 vertex = 0;                        ///< The vertex it moves.
    Array<f32, 3> position = {{0, 0, 0}};  ///< Position delta.
    Array<f32, 3> normal = {{0, 0, 0}};    ///< Normal delta.
    Array<f32, 3> tangent = {{0, 0, 0}};   ///< Tangent delta (xyz; the sign is the base's).
};

/// A morph target as the minter takes it.
///
/// - Thread-safety: thread-compatible (a plain value; borrows its deltas).
struct MeshMorphTarget
{
    Array<char, kMaxMorphNameBytes + 1> name = {};  ///< NUL-terminated, not empty.
    Span<const MeshMorphDelta> deltas;              ///< Sorted by vertex, no repeats; all-zero deltas are dropped.
};

/// A morph target of a blob.
///
/// - Thread-safety: thread-compatible (a plain value).
struct MeshMorphTargetInfo
{
    Array<char, kMaxMorphNameBytes + 1> name = {};  ///< NUL-terminated.
    u32 first_delta = 0;                            ///< Its first delta in the delta section.
    u32 delta_count = 0;                            ///< Its number of deltas.
};

/// One vertex attribute of a blob.
///
/// - Thread-safety: immutable value.
struct MeshAttribute
{
    u32 location = 0;                                      ///< Shader input location.
    rhi::VertexFormat format = rhi::VertexFormat::Float3;  ///< Storage format.
    u32 offset = 0;                                        ///< Byte offset inside the vertex of its binding.
    u32 binding = 0;                                       ///< Vertex buffer binding (0 geometry, 1 skin).
};

/// The per-joint box of the bind-pose influence set.
///
/// - Thread-safety: immutable value.
struct MeshJointBounds
{
    bool used = false;                ///< False when no vertex has weight on the joint.
    Array<f32, 3> min = {{0, 0, 0}};  ///< Box minimum.
    Array<f32, 3> max = {{0, 0, 0}};  ///< Box maximum.
};

/// What the minter hands to [[asset::BuildMeshBlob]].
///
/// - Thread-safety: thread-compatible.
/// - Ownership: borrows the spans for the call.
struct MeshBuildInput
{
    Span<const MeshVertex> vertices;            ///< At least one.
    Span<const u32> indices;                    ///< A triangle list.
    Span<const MeshLod> lods;                   ///< May be empty (one implicit LOD).
    Span<const MeshSlot> slots;                 ///< May be empty.
    MeshQuality quality = MeshQuality::Packed;  ///< The storage.
    Span<const MeshSkinInfluence> skin;         ///< Empty, or one per vertex.
    Span<const MeshMorphTarget> morph_targets;  ///< Empty, or up to [[asset::kMaxMorphTargets]].
    Span<const Array<u8, 4>> colors;            ///< Empty, or one per vertex: rgb sRGB-encoded, alpha linear.
    ResourceId skeleton;                        ///< The skeleton of a skinned mesh.
    ContentHash skeleton_hash;                  ///< Its hash.
    u32 skeleton_joint_count = 0;               ///< Its joint count.
};

/// Packs a source's up-to-four influences into quantized ones: the four largest weights (ties by joint index), weights
/// renormalized in `f64` and rounded by the largest-remainder rule to sum to exactly 255, sorted by weight then joint.
///
/// - Param joints: the influenced joints (duplicates are summed first by the caller).
/// - Param weights: their non-negative finite weights, at least one positive.
/// - Param count: the number of pairs.
/// - Returns: false when no weight is positive.
/// - Fails: reported by the return value.
/// - Ownership: borrows the arrays and `out` for the call.
bool QuantizeSkinInfluences(const u32* joints, const f64* weights, usize count, MeshSkinInfluence* out);

/// Encodes a linear colour as a mesh stores it: rgb to sRGB, alpha linear, each rounded to 8 bits (the inverse of the
/// shader's `SrgbToLinear`). Values outside 0..1 are clamped.
///
/// - Param rgba: the linear colour.
/// - Returns: the four bytes.
/// - Fails: never; a value that is not a number encodes as 0.
Array<u8, 4> EncodeMeshColor(const Array<f32, 4>& rgba);

/// Builds a mesh blob: packs the attributes, computes the bounds (and the joint boxes of a skinned mesh), records the
/// measured packing error, and writes the sections.
///
/// - Fails: `Invalid` (no vertices or indices, an index count that is not a multiple of 3, an index past the vertices,
///   a non-finite value, a LOD or slot range outside the indices or not a multiple of 3, LOD screen sizes that grow, a
///   slot name that is empty or too long, a morph target with a bad name, a delta past the vertices or out of
///   order, more targets than [[asset::kMaxMorphTargets]], a colour array of the wrong length, a skin array of the
///   wrong length, a skin joint at or past the skeleton's joint count or weights that do not sum to 255);
///   `LimitExceeded` for a count above the limits.
/// - Ownership: borrows `input` for the call; `*out` is replaced.
Status BuildMeshBlob(const TypeRegistry& types, ResourceId id, const MeshBuildInput& input, Vector<byte>* out,
                     u32 target = 0);

/// A mesh blob read in place.
///
/// - Thread-safety: thread-compatible; immutable once opened.
/// - Ownership: borrows the blob bytes, which must outlive the view.
class MeshView
{
public:
    /// Creates an empty view.
    MeshView() = default;

    /// Validates `data` as a mesh blob of the current version and keeps spans into it. The indices are not scanned (see
    /// `CheckIndices`).
    ///
    /// - Fails: the errors of [[asset::BlobReader::Open]]; `TypeMismatch` for another type; `Corrupt` for a missing or
    ///   mis-sized section, a count above the limit, an attribute that is not valid for its binding, a stream whose
    ///   size is not the stride times the count, a LOD or slot range outside the indices, LOD screen sizes that grow,
    ///   a non-finite bound, a skin section without its skeleton dependency or the reverse, or a morph section that is
    ///   mis-sized, has more targets than the limit, a delta past the vertices, out of order or not finite, or deltas
    ///   that the targets do not cover exactly (each delta is checked: the GPU indexes vertices with them).
    /// - Ownership: borrows `data` and `types` for the call; the view keeps spans into `data`.
    Status Open(ByteSpan data, const TypeRegistry& types);

    /// The number of vertices.
    u32 VertexCount() const { return vertex_count_; }
    /// The number of indices.
    u32 IndexCount() const { return index_count_; }
    /// The width of an index in bytes (2 or 4).
    u32 IndexBytesPerIndex() const { return index_width_; }
    /// True when the mesh has per-vertex colours (an attribute at [[asset::kMeshLocationColor]]).
    ///
    /// - Fails: never.
    bool HasColors() const { return has_colors_; }
    /// The number of morph targets (0 without).
    u32 MorphTargetCount() const { return morph_target_count_; }
    /// Morph target `i`.
    ///
    /// - Aborts: `i` is not below `MorphTargetCount()`.
    MeshMorphTargetInfo MorphTarget(u32 i) const;
    /// The total number of morph deltas.
    u32 MorphDeltaCount() const { return morph_delta_count_; }
    /// Morph delta `k` of the delta section (a target's are `first_delta` to `first_delta + delta_count - 1`).
    ///
    /// - Aborts: `k` is not below `MorphDeltaCount()`.
    MeshMorphDelta MorphDelta(u32 k) const;
    /// The bytes of the delta section, 40 per delta, as the blob holds them.
    ByteSpan MorphDeltaBytes() const { return morph_deltas_; }
    /// True when the mesh has a skin stream.
    ///
    /// - Fails: never.
    bool Skinned() const { return skinned_; }
    /// The storage the minter used.
    MeshQuality Quality() const { return quality_; }
    /// The number of attributes.
    u32 AttributeCount() const { return attribute_count_; }
    /// Attribute `i`.
    ///
    /// - Aborts: `i` is not below `AttributeCount()`.
    MeshAttribute Attribute(u32 i) const;
    /// The bytes of vertex stream 0, exactly as the vertex buffer holds them.
    ByteSpan VertexBytes() const { return vertices_; }
    /// The bytes of the indices.
    ByteSpan IndexBytes() const { return indices_; }
    /// The bytes of vertex stream 1 (empty when not skinned).
    ByteSpan SkinBytes() const { return skin_; }
    /// The bytes per vertex of stream 0.
    u32 VertexStride() const { return stride_; }
    /// The object-space box.
    Array<f32, 3> BoundsMin() const { return bounds_min_; }
    /// The object-space box.
    Array<f32, 3> BoundsMax() const { return bounds_max_; }
    /// The texture-coordinate decode: `uv = offset + scale * value` for `UNorm16x2` (offset 0, scale 1 for `Full`).
    Array<f32, 2> UvOffset() const { return uv_offset_; }
    /// See `UvOffset`.
    Array<f32, 2> UvScale() const { return uv_scale_; }
    /// The largest error the packing measured on the texture coordinates.
    f32 UvError() const { return uv_error_; }
    /// The largest component error the packing measured on the normals.
    f32 NormalError() const { return normal_error_; }
    /// The number of LODs (at least 1).
    u32 LodCount() const { return lod_count_; }
    /// LOD `i`.
    ///
    /// - Aborts: `i` is not below `LodCount()`.
    MeshLod Lod(u32 i) const;
    /// The number of material slots.
    u32 SlotCount() const { return slot_count_; }
    /// Slot `i`.
    ///
    /// - Aborts: `i` is not below `SlotCount()`.
    MeshSlot Slot(u32 i) const;
    /// The skeleton a skinned mesh was minted against.
    ResourceId Skeleton() const { return skeleton_; }
    /// The recorded skeleton hash.
    ContentHash SkeletonHash() const { return skeleton_hash_; }
    /// The recorded skeleton joint count.
    u32 SkinJointCount() const { return skin_joint_count_; }
    /// The bind-pose box of joint `i`'s influence set.
    ///
    /// - Aborts: `i` is not below `SkinJointCount()`.
    MeshJointBounds JointBounds(u32 i) const;
    /// The joint bounds section: [[asset::MeshView::SkinJointCount]] records of 32 bytes (minimum `f32x3`, maximum
    /// `f32x3`, a `u32` that is 1 when the joint has vertices, 4 bytes of padding); empty for a rigid mesh.
    ///
    /// - Fails: never.
    /// - Ownership: borrowed from the blob.
    ByteSpan JointBoundsBytes() const { return joint_bounds_; }

    /// Checks a loaded skeleton against the record of a skinned mesh.
    ///
    /// - Fails: `DependencyTypeMismatch` when the hash or the joint count differs; `Success` for a mesh that is not
    ///   skinned.
    Status CheckSkeleton(const SkeletonView& skeleton) const;

    /// Checks that every index is below the vertex count (a pass over the indices: a tool's job, not the load path).
    ///
    /// - Fails: `Corrupt`.
    Status CheckIndices() const;

    /// Checks the skin stream: every vertex's weights sum to exactly 255 and every joint with a weight is below the
    /// joint count (a pass over the stream: a tool's job, not the load path).
    ///
    /// - Fails: `Corrupt`.
    Status CheckSkin() const;

private:
    u32 vertex_count_ = 0;
    u32 index_count_ = 0;
    u32 index_width_ = 2;
    bool skinned_ = false;
    bool has_colors_ = false;
    u32 morph_target_count_ = 0;
    u32 morph_delta_count_ = 0;
    ByteSpan morph_targets_;
    ByteSpan morph_deltas_;
    MeshQuality quality_ = MeshQuality::Packed;
    u32 attribute_count_ = 0;
    u32 stride_ = 0;
    u32 lod_count_ = 0;
    u32 slot_count_ = 0;
    u32 skin_joint_count_ = 0;
    Array<f32, 3> bounds_min_ = {{0, 0, 0}};
    Array<f32, 3> bounds_max_ = {{0, 0, 0}};
    Array<f32, 2> uv_offset_ = {{0, 0}};
    Array<f32, 2> uv_scale_ = {{1, 1}};
    f32 uv_error_ = 0;
    f32 normal_error_ = 0;
    ResourceId skeleton_;
    ContentHash skeleton_hash_;
    ByteSpan attributes_;
    ByteSpan vertices_;
    ByteSpan indices_;
    ByteSpan lods_;
    ByteSpan slots_;
    ByteSpan skin_;
    ByteSpan joint_bounds_;
};

}  // namespace vse::asset
