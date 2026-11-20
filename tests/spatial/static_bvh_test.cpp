// The static BVH (Phase 18 Day 239): every item reachable once, the root encloses all, box queries and closest-hit rays
// equal a brute-force scan on 1,000 random scenes of six kinds, degenerate inputs build within the traversal stack, the
// build is deterministic, and the tree's SAH cost is close to the best of five builds of the same items in shuffled
// orders. The 100k build time and the tree quality are printed (not gated in a debug build).
#include <catch2/catch_test_macros.hpp>

#include "algorithm.h"
#include "lang/limits.h"
#include "log/assert.h"
#include "containers/vector.h"
#include "cstr/mem_ops.h"
#include "os/os.h"
#include "spatial/static_bvh.h"
#include "spatial_test_rng.h"
#include "spatial_test_util.h"

using namespace vse;
using namespace vse::spatial;
using spatial_test::Rng;
using spatial_test::SortKeys;

namespace
{

u32 g_bvh_asserts = 0;
AssertAction CountBvhAssert(const AssertInfo&)
{
    ++g_bvh_asserts;
    return AssertAction::Continue;
}

Item MakeItem(const Vec3& c, const Vec3& h, SpatialKey key)
{
    Item item;
    item.box = Aabb::FromMinMax(c - h, c + h);
    item.key = key;
    item.layer = static_cast<u32>(key % 3);
    return item;
}

// Scene kinds: 0 random boxes, 1 clusters, 2 many identical boxes, 3 a long line, 4 a single item, 5 none.
void MakeScene(Rng& rng, u32 kind, Vector<Item>* out)
{
    out->Clear();
    const u32 n = 1 + rng.Below(300);
    switch (kind)
    {
    case 0:
        for (u32 i = 0; i < n; ++i)
        {
            out->Push(MakeItem(rng.Point(-50, 50), Vec3(rng.Float(0, 4), rng.Float(0, 4), rng.Float(0, 4)), i));
        }
        break;
    case 1:
    {
        const u32 clusters = 1 + rng.Below(5);
        Vector<Vec3> centers;
        for (u32 c = 0; c < clusters; ++c)
        {
            centers.Push(rng.Point(-50, 50));
        }
        for (u32 i = 0; i < n; ++i)
        {
            out->Push(MakeItem(centers[rng.Below(clusters)] + rng.Point(-3, 3),
                               Vec3(rng.Float(0, 1), rng.Float(0, 1), rng.Float(0, 1)), i));
        }
        break;
    }
    case 2:
    {
        const Vec3 c = rng.Point(-10, 10);
        for (u32 i = 0; i < n; ++i)
        {
            out->Push(MakeItem(c, Vec3(1, 1, 1), i));
        }
        break;
    }
    case 3:
        for (u32 i = 0; i < n; ++i)
        {
            out->Push(MakeItem(Vec3(static_cast<f32>(i) * 2.0f - 300.0f, 0, 0), Vec3(0.5f, 0.5f, 0.5f), i));
        }
        break;
    case 4: out->Push(MakeItem(rng.Point(-5, 5), Vec3(1, 2, 3), 0)); break;
    default: break;
    }
}

bool SameKeys(Vector<SpatialKey>& a, Vector<SpatialKey>& b)
{
    SortKeys(&a);
    SortKeys(&b);
    if (a.Size() != b.Size())
    {
        return false;
    }
    for (usize i = 0; i < a.Size(); ++i)
    {
        if (a[i] != b[i])
        {
            return false;
        }
    }
    return true;
}

void QueryBox(const StaticBvh& bvh, const Aabb& query, Vector<SpatialKey>* out)
{
    out->Clear();
    const TraversalStats stats = bvh.Traverse(
        [&](const Aabb& node) { return Overlaps(node, query) ? NodeVisit::Descend : NodeVisit::Reject; },
        [&](const Item& item)
        {
            if (Overlaps(item.box, query))
            {
                out->Push(item.key);
            }
            return true;
        });
    CHECK(stats.refused == 0);
}

void BruteBox(Span<const Item> items, const Aabb& query, Vector<SpatialKey>* out)
{
    out->Clear();
    for (usize i = 0; i < items.Size(); ++i)
    {
        if (Overlaps(items[i].box, query))
        {
            out->Push(items[i].key);
        }
    }
}

// The closest hit: its parameter, or -1 for none.
f32 RayBvh(const StaticBvh& bvh, const Ray& ray, f32 max_t, SpatialKey* key)
{
    f32 best = -1.0f;
    const TraversalStats stats = bvh.TraverseRay(ray, max_t,
                                                 [&](const Item& item, f32& limit)
                                                 {
                                                     f32 t = 0.0f;
                                                     if (Intersect(ray, item.box, limit, &t) &&
                                                         (best < 0.0f || t < best))
                                                     {
                                                         best = t;
                                                         *key = item.key;
                                                         limit = t;
                                                     }
                                                     return true;
                                                 });
    CHECK(stats.refused == 0);
    return best;
}

f32 RayBrute(Span<const Item> items, const Ray& ray, f32 max_t)
{
    f32 best = -1.0f;
    for (usize i = 0; i < items.Size(); ++i)
    {
        f32 t = 0.0f;
        if (Intersect(ray, items[i].box, max_t, &t) && (best < 0.0f || t < best))
        {
            best = t;
        }
    }
    return best;
}

f32 RayOf(Span<const Item> items, SpatialKey key, const Ray& ray, f32 max_t)
{
    for (usize i = 0; i < items.Size(); ++i)
    {
        f32 t = 0.0f;
        if (items[i].key == key && Intersect(ray, items[i].box, max_t, &t))
        {
            return t;
        }
    }
    return -1.0f;
}

void CheckStructure(const StaticBvh& bvh, Span<const Item> items)
{
    REQUIRE(bvh.ItemCount() == items.Size());
    if (items.Size() == 0)
    {
        CHECK(bvh.NodeCount() == 0);
        CHECK(bvh.Bounds().IsEmpty());
        return;
    }
    CHECK(bvh.Depth() <= 60);
    for (usize i = 0; i < items.Size(); ++i)
    {
        CHECK(Contains(bvh.Bounds(), items[i].box));
    }
    // Every item once, by descending everywhere and by accepting the root whole.
    Vector<u32> seen;
    seen.Resize(items.Size());
    for (usize i = 0; i < seen.Size(); ++i)
    {
        seen[i] = 0;
    }
    TraversalStats stats = bvh.Traverse([](const Aabb&) { return NodeVisit::Descend; },
                                        [&](const Item& item)
                                        {
                                            ++seen[static_cast<usize>(item.key)];
                                            return true;
                                        });
    CHECK(stats.refused == 0);
    stats = bvh.Traverse([](const Aabb&) { return NodeVisit::AcceptAll; },
                         [&](const Item& item)
                         {
                             ++seen[static_cast<usize>(item.key)];
                             return true;
                         });
    CHECK(stats.nodes_tested == 1);
    for (usize i = 0; i < seen.Size(); ++i)
    {
        CHECK(seen[i] == 2);
    }
    // Each node's box encloses its items, and each subtree's items are its range.
    const Span<const StaticBvh::Node> nodes = bvh.Nodes();
    const Span<const Item> leaf_items = bvh.Items();
    for (u32 n = 0; n < nodes.Size(); ++n)
    {
        const u32 first = bvh.FirstItem(n);
        for (u32 k = 0; k < nodes[n].ItemCount(); ++k)
        {
            CHECK(Contains(nodes[n].box, leaf_items[first + k].box));
        }
        if (nodes[n].IsLeaf())
        {
            CHECK(nodes[n].ItemCount() <= kStaticBvhLeafSize);
        }
        else
        {
            CHECK(nodes[n].ItemCount() == nodes[n + 1].ItemCount() + nodes[nodes[n].a].ItemCount());
        }
    }
}

}  // namespace

TEST_CASE("StaticBvh: box queries and closest rays equal a brute-force scan on 1,000 random scenes", "[spatial][bvh]")
{
    Vector<Item> items;
    Vector<SpatialKey> got;
    Vector<SpatialKey> want;
    StaticBvh bvh;
    for (u64 seed = 1; seed <= 1000; ++seed)
    {
        Rng rng(239000 + seed);
        const u32 kind = static_cast<u32>(seed % 6);
        MakeScene(rng, kind, &items);
        // Keys are the item indices, so CheckStructure can count them.
        bvh.Build(Span<const Item>(items.Data(), items.Size()));
        CheckStructure(bvh, Span<const Item>(items.Data(), items.Size()));
        for (u32 q = 0; q < 8; ++q)
        {
            const Vec3 c = rng.Point(-60, 60);
            const Vec3 h(rng.Float(0, 20), rng.Float(0, 20), rng.Float(0, 20));
            const Aabb query = Aabb::FromMinMax(c - h, c + h);
            QueryBox(bvh, query, &got);
            BruteBox(Span<const Item>(items.Data(), items.Size()), query, &want);
            INFO("seed " << seed << " kind " << kind << " query " << q);
            CHECK(SameKeys(got, want));

            Vec3 direction = rng.Point(-1, 1);
            if (direction.LengthSquared() < 1e-3f)
            {
                direction = Vec3(1, 0.5f, 0);
            }
            const Ray ray = Ray::Make(rng.Point(-70, 70), direction);
            SpatialKey key = ~0ull;
            const f32 t = RayBvh(bvh, ray, 500.0f, &key);
            const f32 brute = RayBrute(Span<const Item>(items.Data(), items.Size()), ray, 500.0f);
            CHECK(t == brute);
            if (t >= 0.0f)
            {
                CHECK(RayOf(Span<const Item>(items.Data(), items.Size()), key, ray, 500.0f) == t);
            }
        }
    }
}

TEST_CASE("StaticBvh: degenerate inputs build within the stack and answer exactly", "[spatial][bvh]")
{
    StaticBvh bvh;
    Vector<Item> items;
    Vector<SpatialKey> got;
    Vector<SpatialKey> want;
    Rng rng(239100);

    SECTION("zero-size boxes (points)")
    {
        for (u32 i = 0; i < 2000; ++i)
        {
            items.Push(MakeItem(rng.Point(-20, 20), Vec3(0, 0, 0), i));
        }
    }
    SECTION("boxes sharing faces (a grid of touching cubes)")
    {
        u32 key = 0;
        for (u32 x = 0; x < 12; ++x)
        {
            for (u32 y = 0; y < 12; ++y)
            {
                for (u32 z = 0; z < 12; ++z)
                {
                    items.Push(MakeItem(Vec3(static_cast<f32>(x), static_cast<f32>(y), static_cast<f32>(z)),
                                        Vec3(0.5f, 0.5f, 0.5f), key++));
                }
            }
        }
    }
    SECTION("geometric spacing (a pure SAH build would go deeper than the stack)")
    {
        f32 x = 1.0f;
        for (u32 i = 0; i < 300; ++i)
        {
            items.Push(MakeItem(Vec3(x, 0, 0), Vec3(0.25f, 0.25f, 0.25f), i));
            x *= 1.3f;
        }
    }
    bvh.Build(Span<const Item>(items.Data(), items.Size()));
    CheckStructure(bvh, Span<const Item>(items.Data(), items.Size()));
    for (u32 q = 0; q < 50; ++q)
    {
        const Vec3 c = rng.Point(-25, 25);
        const Aabb query = Aabb::FromMinMax(c - Vec3(3, 3, 3), c + Vec3(3, 3, 3));
        QueryBox(bvh, query, &got);
        BruteBox(Span<const Item>(items.Data(), items.Size()), query, &want);
        CHECK(SameKeys(got, want));
    }
}

TEST_CASE("StaticBvh: 100,000 identical boxes build in bounded depth", "[spatial][bvh]")
{
    Vector<Item> items;
    for (u32 i = 0; i < 100000; ++i)
    {
        items.Push(MakeItem(Vec3(3, 4, 5), Vec3(1, 1, 1), i));
    }
    StaticBvh bvh;
    bvh.Build(Span<const Item>(items.Data(), items.Size()));
    CHECK(bvh.Depth() <= 17);  // halved down to leaves of 4: 25,000 leaves, 15 splits
    CHECK(bvh.ItemCount() == 100000);
    Vector<SpatialKey> got;
    QueryBox(bvh, Aabb::FromMinMax(Vec3(3.5f, 4.5f, 5.5f), Vec3(3.6f, 4.6f, 5.6f)), &got);
    CHECK(got.Size() == 100000);
    QueryBox(bvh, Aabb::FromMinMax(Vec3(10, 10, 10), Vec3(11, 11, 11)), &got);
    CHECK(got.Size() == 0);
}

TEST_CASE("StaticBvh: the same items build the same tree, and Clear empties it", "[spatial][bvh]")
{
    Rng rng(239200);
    Vector<Item> items;
    MakeScene(rng, 0, &items);
    for (u32 i = 0; i < 2000; ++i)
    {
        items.Push(MakeItem(rng.Point(-80, 80), Vec3(rng.Float(0, 3), rng.Float(0, 3), rng.Float(0, 3)), 1000 + i));
    }
    StaticBvh a;
    StaticBvh b;
    a.Build(Span<const Item>(items.Data(), items.Size()));
    b.Build(Span<const Item>(items.Data(), items.Size()));
    REQUIRE(a.NodeCount() == b.NodeCount());
    CHECK(MemCompare(a.Nodes().Data(), b.Nodes().Data(), a.NodeCount() * sizeof(StaticBvh::Node)) == 0);
    for (u32 i = 0; i < a.ItemCount(); ++i)
    {
        CHECK(a.Items()[i].key == b.Items()[i].key);
    }
    CHECK(sizeof(StaticBvh::Node) == 32);
    // Building again over the same object gives the same tree too.
    a.Build(Span<const Item>(items.Data(), items.Size()));
    CHECK(MemCompare(a.Nodes().Data(), b.Nodes().Data(), a.NodeCount() * sizeof(StaticBvh::Node)) == 0);
    a.Clear();
    CHECK(a.NodeCount() == 0);
    CHECK(a.ItemCount() == 0);
    CHECK(a.Depth() == 0);
    CHECK(a.SahCost() == 0.0f);
    const TraversalStats none = a.Traverse([](const Aabb&) { return NodeVisit::Descend; },
                                           [](const Item&) { return true; });
    CHECK(none.nodes_tested == 0);
}

TEST_CASE("StaticBvh: the SAH cost is within 10 percent of the best of five shuffled builds", "[spatial][bvh]")
{
    Rng rng(239300);
    Vector<Item> items;
    for (u32 i = 0; i < 5000; ++i)
    {
        items.Push(MakeItem(rng.Point(-200, 200), Vec3(rng.Float(0.2f, 6), rng.Float(0.2f, 6), rng.Float(0.2f, 6)), i));
    }
    StaticBvh bvh;
    bvh.Build(Span<const Item>(items.Data(), items.Size()));
    const f32 cost = bvh.SahCost();
    f32 best = 0.0f;
    for (u32 restart = 0; restart < 5; ++restart)
    {
        Vector<Item> shuffled;
        for (usize i = 0; i < items.Size(); ++i)
        {
            shuffled.Push(items[i]);
        }
        for (usize i = shuffled.Size(); i > 1; --i)
        {
            const usize j = rng.Below(static_cast<u32>(i));
            const Item tmp = shuffled[i - 1];
            shuffled[i - 1] = shuffled[j];
            shuffled[j] = tmp;
        }
        StaticBvh other;
        other.Build(Span<const Item>(shuffled.Data(), shuffled.Size()));
        const f32 c = other.SahCost();
        if (restart == 0 || c < best)
        {
            best = c;
        }
    }
    INFO("cost " << cost << " best shuffled " << best);
    CHECK(cost <= best * 1.10f);
}

TEST_CASE("StaticBvh: the build time and quality of 100,000 boxes (recorded, not gated)", "[spatial][bvh][benchmark]")
{
    Rng rng(239400);
    Vector<Item> items;
    for (u32 i = 0; i < 100000; ++i)
    {
        items.Push(MakeItem(rng.Point(-2000, 2000), Vec3(rng.Float(4, 20), rng.Float(2, 40), rng.Float(4, 20)), i));
    }
    StaticBvh bvh;
    const u64 start = os::MonotonicTimeNanos();
    bvh.Build(Span<const Item>(items.Data(), items.Size()));
    const f64 ms = static_cast<f64>(os::MonotonicTimeNanos() - start) / 1e6;
    const f64 bytes = static_cast<f64>(bvh.NodeCount() * sizeof(StaticBvh::Node) + bvh.ItemCount() * sizeof(Item)) /
                      bvh.ItemCount();
    WARN("StaticBvh: 100,000 boxes built in " << ms << " ms; " << bvh.NodeCount() << " nodes, depth " << bvh.Depth()
                                              << ", SAH cost " << bvh.SahCost() << ", " << bytes << " bytes per item");
    CHECK(bvh.Depth() <= 60);
    CHECK(bytes <= 64.0);
}

TEST_CASE("StaticBvh: Build refuses a box that is not finite", "[spatial][bvh]")
{
    // A NaN corner makes a box that is neither empty nor containable: no node box would enclose it and no query could
    // find it.
    const f32 nan = Limits<f32>::QuietNaN();
    const f32 inf = Limits<f32>::Infinity();
    for (u32 bad = 0; bad < 3; ++bad)
    {
        Vector<Item> items;
        items.Push(MakeItem(Vec3(0, 0, 0), Vec3(1, 1, 1), 0));
        Item odd = MakeItem(Vec3(5, 0, 0), Vec3(1, 1, 1), 1);
        if (bad == 0)
        {
            odd.box.min.x = nan;
        }
        if (bad == 1)
        {
            odd.box.max.y = inf;
        }
        if (bad == 2)
        {
            odd.box.min.z = -inf;
        }
        items.Push(odd);
        const AssertHandler previous = SetAssertHandler(&CountBvhAssert);
        g_bvh_asserts = 0;
        StaticBvh bvh;
        bvh.Build(Span<const Item>(items.Data(), items.Size()));
        SetAssertHandler(previous);
        INFO("case " << bad);
        CHECK(g_bvh_asserts == 1);
    }
}
