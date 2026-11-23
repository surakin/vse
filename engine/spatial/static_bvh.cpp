#include "spatial/static_bvh.h"

#include "lang/scalar.h"
#include "log/assert.h"
#include "spatial/internal/binned_sah.h"

namespace vse::spatial
{

namespace
{

static_assert(kStaticBvhBins == internal::kSahBins, "the static BVH's bin count is the shared split's");

}  // namespace

void StaticBvh::Clear()
{
    nodes_.Clear();
    items_.Clear();
    depth_ = 0;
}

void StaticBvh::Build(Span<const Item> items)
{
    Clear();
    const usize n = items.Size();
    if (n == 0)
    {
        return;
    }
    ENGINE_ASSERT(n < kLeafBit, "StaticBvh::Build: more than 2^31 - 1 items");
    source_ = items;
    order_.Resize(n);
    centroids_.Resize(n);
    scratch_.Resize(n);
    bins_.Resize(n);
    for (usize i = 0; i < n; ++i)
    {
        const Aabbd& box = items[i].box;
        ENGINE_ASSERT(!box.IsEmpty(), "StaticBvh::Build: an item has an empty box");
        ENGINE_ASSERT(IsFinite(box.min.x) && IsFinite(box.min.y) && IsFinite(box.min.z) && IsFinite(box.max.x) &&
                          IsFinite(box.max.y) && IsFinite(box.max.z),
                      "StaticBvh::Build: an item's box is not finite (a NaN or an infinite corner)");
        order_[i] = static_cast<u32>(i);
        centroids_[i] = box.Center();
    }
    nodes_.Reserve(2 * n / kStaticBvhLeafSize + 1);
    items_.Reserve(n);
    BuildNode(0, static_cast<u32>(n), 1);
    // The scratch is not kept: a built tree holds only its nodes and items.
    order_ = Vector<u32>();
    centroids_ = Vector<Vec3d>();
    scratch_ = Vector<u32>();
    bins_ = Vector<u8>();
    source_ = Span<const Item>();
}

u32 StaticBvh::BuildNode(u32 begin, u32 end, u32 level)
{
    const u32 index = static_cast<u32>(nodes_.Size());
    nodes_.Push(Node{});
    if (level > depth_)
    {
        depth_ = level;
    }
    Aabbd box = Aabbd::Empty();
    Aabbd centroid_box = Aabbd::Empty();
    for (u32 i = begin; i < end; ++i)
    {
        internal::GrowBox(box, source_[order_[i]].box);
        internal::GrowBox(centroid_box, centroids_[order_[i]]);
    }
    const u32 count = end - begin;
    if (count <= kStaticBvhLeafSize)
    {
        Node leaf;
        leaf.box = box;
        leaf.a = static_cast<u32>(items_.Size());
        leaf.b = count | kLeafBit;
        for (u32 i = begin; i < end; ++i)
        {
            items_.Push(source_[order_[i]]);
        }
        nodes_[index] = leaf;
        return index;
    }
    // The middle split when the tree would otherwise grow deeper than its traversal stack.
    const bool middle = level + internal::SplitsNeeded(count, kStaticBvhLeafSize) >= internal::kMaxBuildDepth;
    const u32 mid = internal::SplitBinnedSah(
        order_.Data(), begin, end, centroid_box, middle, scratch_.Data(), bins_.Data(), [this](u32 i) -> const Aabbd&
        { return source_[i].box; }, [this](u32 i) -> const Vec3d& { return centroids_[i]; });
    const u32 left_child = BuildNode(begin, mid, level + 1);
    const u32 right_child = BuildNode(mid, end, level + 1);
    ENGINE_ASSERT(left_child == index + 1, "StaticBvh: the left child must follow its parent");
    Node inner;
    inner.box = box;
    inner.a = right_child;
    inner.b = count;
    nodes_[index] = inner;
    return index;
}

u32 StaticBvh::FirstItem(u32 node) const
{
    ENGINE_ASSERT(node < nodes_.Size(), "StaticBvh::FirstItem: no such node");
    while (!nodes_[node].IsLeaf())
    {
        node = node + 1;  // the leftmost leaf holds the subtree's first item
    }
    return nodes_[node].a;
}

f64 StaticBvh::SahCost() const
{
    if (nodes_.Size() == 0)
    {
        return 0.0f;
    }
    const f64 root_area = SurfaceArea(nodes_[0].box);
    // A tree of zero-size boxes at one point has no area to weigh by: every node then counts in full.
    const f64 inverse = root_area > 0.0f ? 1.0f / root_area : 0.0f;
    f64 cost = 0.0f;
    for (usize i = 0; i < nodes_.Size(); ++i)
    {
        const Node& node = nodes_[i];
        const f64 weight = root_area > 0.0f ? SurfaceArea(node.box) * inverse : 1.0f;
        cost += node.IsLeaf() ? weight * static_cast<f64>(node.ItemCount()) : weight;
    }
    return cost;
}

}  // namespace vse::spatial
