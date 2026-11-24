#pragma once
//! The instance tree resource: a forest of instanced nodes as minted bytes. [[asset::BuildInstanceTreeBlob]] writes an
//! `ITRE` blob from nodes listed with parent indices, [[asset::InstanceTreeBlobView]] reads one in place, and
//! [[asset::MakeInstanceTree]] makes the renderer's [[renderer::InstanceTree]] from it.
//!
//! Layout of the blob's sections (little-endian):
//!
//! | Section | Content |
//! |---|---|
//! | 16 header | 32 bytes: node count `u32`, mesh count `u32`, flags `u32` (zero), reserved `u32` x5 (zero) |
//! | 17 meshes | 24 bytes a mesh: box minimum `f32` x3, maximum `f32` x3 |
//! | 18 nodes | 88 bytes a node, in the order given (a parent comes before its children): parent index `u32`
//! (`0xFFFFFFFF` for a root), mesh `u32`, material slot `u32`, flags `u32`, position `f64` x3 (a root's world position,
//! otherwise the offset from the parent), rotation `f32` x4, scale `f32` x3, tint `f32` x4, custom `u32` |
//!
//! The blob is a function of the list, and loading rebuilds the depth-first array through the builder, so a blob that
//! holds a value the builder refuses (a parent index that is not smaller than the node's, a mesh out of range, a scale
//! not above 0) is refused as corrupt.

#include "asset/blob.h"
#include "asset/resource_id.h"
#include "asset/resource_type.h"
#include "asset/status.h"
#include "containers/shared_bytes.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "jobs/scheduler.h"
#include "renderer/instance_tree.h"
#include "types.h"

namespace vse::asset
{

/// The size of the header section.
inline constexpr usize kInstanceTreeBlobHeaderBytes = 32;
/// The size of one mesh entry.
inline constexpr usize kInstanceTreeBlobMeshBytes = 24;
/// The size of one node entry.
inline constexpr usize kInstanceTreeBlobNodeBytes = 88;

/// The section ids of an instance tree blob.
enum InstanceTreeBlobSection : u32
{
    kInstanceTreeBlobSectionHeader = 16,  ///< Counts (32 bytes).
    kInstanceTreeBlobSectionMeshes = 17,  ///< The mesh boxes.
    kInstanceTreeBlobSectionNodes = 18,   ///< The nodes.
};

/// One node of a tree source: its parent as an index into the list (or `kNoParent`) and what the builder needs.
///
/// - Thread-safety: thread-compatible plain data.
struct InstanceTreeSourceNode
{
    static constexpr u32 kNoParent = 0xFFFFFFFFu;  ///< A root.
    u32 parent = kNoParent;                        ///< Index of an earlier node, or `kNoParent`.
    renderer::TreeNodeDesc desc;                   ///< The node.
};

/// Writes the blob of a tree: the nodes in the order given and the mesh boxes.
///
/// - Param types: the registry; it must hold `kInstanceTrees`.
/// - Param id: the resource id.
/// - Param meshes: the mesh boxes the nodes name.
/// - Param nodes: the nodes; a parent index must be smaller than the node's own index.
/// - Param out: receives the blob bytes.
/// - Param target: the target platform code of the blob header.
/// - Fails: `Invalid` when the builder would refuse the list (checked by building it once); the errors of
///   [[asset::BlobWriter::Finish]].
/// - Ownership: borrows its arguments for the call; `*out` receives a copy.
Status BuildInstanceTreeBlob(const TypeRegistry& types, ResourceId id, Span<const renderer::TreeMeshInfo> meshes,
                             Span<const InstanceTreeSourceNode> nodes, Vector<byte>* out, u32 target = 0);

/// An instance tree blob validated and read in place.
///
/// - Thread-safety: thread-compatible; immutable after `Open`.
/// - Ownership: shares the buffer it was opened on.
class InstanceTreeBlobView
{
public:
    /// Creates an empty view.
    InstanceTreeBlobView() = default;

    /// Validates `bytes` as an instance tree blob and keeps a share of them. The node values are checked when the
    /// tree is made.
    ///
    /// - Fails: the errors of [[asset::BlobReader::Open]]; `TypeMismatch` when the blob is not an `ITRE`; `Corrupt` for
    ///   a missing or mis-sized section, counts outside their limits, or a reserved field that is not zero.
    /// - Ownership: shares `bytes`.
    Status Open(const SharedBytesPtr& bytes, const TypeRegistry& types);

    /// The number of nodes.
    /// - Fails: never.
    u32 NodeCount() const { return node_count_; }
    /// The number of meshes.
    /// - Fails: never.
    u32 MeshCount() const { return mesh_count_; }
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
    friend Status MakeInstanceTree(const InstanceTreeBlobView& view, jobs::Scheduler* scheduler,
                                   renderer::InstanceTreePtr* out);
    u32 node_count_ = 0;
    u32 mesh_count_ = 0;
    ResourceId id_;
    SharedBytesPtr bytes_;
    usize meshes_ = 0;  // offset of the mesh section
    usize nodes_ = 0;   // offset of the node section
};

/// Makes the tree a blob holds: the nodes go through [[renderer::InstanceTreeBuilder]] in order.
///
/// - Param view: an open view.
/// - Param scheduler: used for the first world pass; may be null.
/// - Param out: receives the tree.
/// - Fails: `Invalid` for an empty view; `Corrupt` when the builder refuses a node (a hostile blob).
/// - Ownership: copies out of the view's bytes; the tree shares nothing with them.
Status MakeInstanceTree(const InstanceTreeBlobView& view, jobs::Scheduler* scheduler, renderer::InstanceTreePtr* out);

}  // namespace vse::asset
