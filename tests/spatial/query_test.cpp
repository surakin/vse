// The query vocabulary (Phase 18 Day 241): every query on both trees equals a brute-force scan on 1,000 random scenes
// with random masks and a filter; early stops stop; pairs are each reported once; a sweep never reports a hit past the
// first contact found by fine stepping; nearest equals a full sort; queries allocate nothing; empty trees, point
// queries and NaN input are defined; and 8 threads querying one tree at once get the single-threaded answers.
#include <catch2/catch_test_macros.hpp>

#include "algorithm.h"
#include "alloc_counter.h"
#include "containers/vector.h"
#include "lang/limits.h"
#include "spatial/query.h"
#include "spatial_test_rng.h"
#include "spatial_test_util.h"
#include "sync/atomic.h"
#include "sync/thread.h"

using namespace vse;
using namespace vse::spatial;
using spatial_test::BoxAt;
using spatial_test::Rng;
using spatial_test::SortKeys;

namespace
{

struct Scene
{
    Vector<Item> items;
    StaticBvh fixed;
    DynamicBvh moving;
};

void MakeScene(Rng& rng, u32 count, Scene* scene)
{
    scene->items.Clear();
    scene->moving.Clear();
    for (u32 i = 0; i < count; ++i)
    {
        Item it;
        it.box = BoxAt(rng.Point(-40, 40), Vec3(rng.Float(0, 4), rng.Float(0, 4), rng.Float(0, 4)));
        it.key = 1000 + i * 7;  // keys are not indices
        it.layer = rng.Below(4);
        scene->items.Push(it);
        scene->moving.Insert(it.box, it.key, it.layer);
    }
    scene->fixed.Build(Span<const Item>(scene->items.Data(), scene->items.Size()));
}

bool OddKey(SpatialKey key, void*)
{
    return (key & 1u) != 0;
}

QueryFilter RandomFilter(Rng& rng)
{
    QueryFilter f;
    switch (rng.Below(4))
    {
    case 0: break;                               // everything
    case 1: f.mask = 1u << rng.Below(4); break;  // one layer
    case 2: f.mask = 0x5u; break;                // layers 0 and 2
    default: f.filter = &OddKey; break;          // a filter
    }
    return f;
}

Frustum FrustumAt(const Vec3& apex, f32 far_distance)
{
    Frustum f;
    f.planes[0] = Plane::FromPointNormal(apex, Vec3(0.766f, 0, -0.643f));
    f.planes[1] = Plane::FromPointNormal(apex, Vec3(-0.766f, 0, -0.643f));
    f.planes[2] = Plane::FromPointNormal(apex, Vec3(0, 0.866f, -0.5f));
    f.planes[3] = Plane::FromPointNormal(apex, Vec3(0, -0.866f, -0.5f));
    f.planes[4] = Plane::FromPointNormal(apex + Vec3(0, 0, -1), Vec3(0, 0, -1));
    f.planes[5] = Plane::FromPointNormal(apex + Vec3(0, 0, -far_distance), Vec3(0, 0, 1));
    return f;
}

template <class Shape>
void BruteOverlap(const Vector<Item>& items, const Shape& shape, const QueryFilter& f, Vector<SpatialKey>* out)
{
    out->Clear();
    for (usize i = 0; i < items.Size(); ++i)
    {
        if (f.Accepts(items[i]) && Overlaps(items[i].box, shape))
        {
            out->Push(items[i].key);
        }
    }
    SortKeys(out);
}

void BruteOverlap(const Vector<Item>& items, const Frustum& frustum, const QueryFilter& f, Vector<SpatialKey>* out)
{
    out->Clear();
    for (usize i = 0; i < items.Size(); ++i)
    {
        if (f.Accepts(items[i]) && Overlaps(frustum, items[i].box))
        {
            out->Push(items[i].key);
        }
    }
    SortKeys(out);
}

bool SameSortedKeys(const Vector<SpatialKey>& a, const Vector<SpatialKey>& b)
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

bool HitBefore(const RayHit& x, const RayHit& y)
{
    return x.t < y.t || (x.t == y.t && x.key < y.key);
}

void BruteRayAll(const Vector<Item>& items, const Ray& ray, f32 max_t, const QueryFilter& f, Vector<RayHit>* out)
{
    out->Clear();
    for (usize i = 0; i < items.Size(); ++i)
    {
        f32 t = 0.0f;
        if (f.Accepts(items[i]) && Intersect(ray, items[i].box, max_t, &t))
        {
            RayHit h;
            h.key = items[i].key;
            h.t = t;
            out->Push(h);
        }
    }
    Sort(Span<RayHit>(out->Data(), out->Size()), &HitBefore);
}

bool BruteSweep(const Vector<Item>& items, const Aabb& box, const Vec3& motion, const QueryFilter& f, RayHit* hit)
{
    const Vec3 half = box.HalfExtents();
    const bool moves = motion.x != 0 || motion.y != 0 || motion.z != 0;
    const Ray ray = Ray::Make(box.Center(), moves ? motion : Vec3(1, 0, 0));
    bool found = false;
    for (usize i = 0; i < items.Size(); ++i)
    {
        f32 t = 0.0f;
        if (!f.Accepts(items[i]) || !Intersect(ray, Aabb::FromMinMax(items[i].box.min - half, items[i].box.max + half),
                                               moves ? 1.0f : 0.0f, &t))
        {
            continue;
        }
        RayHit h;
        h.key = items[i].key;
        h.t = t;
        if (!found || HitBefore(h, *hit))
        {
            *hit = h;
        }
        found = true;
    }
    return found;
}

void BruteNearest(const Vector<Item>& items, const Vec3& p, u32 k, f32 max_distance, const QueryFilter& f,
                  Vector<NearestHit>* out)
{
    out->Clear();
    for (usize i = 0; i < items.Size(); ++i)
    {
        const f32 d = Sqrt(Distance2(p, items[i].box));
        if (!f.Accepts(items[i]) || d > max_distance)
        {
            continue;
        }
        NearestHit h;
        h.key = items[i].key;
        h.distance = d;
        out->Push(h);
    }
    Sort(Span<NearestHit>(out->Data(), out->Size()), [](const NearestHit& x, const NearestHit& y)
         { return x.distance < y.distance || (x.distance == y.distance && x.key < y.key); });
    while (out->Size() > k)
    {
        out->Pop();
    }
}

void BrutePairs(const Vector<Item>& items, u32 mask_a, u32 mask_b, Vector<KeyPair>* out)
{
    out->Clear();
    auto in = [](u32 mask, const Item& it) { return (mask & (1u << it.layer)) != 0; };
    for (usize i = 0; i < items.Size(); ++i)
    {
        for (usize j = 0; j < items.Size(); ++j)
        {
            const Item& x = items[i];
            const Item& y = items[j];
            if (x.key >= y.key || !Overlaps(x.box, y.box))
            {
                continue;
            }
            if ((in(mask_a, x) && in(mask_b, y)) || (in(mask_b, x) && in(mask_a, y)))
            {
                KeyPair p;
                p.a = x.key;
                p.b = y.key;
                out->Push(p);
            }
        }
    }
    Sort(Span<KeyPair>(out->Data(), out->Size()),
         [](const KeyPair& x, const KeyPair& y) { return x.a < y.a || (x.a == y.a && x.b < y.b); });
}

// Runs every query on one tree and compares with the brute force.
template <class Tree> void CheckTree(Rng& rng, const Tree& tree, const Vector<Item>& items)
{
    Vector<SpatialKey> got;
    Vector<SpatialKey> want;
    const QueryFilter f = RandomFilter(rng);
    const Aabb box = BoxAt(rng.Point(-45, 45), Vec3(rng.Float(0, 15), rng.Float(0, 15), rng.Float(0, 15)));
    OverlapKeys(tree, box, f, Order::Ordered, &got);
    BruteOverlap(items, box, f, &want);
    CHECK(SameSortedKeys(got, want));

    Sphere sphere;
    sphere.center = rng.Point(-45, 45);
    sphere.radius = rng.Float(0, 15);
    OverlapKeys(tree, sphere, f, Order::Ordered, &got);
    BruteOverlap(items, sphere, f, &want);
    CHECK(SameSortedKeys(got, want));

    const Frustum frustum = FrustumAt(Vec3(rng.Float(-30, 30), rng.Float(-30, 30), rng.Float(0, 50)), rng.Float(5, 80));
    OverlapKeys(tree, frustum, f, Order::Ordered, &got);
    BruteOverlap(items, frustum, f, &want);
    CHECK(SameSortedKeys(got, want));

    Vec3 d = rng.Point(-1, 1);
    if (d.LengthSquared() < 1e-3f)
    {
        d = Vec3(1, 0, 0);
    }
    const Ray ray = Ray::Make(rng.Point(-50, 50), d);
    Vector<RayHit> hits;
    Vector<RayHit> brute;
    BruteRayAll(items, ray, 120.0f, f, &brute);
    Raycast(tree, ray, 120.0f, f, RayMode::All, Order::Ordered, &hits);
    REQUIRE(hits.Size() == brute.Size());
    for (usize i = 0; i < hits.Size(); ++i)
    {
        CHECK(hits[i].key == brute[i].key);
        CHECK(hits[i].t == brute[i].t);
    }
    Raycast(tree, ray, 120.0f, f, RayMode::Closest, Order::Traversal, &hits);
    REQUIRE(hits.Size() == (brute.Size() != 0 ? 1u : 0u));
    if (brute.Size() != 0)
    {
        CHECK(hits[0].key == brute[0].key);
        CHECK(hits[0].t == brute[0].t);
    }
    Raycast(tree, ray, 120.0f, f, RayMode::Any, Order::Traversal, &hits);
    REQUIRE(hits.Size() == (brute.Size() != 0 ? 1u : 0u));
    if (hits.Size() != 0)
    {
        bool real = false;
        for (usize i = 0; i < brute.Size(); ++i)
        {
            real = real || (brute[i].key == hits[0].key && brute[i].t == hits[0].t);
        }
        CHECK(real);
    }

    const Aabb mover = BoxAt(rng.Point(-50, 50), Vec3(rng.Float(0, 3), rng.Float(0, 3), rng.Float(0, 3)));
    const Vec3 motion = rng.Point(-60, 60);
    RayHit sweep;
    RayHit brute_sweep;
    const bool hit = SweepBox(tree, mover, motion, f, &sweep).results == 1;
    const bool brute_hit = BruteSweep(items, mover, motion, f, &brute_sweep);
    REQUIRE(hit == brute_hit);
    if (hit)
    {
        CHECK(sweep.key == brute_sweep.key);
        CHECK(sweep.t == brute_sweep.t);
    }

    Vector<NearestHit> near;
    Vector<NearestHit> brute_near;
    const Vec3 p = rng.Point(-50, 50);
    const u32 k = 1 + rng.Below(8);
    const f32 max_distance = rng.Below(3) == 0 ? rng.Float(1, 20) : Limits<f32>::Infinity();
    Nearest(tree, p, k, max_distance, f, &near);
    BruteNearest(items, p, k, max_distance, f, &brute_near);
    REQUIRE(near.Size() == brute_near.Size());
    for (usize i = 0; i < near.Size(); ++i)
    {
        CHECK(near[i].key == brute_near[i].key);
        CHECK(near[i].distance == brute_near[i].distance);
    }

    const u32 mask_a = 1u + rng.Below(15);
    const u32 mask_b = 1u + rng.Below(15);
    Vector<KeyPair> pairs;
    Vector<KeyPair> brute_pairs;
    PairKeys(tree, mask_a, mask_b, QueryFilter(), Order::Ordered, &pairs);
    BrutePairs(items, mask_a, mask_b, &brute_pairs);
    REQUIRE(pairs.Size() == brute_pairs.Size());
    for (usize i = 0; i < pairs.Size(); ++i)
    {
        CHECK(pairs[i].a == brute_pairs[i].a);
        CHECK(pairs[i].b == brute_pairs[i].b);
    }
}

}  // namespace

TEST_CASE("Queries: every query on both trees equals a brute-force scan on 1,000 random scenes", "[spatial][query]")
{
    Scene scene;
    for (u64 seed = 1; seed <= 1000; ++seed)
    {
        Rng rng(241000 + seed);
        MakeScene(rng, rng.Below(160), &scene);
        INFO("seed " << seed << " items " << scene.items.Size());
        CheckTree(rng, scene.fixed, scene.items);
        CheckTree(rng, scene.moving, scene.items);
    }
}

TEST_CASE("Queries: masks and filters exclude exactly what they should, and a visitor can stop", "[spatial][query]")
{
    DynamicBvh tree;
    for (u32 i = 0; i < 64; ++i)
    {
        tree.Insert(BoxAt(Vec3(0, 0, 0), Vec3(1, 1, 1)), i, i % 4);  // all overlapping
    }
    Vector<SpatialKey> got;
    QueryFilter layer2;
    layer2.mask = 1u << 2;
    OverlapKeys(tree, BoxAt(Vec3(0, 0, 0), Vec3(1, 1, 1)), layer2, Order::Ordered, &got);
    REQUIRE(got.Size() == 16);
    for (usize i = 0; i < got.Size(); ++i)
    {
        CHECK(got[i] % 4 == 2);
    }
    QueryFilter odd;
    odd.filter = &OddKey;
    OverlapKeys(tree, BoxAt(Vec3(0, 0, 0), Vec3(1, 1, 1)), odd, Order::Ordered, &got);
    REQUIRE(got.Size() == 32);
    for (usize i = 0; i < got.Size(); ++i)
    {
        CHECK(got[i] % 2 == 1);
    }
    QueryFilter none;
    none.mask = 0;
    OverlapKeys(tree, BoxAt(Vec3(0, 0, 0), Vec3(1, 1, 1)), none, Order::Ordered, &got);
    CHECK(got.Size() == 0);
    // A layer of 32 or more matches no mask.
    tree.Insert(BoxAt(Vec3(0, 0, 0), Vec3(1, 1, 1)), 999, 40);
    OverlapKeys(tree, BoxAt(Vec3(0, 0, 0), Vec3(1, 1, 1)), QueryFilter(), Order::Ordered, &got);
    CHECK(got.Size() == 64);

    u32 seen = 0;
    QueryStats stats = Overlap(tree, BoxAt(Vec3(0, 0, 0), Vec3(1, 1, 1)), QueryFilter(),
                               [&](const Item&) { return ++seen < 3; });
    CHECK(seen == 3);
    CHECK(stats.results == 3);
    CHECK(stats.stopped);
    u32 pairs = 0;
    stats = ForEachPair(tree, 0xF, 0xF, QueryFilter(), [&](const Item&, const Item&) { return ++pairs < 5; });
    CHECK(pairs == 5);
    CHECK(stats.stopped);
    CHECK(stats.results == 5);
}

TEST_CASE("Queries: each overlapping pair is reported once, the smaller key first", "[spatial][query]")
{
    Rng rng(241100);
    Scene scene;
    MakeScene(rng, 300, &scene);
    Vector<KeyPair> pairs;
    PairKeys(scene.fixed, 0xF, 0xF, QueryFilter(), Order::Ordered, &pairs);
    Vector<KeyPair> brute;
    BrutePairs(scene.items, 0xF, 0xF, &brute);
    REQUIRE(pairs.Size() == brute.Size());
    REQUIRE(pairs.Size() > 20);  // enough pairs for the check to mean something
    for (usize i = 0; i < pairs.Size(); ++i)
    {
        CHECK(pairs[i].a < pairs[i].b);
        if (i > 0)
        {
            CHECK((pairs[i - 1].a < pairs[i].a ||
                   (pairs[i - 1].a == pairs[i].a && pairs[i - 1].b < pairs[i].b)));  // no duplicate
        }
    }
    // One side only: a group against another.
    PairKeys(scene.moving, 1u << 0, 1u << 1, QueryFilter(), Order::Ordered, &pairs);
    BrutePairs(scene.items, 1u << 0, 1u << 1, &brute);
    CHECK(pairs.Size() == brute.Size());
}

TEST_CASE("Queries: a sweep never reports a hit past the first contact found by fine stepping",
          "[spatial][query][sweep]")
{
    Rng rng(241200);
    constexpr u32 kSteps = 2000;
    for (u32 trial = 0; trial < 300; ++trial)
    {
        Scene scene;
        MakeScene(rng, 40, &scene);
        const Aabb mover = BoxAt(rng.Point(-50, 50), Vec3(rng.Float(0.2f, 3), rng.Float(0.2f, 3), rng.Float(0.2f, 3)));
        const Vec3 motion = rng.Point(-80, 80);
        RayHit hit;
        const bool found = SweepBox(scene.fixed, mover, motion, QueryFilter(), &hit).results == 1;
        // The first step at which the moved box overlaps any item.
        f32 first = -1.0f;
        for (u32 s = 0; s <= kSteps && first < 0.0f; ++s)
        {
            const f32 frac = static_cast<f32>(s) / kSteps;
            const Aabb moved = Aabb::FromMinMax(mover.min + motion * frac, mover.max + motion * frac);
            for (usize i = 0; i < scene.items.Size(); ++i)
            {
                if (Overlaps(moved, scene.items[i].box))
                {
                    first = frac;
                }
            }
        }
        INFO("trial " << trial);
        if (first >= 0.0f)
        {
            REQUIRE(found);
            CHECK(hit.t <= first + 1e-5f);  // never later than the first sampled contact
            // At the reported time the box touches the item (within rounding).
            const Aabb at = Aabb::FromMinMax(mover.min + motion * hit.t, mover.max + motion * hit.t);
            bool touches = false;
            for (usize i = 0; i < scene.items.Size(); ++i)
            {
                if (scene.items[i].key == hit.key)
                {
                    touches = Overlaps(Expand(at, 1e-3f), scene.items[i].box);
                }
            }
            CHECK(touches);
        }
    }
    // A box that does not move finds what it stands in.
    Scene scene;
    MakeScene(rng, 0, &scene);
    DynamicBvh tree;
    tree.Insert(BoxAt(Vec3(0, 0, 0), Vec3(1, 1, 1)), 5, 0);
    RayHit hit;
    CHECK(SweepBox(tree, BoxAt(Vec3(0.5f, 0, 0), Vec3(1, 1, 1)), Vec3(0, 0, 0), QueryFilter(), &hit).results == 1);
    CHECK(hit.t == 0.0f);
    CHECK(SweepBox(tree, BoxAt(Vec3(9, 0, 0), Vec3(1, 1, 1)), Vec3(0, 0, 0), QueryFilter(), &hit).results == 0);
}

TEST_CASE("Queries: empty trees, point queries and NaN input are defined", "[spatial][query]")
{
    StaticBvh empty_static;
    DynamicBvh empty_dynamic;
    Vector<SpatialKey> keys;
    Vector<RayHit> hits;
    Vector<NearestHit> near;
    CHECK(
        OverlapKeys(empty_static, BoxAt(Vec3(0, 0, 0), Vec3(1, 1, 1)), QueryFilter(), Order::Ordered, &keys).results ==
        0);
    CHECK(Raycast(empty_dynamic, Ray::Make(Vec3(0, 0, 0), Vec3(1, 0, 0)), 10, QueryFilter(), RayMode::All,
                  Order::Ordered, &hits)
              .results == 0);
    CHECK(Nearest(empty_static, Vec3(0, 0, 0), 3, 10, QueryFilter(), &near).results == 0);

    DynamicBvh tree;
    tree.Insert(BoxAt(Vec3(0, 0, 0), Vec3(1, 1, 1)), 1, 0);
    tree.Insert(BoxAt(Vec3(2, 0, 0), Vec3(1, 1, 1)), 2, 0);  // from x = 1 to 3: it shares the face x = 1 with the first
    // A zero-size box is a point: it finds the boxes it is in (and touches).
    OverlapKeys(tree, BoxAt(Vec3(0.5f, 0, 0), Vec3(0, 0, 0)), QueryFilter(), Order::Ordered, &keys);
    REQUIRE(keys.Size() == 1);
    CHECK(keys[0] == 1);
    OverlapKeys(tree, BoxAt(Vec3(1, 0, 0), Vec3(0, 0, 0)), QueryFilter(), Order::Ordered, &keys);  // on the shared face
    CHECK(keys.Size() == 2);
    // An infinite box finds everything; the empty box nothing.
    const f32 inf = Limits<f32>::Infinity();
    CHECK(OverlapKeys(tree, Aabb::FromMinMax(Vec3(-inf, -inf, -inf), Vec3(inf, inf, inf)), QueryFilter(),
                      Order::Ordered, &keys)
              .results == 2);
    CHECK(OverlapKeys(tree, Aabb::Empty(), QueryFilter(), Order::Ordered, &keys).results == 0);
    // NaN input: no results, and the refusal says so.
    const f32 nan = Limits<f32>::QuietNaN();
    QueryStats stats = OverlapKeys(tree, BoxAt(Vec3(nan, 0, 0), Vec3(1, 1, 1)), QueryFilter(), Order::Ordered, &keys);
    CHECK(stats.refused_input);
    CHECK(keys.Size() == 0);
    Sphere bad;
    bad.radius = nan;
    CHECK(OverlapKeys(tree, bad, QueryFilter(), Order::Ordered, &keys).refused_input);
    CHECK(Raycast(tree, Ray::Make(Vec3(nan, 0, 0), Vec3(1, 0, 0)), 10, QueryFilter(), RayMode::Closest,
                  Order::Traversal, &hits)
              .refused_input);
    CHECK(Raycast(tree, Ray::Make(Vec3(-5, 0, 0), Vec3(1, 0, 0)), nan, QueryFilter(), RayMode::Closest,
                  Order::Traversal, &hits)
              .refused_input);
    CHECK(hits.Size() == 0);
    RayHit hit;
    CHECK(SweepBox(tree, BoxAt(Vec3(0, 0, 0), Vec3(1, 1, 1)), Vec3(nan, 0, 0), QueryFilter(), &hit).refused_input);
    CHECK(Nearest(tree, Vec3(0, nan, 0), 2, 10, QueryFilter(), &near).refused_input);
    CHECK(near.Size() == 0);
    // An unbounded ray and a negative limit.
    CHECK(
        Raycast(tree, Ray::Make(Vec3(-5, 0, 0), Vec3(1, 0, 0)), inf, QueryFilter(), RayMode::All, Order::Ordered, &hits)
            .results == 2);
    CHECK(
        Raycast(tree, Ray::Make(Vec3(-5, 0, 0), Vec3(1, 0, 0)), -1, QueryFilter(), RayMode::All, Order::Ordered, &hits)
            .results == 0);
}

TEST_CASE("Queries: with warm result vectors, queries allocate nothing", "[spatial][query]")
{
    if (!EcsTestCountsAllocations())
    {
        SKIP("this flavor's sanitizer runtime owns operator new");
    }
    Rng rng(241300);
    Scene scene;
    MakeScene(rng, 2000, &scene);
    Vector<SpatialKey> keys;
    Vector<RayHit> hits;
    Vector<NearestHit> near;
    Vector<KeyPair> pairs;
    keys.Reserve(4096);
    hits.Reserve(4096);
    near.Reserve(64);
    pairs.Reserve(1 << 16);
    const unsigned long before = EcsTestHeapAllocations();
    u32 total = 0;
    for (u32 q = 0; q < 50; ++q)
    {
        const Aabb box = BoxAt(rng.Point(-40, 40), Vec3(6, 6, 6));
        total += OverlapKeys(scene.fixed, box, QueryFilter(), Order::Ordered, &keys).results;
        total += OverlapKeys(scene.moving, FrustumAt(rng.Point(-20, 20), 40), QueryFilter(), Order::Traversal, &keys)
                     .results;
        total += Raycast(scene.moving, Ray::Make(rng.Point(-40, 40), Vec3(1, 0.2f, 0.1f)), 100, QueryFilter(),
                         RayMode::All, Order::Ordered, &hits)
                     .results;
        total += Nearest(scene.fixed, rng.Point(-40, 40), 8, Limits<f32>::Infinity(), QueryFilter(), &near).results;
        RayHit hit;
        total += SweepBox(scene.fixed, box, rng.Point(-30, 30), QueryFilter(), &hit).results;
    }
    total += PairKeys(scene.fixed, 0xF, 0xF, QueryFilter(), Order::Ordered, &pairs).results;
    const unsigned long after = EcsTestHeapAllocations();
    CHECK(after == before);
    CHECK(total > 0);
}

TEST_CASE("Queries: 8 threads querying one tree at once get the single-threaded answers", "[spatial][query][threads]")
{
    Rng rng(241400);
    Scene scene;
    MakeScene(rng, 3000, &scene);
    constexpr u32 kQueries = 200;
    // The single-threaded answers: a checksum of each query's ordered keys.
    struct Probe
    {
        Aabb box;
        Ray ray;
        Vec3 point;
        u64 expected = 0;
    };
    Vector<Probe> probes;
    auto run = [&](const Probe& p, Vector<SpatialKey>* keys, Vector<RayHit>* hits, Vector<NearestHit>* near)
    {
        u64 sum = 1469598103934665603ull;
        OverlapKeys(scene.fixed, p.box, QueryFilter(), Order::Ordered, keys);
        for (usize i = 0; i < keys->Size(); ++i)
        {
            sum = (sum ^ (*keys)[i]) * 1099511628211ull;
        }
        OverlapKeys(scene.moving, p.box, QueryFilter(), Order::Ordered, keys);
        for (usize i = 0; i < keys->Size(); ++i)
        {
            sum = (sum ^ (*keys)[i]) * 1099511628211ull;
        }
        Raycast(scene.fixed, p.ray, 150, QueryFilter(), RayMode::All, Order::Ordered, hits);
        for (usize i = 0; i < hits->Size(); ++i)
        {
            sum = (sum ^ (*hits)[i].key) * 1099511628211ull;
        }
        Nearest(scene.moving, p.point, 6, Limits<f32>::Infinity(), QueryFilter(), near);
        for (usize i = 0; i < near->Size(); ++i)
        {
            sum = (sum ^ (*near)[i].key) * 1099511628211ull;
        }
        return sum;
    };
    {
        Vector<SpatialKey> keys;
        Vector<RayHit> hits;
        Vector<NearestHit> near;
        for (u32 q = 0; q < kQueries; ++q)
        {
            Probe p;
            p.box = BoxAt(rng.Point(-40, 40), Vec3(rng.Float(1, 12), rng.Float(1, 12), rng.Float(1, 12)));
            Vec3 d = rng.Point(-1, 1);
            if (d.LengthSquared() < 1e-3f)
            {
                d = Vec3(0, 0, 1);
            }
            p.ray = Ray::Make(rng.Point(-50, 50), d);
            p.point = rng.Point(-50, 50);
            p.expected = run(p, &keys, &hits, &near);
            probes.Push(p);
        }
    }
    Atomic<u32> mismatches{0};
    Vector<Thread> threads;
    for (u32 t = 0; t < 8; ++t)
    {
        threads.Push(Thread(
            [&, t]
            {
                Vector<SpatialKey> keys;
                Vector<RayHit> hits;
                Vector<NearestHit> near;
                for (u32 round = 0; round < 5; ++round)
                {
                    for (u32 q = 0; q < kQueries; ++q)
                    {
                        const Probe& p = probes[(q * 7 + t * 13 + round) % kQueries];
                        if (run(p, &keys, &hits, &near) != p.expected)
                        {
                            mismatches.FetchAdd(1);
                        }
                    }
                }
            }));
    }
    for (usize i = 0; i < threads.Size(); ++i)
    {
        threads[i].Join();
    }
    CHECK(mismatches.Load() == 0);
}
