// The dynamic BVH (Phase 18 Day 240): queries equal a brute-force scan after inserts, after a bulk build and through
// 100,000 random operations against a flat model with Validate() every 500 operations; stale handles are refused; moves
// inside the fat box change nothing; a scene where everything moves for 1,000 frames stays within 2x of a fresh build's
// SAH cost with Optimize; the steady state allocates nothing. Recorded, not gated: the move cost, and the measurement
// that chose the predictive margin and the rebuild threshold (docs/guide/spatial.md, "The dynamic BVH").
#include <catch2/catch_test_macros.hpp>

#include "algorithm.h"
#include "alloc_counter.h"
#include "containers/vector.h"
#include "os/os.h"
#include "spatial/dynamic_bvh.h"
#include "spatial_test_rng.h"
#include "spatial_test_util.h"

using namespace vse;
using namespace vse::spatial;
using spatial_test::BoxAt;
using spatial_test::Rng;

namespace
{

Aabbd RandomBox(Rng& rng, f64 extent)
{
    return BoxAt(rng.Point(-extent, extent), Vec3d(rng.Float(0.1f, 3), rng.Float(0.1f, 3), rng.Float(0.1f, 3)));
}

// The model: what the tree should hold.
struct ModelItem
{
    bool live = false;
    ProxyId id = kInvalidProxy;
    Aabbd box;
    SpatialKey key = 0;
};

void Sorted(Vector<SpatialKey>* keys)
{
    Sort(Span<SpatialKey>(keys->Data(), keys->Size()), [](SpatialKey a, SpatialKey b) { return a < b; });
}

bool Same(Vector<SpatialKey>& a, Vector<SpatialKey>& b)
{
    Sorted(&a);
    Sorted(&b);
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

void TreeBox(const DynamicBvh& tree, const Aabbd& q, Vector<SpatialKey>* out)
{
    out->Clear();
    const TraversalStats stats = tree.Traverse([&](const Aabbd& node)
                                               { return Overlaps(node, q) ? NodeVisit::Descend : NodeVisit::Reject; },
                                               [&](ProxyId, const Item& item)
                                               {
                                                   if (Overlaps(item.box, q))
                                                   {
                                                       out->Push(item.key);
                                                   }
                                                   return true;
                                               });
    CHECK(stats.refused == 0);
}

void ModelBox(const Vector<ModelItem>& model, const Aabbd& q, Vector<SpatialKey>* out)
{
    out->Clear();
    for (usize i = 0; i < model.Size(); ++i)
    {
        if (model[i].live && Overlaps(model[i].box, q))
        {
            out->Push(model[i].key);
        }
    }
}

f64 TreeRay(const DynamicBvh& tree, const Rayd& ray, f64 max_t)
{
    f64 best = -1.0f;
    const TraversalStats stats = tree.TraverseRay(ray, max_t,
                                                  [&](ProxyId, const Item& item, f64& limit)
                                                  {
                                                      f64 t = 0.0f;
                                                      if (Intersect(ray, item.box, limit, &t) &&
                                                          (best < 0.0f || t < best))
                                                      {
                                                          best = t;
                                                          limit = t;
                                                      }
                                                      return true;
                                                  });
    CHECK(stats.refused == 0);
    return best;
}

f64 ModelRay(const Vector<ModelItem>& model, const Rayd& ray, f64 max_t)
{
    f64 best = -1.0f;
    for (usize i = 0; i < model.Size(); ++i)
    {
        f64 t = 0.0f;
        if (model[i].live && Intersect(ray, model[i].box, max_t, &t) && (best < 0.0f || t < best))
        {
            best = t;
        }
    }
    return best;
}

void CompareQueries(Rng& rng, const DynamicBvh& tree, const Vector<ModelItem>& model, u32 queries)
{
    Vector<SpatialKey> got;
    Vector<SpatialKey> want;
    for (u32 q = 0; q < queries; ++q)
    {
        const Aabbd query = BoxAt(rng.Point(-60, 60), Vec3d(rng.Float(0, 15), rng.Float(0, 15), rng.Float(0, 15)));
        TreeBox(tree, query, &got);
        ModelBox(model, query, &want);
        CHECK(Same(got, want));
        Vec3d d = rng.Point(-1, 1);
        if (d.LengthSquared() < 1e-3f)
        {
            d = Vec3d(0, 1, 0);
        }
        const Rayd ray = Rayd::Make(rng.Point(-70, 70), d);
        CHECK(TreeRay(tree, ray, 400.0f) == ModelRay(model, ray, 400.0f));
    }
}

}  // namespace

TEST_CASE("DynamicBvh: inserted items answer like a brute-force scan", "[spatial][dbvh]")
{
    Rng rng(240001);
    DynamicBvh tree;
    Vector<ModelItem> model;
    for (u32 i = 0; i < 2000; ++i)
    {
        ModelItem m;
        m.live = true;
        m.box = RandomBox(rng, 50);
        m.key = i;
        m.id = tree.Insert(m.box, m.key, i % 4);
        REQUIRE(m.id != kInvalidProxy);
        model.Push(m);
    }
    REQUIRE(tree.Validate());
    CHECK(tree.ProxyCount() == 2000);
    CompareQueries(rng, tree, model, 200);
    for (usize i = 0; i < model.Size(); ++i)
    {
        CHECK(tree.IsLive(model[i].id));
        CHECK(tree.ItemOf(model[i].id).key == model[i].key);
        CHECK(Contains(tree.FatBox(model[i].id), model[i].box));
    }
    const DynamicBvhStats stats = tree.Stats();
    CHECK(stats.height <= 2 * 11 + 2);  // balanced: about log2(2000) = 11, AVL at most 1.44 times
    CHECK(stats.nodes == 2 * 2000 - 1);
}

TEST_CASE("DynamicBvh: a bulk build answers like a brute-force scan, and Rebuild keeps the handles", "[spatial][dbvh]")
{
    Rng rng(240002);
    Vector<Item> items;
    Vector<ModelItem> model;
    for (u32 i = 0; i < 3000; ++i)
    {
        Item it;
        it.box = RandomBox(rng, 50);
        it.key = 100 + i;
        items.Push(it);
        ModelItem m;
        m.live = true;
        m.box = it.box;
        m.key = it.key;
        model.Push(m);
    }
    Vector<ProxyId> ids;
    ids.Resize(items.Size());
    DynamicBvh tree;
    tree.Build(Span<const Item>(items.Data(), items.Size()), Span<ProxyId>(ids.Data(), ids.Size()));
    REQUIRE(tree.Validate());
    CompareQueries(rng, tree, model, 200);
    for (usize i = 0; i < ids.Size(); ++i)
    {
        CHECK(tree.ItemOf(ids[i]).key == items[i].key);
    }
    // A top-down build is better than an insert-built tree of the same boxes.
    DynamicBvh inserted;
    for (usize i = 0; i < items.Size(); ++i)
    {
        inserted.Insert(items[i].box, items[i].key, 0);
    }
    INFO("built " << tree.SahCost() << " inserted " << inserted.SahCost());
    CHECK(tree.SahCost() < inserted.SahCost());
    tree.Rebuild();
    REQUIRE(tree.Validate());
    for (usize i = 0; i < ids.Size(); ++i)
    {
        CHECK(tree.ItemOf(ids[i]).key == items[i].key);
    }
    CompareQueries(rng, tree, model, 50);
    // Building again replaces everything: the old handles are stale.
    tree.Build(Span<const Item>(items.Data(), 10), Span<ProxyId>(ids.Data(), 10));
    CHECK(tree.ProxyCount() == 10);
    REQUIRE(tree.Validate());
}

TEST_CASE("DynamicBvh: 100,000 random inserts, removes and moves keep the tree exact and valid", "[spatial][dbvh]")
{
    Rng rng(240003);
    DynamicBvh tree;
    Vector<ModelItem> model;
    Vector<u32> live;  // indices into model
    u32 next_key = 0;
    for (u32 op = 1; op <= 100000; ++op)
    {
        const u32 kind = rng.Below(100);
        if ((kind < 30 && live.Size() < 3000) || live.Size() == 0)
        {
            ModelItem m;
            m.live = true;
            m.box = RandomBox(rng, 50);
            m.key = next_key++;
            m.id = tree.Insert(m.box, m.key, 0);
            REQUIRE(m.id != kInvalidProxy);
            live.Push(static_cast<u32>(model.Size()));
            model.Push(m);
        }
        else if (kind < 50)
        {
            const u32 pick = rng.Below(static_cast<u32>(live.Size()));
            ModelItem& m = model[live[pick]];
            REQUIRE(tree.Remove(m.id));
            m.live = false;
            live[pick] = live[live.Size() - 1];
            live.Pop();
        }
        else
        {
            ModelItem& m = model[live[rng.Below(static_cast<u32>(live.Size()))]];
            const Vec3d c = m.box.Center();
            const Vec3d h = m.box.HalfExtents();
            if (kind < 85)
            {
                m.box = BoxAt(c + rng.Point(-0.3f, 0.3f), h);  // a small step, often inside the fat box
            }
            else if (kind < 97)
            {
                m.box = BoxAt(rng.Point(-50, 50), h);  // a jump
            }
            else
            {
                m.box = BoxAt(c, Vec3d(rng.Float(0.05f, 3), rng.Float(0.05f, 3), rng.Float(0.05f, 3)));  // a new size
            }
            tree.Move(m.id, m.box);
        }
        if (op % 997 == 0)
        {
            tree.Optimize(32);
        }
        if (op % 25000 == 0)
        {
            tree.Rebuild();
        }
        if (op % 500 == 0)
        {
            INFO("operation " << op);
            REQUIRE(tree.Validate());
            REQUIRE(tree.ProxyCount() == live.Size());
            CompareQueries(rng, tree, model, 4);
        }
    }
    const DynamicBvhStats stats = tree.Stats();
    CHECK(stats.moves > 0);
    CHECK(stats.moves_inside_fat > 0);
    CHECK(stats.reinserts > 0);
    CHECK(stats.rotations > 0);
    CHECK(stats.partial_rebuilds > 0);
}

TEST_CASE("DynamicBvh: a stale handle is refused and counted, even after its slot is reused", "[spatial][dbvh]")
{
    DynamicBvh tree;
    const ProxyId a = tree.Insert(BoxAt(Vec3d(0, 0, 0), Vec3d(1, 1, 1)), 1, 0);
    const ProxyId b = tree.Insert(BoxAt(Vec3d(5, 0, 0), Vec3d(1, 1, 1)), 2, 0);
    REQUIRE(tree.Remove(a));
    CHECK_FALSE(tree.IsLive(a));
    CHECK_FALSE(tree.Remove(a));
    CHECK_FALSE(tree.Move(a, BoxAt(Vec3d(9, 9, 9), Vec3d(1, 1, 1))));
    const ProxyId c = tree.Insert(BoxAt(Vec3d(-5, 0, 0), Vec3d(1, 1, 1)), 3,
                                  0);  // reuses a's slot, with a new generation
    CHECK((c & kMaxDynamicProxies) == (a & kMaxDynamicProxies));
    CHECK(c != a);
    CHECK_FALSE(tree.Remove(a));
    CHECK(tree.IsLive(c));
    CHECK_FALSE(tree.Remove(kInvalidProxy));
    CHECK_FALSE(tree.Remove(0x00FFFFF0u));  // a slot that never existed
    CHECK(tree.Stats().stale_refused == 5);
    tree.Clear();
    CHECK_FALSE(tree.IsLive(b));
    CHECK_FALSE(tree.IsLive(c));
    CHECK(tree.ProxyCount() == 0);
    REQUIRE(tree.Validate());
    const ProxyId d = tree.Insert(BoxAt(Vec3d(0, 0, 0), Vec3d(1, 1, 1)), 4, 0);
    CHECK(d != a);
    CHECK(d != b);
    CHECK(d != c);
    CHECK(tree.IsLive(d));
}

TEST_CASE("DynamicBvh: a move inside the fat box changes nothing", "[spatial][dbvh]")
{
    DynamicBvh tree;
    Rng rng(240004);
    Vector<ProxyId> ids;
    for (u32 i = 0; i < 100; ++i)
    {
        ids.Push(tree.Insert(RandomBox(rng, 30), i, 0));
    }
    const ProxyId id = tree.Insert(BoxAt(Vec3d(0, 0, 0), Vec3d(2, 2, 2)), 999, 0);  // largest extent 4, margin 1
    const Aabbd fat = tree.FatBox(id);
    CHECK(fat.min == Vec3d(-3, -3, -3));
    const DynamicBvhStats before = tree.Stats();
    CHECK_FALSE(tree.Move(id, BoxAt(Vec3d(0.5f, 0, 0), Vec3d(2, 2, 2))));
    CHECK_FALSE(tree.Move(id, BoxAt(Vec3d(0.5f, -0.9f, 0.9f), Vec3d(2, 2, 2))));
    const DynamicBvhStats after = tree.Stats();
    CHECK(after.moves_inside_fat == before.moves_inside_fat + 2);
    CHECK(after.reinserts == before.reinserts);
    CHECK(tree.FatBox(id).min == fat.min);
    CHECK(tree.ItemOf(id).box.min == BoxAt(Vec3d(0.5f, -0.9f, 0.9f), Vec3d(2, 2, 2)).min);  // the tight box follows
    CHECK(tree.Move(id, BoxAt(Vec3d(5, 0, 0), Vec3d(2, 2, 2))));  // out of the fat box: re-inserted
    CHECK(tree.Stats().reinserts == before.reinserts + 1);
    REQUIRE(tree.Validate());
}

TEST_CASE("DynamicBvh: everything moving for 1,000 frames stays within 2x of a fresh build with Optimize",
          "[spatial][dbvh]")
{
    Rng rng(240005);
    constexpr u32 kCount = 1000;
    DynamicBvh tree;
    Vector<ProxyId> ids;
    Vector<Aabbd> boxes;
    Vector<Vec3d> velocity;
    for (u32 i = 0; i < kCount; ++i)
    {
        boxes.Push(BoxAt(rng.Point(-100, 100), Vec3d(0.5f, 1, 0.5f)));
        velocity.Push(rng.Point(-0.3f, 0.3f));
        ids.Push(tree.Insert(boxes[i], i, 0));
    }
    f64 worst_ratio = 0.0f;
    for (u32 frame = 1; frame <= 1000; ++frame)
    {
        for (u32 i = 0; i < kCount; ++i)
        {
            Vec3d c = boxes[i].Center() + velocity[i];
            for (u32 k = 0; k < 3; ++k)
            {
                if (c[k] < -100 || c[k] > 100)
                {
                    velocity[i][k] = -velocity[i][k];  // bounce
                }
            }
            if (rng.Below(50) == 0)
            {
                velocity[i] = rng.Point(-0.3f, 0.3f);  // a turn now and then
            }
            boxes[i] = BoxAt(c, boxes[i].HalfExtents());
            tree.Move(ids[i], boxes[i]);
        }
        tree.Optimize(64);
        if (frame % 100 == 0)
        {
            REQUIRE(tree.Validate());
            Vector<Item> items;
            for (u32 i = 0; i < kCount; ++i)
            {
                Item it;
                it.box = boxes[i];
                it.key = i;
                items.Push(it);
            }
            Vector<ProxyId> fresh_ids;
            fresh_ids.Resize(kCount);
            DynamicBvh fresh;
            fresh.Build(Span<const Item>(items.Data(), items.Size()),
                        Span<ProxyId>(fresh_ids.Data(), fresh_ids.Size()));
            const f64 ratio = tree.SahCost() / fresh.SahCost();
            if (ratio > worst_ratio)
            {
                worst_ratio = ratio;
            }
            INFO("frame " << frame << " ratio " << ratio);
            CHECK(ratio <= 2.0f);
        }
    }
    const DynamicBvhStats stats = tree.Stats();
    WARN("DynamicBvh, 1,000 items all moving for 1,000 frames: worst SAH ratio to a fresh build "
         << worst_ratio << ", " << stats.full_rebuilds << " full rebuilds, " << stats.partial_rebuilds << " partial, "
         << stats.reinserts << " re-inserts of " << stats.moves << " moves");
}

TEST_CASE("DynamicBvh: moves and queries allocate nothing once the pool has grown", "[spatial][dbvh]")
{
    if (!EcsTestCountsAllocations())
    {
        SKIP("this flavor's sanitizer runtime owns operator new");
    }
    Rng rng(240006);
    DynamicBvh tree;
    Vector<ProxyId> ids;
    Vector<Aabbd> boxes;
    for (u32 i = 0; i < 2000; ++i)
    {
        boxes.Push(RandomBox(rng, 60));
        ids.Push(tree.Insert(boxes[i], i, 0));
    }
    for (u32 warm = 0; warm < 3; ++warm)
    {  // the pool and the Optimize scratch reach their working size
        for (u32 i = 0; i < 2000; ++i)
        {
            tree.Move(ids[i], BoxAt(rng.Point(-60, 60), boxes[i].HalfExtents()));
        }
        tree.Optimize(64);
    }
    u32 hits = 0;
    const unsigned long before = EcsTestHeapAllocations();
    for (u32 i = 0; i < 2000; ++i)
    {
        boxes[i] = BoxAt(rng.Point(-60, 60), boxes[i].HalfExtents());  // every one re-inserted
        tree.Move(ids[i], boxes[i]);
    }
    for (u32 q = 0; q < 100; ++q)
    {
        const Aabbd query = BoxAt(rng.Point(-60, 60), Vec3d(5, 5, 5));
        tree.Traverse([&](const Aabbd& node) { return Overlaps(node, query) ? NodeVisit::Descend : NodeVisit::Reject; },
                      [&](ProxyId, const Item& item)
                      {
                          hits += Overlaps(item.box, query) ? 1u : 0u;
                          return true;
                      });
    }
    const unsigned long after = EcsTestHeapAllocations();
    CHECK(after == before);
    CHECK(hits > 0);
}

TEST_CASE("DynamicBvh: the move cost and the margin choices (recorded, not gated)", "[spatial][dbvh][benchmark]")
{
    // 10,000 agents for 60 frames, with and without the predictive margin: walking in straight lines (the spec's W3),
    // and jittering (a new random direction every frame, where the last movement predicts nothing).
    for (u32 motion = 0; motion < 2; ++motion)
    {
        for (const f64 predictive : {0.0f, 1.0f, 2.0f, 4.0f})
        {
            Rng rng(240007);
            DynamicBvhConfig config;
            config.predictive = predictive;
            DynamicBvh tree(config);
            constexpr u32 kAgents = 10000;
            Vector<ProxyId> ids;
            Vector<Aabbd> boxes;
            Vector<Vec3d> velocity;
            Vector<Item> items;
            for (u32 i = 0; i < kAgents; ++i)
            {
                Item it;
                it.box = BoxAt(Vec3d(rng.Float(0, 1000), 1, rng.Float(0, 1000)), Vec3d(0.5f, 1, 0.5f));
                it.key = i;
                items.Push(it);
                boxes.Push(it.box);
                const f64 angle = rng.Float(0, 6.283f);
                velocity.Push(Vec3d(Cos(angle) * 0.1f, 0, Sin(angle) * 0.1f));
            }
            ids.Resize(kAgents);
            tree.Build(Span<const Item>(items.Data(), items.Size()), Span<ProxyId>(ids.Data(), ids.Size()));
            u64 move_ns = 0;
            for (u32 frame = 0; frame < 60; ++frame)
            {
                for (u32 i = 0; i < kAgents; ++i)
                {
                    if (motion == 1)
                    {
                        const f64 turn = rng.Float(0, 6.283f);
                        velocity[i] = Vec3d(Cos(turn) * 0.1f, 0, Sin(turn) * 0.1f);
                    }
                    boxes[i] = BoxAt(boxes[i].Center() + velocity[i], boxes[i].HalfExtents());
                }
                const u64 start = os::MonotonicTimeNanos();
                for (u32 i = 0; i < kAgents; ++i)
                {
                    tree.Move(ids[i], boxes[i]);
                }
                move_ns += os::MonotonicTimeNanos() - start;
                tree.Optimize(64);
            }
            // The cost of an overlap query after the motion: nodes tested per 10-unit box query.
            u64 tested = 0;
            for (u32 q = 0; q < 2000; ++q)
            {
                const Aabbd query = BoxAt(Vec3d(rng.Float(0, 1000), 1, rng.Float(0, 1000)), Vec3d(5, 2, 5));
                tested += tree.Traverse([&](const Aabbd& node)
                                        { return Overlaps(node, query) ? NodeVisit::Descend : NodeVisit::Reject; },
                                        [](ProxyId, const Item&) { return true; })
                              .nodes_tested;
            }
            const DynamicBvhStats stats = tree.Stats();
            WARN((motion == 0 ? "straight" : "jitter")
                 << ", predictive " << predictive << ": "
                 << (100.0 * static_cast<f64>(stats.reinserts) / static_cast<f64>(stats.moves)) << " % re-inserted, "
                 << static_cast<f64>(move_ns) / static_cast<f64>(stats.moves) << " ns per move, "
                 << static_cast<f64>(tested) / 2000.0 << " nodes per query, SAH " << stats.sah_cost << ", "
                 << stats.full_rebuilds << " full rebuilds");
        }
    }
}

TEST_CASE("DynamicBvh: 100,000 items take at most 256 bytes each, tight boxes included", "[spatial][dbvh]")
{
    Rng rng(240008);
    DynamicBvh tree;
    for (u32 i = 0; i < 100000; ++i)
    {
        tree.Insert(RandomBox(rng, 2000), i, 0);
    }
    const DynamicBvhStats stats = tree.Stats();
    const f64 per_item = static_cast<f64>(stats.memory_bytes) / static_cast<f64>(stats.proxies);
    INFO("bytes per item " << per_item);
    CHECK(per_item <= 256.0);  // the spatial design spec's 128 bytes, doubled: the boxes are double precision
}

TEST_CASE("DynamicBvh: Optimize does nothing, and costs no pass over the nodes, when nothing changed",
          "[spatial][dbvh]")
{
    Rng rng(240009);
    DynamicBvh tree;
    Vector<ProxyId> ids;
    for (u32 i = 0; i < 500; ++i)
    {
        ids.Push(tree.Insert(RandomBox(rng, 50), i, 0));
    }
    tree.Optimize(64);
    const u64 after_first = tree.Stats().partial_rebuilds + tree.Stats().full_rebuilds;
    for (u32 quiet = 0; quiet < 10; ++quiet)
    {
        tree.Optimize(64);  // a quiet scene: no insert, remove or re-insert since
    }
    CHECK(tree.Stats().partial_rebuilds + tree.Stats().full_rebuilds == after_first);
    CHECK_FALSE(tree.Move(ids[0], tree.ItemOf(ids[0]).box));  // a move inside the fat box is no change either
    tree.Optimize(64);
    CHECK(tree.Stats().partial_rebuilds + tree.Stats().full_rebuilds == after_first);
    tree.Move(ids[1], Aabbd::FromMinMax(Vec3d(500, 500, 500),
                                        Vec3d(501, 501, 501)));  // re-inserted: the next Optimize works again
    tree.Optimize(64);
    CHECK(tree.Stats().partial_rebuilds + tree.Stats().full_rebuilds == after_first + 1);
    REQUIRE(tree.Validate());
}
