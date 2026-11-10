#include "spatial/static_bvh.h"

#include "containers/array.h"
#include "lang/scalar.h"
#include "log/assert.h"

namespace engine_placeholder::spatial {

namespace {

// The deepest level the build makes (the root is level 1); with the traversal stack of 64 entries (at most one pending sibling per level plus the root)
// every tree fits.
constexpr u32 kMaxBuildDepth = 60;

// The number of further splits a node of `count` items needs to reach leaves when it is halved each time.
u32 SplitsNeeded(u32 count) {
    u32 splits = 0;
    u32 leaves = (count + kStaticBvhLeafSize - 1) / kStaticBvhLeafSize;
    while (leaves > 1) {
        leaves = (leaves + 1) / 2;
        ++splits;
    }
    return splits;
}

// Grows `box` to contain `other` (both non-empty, or `box` the infinite Aabb::Empty(), which min and max handle exactly): Union without its checks.
void Grow(Aabb& box, const Aabb& other) {
    box.min.x = other.min.x < box.min.x ? other.min.x : box.min.x;
    box.min.y = other.min.y < box.min.y ? other.min.y : box.min.y;
    box.min.z = other.min.z < box.min.z ? other.min.z : box.min.z;
    box.max.x = other.max.x > box.max.x ? other.max.x : box.max.x;
    box.max.y = other.max.y > box.max.y ? other.max.y : box.max.y;
    box.max.z = other.max.z > box.max.z ? other.max.z : box.max.z;
}

void Grow(Aabb& box, const Vec3& p) { Grow(box, Aabb::FromMinMax(p, p)); }

u32 BinOf(f32 centroid, f32 low, f32 scale) {
    const f32 position = (centroid - low) * scale;
    if (!(position > 0.0f)) return 0; // also a NaN
    const u32 bin = static_cast<u32>(position);
    return bin < kStaticBvhBins ? bin : kStaticBvhBins - 1;
}

} // namespace

void StaticBvh::Clear() {
    nodes_.Clear();
    items_.Clear();
    depth_ = 0;
}

void StaticBvh::Build(Span<const Item> items) {
    Clear();
    const usize n = items.Size();
    if (n == 0) return;
    ENGINE_ASSERT(n < kLeafBit, "StaticBvh::Build: more than 2^31 - 1 items");
    source_ = items;
    order_.Resize(n);
    centroids_.Resize(n);
    scratch_.Resize(n);
    bins_.Resize(n);
    for (usize i = 0; i < n; ++i) {
        const Aabb& box = items[i].box;
        ENGINE_ASSERT(!box.IsEmpty(), "StaticBvh::Build: an item has an empty box");
        ENGINE_ASSERT(IsFinite(box.min.x) && IsFinite(box.min.y) && IsFinite(box.min.z) && IsFinite(box.max.x) && IsFinite(box.max.y) && IsFinite(box.max.z),
                      "StaticBvh::Build: an item's box is not finite (a NaN or an infinite corner)");
        order_[i] = static_cast<u32>(i);
        centroids_[i] = items[i].box.Center();
    }
    nodes_.Reserve(2 * n / kStaticBvhLeafSize + 1);
    items_.Reserve(n);
    BuildNode(0, static_cast<u32>(n), 1);
    // The scratch is not kept: a built tree holds only its nodes and items.
    order_ = Vector<u32>();
    centroids_ = Vector<Vec3>();
    scratch_ = Vector<u32>();
    bins_ = Vector<u8>();
    source_ = Span<const Item>();
}

u32 StaticBvh::BuildNode(u32 begin, u32 end, u32 level) {
    const u32 index = static_cast<u32>(nodes_.Size());
    nodes_.Push(Node{});
    if (level > depth_) depth_ = level;
    Aabb box = Aabb::Empty();
    Aabb centroid_box = Aabb::Empty();
    for (u32 i = begin; i < end; ++i) {
        Grow(box, source_[order_[i]].box);
        Grow(centroid_box, centroids_[order_[i]]);
    }
    const u32 count = end - begin;
    if (count <= kStaticBvhLeafSize) {
        Node leaf;
        leaf.box = box;
        leaf.a = static_cast<u32>(items_.Size());
        leaf.b = count | kLeafBit;
        for (u32 i = begin; i < end; ++i) items_.Push(source_[order_[i]]);
        nodes_[index] = leaf;
        return index;
    }

    // The longest centroid axis (the lowest axis on a tie).
    const Vec3 extent = centroid_box.Extents();
    u32 axis = 0;
    if (extent.y > extent[axis]) axis = 1;
    if (extent.z > extent[axis]) axis = 2;

    u32 mid = begin + count / 2; // the middle split: for coincident centroids, and to keep the depth bounded
    if (extent[axis] > 0.0f && level + SplitsNeeded(count) < kMaxBuildDepth) {
        const f32 low = centroid_box.min[axis];
        const f32 scale = static_cast<f32>(kStaticBvhBins) / extent[axis];
        Array<Aabb, kStaticBvhBins> bin_box;
        Array<u32, kStaticBvhBins> bin_count;
        for (u32 b = 0; b < kStaticBvhBins; ++b) {
            bin_box[b] = Aabb::Empty();
            bin_count[b] = 0;
        }
        for (u32 i = begin; i < end; ++i) {
            const u32 b = BinOf(centroids_[order_[i]][axis], low, scale);
            bins_[i] = static_cast<u8>(b); // kept for the partition below
            Grow(bin_box[b], source_[order_[i]].box);
            ++bin_count[b];
        }
        // The cost of each plane k (bins 0..k on the left): items times area on both sides; the lowest cost wins, the lowest plane on a tie.
        Array<f32, kStaticBvhBins> right_area;
        Array<u32, kStaticBvhBins> right_count;
        Aabb acc = Aabb::Empty();
        u32 acc_count = 0;
        for (u32 b = kStaticBvhBins - 1; b > 0; --b) {
            if (bin_count[b] != 0) Grow(acc, bin_box[b]);
            acc_count += bin_count[b];
            right_area[b] = SurfaceArea(acc);
            right_count[b] = acc_count;
        }
        u32 best_plane = kStaticBvhBins;
        f32 best_cost = 0.0f;
        acc = Aabb::Empty();
        acc_count = 0;
        for (u32 k = 0; k + 1 < kStaticBvhBins; ++k) {
            if (bin_count[k] != 0) Grow(acc, bin_box[k]);
            acc_count += bin_count[k];
            const u32 rc = right_count[k + 1];
            if (acc_count == 0 || rc == 0) continue;
            const f32 cost = static_cast<f32>(acc_count) * SurfaceArea(acc) + static_cast<f32>(rc) * right_area[k + 1];
            if (best_plane == kStaticBvhBins || cost < best_cost) {
                best_plane = k;
                best_cost = cost;
            }
        }
        if (best_plane != kStaticBvhBins) {
            // A stable partition: the left items keep their order, then the right ones keep theirs.
            u32 left = 0;
            for (u32 i = begin; i < end; ++i) {
                if (bins_[i] <= best_plane) scratch_[begin + left++] = order_[i];
            }
            u32 right = left;
            for (u32 i = begin; i < end; ++i) {
                if (bins_[i] > best_plane) scratch_[begin + right++] = order_[i];
            }
            for (u32 i = begin; i < end; ++i) order_[i] = scratch_[i];
            mid = begin + left;
        }
    }

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

u32 StaticBvh::FirstItem(u32 node) const {
    ENGINE_ASSERT(node < nodes_.Size(), "StaticBvh::FirstItem: no such node");
    while (!nodes_[node].IsLeaf()) node = node + 1; // the leftmost leaf holds the subtree's first item
    return nodes_[node].a;
}

f32 StaticBvh::SahCost() const {
    if (nodes_.Size() == 0) return 0.0f;
    const f32 root_area = SurfaceArea(nodes_[0].box);
    // A tree of zero-size boxes at one point has no area to weigh by: every node then counts in full.
    const f32 inverse = root_area > 0.0f ? 1.0f / root_area : 0.0f;
    f32 cost = 0.0f;
    for (usize i = 0; i < nodes_.Size(); ++i) {
        const Node& node = nodes_[i];
        const f32 weight = root_area > 0.0f ? SurfaceArea(node.box) * inverse : 1.0f;
        cost += node.IsLeaf() ? weight * static_cast<f32>(node.ItemCount()) : weight;
    }
    return cost;
}

} // namespace engine_placeholder::spatial
