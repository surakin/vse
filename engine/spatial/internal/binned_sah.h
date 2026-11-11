#pragma once

// The binned surface-area-heuristic split shared by the static and the dynamic BVH builds (Phase 18 Days 239-240).
// Internal to engine/spatial: not public API (the reference covers engine/spatial/*.h only).

#include "containers/array.h"
#include "math/geometry.h"
#include "types.h"

namespace engine_placeholder::spatial::internal
{

/// The number of bins along a node's longest centroid axis.
inline constexpr u32 kSahBins = 16;

/// The deepest level a top-down build makes (the root is level 1): with a traversal stack of 64 entries (at most one
/// pending sibling per level plus the root) every tree fits. A node that would go deeper is split in its middle.
inline constexpr u32 kMaxBuildDepth = 60;

// Grows `box` to contain `other` (both non-empty, or `box` the infinite Aabb::Empty(), which min and max handle
// exactly): Union without its checks.
inline void GrowBox(Aabb& box, const Aabb& other)
{
    box.min.x = other.min.x < box.min.x ? other.min.x : box.min.x;
    box.min.y = other.min.y < box.min.y ? other.min.y : box.min.y;
    box.min.z = other.min.z < box.min.z ? other.min.z : box.min.z;
    box.max.x = other.max.x > box.max.x ? other.max.x : box.max.x;
    box.max.y = other.max.y > box.max.y ? other.max.y : box.max.y;
    box.max.z = other.max.z > box.max.z ? other.max.z : box.max.z;
}

inline void GrowBox(Aabb& box, const Vec3& p)
{
    GrowBox(box, Aabb::FromMinMax(p, p));
}

// The number of halvings a node of `count` items needs to reach leaves of at most `leaf_size`.
inline u32 SplitsNeeded(u32 count, u32 leaf_size)
{
    u32 splits = 0;
    u32 leaves = (count + leaf_size - 1) / leaf_size;
    while (leaves > 1)
    {
        leaves = (leaves + 1) / 2;
        ++splits;
    }
    return splits;
}

inline u32 SahBinOf(f32 centroid, f32 low, f32 scale)
{
    const f32 position = (centroid - low) * scale;
    if (!(position > 0.0f))
    {
        return 0;  // also a NaN
    }
    const u32 bin = static_cast<u32>(position);
    return bin < kSahBins ? bin : kSahBins - 1;
}

// Splits `order[begin, end)` (indices of items; at least 2) for a top-down build and returns where the right half
// starts (`begin < mid < end`). The split is the binned SAH plane of lowest cost on the longest axis of `centroid_box`
// (the lowest axis and plane on a tie), after a stable partition (each side keeps the order it had), so the same input
// always gives the same split. When `middle` is true, when the centroids coincide on that axis, or when no plane has
// items on both sides, the range is split in its middle and its order is kept. `box_of(i)` and `centroid_of(i)` give
// item i's box and centroid; `scratch` has room for `end` indices and `bins` for `end` bytes (both indexed by position,
// like `order`).
template <class BoxOf, class CentroidOf>
u32 SplitBinnedSah(u32* order, u32 begin, u32 end, const Aabb& centroid_box, bool middle, u32* scratch, u8* bins,
                   BoxOf&& box_of, CentroidOf&& centroid_of)
{
    const u32 count = end - begin;
    const u32 mid = begin + count / 2;
    const Vec3 extent = centroid_box.Extents();
    u32 axis = 0;
    if (extent.y > extent[axis])
    {
        axis = 1;
    }
    if (extent.z > extent[axis])
    {
        axis = 2;
    }
    if (middle || !(extent[axis] > 0.0f))
    {
        return mid;
    }

    const f32 low = centroid_box.min[axis];
    const f32 scale = static_cast<f32>(kSahBins) / extent[axis];
    Array<Aabb, kSahBins> bin_box;
    Array<u32, kSahBins> bin_count;
    for (u32 b = 0; b < kSahBins; ++b)
    {
        bin_box[b] = Aabb::Empty();
        bin_count[b] = 0;
    }
    for (u32 i = begin; i < end; ++i)
    {
        const u32 b = SahBinOf(centroid_of(order[i])[axis], low, scale);
        bins[i] = static_cast<u8>(b);
        GrowBox(bin_box[b], box_of(order[i]));
        ++bin_count[b];
    }
    // The cost of plane k (bins 0..k on the left): items times area on both sides.
    Array<f32, kSahBins> right_area;
    Array<u32, kSahBins> right_count;
    Aabb acc = Aabb::Empty();
    u32 acc_count = 0;
    for (u32 b = kSahBins - 1; b > 0; --b)
    {
        if (bin_count[b] != 0)
        {
            GrowBox(acc, bin_box[b]);
        }
        acc_count += bin_count[b];
        right_area[b] = SurfaceArea(acc);
        right_count[b] = acc_count;
    }
    u32 best_plane = kSahBins;
    f32 best_cost = 0.0f;
    acc = Aabb::Empty();
    acc_count = 0;
    for (u32 k = 0; k + 1 < kSahBins; ++k)
    {
        if (bin_count[k] != 0)
        {
            GrowBox(acc, bin_box[k]);
        }
        acc_count += bin_count[k];
        const u32 rc = right_count[k + 1];
        if (acc_count == 0 || rc == 0)
        {
            continue;
        }
        const f32 cost = static_cast<f32>(acc_count) * SurfaceArea(acc) + static_cast<f32>(rc) * right_area[k + 1];
        if (best_plane == kSahBins || cost < best_cost)
        {
            best_plane = k;
            best_cost = cost;
        }
    }
    if (best_plane == kSahBins)
    {
        return mid;
    }
    u32 left = 0;
    for (u32 i = begin; i < end; ++i)
    {
        if (bins[i] <= best_plane)
        {
            scratch[begin + left++] = order[i];
        }
    }
    u32 right = left;
    for (u32 i = begin; i < end; ++i)
    {
        if (bins[i] > best_plane)
        {
            scratch[begin + right++] = order[i];
        }
    }
    for (u32 i = begin; i < end; ++i)
    {
        order[i] = scratch[i];
    }
    return begin + left;
}

}  // namespace engine_placeholder::spatial::internal
