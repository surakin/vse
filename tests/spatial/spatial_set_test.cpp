// The hash grid and the spatial set (Phase 18 Day 242): the grid equals a flat model under 100,000 random operations
// (oversized and far items included); a mixed set (static tree, pending static items, dynamic tree or grid) answers
// every query like a brute force, also while items move between the static and the dynamic side and around
// RebuildStatic; sets are independent; the traversal order is the documented one. Recorded, not gated: the grid's
// all-pairs and update cost against the static tree's on 100,000 agents (the design spec's experiment (a)).
#include <catch2/catch_test_macros.hpp>

#include "algorithm.h"
#include "containers/vector.h"
#include "lang/limits.h"
#include "log/assert.h"
#include "os/os.h"
#include "spatial/query.h"
#include "spatial_test_rng.h"
#include "spatial_test_util.h"

using namespace vse;
using namespace vse::spatial;
using spatial_test::BoxAt;
using spatial_test::Rng;
using spatial_test::SortKeys;

namespace
{

u32 g_set_asserts = 0;
AssertAction CountSetAssert(const AssertInfo&)
{
    ++g_set_asserts;
    return AssertAction::Continue;
}

struct SetModelItem
{
    bool live = false;
    bool is_static = false;
    u32 handle = 0xFFFFFFFFu;
    Item item;
};

bool SameSet(const Vector<SpatialKey>& a, const Vector<SpatialKey>& b)
{
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

template <class Shape>
void ModelOverlap(const Vector<SetModelItem>& model, const Shape& shape, const QueryFilter& f, Vector<SpatialKey>* out)
{
    out->Clear();
    for (usize i = 0; i < model.Size(); ++i)
    {
        if (model[i].live && f.Accepts(model[i].item) && Overlaps(model[i].item.box, shape))
        {
            out->Push(model[i].item.key);
        }
    }
    SortKeys(out);
}

f64 ModelRay(const Vector<SetModelItem>& model, const Rayd& ray, f64 max_t, SpatialKey* key)
{
    f64 best = -1.0f;
    for (usize i = 0; i < model.Size(); ++i)
    {
        f64 t = 0.0f;
        if (!model[i].live || !Intersect(ray, model[i].item.box, max_t, &t))
        {
            continue;
        }
        if (best < 0.0f || t < best || (t == best && model[i].item.key < *key))
        {
            best = t;
            *key = model[i].item.key;
        }
    }
    return best;
}

u32 ModelPairs(const Vector<SetModelItem>& model)
{
    u32 n = 0;
    for (usize i = 0; i < model.Size(); ++i)
    {
        for (usize j = i + 1; j < model.Size(); ++j)
        {
            n += model[i].live && model[j].live && Overlaps(model[i].item.box, model[j].item.box) ? 1u : 0u;
        }
    }
    return n;
}

// Every query kind on a structure against the model.
template <class Structure> void CompareAll(Rng& rng, const Structure& s, const Vector<SetModelItem>& model, u32 rounds)
{
    Vector<SpatialKey> got;
    Vector<SpatialKey> want;
    Vector<RayHit> hits;
    for (u32 r = 0; r < rounds; ++r)
    {
        QueryFilter f;
        if (rng.Below(3) == 0)
        {
            f.mask = 1u << rng.Below(3);
        }
        const Aabbd box = BoxAt(rng.Point(-60, 60), Vec3d(rng.Float(0, 20), rng.Float(0, 20), rng.Float(0, 20)));
        OverlapKeys(s, box, f, Order::Ordered, &got);
        ModelOverlap(model, box, f, &want);
        CHECK(SameSet(got, want));
        Sphered sphere;
        sphere.center = rng.Point(-60, 60);
        sphere.radius = rng.Float(0, 20);
        OverlapKeys(s, sphere, f, Order::Ordered, &got);
        ModelOverlap(model, sphere, f, &want);
        CHECK(SameSet(got, want));
        Vec3d d = rng.Point(-1, 1);
        if (d.LengthSquared() < 1e-3f)
        {
            d = Vec3d(0, 0, 1);
        }
        const Rayd ray = Rayd::Make(rng.Point(-70, 70), d);
        SpatialKey key = 0;
        const f64 t = ModelRay(model, ray, 200, &key);
        Raycast(s, ray, 200, QueryFilter(), RayMode::Closest, Order::Traversal, &hits);
        REQUIRE(hits.Size() == (t >= 0.0f ? 1u : 0u));
        if (t >= 0.0f)
        {
            CHECK(hits[0].t == t);
            CHECK(hits[0].key == key);
        }
    }
    Vector<KeyPair> pairs;
    PairKeys(s, 0xFFFFFFFFu, 0xFFFFFFFFu, QueryFilter(), Order::Ordered, &pairs);
    CHECK(pairs.Size() == ModelPairs(model));
    for (usize i = 1; i < pairs.Size(); ++i)
    {
        CHECK((pairs[i - 1].a < pairs[i].a || (pairs[i - 1].a == pairs[i].a && pairs[i - 1].b < pairs[i].b)));
    }
}

Aabbd RandomItemBox(Rng& rng)
{
    const u32 kind = rng.Below(20);
    if (kind == 0)
    {
        return BoxAt(rng.Point(-50, 50),
                     Vec3d(rng.Float(10, 30), rng.Float(10, 30), rng.Float(10, 30)));  // oversized for cells of 4
    }
    if (kind == 1)
    {
        return BoxAt(Vec3d(rng.Float(-1, 1) * 1e7f, 0, 0), Vec3d(1, 1, 1));  // beyond the grid's cells
    }
    return BoxAt(rng.Point(-50, 50), Vec3d(rng.Float(0, 2), rng.Float(0, 2), rng.Float(0, 2)));
}

}  // namespace

TEST_CASE("HashGrid: 100,000 random operations against a flat model, oversized and far items included",
          "[spatial][grid]")
{
    Rng rng(242001);
    HashGrid grid(4.0f);
    Vector<SetModelItem> model;
    Vector<u32> live;
    u32 next_key = 0;
    for (u32 op = 1; op <= 100000; ++op)
    {
        const u32 kind = rng.Below(100);
        if ((kind < 30 && live.Size() < 600) || live.Size() == 0)
        {
            SetModelItem m;
            m.live = true;
            m.item.box = RandomItemBox(rng);
            m.item.key = next_key++;
            m.item.layer = rng.Below(3);
            m.handle = grid.Insert(m.item.box, m.item.key, m.item.layer);
            REQUIRE(m.handle != kInvalidProxy);
            live.Push(static_cast<u32>(model.Size()));
            model.Push(m);
        }
        else if (kind < 45)
        {
            const u32 pick = rng.Below(static_cast<u32>(live.Size()));
            REQUIRE(grid.Remove(model[live[pick]].handle));
            model[live[pick]].live = false;
            live[pick] = live[live.Size() - 1];
            live.Pop();
        }
        else
        {
            SetModelItem& m = model[live[rng.Below(static_cast<u32>(live.Size()))]];
            m.item.box = kind < 90 ? BoxAt(m.item.box.Center() + rng.Point(-1, 1), m.item.box.HalfExtents())
                                   : RandomItemBox(rng);
            grid.Move(m.handle, m.item.box);
        }
        if (op % 500 == 0)
        {
            INFO("operation " << op);
            REQUIRE(grid.ProxyCount() == live.Size());
            CompareAll(rng, grid, model, 3);
        }
    }
    const HashGridStats stats = grid.Stats();
    CHECK(stats.oversized > 0);
    CHECK(stats.moves_same_cells > 0);
    // A removed item's handle is refused, also after its slot is reused.
    const ProxyId gone = model[live[0]].handle;
    REQUIRE(grid.Remove(gone));
    CHECK_FALSE(grid.Remove(gone));
    CHECK_FALSE(grid.Move(gone, BoxAt(Vec3d(0, 0, 0), Vec3d(1, 1, 1))));
    const ProxyId reused = grid.Insert(BoxAt(Vec3d(0, 0, 0), Vec3d(1, 1, 1)), 999999, 0);
    CHECK(reused != gone);
    CHECK_FALSE(grid.IsLive(gone));
    CHECK(grid.Stats().stale_refused == 2);
}

TEST_CASE("SpatialSet: a mixed set answers like a brute force through changes, moves across sides and rebuilds",
          "[spatial][set]")
{
    for (const DynamicKind kind : {DynamicKind::Bvh, DynamicKind::Grid})
    {
        Rng rng(kind == DynamicKind::Bvh ? 242100u : 242200u);
        SpatialSetConfig config;
        config.dynamic = kind;
        config.grid_cell_size = 4.0f;
        SpatialSet set(config);
        Vector<SetModelItem> model;
        Vector<u32> live;
        u32 next_key = 0;
        for (u32 op = 1; op <= 20000; ++op)
        {
            const u32 r = rng.Below(100);
            if ((r < 25 && live.Size() < 500) || live.Size() == 0)
            {
                SetModelItem m;
                m.live = true;
                m.is_static = rng.Below(2) == 0;
                m.item.box = RandomItemBox(rng);
                m.item.key = next_key++;
                m.item.layer = rng.Below(3);
                m.handle = m.is_static ? set.AddStatic(m.item.box, m.item.key, m.item.layer)
                                       : set.AddDynamic(m.item.box, m.item.key, m.item.layer);
                REQUIRE(m.handle != kInvalidSetHandle);
                live.Push(static_cast<u32>(model.Size()));
                model.Push(m);
            }
            else if (r < 35)
            {
                const u32 pick = rng.Below(static_cast<u32>(live.Size()));
                REQUIRE(set.Remove(model[live[pick]].handle));
                model[live[pick]].live = false;
                live[pick] = live[live.Size() - 1];
                live.Pop();
            }
            else if (r < 80)
            {
                SetModelItem& m = model[live[rng.Below(static_cast<u32>(live.Size()))]];
                const Aabbd moved = BoxAt(m.item.box.Center() + rng.Point(-2, 2), m.item.box.HalfExtents());
                if (m.is_static)
                {
                    CHECK_FALSE(set.Move(m.handle, moved));  // refused: static
                }
                else
                {
                    set.Move(m.handle, moved);
                    m.item.box = moved;
                }
            }
            else if (r < 90)
            {
                SetModelItem& m = model[live[rng.Below(static_cast<u32>(live.Size()))]];
                REQUIRE((m.is_static ? set.MakeDynamic(m.handle) : set.MakeStatic(m.handle)));
                m.is_static = !m.is_static;
                CHECK(set.IsStatic(m.handle) == m.is_static);
            }
            else if (r < 92)
            {
                set.RebuildStatic();
                CHECK(set.Stats().pending_static == 0);
            }
            else if (r < 94)
            {
                set.Optimize(32);
            }
            if (op % 400 == 0)
            {
                INFO("kind " << static_cast<u32>(kind) << " operation " << op);
                REQUIRE(set.ItemCount() == live.Size());
                CompareAll(rng, set, model, 3);
                // Every live handle names its item.
                for (usize i = 0; i < live.Size(); ++i)
                {
                    CHECK(set.ItemOf(model[live[i]].handle).key == model[live[i]].item.key);
                }
            }
        }
        const SpatialSetStats stats = set.Stats();
        CHECK(stats.static_rebuilds > 0);
        CHECK(stats.static_moves_refused > 0);
    }
}

TEST_CASE("SpatialSet: RebuildStatic does not change any result", "[spatial][set]")
{
    Rng rng(242300);
    SpatialSet set;
    Vector<SetModelItem> model;
    for (u32 i = 0; i < 400; ++i)
    {
        SetModelItem m;
        m.live = true;
        m.is_static = i % 3 != 0;
        m.item.box = BoxAt(rng.Point(-40, 40), Vec3d(rng.Float(0, 3), rng.Float(0, 3), rng.Float(0, 3)));
        m.item.key = i;
        m.handle = m.is_static ? set.AddStatic(m.item.box, i, 0) : set.AddDynamic(m.item.box, i, 0);
        model.Push(m);
    }
    set.RebuildStatic();
    // Remove some static items and turn some dynamic: the tree now has stale entries, skipped.
    for (u32 i = 0; i < 400; i += 7)
    {
        if (model[i].is_static)
        {
            set.Remove(model[i].handle);
            model[i].live = false;
        }
    }
    for (u32 i = 1; i < 400; i += 11)
    {
        if (model[i].live && model[i].is_static)
        {
            set.MakeDynamic(model[i].handle);
            model[i].is_static = false;
        }
    }
    CHECK(set.Stats().stale_tree_entries > 0);
    Vector<Vector<SpatialKey>> before;
    Rng probe(242301);
    for (u32 q = 0; q < 50; ++q)
    {
        Vector<SpatialKey> keys;
        OverlapKeys(set, BoxAt(probe.Point(-40, 40), Vec3d(8, 8, 8)), QueryFilter(), Order::Ordered, &keys);
        before.Push(static_cast<Vector<SpatialKey>&&>(keys));
    }
    set.RebuildStatic();
    CHECK(set.Stats().stale_tree_entries == 0);
    Rng again(242301);
    Vector<SpatialKey> keys;
    for (u32 q = 0; q < 50; ++q)
    {
        OverlapKeys(set, BoxAt(again.Point(-40, 40), Vec3d(8, 8, 8)), QueryFilter(), Order::Ordered, &keys);
        CHECK(SameSet(keys, before[q]));
    }
    CompareAll(rng, set, model, 20);
}

TEST_CASE("SpatialSet: sets are independent, and the traversal order is static, pending, then dynamic",
          "[spatial][set]")
{
    SpatialSet render;
    SpatialSet physics;
    const SetHandle crate = render.AddStatic(BoxAt(Vec3d(0, 0, 0), Vec3d(1, 1, 1)), 10, 0);
    physics.AddDynamic(BoxAt(Vec3d(0, 0, 0), Vec3d(1, 1, 1)), 20, 0);
    Vector<SpatialKey> keys;
    OverlapKeys(render, BoxAt(Vec3d(0, 0, 0), Vec3d(2, 2, 2)), QueryFilter(), Order::Ordered, &keys);
    REQUIRE(keys.Size() == 1);
    CHECK(keys[0] == 10);
    OverlapKeys(physics, BoxAt(Vec3d(0, 0, 0), Vec3d(2, 2, 2)), QueryFilter(), Order::Ordered, &keys);
    REQUIRE(keys.Size() == 1);
    CHECK(keys[0] == 20);
    CHECK_FALSE((physics.IsLive(crate) && physics.ItemOf(crate).key == 10));

    // The documented order: the static tree, then the pending static items, then the dynamic side.
    SpatialSet set;
    for (u32 i = 0; i < 5; ++i)
    {
        set.AddStatic(BoxAt(Vec3d(static_cast<f64>(i), 0, 0), Vec3d(0.4f, 0.4f, 0.4f)), 100 + i, 0);
    }
    set.RebuildStatic();
    for (u32 i = 0; i < 3; ++i)
    {
        set.AddStatic(BoxAt(Vec3d(static_cast<f64>(i), 2, 0), Vec3d(0.4f, 0.4f, 0.4f)), 200 + i, 0);
    }
    for (u32 i = 0; i < 4; ++i)
    {
        set.AddDynamic(BoxAt(Vec3d(static_cast<f64>(i), 4, 0), Vec3d(0.4f, 0.4f, 0.4f)), 300 + i, 0);
    }
    Vector<SpatialKey> order;
    Overlap(set, BoxAt(Vec3d(2, 2, 0), Vec3d(10, 10, 10)), QueryFilter(),
            [&](const Item& item)
            {
                order.Push(item.key);
                return true;
            });
    REQUIRE(order.Size() == 12);
    for (usize i = 0; i < order.Size(); ++i)
    {
        const u32 group = static_cast<u32>(order[i] / 100);
        CHECK(group == (i < 5 ? 1u : (i < 8 ? 2u : 3u)));
    }
    CHECK(order[5] == 200);  // the pending list keeps the order the items came in
    CHECK(order[7] == 202);
}

TEST_CASE("HashGrid: the all-pairs and update cost on 100,000 agents against the static tree (recorded, not gated)",
          "[spatial][grid][benchmark]")
{
    // The design spec's W3: 1 x 2 x 1 agents on a 1000 x 1000 plane, each moving 0.1 a frame.
    Rng rng(242400);
    constexpr u32 kAgents = 100000;
    Vector<Item> items;
    Vector<Vec3d> velocity;
    for (u32 i = 0; i < kAgents; ++i)
    {
        Item it;
        it.box = BoxAt(Vec3d(rng.Float(0, 1000), 1, rng.Float(0, 1000)), Vec3d(0.5f, 1, 0.5f));
        it.key = i;
        items.Push(it);
        const f64 angle = rng.Float(0, 6.283f);
        velocity.Push(Vec3d(Cos(angle) * 0.1f, 0, Sin(angle) * 0.1f));
    }
    HashGrid grid(4.0f);
    Vector<ProxyId> ids;
    for (u32 i = 0; i < kAgents; ++i)
    {
        ids.Push(grid.Insert(items[i].box, i, 0));
    }
    u64 update_ns = 0;
    for (u32 frame = 0; frame < 20; ++frame)
    {
        for (u32 i = 0; i < kAgents; ++i)
        {
            items[i].box = BoxAt(items[i].box.Center() + velocity[i], items[i].box.HalfExtents());
        }
        const u64 start = os::MonotonicTimeNanos();
        for (u32 i = 0; i < kAgents; ++i)
        {
            grid.Move(ids[i], items[i].box);
        }
        update_ns += os::MonotonicTimeNanos() - start;
    }
    u64 start = os::MonotonicTimeNanos();
    u32 grid_pairs = 0;
    ForEachPair(grid, 1, 1, QueryFilter(),
                [&](const Item&, const Item&)
                {
                    ++grid_pairs;
                    return true;
                });
    const f64 grid_ms = static_cast<f64>(os::MonotonicTimeNanos() - start) / 1e6;
    StaticBvh tree;
    start = os::MonotonicTimeNanos();
    tree.Build(Span<const Item>(items.Data(), items.Size()));
    const f64 build_ms = static_cast<f64>(os::MonotonicTimeNanos() - start) / 1e6;
    start = os::MonotonicTimeNanos();
    u32 tree_pairs = 0;
    ForEachPair(tree, 1, 1, QueryFilter(),
                [&](const Item&, const Item&)
                {
                    ++tree_pairs;
                    return true;
                });
    const f64 tree_ms = static_cast<f64>(os::MonotonicTimeNanos() - start) / 1e6;
    CHECK(grid_pairs == tree_pairs);
    WARN("100,000 agents: grid all-pairs " << grid_ms << " ms, static tree all-pairs " << tree_ms << " ms (its build "
                                           << build_ms << " ms), " << grid_pairs << " pairs; grid update "
                                           << static_cast<f64>(update_ns) / (20.0 * kAgents) << " ns per moved agent");
}

TEST_CASE("SpatialSet: a set with a dynamic tree does not check the grid's cell size it never uses", "[spatial][set]")
{
    SpatialSetConfig config;
    config.dynamic = DynamicKind::Bvh;
    config.grid_cell_size = 0.0f;  // not a valid cell size, but no grid is made
    const AssertHandler previous = SetAssertHandler(&CountSetAssert);
    g_set_asserts = 0;
    {
        SpatialSet set(config);
        set.AddDynamic(BoxAt(Vec3d(0, 0, 0), Vec3d(1, 1, 1)), 1, 0);
        CHECK(set.ItemCount() == 1);
    }
    CHECK(g_set_asserts == 0);
    config.dynamic = DynamicKind::Grid;  // a grid needs one
    {
        SpatialSet grid_set(config);
    }
    SetAssertHandler(previous);
    CHECK(g_set_asserts == 1);
}

// Double precision (maintenance series): the same scene 100,000 km from the origin answers every query alike.
TEST_CASE("SpatialSet: a scene 100,000 km away gives the same overlaps, rays and nearest as at the origin",
          "[spatial][double]")
{
    const Vec3d far(1.0e8, -2.0e7, 5.0e7);
    SpatialSetConfig config;
    SpatialSet near_set(config);
    SpatialSet far_set(config);
    Rng rng(777);
    for (u32 i = 0; i < 300; ++i)
    {
        const Vec3 c = rng.Point(-40.0f, 40.0f);
        const Vec3 h(rng.Float(0.2f, 2.0f), rng.Float(0.2f, 2.0f), rng.Float(0.2f, 2.0f));
        const Aabbd box = BoxAt(Vec3d(c), Vec3d(h));
        const Aabbd moved = Aabbd::FromMinMax(box.min + far, box.max + far);
        if (i % 3 == 0)
        {
            near_set.AddStatic(box, i, 0);
            far_set.AddStatic(moved, i, 0);
        }
        else
        {
            near_set.AddDynamic(box, i, 0);
            far_set.AddDynamic(moved, i, 0);
        }
    }
    near_set.RebuildStatic();
    far_set.RebuildStatic();
    for (u32 q = 0; q < 50; ++q)
    {
        const Vec3 c = rng.Point(-40.0f, 40.0f);
        const Aabbd region = BoxAt(Vec3d(c), Vec3d(6, 6, 6));
        const Aabbd far_region = Aabbd::FromMinMax(region.min + far, region.max + far);
        Vector<SpatialKey> a;
        Vector<SpatialKey> b;
        OverlapKeys(near_set, region, QueryFilter(), Order::Ordered, &a);
        OverlapKeys(far_set, far_region, QueryFilter(), Order::Ordered, &b);
        CHECK(SameSet(a, b));
        const Vec3d dir = Vec3d(rng.Point(-1.0f, 1.0f)) + Vec3d(0.01, 0.01, 0.01);
        Vector<RayHit> ha;
        Vector<RayHit> hb;
        Raycast(near_set, Rayd::Make(Vec3d(c), dir), 200.0, QueryFilter(), RayMode::Closest, Order::Ordered, &ha);
        Raycast(far_set, Rayd::Make(Vec3d(c) + far, dir), 200.0, QueryFilter(), RayMode::Closest, Order::Ordered, &hb);
        REQUIRE(ha.Size() == hb.Size());
        for (usize i = 0; i < ha.Size(); ++i)
        {
            CHECK(ha[i].key == hb[i].key);
            CHECK(Abs(ha[i].t - hb[i].t) < 1.0e-4);  // the distances agree to the offset's rounding
        }
    }
}
