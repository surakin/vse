#pragma once
//! The instance hierarchy: a forest of instanced nodes (a tree with branches and leaves, a building with its parts) in
//! one packed array. [[renderer::InstanceTreeBuilder]] makes it from nodes added under their parents,
//! [[renderer::InstanceTree]] holds it, moves a node ([[renderer::InstanceTree::SetLocal]]), recomposes the world
//! transforms of what moved ([[renderer::InstanceTree::UpdateWorldTransforms]]) and re-parents a subtree
//! ([[renderer::InstanceTree::Attach]], [[renderer::InstanceTree::Detach]]).
//!
//! The nodes are in depth-first order, so the subtree of a node is the contiguous range `[index, index + subtree_size)`
//! and a parent comes before its children. A root has an `f64` world position; every other node has an `f32` offset
//! from its parent, and every world transform and bound is in the space of the node's root (the root's position is
//! the space's origin), so a tree 10,000 km from the world origin is as exact as one at the origin. World transforms
//! compose by the simplified rule of [[vse::Compose]]: scale, then rotation, then translation, with no shear.

#include "containers/shared_ptr.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "jobs/scheduler.h"
#include "math/geometry.h"
#include "math/quaternion.h"
#include "math/transform.h"
#include "math/vector.h"
#include "renderer/instance_set.h"
#include "types.h"

namespace vse::renderer
{

/// The most nodes a tree holds.
inline constexpr u32 kMaxTreeNodes = 1u << 22;
/// The most meshes a tree names.
inline constexpr u32 kMaxTreeMeshes = 256;
/// The most material slots a node may name.
inline constexpr u32 kMaxTreeMaterialSlots = 256;
/// The deepest level a node may sit at (a root is level 0).
inline constexpr u32 kMaxTreeDepth = 255;
/// The mesh index of a node that draws nothing (a group).
inline constexpr u32 kTreeNoMesh = 0xFFFFFFFFu;
/// The index [[renderer::InstanceTree::Find]] and [[renderer::InstanceTree::Parent]] return for "none".
inline constexpr u32 kTreeNone = 0xFFFFFFFFu;

/// What an edit of a tree or a builder reports.
enum class TreeStatus : u8
{
    Ok,             ///< Done.
    InvalidValue,   ///< A value is not finite, a scale is not above 0, a coordinate is beyond `kMaxInstanceCoordinate`,
                    ///< a mesh or a material slot is out of range.
    UnknownParent,  ///< The parent id was not added (yet).
    UnknownNode,    ///< The id names no node of the tree.
    Full,           ///< More than `kMaxTreeNodes` nodes.
    TooDeep,        ///< A node would sit below `kMaxTreeDepth`.
    Cycle,          ///< The new parent is the node itself or inside its subtree.
};

/// A node's stable name: ids are the order in which the builder added the nodes, and stay with the node through every
/// edit.
///
/// - Thread-safety: thread-compatible plain data.
struct TreeNodeId
{
    u32 value = kTreeNone;  ///< The id, or `kTreeNone`.
    /// Returns whether this names a node (not whether the node still exists).
    /// - Fails: never.
    bool IsValid() const { return value != kTreeNone; }
    /// Returns whether both name the same node.
    /// - Fails: never.
    bool operator==(const TreeNodeId& o) const { return value == o.value; }
};

/// The local box of one mesh a tree draws.
///
/// - Thread-safety: thread-compatible plain data.
struct TreeMeshInfo
{
    Aabb bounds;  ///< The box of the mesh in its own space; finite, not empty.
};

/// What a node is made with.
///
/// - Thread-safety: thread-compatible plain data.
struct TreeNodeDesc
{
    /// A root: its world position (`f64`). Any other node: the offset from its parent, narrowed to `f32`.
    Vec3d position;
    Quaternion rotation = Quaternion::Identity();  ///< Unit rotation, relative to the parent.
    Vec3 scale = Vec3(1.0f, 1.0f, 1.0f);           ///< Scale on each axis, above 0, relative to the parent.
    u32 mesh = kTreeNoMesh;                        ///< Index into the tree's meshes, or `kTreeNoMesh`.
    u32 material_slot = 0;                         ///< Below `kMaxTreeMaterialSlots`.
    InstanceExtra extra;                           ///< The node's tint and custom value.
    u32 flags = 0;                                 ///< Free bits for the user of the tree.
};

/// One node of the packed array.
///
/// - Thread-safety: thread-compatible plain data.
struct InstanceTreeNode
{
    Transform local;         ///< Relative to the parent; a root's position is zero (see `origin`).
    Transform world;         ///< In the root's space (a root's position is zero).
    Aabb bounds;             ///< The node's own mesh box in the root's space; empty for a group.
    Aabb subtree_bounds;     ///< The union of `bounds` over the subtree, in the root's space.
    Vec3d origin;            ///< A root's world position; zero for every other node.
    InstanceExtra extra;     ///< Tint and custom value.
    u32 id = 0;              ///< The stable id.
    u32 parent = kTreeNone;  ///< Index of the parent, or `kTreeNone` for a root.
    u32 subtree_size = 1;    ///< Nodes in the subtree, this one included.
    u32 mesh = kTreeNoMesh;  ///< Mesh index or `kTreeNoMesh`.
    u32 material_slot = 0;   ///< Material slot.
    u32 flags = 0;           ///< User bits.
    u32 depth = 0;           ///< 0 for a root.
    u8 dirty = 0;            ///< Bit 0: the local transform changed; bit 1: the world transform changed this pass.
};

class InstanceTree;

/// The shared pointer of an instance tree.
using InstanceTreePtr = SharedPtr<InstanceTree>;
/// The shared pointer of an instance tree that is only read.
using InstanceTreeConstPtr = SharedPtr<const InstanceTree>;

/// A forest of instanced nodes in depth-first order.
///
/// - Thread-safety: thread-compatible; concurrent readers are safe while nobody edits.
/// - Ownership: made by [[renderer::InstanceTreeBuilder::Finish]] and held by an `InstanceTreePtr`.
class InstanceTree
{
public:
    /// Returns the number of nodes.
    /// - Fails: never.
    u32 NodeCount() const { return static_cast<u32>(nodes_.Size()); }
    /// Returns the number of roots.
    /// - Fails: never.
    u32 RootCount() const { return static_cast<u32>(roots_.Size()); }
    /// Returns the index of root number `n` (in array order).
    /// - Aborts: `n` is not below `RootCount()`.
    u32 RootIndex(u32 n) const;
    /// Returns the node at `index`.
    /// - Aborts: `index` is not below `NodeCount()`.
    const InstanceTreeNode& Node(u32 index) const;
    /// Returns the number of meshes the tree names.
    /// - Fails: never.
    u32 MeshCount() const { return static_cast<u32>(meshes_.Size()); }
    /// Returns the box of mesh `index`.
    /// - Aborts: `index` is not below `MeshCount()`.
    const TreeMeshInfo& Mesh(u32 index) const;

    /// Returns the index of the node with this id, or `kTreeNone` when there is none.
    /// - Fails: never.
    u32 Find(TreeNodeId id) const;
    /// Returns the id of the node at `index`.
    /// - Aborts: `index` is not below `NodeCount()`.
    TreeNodeId IdAt(u32 index) const;
    /// Returns the index of the parent of the node at `index`, or `kTreeNone` for a root.
    /// - Aborts: `index` is not below `NodeCount()`.
    u32 Parent(u32 index) const;
    /// Returns the index one past the last node of the subtree of the node at `index`.
    /// - Aborts: `index` is not below `NodeCount()`.
    u32 SubtreeEnd(u32 index) const;
    /// Returns the index of the root of the node at `index`.
    /// - Aborts: `index` is not below `NodeCount()`.
    u32 RootOf(u32 index) const;
    /// Returns the world position of the node at `index` (the root's position plus the node's offset in the root's
    /// space, summed in `f64`). Valid after [[renderer::InstanceTree::UpdateWorldTransforms]].
    /// - Aborts: `index` is not below `NodeCount()`.
    Vec3d WorldPosition(u32 index) const;

    /// Changes a node's local transform and marks it, and so its subtree, for the next update. For a root,
    /// `position` is the world position.
    ///
    /// - Fails: `UnknownNode`, or `InvalidValue` for a value that is not finite, a scale not above 0 or a coordinate
    ///   beyond `kMaxInstanceCoordinate`; the tree is unchanged.
    /// - Ownership: borrows its arguments for the call.
    TreeStatus SetLocal(TreeNodeId id, const Vec3d& position, const Quaternion& rotation, const Vec3& scale);

    /// Recomposes the world transform of every marked node and of its subtree, recomputes the bounds they change
    /// (the node's own, its subtree's and its ancestors'), and clears the marks. Work is proportional to the marked
    /// subtrees, not to the tree; roots with marks are updated in parallel when a scheduler is given, and the result
    /// is the same for any number of threads.
    ///
    /// - Param scheduler: the job scheduler, or null to run on the calling thread.
    /// - Returns: the number of nodes whose world transform was recomposed.
    /// - Fails: never.
    /// - Ownership: borrows the scheduler for the call.
    u32 UpdateWorldTransforms(jobs::Scheduler* scheduler);

    /// Returns whether some node is marked for the next update.
    /// - Fails: never.
    bool NeedsUpdate() const { return !dirty_nodes_.Empty(); }

    /// Moves the subtree of `node` under `parent`, as its last child, keeping the node's local transform. Costs a
    /// pass over the array. Updates the world transforms first when something is marked.
    ///
    /// - Fails: `UnknownNode`; `Cycle` when `parent` is the node or inside its subtree; `TooDeep`.
    TreeStatus Attach(TreeNodeId node, TreeNodeId parent);

    /// Makes the node a root that stays where it is in the world (its world position, rotation and scale become its
    /// own); a root is left as it is. Costs a pass over the array. Updates the world transforms first when something
    /// is marked.
    ///
    /// - Fails: `UnknownNode`.
    TreeStatus Detach(TreeNodeId node);

    /// Checks every invariant of the array: depth-first order, parent and `subtree_size` agreeing with the depths,
    /// the id map, the root list, and the limits.
    ///
    /// - Returns: true when the tree is consistent.
    /// - Fails: never.
    bool Validate() const;

    /// Returns the bytes the tree has reserved.
    /// - Fails: never.
    usize MemoryBytes() const;

    /// Makes an empty tree; trees are made by [[renderer::InstanceTreeBuilder::Finish]].
    /// - Fails: never.
    InstanceTree() = default;

private:
    friend class InstanceTreeBuilder;

    void Relink();
    void MoveRange(u32 first, u32 count, u32 to);
    void RecomputeBoundsOf(u32 index);
    void UpdateGroup(u32 lo, u32 hi);
    static Aabb OwnBounds(const TreeMeshInfo* meshes, u32 mesh_count, const InstanceTreeNode& node);

    Vector<InstanceTreeNode> nodes_;
    Vector<u32> index_of_id_;
    Vector<u32> roots_;
    Vector<TreeMeshInfo> meshes_;
    Vector<u32> dirty_nodes_;
};

/// Makes a tree from nodes added under their parents.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns the pending nodes; [[renderer::InstanceTreeBuilder::Finish]] copies them into the tree.
class InstanceTreeBuilder
{
public:
    /// Starts a tree over these meshes.
    ///
    /// - Param meshes: the meshes the nodes may name; at most `kMaxTreeMeshes`, each box finite and not empty. A list
    ///   that does not fit makes every later `Add` return `InvalidValue`.
    /// - Ownership: copies the list.
    explicit InstanceTreeBuilder(Span<const TreeMeshInfo> meshes);

    /// Adds a root.
    ///
    /// - Param desc: the node; `position` is its world position.
    /// - Param out: receives the id; may be null.
    /// - Fails: `InvalidValue`, `Full`.
    /// - Ownership: copies `desc`; `*out` is written when given.
    TreeStatus AddRoot(const TreeNodeDesc& desc, TreeNodeId* out);

    /// Adds a child of a node added before.
    ///
    /// - Param parent: a node of this builder.
    /// - Param desc: the node; `position` is the offset from the parent.
    /// - Param out: receives the id; may be null.
    /// - Fails: `UnknownParent` (a parent not added yet is the only way to ask for a cycle, so none is possible),
    ///   `InvalidValue`, `Full`, `TooDeep`.
    /// - Ownership: copies `desc`; `*out` is written when given.
    TreeStatus AddChild(TreeNodeId parent, const TreeNodeDesc& desc, TreeNodeId* out);

    /// Returns the number of nodes added.
    /// - Fails: never.
    u32 Count() const { return static_cast<u32>(pending_.Size()); }

    /// Makes the tree: nodes in depth-first order (children in the order they were added), world transforms and bounds
    /// computed. The builder is left empty.
    ///
    /// - Param scheduler: used for the first world pass; may be null.
    /// - Returns: the tree, or an empty pointer for a builder with no node or a bad mesh list.
    /// - Fails: returns an empty pointer when the heap is exhausted.
    /// - Ownership: the tree owns its copy of the nodes; the scheduler is borrowed for the call.
    InstanceTreePtr Finish(jobs::Scheduler* scheduler);

private:
    struct Pending
    {
        TreeNodeDesc desc;
        u32 parent = kTreeNone;
        u32 depth = 0;
    };
    TreeStatus Check(const TreeNodeDesc& desc, bool root) const;

    Vector<TreeMeshInfo> meshes_;
    Vector<Pending> pending_;
    bool meshes_ok_ = true;
};

}  // namespace vse::renderer
