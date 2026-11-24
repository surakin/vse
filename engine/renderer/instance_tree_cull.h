#pragma once
//! Culling and levels of detail for an [[renderer::InstanceTree]]. [[renderer::CullInstanceTree]] walks the packed
//! array with a frustum: a subtree whose box is outside is skipped with one jump, a subtree wholly inside (with no
//! level of detail below it) is emitted without further tests, and the rest is tested node by node. Each emitted node
//! names the mesh its distance selects ([[renderer::TreeLod]]), and the result is grouped into runs of one (mesh,
//! material slot, level) by counting sorts, so a draw is one instanced draw per run, not one per node. The work is
//! proportional to the nodes the walk looks at, which are the visible ones and the boxes that bound them, never the
//! whole tree.
//!
//! The frustum and the camera are in the space whose origin is `render_origin` (a world position in `f64`), as for
//! [[renderer::CullInstances]]; a root's position is subtracted in `f64` before it is narrowed.

#include "containers/span.h"
#include "containers/vector.h"
#include "jobs/scheduler.h"
#include "math/geometry.h"
#include "renderer/instance_set.h"
#include "renderer/instance_tree.h"
#include "types.h"

namespace vse::renderer
{

/// How a tree is culled.
///
/// - Thread-safety: thread-compatible plain data.
struct TreeCullParams
{
    Frustum frustum;       ///< The view volume, in the space whose origin is `render_origin`.
    Vec3d render_origin;   ///< The world position of that space's origin.
    Vec3 camera_position;  ///< The viewer's place in that space, for the levels of detail.
};

/// The level each node had when it was last culled, kept by the caller so that [[renderer::TreeLod::hysteresis]] has
/// something to remember. One state belongs to one tree and one camera. Entries are by node id, so edits that move
/// nodes in the array keep them.
///
/// - Thread-safety: thread-compatible; [[renderer::CullInstanceTree]] writes disjoint entries from its jobs.
class TreeLodState
{
public:
    /// Forgets everything (every node starts at level 0).
    /// - Fails: never.
    void Reset() { levels_.Clear(); }

private:
    friend void CullInstanceTree(const InstanceTree&, const TreeCullParams&, TreeLodState*, jobs::Scheduler*,
                                 struct TreeCullResult*);
    Vector<u8> levels_;
};

/// One node the cull emitted.
///
/// - Thread-safety: thread-compatible plain data.
struct TreeVisibleNode
{
    u32 node = 0;           ///< Index in the tree's array.
    u32 mesh = 0;           ///< The mesh to draw (a level's mesh or the stand-in).
    u32 material_slot = 0;  ///< The node's material slot.
    u32 level = 0;          ///< 0 to `kMaxTreeLevels - 1`, or `kTreeStandinLevel`.
};

/// A run of emitted nodes that share a mesh, a material slot and a level.
///
/// - Thread-safety: thread-compatible plain data.
struct TreeDrawRun
{
    u32 mesh = 0;           ///< The mesh.
    u32 material_slot = 0;  ///< The material slot.
    u32 level = 0;          ///< The level.
    u32 first = 0;          ///< Index of the run's first node in `TreeCullResult::nodes`.
    u32 count = 0;          ///< Nodes in the run; at least 1.
};

/// What one cull did.
///
/// - Thread-safety: thread-compatible plain data.
struct TreeCullStats
{
    u32 nodes_looked_at = 0;    ///< Array entries the walk read (a skipped subtree counts its root only).
    u32 subtrees_skipped = 0;   ///< Subtrees rejected by their box, or by `cull_children_beyond`, or replaced by a
                                ///< stand-in.
    u32 subtrees_accepted = 0;  ///< Subtrees emitted whole with no per-node test.
    u32 nodes_emitted = 0;      ///< Nodes in the result.
    u32 standins = 0;           ///< Stand-ins emitted.
    u32 runs = 0;               ///< Runs in the result.
};

/// The visible nodes of a tree, grouped for drawing, and what finding them cost.
///
/// - Thread-safety: thread-compatible plain data.
struct TreeCullResult
{
    Vector<TreeVisibleNode> nodes;  ///< Grouped by (mesh, material slot, level); in node order inside a run.
    Vector<TreeDrawRun> runs;       ///< In (mesh, level, material slot) order.
    TreeCullStats stats;            ///< The counters.
};

/// Finds the visible nodes of a tree and the mesh each draws.
///
/// - Param tree: the tree; its world transforms must be current ([[renderer::InstanceTree::UpdateWorldTransforms]]);
///   not edited during the call.
/// - Param params: the view.
/// - Param state: remembers levels between calls for hysteresis, or null for the plain distance rule.
/// - Param scheduler: runs groups of roots in parallel and joins them in root order, or null for the calling thread.
///   Called from the main thread, outside any job, like `jobs::ParallelForBlocking`.
/// - Param out: receives the nodes, runs and counters; its storage is reused.
/// - Ownership: borrows everything for the call.
/// - Thread-safety: thread-compatible; the result is the same for any number of threads.
/// - Fails: never.
void CullInstanceTree(const InstanceTree& tree, const TreeCullParams& params, TreeLodState* state,
                      jobs::Scheduler* scheduler, TreeCullResult* out);

/// The level a distance selects for a node's [[renderer::TreeLod]], by the plain rule (no hysteresis).
///
/// - Param lod: the entry.
/// - Param distance: from the camera to the node.
/// - Returns: 0 to `level_count - 1`, or `level_count` when the node is beyond `cull_distance` (not drawn).
/// - Fails: never.
u32 TreeLevelForDistance(const TreeLod& lod, f32 distance);

/// Writes the instance records and the data of the emitted nodes, in order, relative to `render_origin`: each
/// position is the root's position plus the node's offset, minus `render_origin`, in `f64`, then narrowed.
///
/// - Param tree: the tree the nodes were found in.
/// - Param nodes: the nodes.
/// - Param render_origin: the world position the records are relative to.
/// - Param records: receives the records (cleared first), one per node.
/// - Param extras: receives the tint and custom value of each node (cleared first), or null.
/// - Ownership: the outputs are borrowed for the call.
/// - Fails: never; a node index outside the tree is skipped.
void CompactTreeNodes(const InstanceTree& tree, Span<const TreeVisibleNode> nodes, const Vec3d& render_origin,
                      Vector<InstanceRecord>* records, Vector<InstanceExtra>* extras);

}  // namespace vse::renderer
