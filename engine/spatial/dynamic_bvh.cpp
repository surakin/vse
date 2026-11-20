#include "spatial/dynamic_bvh.h"

#include "lang/scalar.h"
#include "log/assert.h"
#include "spatial/internal/binned_sah.h"

namespace vse::spatial
{

namespace
{

// The branch-and-bound search for an insertion's sibling keeps at most this many pending nodes (a depth-first stack:
// about the tree's height).
constexpr u32 kSiblingStack = 128;
constexpr u32 kSlotMask = 0x00FFFFFFu;

bool FiniteBox(const Aabb& b)
{
    return IsFinite(b.min.x) && IsFinite(b.min.y) && IsFinite(b.min.z) && IsFinite(b.max.x) && IsFinite(b.max.y) &&
           IsFinite(b.max.z);
}

f32 LargestExtent(const Aabb& b)
{
    const Vec3 e = b.Extents();
    f32 m = e.x;
    if (e.y > m)
    {
        m = e.y;
    }
    if (e.z > m)
    {
        m = e.z;
    }
    return m;
}

i32 MaxHeight(i32 a, i32 b)
{
    return a > b ? a : b;
}

}  // namespace

DynamicBvh::DynamicBvh(const DynamicBvhConfig& config) : config_(config) {}

u32 DynamicBvh::AllocateNode()
{
    u32 index;
    if (free_node_ != kNull)
    {
        index = free_node_;
        free_node_ = nodes_[index].parent;
        nodes_[index] = Node{};
    }
    else
    {
        index = static_cast<u32>(nodes_.Size());
        nodes_.Push(Node{});
    }
    nodes_[index].height = 0;
    return index;
}

void DynamicBvh::FreeNode(u32 node)
{
    nodes_[node] = Node{};
    nodes_[node].parent = free_node_;
    nodes_[node].height = -1;
    free_node_ = node;
}

i32 DynamicBvh::SlotOf(ProxyId proxy) const
{
    if (proxy == kInvalidProxy)
    {
        return -1;
    }
    const u32 slot = proxy & kSlotMask;
    const u32 generation = proxy >> 24;
    if (slot >= proxies_.Size())
    {
        return -1;
    }
    const Proxy& p = proxies_[slot];
    if (!p.Live() || p.Generation() != generation)
    {
        return -1;
    }
    return static_cast<i32>(slot);
}

Aabb DynamicBvh::FatOf(const Aabb& tight, const Vec3& displacement) const
{
    Aabb fat = Expand(tight, config_.fat_margin * LargestExtent(tight));
    if (config_.predictive > 0.0f)
    {
        for (u32 k = 0; k < 3; ++k)
        {
            const f32 reach = displacement[k] * config_.predictive;
            if (reach > 0.0f)
            {
                fat.max[k] += reach;
            }
            else
            {
                fat.min[k] += reach;
            }
        }
    }
    return fat;
}

ProxyId DynamicBvh::Insert(const Aabb& box, SpatialKey key, u32 layer)
{
    ENGINE_ASSERT(!box.IsEmpty() && FiniteBox(box), "DynamicBvh::Insert: the box is empty or not finite");
    if (proxy_count_ >= kMaxDynamicProxies)
    {
        ++counters_.refused_full;
        return kInvalidProxy;
    }
    u32 slot;
    if (free_proxy_ != kNull)
    {
        slot = free_proxy_;
        free_proxy_ = proxies_[slot].node;
    }
    else
    {
        slot = static_cast<u32>(proxies_.Size());
        proxies_.Push(Proxy{});
    }
    const u32 leaf = AllocateNode();
    nodes_[leaf].box = FatOf(box, Vec3(0, 0, 0));
    nodes_[leaf].child2 = slot;
    Proxy& p = proxies_[slot];
    p.item.box = box;
    p.item.key = key;
    p.item.layer = layer;
    p.node = leaf;
    p.state |= kLiveBit;
    InsertLeaf(leaf);
    ++proxy_count_;
    ++changes_;
    return slot | (p.Generation() << 24);
}

bool DynamicBvh::Remove(ProxyId proxy)
{
    const i32 found = SlotOf(proxy);
    if (found < 0)
    {
        ++counters_.stale_refused;
        return false;
    }
    const u32 slot = static_cast<u32>(found);
    const u32 leaf = proxies_[slot].node;
    RemoveLeaf(leaf);
    FreeNode(leaf);
    Proxy& p = proxies_[slot];
    p.state = (p.Generation() + 1) & 0xFFu;  // dead, the next generation
    p.node = free_proxy_;
    free_proxy_ = slot;
    --proxy_count_;
    ++changes_;
    return true;
}

bool DynamicBvh::Move(ProxyId proxy, const Aabb& box)
{
    const i32 found = SlotOf(proxy);
    if (found < 0)
    {
        ++counters_.stale_refused;
        return false;
    }
    ENGINE_ASSERT(!box.IsEmpty() && FiniteBox(box), "DynamicBvh::Move: the box is empty or not finite");
    ++counters_.moves;
    Proxy& p = proxies_[static_cast<u32>(found)];
    const Vec3 displacement = box.Center() - p.item.box.Center();
    p.item.box = box;
    const u32 leaf = p.node;
    const Aabb fresh = FatOf(box, displacement);
    const Aabb& stored = nodes_[leaf].box;
    if (Contains(stored, box))
    {
        // Still inside the fat box. Keep it unless it has become far larger than needed (an item that moved fast and
        // stopped, or shrank): Box2D's "huge box" rule, four margins beyond the fresh fat box.
        const Aabb huge = Expand(fresh, 4.0f * config_.fat_margin * LargestExtent(box));
        if (Contains(huge, stored))
        {
            ++counters_.moves_inside_fat;
            return false;
        }
    }
    RemoveLeaf(leaf);
    nodes_[leaf].box = fresh;
    InsertLeaf(leaf);
    ++counters_.reinserts;
    ++changes_;
    return true;
}

void DynamicBvh::InsertLeaf(u32 leaf)
{
    if (root_ == kNull)
    {
        root_ = leaf;
        nodes_[leaf].parent = kNull;
        return;
    }
    // The sibling of least total cost: the area of the new parent plus the growth of every ancestor (branch and bound:
    // a subtree is skipped when even the leaf's own area plus the growth so far cannot beat the best).
    const Aabb leaf_box = nodes_[leaf].box;
    const f32 leaf_area = SurfaceArea(leaf_box);
    u32 best = root_;
    f32 best_cost = SurfaceArea(Union(nodes_[root_].box, leaf_box));
    Array<u32, kSiblingStack> stack;
    Array<f32, kSiblingStack> inherited;
    u32 top = 0;
    stack[top] = root_;
    inherited[top++] = 0.0f;
    while (top != 0)
    {
        --top;
        const u32 n = stack[top];
        const f32 inherited_cost = inherited[top];
        const f32 direct = SurfaceArea(Union(nodes_[n].box, leaf_box));
        const f32 cost = direct + inherited_cost;
        if (cost < best_cost)
        {
            best = n;
            best_cost = cost;
        }
        if (nodes_[n].IsLeaf())
        {
            continue;
        }
        const f32 child_inherited = inherited_cost + direct - SurfaceArea(nodes_[n].box);
        if (leaf_area + child_inherited < best_cost && top + 2 <= kSiblingStack)
        {
            stack[top] = nodes_[n].child2;
            inherited[top++] = child_inherited;
            stack[top] = nodes_[n].child1;
            inherited[top++] = child_inherited;
        }
    }

    const u32 sibling = best;
    const u32 old_parent = nodes_[sibling].parent;
    const u32 new_parent = AllocateNode();
    nodes_[new_parent].parent = old_parent;
    nodes_[new_parent].box = Union(leaf_box, nodes_[sibling].box);
    nodes_[new_parent].height = nodes_[sibling].height + 1;
    nodes_[new_parent].child1 = sibling;
    nodes_[new_parent].child2 = leaf;
    if (old_parent != kNull)
    {
        if (nodes_[old_parent].child1 == sibling)
        {
            nodes_[old_parent].child1 = new_parent;
        }
        else
        {
            nodes_[old_parent].child2 = new_parent;
        }
    }
    else
    {
        root_ = new_parent;
    }
    nodes_[sibling].parent = new_parent;
    nodes_[leaf].parent = new_parent;
    Refit(new_parent);  // from the new parent: it is the first node whose balance may have changed
}

void DynamicBvh::RemoveLeaf(u32 leaf)
{
    if (leaf == root_)
    {
        root_ = kNull;
        return;
    }
    const u32 parent = nodes_[leaf].parent;
    const u32 grand = nodes_[parent].parent;
    const u32 sibling = nodes_[parent].child1 == leaf ? nodes_[parent].child2 : nodes_[parent].child1;
    if (grand != kNull)
    {
        if (nodes_[grand].child1 == parent)
        {
            nodes_[grand].child1 = sibling;
        }
        else
        {
            nodes_[grand].child2 = sibling;
        }
        nodes_[sibling].parent = grand;
        FreeNode(parent);
        Refit(grand);
    }
    else
    {
        root_ = sibling;
        nodes_[sibling].parent = kNull;
        FreeNode(parent);
    }
    nodes_[leaf].parent = kNull;
}

void DynamicBvh::Refit(u32 index)
{
    while (index != kNull)
    {
        index = Balance(index);
        Node& n = nodes_[index];
        n.height = 1 + MaxHeight(nodes_[n.child1].height, nodes_[n.child2].height);
        n.box = Union(nodes_[n.child1].box, nodes_[n.child2].box);
        index = n.parent;
    }
}

// An AVL rotation at node A when one child is more than one level taller than the other (Box2D's
// b2DynamicTree::Balance). Returns the node now at A's place.
u32 DynamicBvh::Balance(u32 a)
{
    if (nodes_[a].IsLeaf() || nodes_[a].height < 2)
    {
        return a;
    }
    const u32 b = nodes_[a].child1;
    const u32 c = nodes_[a].child2;
    const i32 balance = nodes_[c].height - nodes_[b].height;
    if (balance > 1)
    {  // C rises
        const u32 f = nodes_[c].child1;
        const u32 g = nodes_[c].child2;
        nodes_[c].child1 = a;
        nodes_[c].parent = nodes_[a].parent;
        nodes_[a].parent = c;
        if (nodes_[c].parent != kNull)
        {
            Node& up = nodes_[nodes_[c].parent];
            if (up.child1 == a)
            {
                up.child1 = c;
            }
            else
            {
                up.child2 = c;
            }
        }
        else
        {
            root_ = c;
        }
        if (nodes_[f].height > nodes_[g].height)
        {
            nodes_[c].child2 = f;
            nodes_[a].child2 = g;
            nodes_[g].parent = a;
            nodes_[a].box = Union(nodes_[b].box, nodes_[g].box);
            nodes_[c].box = Union(nodes_[a].box, nodes_[f].box);
            nodes_[a].height = 1 + MaxHeight(nodes_[b].height, nodes_[g].height);
            nodes_[c].height = 1 + MaxHeight(nodes_[a].height, nodes_[f].height);
        }
        else
        {
            nodes_[c].child2 = g;
            nodes_[a].child2 = f;
            nodes_[f].parent = a;
            nodes_[a].box = Union(nodes_[b].box, nodes_[f].box);
            nodes_[c].box = Union(nodes_[a].box, nodes_[g].box);
            nodes_[a].height = 1 + MaxHeight(nodes_[b].height, nodes_[f].height);
            nodes_[c].height = 1 + MaxHeight(nodes_[a].height, nodes_[g].height);
        }
        ++counters_.rotations;
        return c;
    }
    if (balance < -1)
    {  // B rises
        const u32 d = nodes_[b].child1;
        const u32 e = nodes_[b].child2;
        nodes_[b].child1 = a;
        nodes_[b].parent = nodes_[a].parent;
        nodes_[a].parent = b;
        if (nodes_[b].parent != kNull)
        {
            Node& up = nodes_[nodes_[b].parent];
            if (up.child1 == a)
            {
                up.child1 = b;
            }
            else
            {
                up.child2 = b;
            }
        }
        else
        {
            root_ = b;
        }
        if (nodes_[d].height > nodes_[e].height)
        {
            nodes_[b].child2 = d;
            nodes_[a].child1 = e;
            nodes_[e].parent = a;
            nodes_[a].box = Union(nodes_[c].box, nodes_[e].box);
            nodes_[b].box = Union(nodes_[a].box, nodes_[d].box);
            nodes_[a].height = 1 + MaxHeight(nodes_[c].height, nodes_[e].height);
            nodes_[b].height = 1 + MaxHeight(nodes_[a].height, nodes_[d].height);
        }
        else
        {
            nodes_[b].child2 = e;
            nodes_[a].child1 = d;
            nodes_[d].parent = a;
            nodes_[a].box = Union(nodes_[c].box, nodes_[d].box);
            nodes_[b].box = Union(nodes_[a].box, nodes_[e].box);
            nodes_[a].height = 1 + MaxHeight(nodes_[c].height, nodes_[d].height);
            nodes_[b].height = 1 + MaxHeight(nodes_[a].height, nodes_[e].height);
        }
        ++counters_.rotations;
        return b;
    }
    return a;
}

void DynamicBvh::Clear()
{
    nodes_.Clear();
    free_node_ = kNull;
    root_ = kNull;
    // Every handle becomes stale: each live slot's generation moves on, and the free list is rebuilt so slot 0 is
    // reused first.
    free_proxy_ = kNull;
    for (usize i = proxies_.Size(); i > 0; --i)
    {
        Proxy& p = proxies_[i - 1];
        if (p.Live())
        {
            p.state = (p.Generation() + 1) & 0xFFu;
        }
        p.node = free_proxy_;
        free_proxy_ = static_cast<u32>(i - 1);
    }
    proxy_count_ = 0;
    built_cost_ = 0.0f;
}

void DynamicBvh::Build(Span<const Item> items, Span<ProxyId> proxies)
{
    ENGINE_ASSERT(proxies.Size() >= items.Size(), "DynamicBvh::Build: the handle span is shorter than the items");
    ENGINE_ASSERT(items.Size() <= kMaxDynamicProxies, "DynamicBvh::Build: more items than a dynamic tree holds");
    Clear();
    for (usize i = 0; i < items.Size(); ++i)
    {
        const Aabb& box = items[i].box;
        ENGINE_ASSERT(!box.IsEmpty() && FiniteBox(box), "DynamicBvh::Build: an item's box is empty or not finite");
        u32 slot;
        if (free_proxy_ != kNull)
        {
            slot = free_proxy_;
            free_proxy_ = proxies_[slot].node;
        }
        else
        {
            slot = static_cast<u32>(proxies_.Size());
            proxies_.Push(Proxy{});
        }
        Proxy& p = proxies_[slot];
        p.item = items[i];
        p.state |= kLiveBit;
        p.node = kNull;
        ++proxy_count_;
        proxies[i] = slot | (p.Generation() << 24);
    }
    // Fresh fat boxes, then the top-down build over them (Rebuild reads each leaf's fat box, so give each item a leaf
    // first).
    if (fat_.Size() < proxies_.Size())
    {
        fat_.Resize(proxies_.Size());
    }
    for (usize s = 0; s < proxies_.Size(); ++s)
    {
        if (proxies_[s].Live())
        {
            fat_[s] = FatOf(proxies_[s].item.box, Vec3(0, 0, 0));
        }
    }
    RebuildFrom(true);
}

void DynamicBvh::Rebuild()
{
    RebuildFrom(false);
}

void DynamicBvh::RebuildFrom(bool fat_ready)
{
    ++counters_.full_rebuilds;
    changes_ = 0;
    if (proxy_count_ == 0)
    {
        nodes_.Clear();
        free_node_ = kNull;
        root_ = kNull;
        built_cost_ = 0.0f;
        return;
    }
    const usize slots = proxies_.Size();
    if (fat_.Size() < slots)
    {
        fat_.Resize(slots);
    }
    if (centroids_.Size() < slots)
    {
        centroids_.Resize(slots);
    }
    order_.Clear();
    for (usize s = 0; s < slots; ++s)
    {
        const Proxy& p = proxies_[s];
        if (!p.Live())
        {
            continue;
        }
        if (!fat_ready)
        {
            fat_[s] = nodes_[p.node].box;  // the fat boxes stay what they were
        }
        centroids_[s] = fat_[s].Center();
        order_.Push(static_cast<u32>(s));
    }
    if (scratch_.Size() < order_.Size())
    {
        scratch_.Resize(order_.Size());
    }
    if (bins_.Size() < order_.Size())
    {
        bins_.Resize(order_.Size());
    }
    // A fresh, compact pool: nodes come out in build order.
    nodes_.Clear();
    free_node_ = kNull;
    root_ = BuildRange(0, static_cast<u32>(order_.Size()), 1);
    nodes_[root_].parent = kNull;
    built_cost_ = SahCost();
}

u32 DynamicBvh::BuildRange(u32 begin, u32 end, u32 level)
{
    const u32 count = end - begin;
    if (count == 1)
    {
        const u32 slot = order_[begin];
        const u32 leaf = AllocateNode();
        nodes_[leaf].box = fat_[slot];
        nodes_[leaf].child2 = slot;
        proxies_[slot].node = leaf;
        return leaf;
    }
    Aabb centroid_box = Aabb::Empty();
    for (u32 i = begin; i < end; ++i)
    {
        internal::GrowBox(centroid_box, centroids_[order_[i]]);
    }
    const bool middle = level + internal::SplitsNeeded(count, 1) >= internal::kMaxBuildDepth;
    const u32 mid = internal::SplitBinnedSah(
        order_.Data(), begin, end, centroid_box, middle, scratch_.Data(), bins_.Data(),
        [this](u32 s) -> const Aabb& { return fat_[s]; }, [this](u32 s) -> const Vec3& { return centroids_[s]; });
    const u32 node = AllocateNode();
    const u32 left = BuildRange(begin, mid, level + 1);
    const u32 right = BuildRange(mid, end, level + 1);
    nodes_[node].child1 = left;
    nodes_[node].child2 = right;
    nodes_[left].parent = node;
    nodes_[right].parent = node;
    nodes_[node].box = Union(nodes_[left].box, nodes_[right].box);
    nodes_[node].height = 1 + MaxHeight(nodes_[left].height, nodes_[right].height);
    return node;
}

void DynamicBvh::RebuildSubtree(u32 subtree)
{
    if (nodes_[subtree].IsLeaf())
    {
        return;
    }
    const u32 parent = nodes_[subtree].parent;
    u32 level = 1;
    for (u32 up = parent; up != kNull; up = nodes_[up].parent)
    {
        ++level;
    }
    const usize slots = proxies_.Size();
    if (fat_.Size() < slots)
    {
        fat_.Resize(slots);
    }
    if (centroids_.Size() < slots)
    {
        centroids_.Resize(slots);
    }
    // Collect the subtree's leaves (their fat boxes stay), freeing every node of it; `walk_` is the explicit stack.
    order_.Clear();
    walk_.Clear();
    walk_.Push(subtree);
    while (walk_.Size() != 0)
    {
        const u32 n = walk_[walk_.Size() - 1];
        walk_.Pop();
        if (nodes_[n].IsLeaf())
        {
            const u32 slot = nodes_[n].child2;
            fat_[slot] = nodes_[n].box;
            centroids_[slot] = fat_[slot].Center();
            order_.Push(slot);
        }
        else
        {
            walk_.Push(nodes_[n].child2);
            walk_.Push(nodes_[n].child1);
        }
        FreeNode(n);
    }
    if (scratch_.Size() < order_.Size())
    {
        scratch_.Resize(order_.Size());
    }
    if (bins_.Size() < order_.Size())
    {
        bins_.Resize(order_.Size());
    }
    const u32 rebuilt = BuildRange(0, static_cast<u32>(order_.Size()), level);
    nodes_[rebuilt].parent = parent;
    if (parent == kNull)
    {
        root_ = rebuilt;
    }
    else
    {
        // The parent's child that was `subtree` (freed above, so compare by "is not the other child").
        Node& p = nodes_[parent];
        if (p.child1 == subtree)
        {
            p.child1 = rebuilt;
        }
        else
        {
            p.child2 = rebuilt;
        }
        Refit(parent);
    }
}

void DynamicBvh::Optimize(u32 budget)
{
    // A quiet scene (no insert, remove or re-insert since the last call) has nothing to improve: return before the pass
    // over the nodes.
    if (root_ == kNull || changes_ == 0)
    {
        return;
    }
    changes_ = 0;
    const f32 cost = SahCost();
    if (built_cost_ > 0.0f && cost > built_cost_ * config_.rebuild_threshold)
    {
        Rebuild();
        return;
    }
    if (budget < 2)
    {
        return;
    }
    // A subtree of height h has at most 2^h leaves: descend to the first node at or below the height that fits the
    // budget, choosing each turn by the next bit of a counter (its low bit alternates each call, so successive calls
    // spread over the whole tree).
    i32 fit = 0;
    while ((2u << fit) <= budget && fit < 30)
    {
        ++fit;
    }
    u32 bits = optimize_cursor_++;
    u32 n = root_;
    while (!nodes_[n].IsLeaf() && nodes_[n].height > fit)
    {
        n = (bits & 1u) != 0 ? nodes_[n].child2 : nodes_[n].child1;
        bits >>= 1;
    }
    if (nodes_[n].IsLeaf())
    {
        return;
    }
    RebuildSubtree(n);
    ++counters_.partial_rebuilds;
}

bool DynamicBvh::IsLive(ProxyId proxy) const
{
    return SlotOf(proxy) >= 0;
}

const Item& DynamicBvh::ItemOf(ProxyId proxy) const
{
    const i32 slot = SlotOf(proxy);
    ENGINE_ASSERT(slot >= 0, "DynamicBvh::ItemOf: the handle is not live");
    return proxies_[static_cast<u32>(slot)].item;
}

const Aabb& DynamicBvh::FatBox(ProxyId proxy) const
{
    const i32 slot = SlotOf(proxy);
    ENGINE_ASSERT(slot >= 0, "DynamicBvh::FatBox: the handle is not live");
    return nodes_[proxies_[static_cast<u32>(slot)].node].box;
}

u32 DynamicBvh::Height() const
{
    return root_ == kNull ? 0u : static_cast<u32>(nodes_[root_].height);
}

Aabb DynamicBvh::Bounds() const
{
    return root_ == kNull ? Aabb::Empty() : nodes_[root_].box;
}

f32 DynamicBvh::SahCost() const
{
    if (root_ == kNull)
    {
        return 0.0f;
    }
    const f32 root_area = SurfaceArea(nodes_[root_].box);
    const f32 inverse = root_area > 0.0f ? 1.0f / root_area : 0.0f;
    f32 cost = 0.0f;
    for (usize i = 0; i < nodes_.Size(); ++i)
    {
        if (nodes_[i].height < 0)
        {
            continue;  // free
        }
        cost += root_area > 0.0f ? SurfaceArea(nodes_[i].box) * inverse : 1.0f;
    }
    return cost;
}

DynamicBvhStats DynamicBvh::Stats() const
{
    DynamicBvhStats s = counters_;
    s.proxies = proxy_count_;
    s.node_capacity = static_cast<u32>(nodes_.Size());
    s.memory_bytes = static_cast<u64>(nodes_.Size()) * sizeof(Node) + static_cast<u64>(proxies_.Size()) * sizeof(Proxy);
    u32 used = 0;
    for (usize i = 0; i < nodes_.Size(); ++i)
    {
        used += nodes_[i].height >= 0 ? 1u : 0u;
    }
    s.nodes = used;
    s.height = Height();
    s.sah_cost = SahCost();
    s.built_sah_cost = built_cost_;
    return s;
}

bool DynamicBvh::Validate() const
{
    const usize pool = nodes_.Size();
    Vector<u8> seen;
    seen.Resize(pool);
    for (usize i = 0; i < pool; ++i)
    {
        seen[i] = 0;
    }
    u32 reached = 0;
    u32 leaves = 0;
    if (root_ != kNull)
    {
        if (root_ >= pool || nodes_[root_].parent != kNull)
        {
            return false;
        }
        Vector<u32> walk;
        walk.Push(root_);
        while (walk.Size() != 0)
        {
            const u32 n = walk[walk.Size() - 1];
            walk.Pop();
            if (n >= pool || seen[n] != 0)
            {
                return false;  // out of range, or reached twice (a cycle or a shared child)
            }
            seen[n] = 1;
            ++reached;
            const Node& node = nodes_[n];
            if (node.height < 0)
            {
                return false;
            }
            if (node.IsLeaf())
            {
                if (node.height != 0 || node.child2 >= proxies_.Size())
                {
                    return false;
                }
                const Proxy& p = proxies_[node.child2];
                if (!p.Live() || p.node != n || !Contains(node.box, p.item.box))
                {
                    return false;
                }
                ++leaves;
                continue;
            }
            if (node.child2 >= pool || node.child1 >= pool)
            {
                return false;
            }
            const Node& c1 = nodes_[node.child1];
            const Node& c2 = nodes_[node.child2];
            if (c1.parent != n || c2.parent != n)
            {
                return false;
            }
            if (!Contains(node.box, c1.box) || !Contains(node.box, c2.box))
            {
                return false;
            }
            if (node.height != 1 + MaxHeight(c1.height, c2.height))
            {
                return false;
            }
            walk.Push(node.child1);
            walk.Push(node.child2);
        }
    }
    if (leaves != proxy_count_)
    {
        return false;
    }
    u32 live = 0;
    for (usize s = 0; s < proxies_.Size(); ++s)
    {
        live += proxies_[s].Live() ? 1u : 0u;
    }
    if (live != proxy_count_)
    {
        return false;
    }
    // The free list holds exactly the nodes not in the tree.
    u32 free_count = 0;
    for (u32 f = free_node_; f != kNull; f = nodes_[f].parent)
    {
        if (f >= pool || seen[f] != 0 || nodes_[f].height != -1 || free_count > pool)
        {
            return false;
        }
        seen[f] = 2;
        ++free_count;
    }
    if (reached + free_count != pool)
    {
        return false;
    }
    // And the free proxy slots are exactly the dead ones.
    u32 free_slots = 0;
    for (u32 s = free_proxy_; s != kNull; s = proxies_[s].node)
    {
        if (s >= proxies_.Size() || proxies_[s].Live() || free_slots > proxies_.Size())
        {
            return false;
        }
        ++free_slots;
    }
    return free_slots + live == proxies_.Size();
}

}  // namespace vse::spatial
