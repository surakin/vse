#include "renderer/instance_tree.h"

#include "algorithm.h"
#include "jobs/parallel_for.h"
#include "lang/scalar.h"
#include "log/assert.h"

namespace vse::renderer
{

namespace
{

bool TreeFiniteVec(const Vec3& v)
{
    return IsFinite(v.x) && IsFinite(v.y) && IsFinite(v.z);
}

bool TreeValidLocal(const Vec3d& position, const Quaternion& rotation, const Vec3& scale)
{
    if (!IsFinite(position.x) || !IsFinite(position.y) || !IsFinite(position.z) ||
        Abs(position.x) > kMaxInstanceCoordinate || Abs(position.y) > kMaxInstanceCoordinate ||
        Abs(position.z) > kMaxInstanceCoordinate)
    {
        return false;
    }
    if (!IsFinite(rotation.x) || !IsFinite(rotation.y) || !IsFinite(rotation.z) || !IsFinite(rotation.w))
    {
        return false;
    }
    return TreeFiniteVec(scale) && scale.x > 0.0f && scale.y > 0.0f && scale.z > 0.0f;
}

Transform TreeLocalTransform(const Vec3d& position, const Quaternion& rotation, const Vec3& scale, bool root)
{
    Transform t;
    t.position = root ? Vec3(0.0f, 0.0f, 0.0f) : position.Narrow();
    t.rotation = rotation;
    t.scale = scale;
    return t;
}

bool LodValid(const TreeLod& lod, usize mesh_count)
{
    if (lod.level_count < 1 || lod.level_count > kMaxTreeLevels || !IsFinite(lod.hysteresis) || lod.hysteresis < 0.0f ||
        !IsFinite(lod.cull_distance) || lod.cull_distance < 0.0f || !IsFinite(lod.standin_distance) ||
        lod.standin_distance < 0.0f)
    {
        return false;
    }
    f32 previous = 0.0f;
    for (u32 k = 1; k < lod.level_count; ++k)
    {
        if (lod.mesh[k] >= mesh_count || !IsFinite(lod.switch_distance[k - 1]) ||
            !(lod.switch_distance[k - 1] > previous))
        {
            return false;
        }
        previous = lod.switch_distance[k - 1];
    }
    if (lod.cull_distance != 0.0f && !(lod.cull_distance > previous))
    {
        return false;
    }
    if (lod.standin_mesh != kTreeNoMesh && (lod.standin_mesh >= mesh_count || !(lod.standin_distance > 0.0f)))
    {
        return false;
    }
    return true;
}

}  // namespace

// ---- the builder ---------------------------------------------------------------------------------------------------

InstanceTreeBuilder::InstanceTreeBuilder(Span<const TreeMeshInfo> meshes)
{
    if (meshes.Size() > kMaxTreeMeshes)
    {
        meshes_ok_ = false;
        return;
    }
    for (usize i = 0; i < meshes.Size(); ++i)
    {
        const Aabb& b = meshes[i].bounds;
        if (!TreeFiniteVec(b.min) || !TreeFiniteVec(b.max) || b.IsEmpty())
        {
            meshes_ok_ = false;
            return;
        }
        meshes_.Push(meshes[i]);
    }
}

TreeStatus InstanceTreeBuilder::Check(const TreeNodeDesc& desc, bool root) const
{
    (void)root;
    if (!meshes_ok_ || !TreeValidLocal(desc.position, desc.rotation, desc.scale) ||
        (desc.mesh != kTreeNoMesh && desc.mesh >= meshes_.Size()) || desc.material_slot >= kMaxTreeMaterialSlots)
    {
        return TreeStatus::InvalidValue;
    }
    if (!IsFinite(desc.extra.tint.x) || !IsFinite(desc.extra.tint.y) || !IsFinite(desc.extra.tint.z) ||
        !IsFinite(desc.extra.tint.w))
    {
        return TreeStatus::InvalidValue;
    }
    if (desc.has_lod && !LodValid(desc.lod, meshes_.Size()))
    {
        return TreeStatus::InvalidValue;
    }
    return pending_.Size() >= kMaxTreeNodes ? TreeStatus::Full : TreeStatus::Ok;
}

TreeStatus InstanceTreeBuilder::AddRoot(const TreeNodeDesc& desc, TreeNodeId* out)
{
    const TreeStatus status = Check(desc, true);
    if (status != TreeStatus::Ok)
    {
        return status;
    }
    Pending p;
    p.desc = desc;
    pending_.Push(p);
    if (out != nullptr)
    {
        out->value = static_cast<u32>(pending_.Size() - 1);
    }
    return TreeStatus::Ok;
}

TreeStatus InstanceTreeBuilder::AddChild(TreeNodeId parent, const TreeNodeDesc& desc, TreeNodeId* out)
{
    if (!parent.IsValid() || parent.value >= pending_.Size())
    {
        return TreeStatus::UnknownParent;
    }
    const TreeStatus status = Check(desc, false);
    if (status != TreeStatus::Ok)
    {
        return status;
    }
    const u32 depth = pending_[parent.value].depth + 1;
    if (depth > kMaxTreeDepth)
    {
        return TreeStatus::TooDeep;
    }
    Pending p;
    p.desc = desc;
    p.parent = parent.value;
    p.depth = depth;
    pending_.Push(p);
    if (out != nullptr)
    {
        out->value = static_cast<u32>(pending_.Size() - 1);
    }
    return TreeStatus::Ok;
}

InstanceTreePtr InstanceTreeBuilder::Finish(jobs::Scheduler* scheduler)
{
    if (pending_.Empty() || !meshes_ok_)
    {
        return InstanceTreePtr();
    }
    const u32 n = static_cast<u32>(pending_.Size());
    // Children in the order they were added: a first-child and a next-sibling list, filled from the back.
    Vector<u32> first_child;
    Vector<u32> next_sibling;
    first_child.Resize(n);
    next_sibling.Resize(n);
    for (u32 i = 0; i < n; ++i)
    {
        first_child[i] = kTreeNone;
        next_sibling[i] = kTreeNone;
    }
    Vector<u32> roots;
    for (u32 i = n; i > 0; --i)
    {
        const u32 node = i - 1;
        const u32 parent = pending_[node].parent;
        if (parent == kTreeNone)
        {
            roots.Push(node);
        }
        else
        {
            next_sibling[node] = first_child[parent];
            first_child[parent] = node;
        }
    }
    InstanceTreePtr tree = MakeShared<InstanceTree>();
    if (tree.Get() == nullptr)
    {
        return tree;
    }
    tree->meshes_.Append(meshes_.Data(), meshes_.Size());
    tree->nodes_.Reserve(n);
    // Depth-first walk with an explicit stack (roots were pushed last to first, so pop from the back).
    Vector<u32> stack;
    for (usize r = roots.Size(); r > 0; --r)  // the list was filled from the back
    {
        stack.Push(roots[r - 1]);
        while (!stack.Empty())
        {
            const u32 id = stack[stack.Size() - 1];
            stack.Pop();
            const Pending& p = pending_[id];
            InstanceTreeNode node;
            node.local = TreeLocalTransform(p.desc.position, p.desc.rotation, p.desc.scale, p.parent == kTreeNone);
            node.world = node.local;
            node.origin = p.parent == kTreeNone ? p.desc.position : Vec3d(0.0, 0.0, 0.0);
            node.extra = p.desc.extra;
            node.id = id;
            node.mesh = p.desc.mesh;
            node.material_slot = p.desc.material_slot;
            node.flags = p.desc.flags;
            node.depth = p.depth;
            node.dirty = 1;
            if (p.desc.has_lod)
            {
                node.lod = static_cast<u32>(tree->lods_.Size());
                tree->lods_.Push(p.desc.lod);
            }
            tree->nodes_.Push(node);
            // Push the children so that the first added is popped first: walk the sibling list, which is in order,
            // into a small reversed run.
            const usize mark = stack.Size();
            for (u32 c = first_child[id]; c != kTreeNone; c = next_sibling[c])
            {
                stack.Push(c);
            }
            for (usize a = mark, b = stack.Size(); a + 1 < b; ++a, --b)
            {
                const u32 t = stack[a];
                stack[a] = stack[b - 1];
                stack[b - 1] = t;
            }
        }
    }
    pending_.Clear();
    meshes_.Clear();
    tree->Relink();
    for (u32 i = 0; i < tree->NodeCount(); ++i)
    {
        tree->dirty_nodes_.Push(i);
    }
    tree->UpdateWorldTransforms(scheduler);
    return tree;
}

// ---- the tree ------------------------------------------------------------------------------------------------------

u32 InstanceTree::RootIndex(u32 n) const
{
    ENGINE_ASSERT(n < roots_.Size(), "the root number is out of range");
    return roots_[n];
}

const InstanceTreeNode& InstanceTree::Node(u32 index) const
{
    ENGINE_ASSERT(index < nodes_.Size(), "the node index is out of range");
    return nodes_[index];
}

const TreeMeshInfo& InstanceTree::Mesh(u32 index) const
{
    ENGINE_ASSERT(index < meshes_.Size(), "the mesh index is out of range");
    return meshes_[index];
}

const TreeLod& InstanceTree::Lod(u32 index) const
{
    ENGINE_ASSERT(index < lods_.Size(), "the level-of-detail index is out of range");
    return lods_[index];
}

u32 InstanceTree::Find(TreeNodeId id) const
{
    return id.IsValid() && id.value < index_of_id_.Size() ? index_of_id_[id.value] : kTreeNone;
}

TreeNodeId InstanceTree::IdAt(u32 index) const
{
    ENGINE_ASSERT(index < nodes_.Size(), "the node index is out of range");
    TreeNodeId id;
    id.value = nodes_[index].id;
    return id;
}

u32 InstanceTree::Parent(u32 index) const
{
    ENGINE_ASSERT(index < nodes_.Size(), "the node index is out of range");
    return nodes_[index].parent;
}

u32 InstanceTree::SubtreeEnd(u32 index) const
{
    ENGINE_ASSERT(index < nodes_.Size(), "the node index is out of range");
    return index + nodes_[index].subtree_size;
}

u32 InstanceTree::RootOf(u32 index) const
{
    ENGINE_ASSERT(index < nodes_.Size(), "the node index is out of range");
    while (nodes_[index].parent != kTreeNone)
    {
        index = nodes_[index].parent;
    }
    return index;
}

Vec3d InstanceTree::WorldPosition(u32 index) const
{
    ENGINE_ASSERT(index < nodes_.Size(), "the node index is out of range");
    const InstanceTreeNode& root = nodes_[RootOf(index)];
    return root.origin + Vec3d(nodes_[index].world.position);
}

usize InstanceTree::MemoryBytes() const
{
    return nodes_.Capacity() * sizeof(InstanceTreeNode) +
           (index_of_id_.Capacity() + roots_.Capacity() + dirty_nodes_.Capacity()) * sizeof(u32) +
           meshes_.Capacity() * sizeof(TreeMeshInfo) + lods_.Capacity() * sizeof(TreeLod);
}

// Recomputes parent, subtree_size, the id map and the root list from the order and the depths.
void InstanceTree::Relink()
{
    const u32 n = static_cast<u32>(nodes_.Size());
    Vector<u32> stack;
    roots_.Clear();
    for (u32 i = 0; i < n; ++i)
    {
        InstanceTreeNode& node = nodes_[i];
        stack.Resize(Min(static_cast<u32>(stack.Size()), node.depth));
        node.parent = node.depth == 0 ? kTreeNone : stack[node.depth - 1];
        node.subtree_size = 1;
        node.lod_in_subtree = node.lod != kTreeNone;
        stack.Push(i);
        if (node.depth == 0)
        {
            roots_.Push(i);
        }
    }
    for (u32 i = n; i > 0; --i)
    {
        const InstanceTreeNode& node = nodes_[i - 1];
        if (node.parent != kTreeNone)
        {
            nodes_[node.parent].subtree_size += node.subtree_size;
            nodes_[node.parent].lod_in_subtree = nodes_[node.parent].lod_in_subtree || node.lod_in_subtree;
        }
    }
    index_of_id_.Resize(n);
    for (u32 i = 0; i < n; ++i)
    {
        index_of_id_[nodes_[i].id] = i;
    }
}

Aabb InstanceTree::OwnBounds(const TreeMeshInfo* meshes, u32 mesh_count, const InstanceTreeNode& node)
{
    if (node.mesh == kTreeNoMesh || node.mesh >= mesh_count)
    {
        return Aabb::Empty();
    }
    return TransformAabb(meshes[node.mesh].bounds, node.world.ToMatrix());
}

// The subtree bounds of one node from its own box and its children's.
void InstanceTree::RecomputeBoundsOf(u32 index)
{
    InstanceTreeNode& node = nodes_[index];
    Aabb box = node.bounds;
    const u32 end = index + node.subtree_size;
    for (u32 c = index + 1; c < end; c += nodes_[c].subtree_size)
    {
        box = Union(box, nodes_[c].subtree_bounds);
    }
    node.subtree_bounds = box;
}

TreeStatus InstanceTree::SetLocal(TreeNodeId id, const Vec3d& position, const Quaternion& rotation, const Vec3& scale)
{
    const u32 index = Find(id);
    if (index == kTreeNone)
    {
        return TreeStatus::UnknownNode;
    }
    if (!TreeValidLocal(position, rotation, scale))
    {
        return TreeStatus::InvalidValue;
    }
    InstanceTreeNode& node = nodes_[index];
    const bool root = node.parent == kTreeNone;
    node.local = TreeLocalTransform(position, rotation, scale, root);
    if (root)
    {
        node.origin = position;
    }
    if ((node.dirty & 1u) == 0)
    {
        node.dirty |= 1u;
        dirty_nodes_.Push(index);
    }
    return TreeStatus::Ok;
}

// Updates the world transforms and bounds of [lo, hi) (inside one root's subtree): a node is recomposed when it is
// marked or its parent was recomposed in this pass.
void InstanceTree::UpdateGroup(u32 lo, u32 hi)
{
    const TreeMeshInfo* meshes = meshes_.Data();
    const u32 mesh_count = static_cast<u32>(meshes_.Size());
    for (u32 i = lo; i < hi; ++i)
    {
        InstanceTreeNode& node = nodes_[i];
        const bool parent_changed = node.parent != kTreeNone && node.parent >= lo &&
                                    (nodes_[node.parent].dirty & 2u) != 0;
        if ((node.dirty & 1u) == 0 && !parent_changed)
        {
            continue;
        }
        node.world = node.parent == kTreeNone ? node.local : Compose(nodes_[node.parent].world, node.local);
        node.bounds = OwnBounds(meshes, mesh_count, node);
        node.dirty = static_cast<u8>(node.dirty | 2u);
    }
    for (u32 i = hi; i > lo; --i)
    {
        RecomputeBoundsOf(i - 1);
    }
    for (u32 a = nodes_[lo].parent; a != kTreeNone; a = nodes_[a].parent)
    {
        RecomputeBoundsOf(a);
    }
    for (u32 i = lo; i < hi; ++i)
    {
        nodes_[i].dirty = 0;
    }
}

u32 InstanceTree::UpdateWorldTransforms(jobs::Scheduler* scheduler)
{
    if (dirty_nodes_.Empty())
    {
        return 0;
    }
    Sort(Span<u32>(dirty_nodes_.Data(), dirty_nodes_.Size()));
    struct Group
    {
        u32 lo;
        u32 hi;
    };
    Vector<Group> groups;
    u32 root_end = 0;
    for (usize k = 0; k < dirty_nodes_.Size(); ++k)
    {
        const u32 idx = dirty_nodes_[k];
        const u32 end = idx + nodes_[idx].subtree_size;
        if (groups.Empty() || idx >= root_end)
        {
            const u32 root = RootOf(idx);
            root_end = root + nodes_[root].subtree_size;
            groups.Push(Group{idx, end});
        }
        else if (end > groups[groups.Size() - 1].hi)
        {
            groups[groups.Size() - 1].hi = end;
        }
    }
    u32 touched = 0;
    for (usize g = 0; g < groups.Size(); ++g)
    {
        touched += groups[g].hi - groups[g].lo;
    }
    if (scheduler != nullptr && groups.Size() > 1)
    {
        auto body = [this, &groups](usize begin, usize end)
        {
            for (usize g = begin; g < end; ++g)
            {
                UpdateGroup(groups[g].lo, groups[g].hi);
            }
        };
        jobs::ParallelForBlocking(*scheduler, groups.Size(), 1, body);
    }
    else
    {
        for (usize g = 0; g < groups.Size(); ++g)
        {
            UpdateGroup(groups[g].lo, groups[g].hi);
        }
    }
    dirty_nodes_.Clear();
    return touched;
}

// Moves nodes [first, first + count) so that they start at index `to` of the array that remains once they are taken
// out (to is at most the remaining size).
void InstanceTree::MoveRange(u32 first, u32 count, u32 to)
{
    Vector<InstanceTreeNode> next;
    next.Reserve(nodes_.Size());
    Vector<InstanceTreeNode> moved;
    moved.Append(nodes_.Data() + first, count);
    u32 kept = 0;
    for (u32 i = 0; i < nodes_.Size(); ++i)
    {
        if (i >= first && i < first + count)
        {
            continue;
        }
        if (kept == to)
        {
            next.Append(moved.Data(), moved.Size());
        }
        next.Push(nodes_[i]);
        ++kept;
    }
    if (kept == to)
    {
        next.Append(moved.Data(), moved.Size());
    }
    nodes_.Swap(next);
}

TreeStatus InstanceTree::Detach(TreeNodeId node)
{
    u32 index = Find(node);
    if (index == kTreeNone)
    {
        return TreeStatus::UnknownNode;
    }
    if (nodes_[index].parent == kTreeNone)
    {
        return TreeStatus::Ok;
    }
    UpdateWorldTransforms(nullptr);
    InstanceTreeNode moved = nodes_[index];
    const u32 count = moved.subtree_size;
    const Vec3d origin = nodes_[RootOf(index)].origin + Vec3d(moved.world.position);
    // The node keeps its world rotation and scale; its position becomes its root position.
    nodes_[index].local = moved.world;
    nodes_[index].local.position = Vec3(0.0f, 0.0f, 0.0f);
    nodes_[index].world = nodes_[index].local;
    nodes_[index].origin = origin;
    const u32 delta = moved.depth;
    for (u32 i = index; i < index + count; ++i)
    {
        nodes_[i].depth -= delta;
    }
    MoveRange(index, count, static_cast<u32>(nodes_.Size()) - count);
    Relink();
    // The world transforms of the moved nodes are now relative to the new root: recompose them.
    const u32 at = Find(node);
    nodes_[at].dirty |= 1u;
    dirty_nodes_.Push(at);
    UpdateWorldTransforms(nullptr);
    return TreeStatus::Ok;
}

TreeStatus InstanceTree::Attach(TreeNodeId node, TreeNodeId parent)
{
    const u32 index = Find(node);
    const u32 pindex = Find(parent);
    if (index == kTreeNone || pindex == kTreeNone)
    {
        return TreeStatus::UnknownNode;
    }
    if (pindex >= index && pindex < index + nodes_[index].subtree_size)
    {
        return TreeStatus::Cycle;
    }
    UpdateWorldTransforms(nullptr);
    u32 deepest = 0;
    for (u32 i = index; i < index + nodes_[index].subtree_size; ++i)
    {
        deepest = Max(deepest, nodes_[i].depth - nodes_[index].depth);
    }
    if (nodes_[pindex].depth + 1 + deepest > kMaxTreeDepth)
    {
        return TreeStatus::TooDeep;
    }
    const u32 count = nodes_[index].subtree_size;
    const u32 new_depth = nodes_[pindex].depth + 1;
    const u32 old_depth = nodes_[index].depth;
    if (nodes_[index].parent == kTreeNone)
    {
        nodes_[index].origin = Vec3d(0.0, 0.0, 0.0);
    }
    for (u32 i = index; i < index + count; ++i)
    {
        nodes_[i].depth = nodes_[i].depth - old_depth + new_depth;
    }
    // Where the parent's subtree ends once the moved nodes are out of the array.
    const u32 node_id = node.value;
    const u32 old_end = pindex + nodes_[pindex].subtree_size;
    const u32 end = old_end - (index < old_end ? count : 0);
    MoveRange(index, count, end);
    Relink();
    const u32 at = index_of_id_[node_id];
    nodes_[at].dirty |= 1u;
    dirty_nodes_.Push(at);
    UpdateWorldTransforms(nullptr);
    return TreeStatus::Ok;
}

bool InstanceTree::Validate() const
{
    const u32 n = static_cast<u32>(nodes_.Size());
    if (n > kMaxTreeNodes || index_of_id_.Size() != n)
    {
        return false;
    }
    Vector<u32> stack;
    u32 root_count = 0;
    for (u32 i = 0; i < n; ++i)
    {
        const InstanceTreeNode& node = nodes_[i];
        if (node.depth > kMaxTreeDepth || node.depth > stack.Size() || node.id >= n || index_of_id_[node.id] != i)
        {
            return false;
        }
        const u32 expected_parent = node.depth == 0 ? kTreeNone : stack[node.depth - 1];
        if (node.parent != expected_parent || node.subtree_size == 0 || i + node.subtree_size > n)
        {
            return false;
        }
        stack.Resize(node.depth);
        stack.Push(i);
        if (node.depth == 0)
        {
            if (root_count >= roots_.Size() || roots_[root_count] != i)
            {
                return false;
            }
            ++root_count;
        }
        if ((node.mesh != kTreeNoMesh && node.mesh >= meshes_.Size()) ||
            (node.lod != kTreeNone && node.lod >= lods_.Size()))
        {
            return false;
        }
        // The subtree is exactly the following nodes of greater depth.
        u32 end = i + 1;
        while (end < n && nodes_[end].depth > node.depth)
        {
            ++end;
        }
        if (end != i + node.subtree_size)
        {
            return false;
        }
    }
    return root_count == roots_.Size();
}

}  // namespace vse::renderer
