#pragma once

//! The skeleton resource: the joint hierarchy, the bind pose and the inverse bind matrices that
//! skinned meshes and animation clips refer to. [[asset::BuildSkeletonBlob]] writes a `SKEL` blob,
//! [[asset::SkeletonView]] reads one in place.
//!
//! Layout of the blob's sections (all little-endian, one section each, 64-byte aligned by the container):
//!
//! | Section | Content |
//! |---|---|
//! | 16 header | 48 bytes: joint count `u32`, flags `u32` (bit 0: names present), skeleton hash (16 bytes), bind-pose
//! bound center `f32 x3`, radius `f32`, two zero `u32` | | 17 parents | `u16` per joint; 0xFFFF for a root; a parent
//! index is always below the joint's own | | 18 translations | `f32 x3` per joint: the bind pose, local space | | 19
//! rotations | `f32 x4` (x, y, z, w) per joint: unit quaternions with w >= 0 | | 20 scales | `f32 x3` per joint | | 21
//! inverse bind | 3x4 row-major `f32` (48 bytes) per joint: the bytes the GPU joint palette takes | | 22 name hashes |
//! `u32` per joint (FNV-1a of the name; only with names) | | 23 names | `u32` offsets (joint count + 1) then the
//! characters, no terminators (only with names) |

#include "asset/blob.h"
#include "asset/content_hash.h"
#include "asset/resource_id.h"
#include "asset/resource_type.h"
#include "asset/status.h"
#include "containers/array.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "string_view.h"
#include "types.h"

namespace vse::asset
{

/// The most joints a skeleton holds in v1 (the joint palette of the renderer is sized for it).
inline constexpr u32 kMaxSkeletonJoints = 256;
/// The longest joint name in bytes.
inline constexpr u32 kMaxJointNameBytes = 64;
/// The parent index of a root joint.
inline constexpr u16 kNoParent = 0xFFFF;
/// The size of one inverse bind matrix in the blob (3x4 `f32`).
inline constexpr usize kInverseBindBytes = 48;

/// The section ids of a skeleton blob.
enum SkeletonSection : u32
{
    kSkeletonSectionHeader = 16,        ///< The counts, flags, skeleton hash and bind-pose bound (48 bytes).
    kSkeletonSectionParents = 17,       ///< A `u16` parent index per joint, `kNoParent` for a root.
    kSkeletonSectionTranslations = 18,  ///< `f32 x3` bind translation per joint.
    kSkeletonSectionRotations = 19,     ///< `f32 x4` bind rotation (x, y, z, w) per joint.
    kSkeletonSectionScales = 20,        ///< `f32 x3` bind scale per joint.
    kSkeletonSectionInverseBind = 21,   ///< A 3x4 row-major `f32` matrix per joint, the bytes of the palette upload.
    kSkeletonSectionNameHashes = 22,    ///< A `u32` name hash per joint (with names only).
    kSkeletonSectionNames = 23,         ///< `n + 1` `u32` offsets, then the characters (with names only).
};

/// The FNV-1a hash of a joint name that the name-hash section stores.
///
/// - Fails: never.
inline u32 JointNameHash(StringView name)
{
    u32 h = 2166136261u;
    for (usize i = 0; i < name.Length(); ++i)
    {
        h = (h ^ static_cast<u8>(name.Data()[i])) * 16777619u;
    }
    return h;
}

/// One joint as the writer takes it.
///
/// - Thread-safety: thread-compatible (a plain value).
struct SkeletonJoint
{
    Array<char, kMaxJointNameBytes + 1> name = {};  ///< The joint's name, NUL-terminated, unique in the skeleton.
    u16 parent = kNoParent;                         ///< The parent's index (below this joint's) or `kNoParent`.
    Array<f32, 3> translation = {{0, 0, 0}};        ///< Bind pose translation, local space.
    Array<f32, 4> rotation = {{0, 0, 0, 1}};        ///< Bind pose rotation as a unit quaternion (x, y, z, w), w >= 0.
    Array<f32, 3> scale = {{1, 1, 1}};              ///< Bind pose scale.
    Array<f32, 12> inverse_bind = {};  ///< The inverse of the joint's model-space bind matrix, 3x4 row-major.
};

/// Fills every joint's `inverse_bind` from the bind pose: the product of the local matrices from the root down,
/// inverted, computed in double precision and rounded to `f32` (deterministic).
///
/// - Param joints: parents before children; each parent index below its joint's.
/// - Fails: never; a degenerate scale gives a non-finite matrix that [[asset::BuildSkeletonBlob]] refuses.
void ComputeInverseBindMatrices(Span<SkeletonJoint> joints);

/// The skeleton hash: 128 bits over the joint count, the parents and the names (what a clip or a skinned mesh records
/// to prove it was made for this skeleton).
///
/// - Fails: never.
ContentHash ComputeSkeletonHash(Span<const SkeletonJoint> joints);

/// Writes the blob of a skeleton, deterministically.
///
/// - Param types: the registry; it must hold `kSkeleton`.
/// - Param id: the skeleton's resource id.
/// - Param joints: at most `kMaxSkeletonJoints`, parents before children, unique non-empty names of at most
///   `kMaxJointNameBytes`, finite values, unit rotations with w >= 0, non-zero scales, finite inverse bind matrices.
/// - Param include_names: whether the name hashes and names go into the blob (the skeleton hash covers them either
/// way).
/// - Fails: `Invalid` naming the first broken rule and the joint; `LimitExceeded` above the joint limit (the message
///   names the limit); the errors of [[asset::BlobWriter::Finish]].
/// - Ownership: `out` receives the bytes; nothing is borrowed after the call.
Status BuildSkeletonBlob(const TypeRegistry& types, ResourceId id, Span<const SkeletonJoint> joints, bool include_names,
                         Vector<byte>* out, u32 target = 0);

/// A skeleton blob validated and read in place. Every section is a span of the caller's buffer; nothing is copied.
///
/// - Thread-safety: thread-compatible; immutable after `Open`.
/// - Ownership: borrows the buffer, which must outlive the view.
class SkeletonView
{
public:
    /// Creates an empty view.
    SkeletonView() = default;

    /// Validates `data` as a skeleton blob of the current version and keeps spans into it.
    ///
    /// - Fails: the errors of [[asset::BlobReader::Open]]; `TypeMismatch` when the blob is not a `SKEL`; `Corrupt` for
    /// a
    ///   missing or mis-sized section, a joint count above the limit, a parent that is not below its joint, a
    ///   non-unit or non-finite rotation, a zero or non-finite scale, a non-finite value, a name offset out of range
    ///   or a name hash that does not match its name.
    /// - Ownership: borrows `data`.
    Status Open(ByteSpan data, const TypeRegistry& types);

    /// The number of joints.
    /// - Fails: never.
    u32 JointCount() const { return joint_count_; }
    /// The resource id recorded in the blob header.
    ///
    /// - Fails: never.
    ResourceId Id() const { return id_; }
    /// The skeleton hash recorded in the blob.
    /// - Fails: never.
    const ContentHash& Hash() const { return hash_; }
    /// Whether the blob holds joint names.
    /// - Fails: never.
    bool HasNames() const { return has_names_; }
    /// The center of the bind pose's joint positions (model space).
    /// - Fails: never.
    Array<f32, 3> BoundCenter() const { return center_; }
    /// The radius around `BoundCenter` that holds every joint position of the bind pose.
    /// - Fails: never.
    f32 BoundRadius() const { return radius_; }

    /// The parent of joint `i`, or `kNoParent`.
    /// - Aborts: `i` is not below `JointCount()`.
    u16 Parent(u32 i) const;
    /// The bind pose translation of joint `i`.
    /// - Aborts: `i` is not below `JointCount()`.
    Array<f32, 3> Translation(u32 i) const;
    /// The bind pose rotation of joint `i` (x, y, z, w).
    /// - Aborts: `i` is not below `JointCount()`.
    Array<f32, 4> Rotation(u32 i) const;
    /// The bind pose scale of joint `i`.
    /// - Aborts: `i` is not below `JointCount()`.
    Array<f32, 3> Scale(u32 i) const;
    /// The inverse bind matrix of joint `i` (3x4 row-major).
    /// - Aborts: `i` is not below `JointCount()`.
    Array<f32, 12> InverseBind(u32 i) const;
    /// The inverse bind matrices of every joint, `kInverseBindBytes` each, exactly the bytes the joint palette takes.
    /// - Ownership: a span of the blob buffer; it is 64-byte aligned relative to the buffer's start.
    ByteSpan InverseBindBytes() const { return inverse_bind_; }
    /// The name of joint `i` (empty without names).
    /// - Aborts: `i` is not below `JointCount()`.
    StringView Name(u32 i) const;

    /// Finds a joint by name: the hash table first, then the name itself.
    ///
    /// - Param index: receives the joint's index when found.
    /// - Returns: false when there is no such joint or the blob has no names.
    /// - Ownership: borrows `name` for the call only.
    bool FindJoint(StringView name, u32* index) const;

private:
    u32 joint_count_ = 0;
    ResourceId id_;
    ContentHash hash_;
    bool has_names_ = false;
    Array<f32, 3> center_ = {};
    f32 radius_ = 0;
    ByteSpan parents_;
    ByteSpan translations_;
    ByteSpan rotations_;
    ByteSpan scales_;
    ByteSpan inverse_bind_;
    ByteSpan name_hashes_;
    ByteSpan names_;
};

}  // namespace vse::asset
