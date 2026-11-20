#pragma once

//! Importing glTF skins as skeletons.
//!
//! Phase 22 Day 337. Each glTF skin becomes one skeleton resource ([[asset::BuildSkeletonBlob]]): joints parent-first,
//! a non-joint node between two joints folded into the child's bind-local transform, the transform above a root joint
//! kept in the root so the skeleton is in the glTF scene's space, inverse bind matrices taken from the skin or
//! computed.

#include "asset/skeleton.h"
#include "asset_import/gltf_import.h"
#include "asset_import/gltf_nodes.h"
#include "containers/vector.h"

namespace vse::asset
{

/// A glTF node that is a joint of a skeleton, and its joint index there.
///
/// - Thread-safety: thread-compatible plain data.
struct NodeJoint
{
    u32 node = 0;   ///< The glTF node.
    u32 joint = 0;  ///< The joint index in the skeleton.
};

/// A skin that became a skeleton, with what meshes and clips need to refer to it.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns its vectors.
struct ImportedSkin
{
    /// An empty skin.
    ImportedSkin() : name(HeapAllocator::Default()) {}
    u32 skin_index = 0;          ///< Its index in the glTF's `skins`.
    String<HeapAllocator> name;  ///< Its name, or `skin<N>`.
    ResourceId id;               ///< The skeleton resource's id.
    ContentHash hash;            ///< The skeleton's hash.
    u32 joint_count = 0;         ///< Its joints.
    Vector<u32> joint_of_slot;   ///< For `skin.joints[k]`: the skeleton's joint index (what `JOINTS_n` values map to).
    Vector<NodeJoint> node_joints;  ///< The skin's joint nodes with their joint indices, sorted by node.
    Vector<GltfMatrix> ancestors;  ///< Per joint: the product of the non-joint nodes folded into it (identity if none).
    Array<f32, 3> bounds_min = {};  ///< The box of the joints' bind positions.
    Array<f32, 3> bounds_max = {};  ///< See `bounds_min`.

    /// The joint index of a glTF node in this skeleton.
    ///
    /// - Returns: `kNoGltfIndex` when the node is no joint of the skin.
    /// - Fails: reported by the return value.
    u32 JointOfNode(u32 node) const
    {
        usize low = 0;
        usize high = node_joints.Size();
        while (low < high)
        {
            const usize mid = low + (high - low) / 2;
            if (node_joints[mid].node < node)
            {
                low = mid + 1;
            }
            else
            {
                high = mid;
            }
        }
        return low < node_joints.Size() && node_joints[low].node == node ? node_joints[low].joint : kNoGltfIndex;
    }
};

/// Imports every skin of a parsed glTF as a skeleton.
///
/// Appends one skeleton resource per skin to `result->resources` (path `<prefix>/skeletons/<name>`), their ids to
/// `result->manifest`, and the renames and mismatches to `result->report`.
///
/// - Param document: a parsed glTF.
/// - Param graph: its node graph ([[asset::ReadNodeGraph]]).
/// - Param options: limits, ids, manifest, path prefix.
/// - Param types: the registry the blobs are minted with.
/// - Param result: receives the resources, manifest entries and notes (appended to).
/// - Param skins: replaced by one entry per skin, in the glTF's order.
/// - Param error: receives the sentence naming the skin or node.
/// - Fails: `Invalid` (no joints, a joint that is not a node or is listed twice, a stored inverse bind matrix that is
///   not affine or of the wrong count, a bind transform with a shear, a skin whose joints are not one hierarchy of
///   nodes), `TooLarge` (more joints than a skeleton holds), `LimitExceeded` (memory).
/// - Thread-safety: thread-safe for separate calls with separate arguments.
/// - Ownership: borrows its arguments for the call only; `*result` and `*skins` own what they receive.
Status ImportSkins(const GltfDocument& document, const GltfNodeGraph& graph, const ImportOptions& options,
                   const TypeRegistry& types, ImportResult* result, Vector<ImportedSkin>* skins, GltfError* error);

}  // namespace vse::asset
