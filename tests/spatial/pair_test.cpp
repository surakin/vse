// The pair tracker (Phase 18 Day 243): the incremental pairs equal a brute force after every one of 2,000 random frames (adds, removes, moves, switches
// between static and dynamic); Begin and End are exactly the set difference; two runs give the same events; the layer matrix and the static-static rule
// hold; a mock narrow phase fed by the events keeps the right contact list; with a margin the pairs sit between the touching and the near_pairs pairs; a removed
// key ends its pairs in the same update. The cost on 100,000 dynamic objects with 1 percent moving is recorded, not gated.
#include <catch2/catch_test_macros.hpp>

#include "algorithm.h"
#include "containers/array.h"
#include "containers/vector.h"
#include "os/os.h"
#include "spatial/pair_tracker.h"
#include "spatial_test_rng.h"
#include "spatial_test_util.h"

using namespace engine_placeholder;
using namespace engine_placeholder::spatial;
using spatial_test::BoxAt;
using spatial_test::Rng;

namespace {

struct PairItem {
    bool live = false;
    bool is_static = false;
    SetHandle handle = kInvalidSetHandle;
    Item item;
};

bool PairLess(const KeyPair& x, const KeyPair& y) {
    return x.a != y.a ? x.a < y.a : x.b < y.b;
}

void PairSort(Vector<KeyPair>* v) {
    Sort(Span<KeyPair>(v->Data(), v->Size()), PairLess);
}

bool PairSame(const Vector<KeyPair>& x, const Vector<KeyPair>& y) {
    if (x.Size() != y.Size()) return false;
    for (usize i = 0; i < x.Size(); ++i) {
        if (x[i].a != y[i].a || x[i].b != y[i].b) return false;
    }
    return true;
}

bool PairHas(const Vector<KeyPair>& v, SpatialKey a, SpatialKey b) {
    for (usize i = 0; i < v.Size(); ++i) {
        if (v[i].a == a && v[i].b == b) return true;
    }
    return false;
}

f32 PairMargin(const Aabb& box, f32 fraction) {
    const Vec3 e = box.Extents();
    return fraction * Max(e.x, Max(e.y, e.z));
}

// The pairs a brute force finds: boxes grown by `fraction` of their size (0 for the touching pairs), the layer matrix and the static-static rule applied.
void BrutePairs(const Vector<PairItem>& items, const PairTrackerConfig& config, f32 fraction, Vector<KeyPair>* out) {
    out->Clear();
    for (usize i = 0; i < items.Size(); ++i) {
        for (usize j = 0; j < items.Size(); ++j) {
            const PairItem& x = items[i];
            const PairItem& y = items[j];
            if (!x.live || !y.live || x.item.key >= y.item.key) continue;
            if (x.is_static && y.is_static) continue;
            if (!config.Meets(x.item.layer, y.item.layer)) continue;
            if (Overlaps(Expand(x.item.box, PairMargin(x.item.box, fraction)), Expand(y.item.box, PairMargin(y.item.box, fraction)))) out->Push(KeyPair{x.item.key, y.item.key});
        }
    }
    PairSort(out);
}

// What `after` has that `before` lacks.
void PairDifference(const Vector<KeyPair>& after, const Vector<KeyPair>& before, Vector<KeyPair>* out) {
    out->Clear();
    for (usize i = 0; i < after.Size(); ++i) {
        if (!PairHas(before, after[i].a, after[i].b)) out->Push(after[i]);
    }
}

PairTrackerConfig PairMatrix(f32 margin) {
    PairTrackerConfig c;
    c.margin = margin;
    c.SetMeets(1, 1, false); // layer 1 items do not meet each other
    c.SetMeets(0, 2, false);
    return c;
}

struct PairScene {
    SpatialSet set;
    PairTracker tracker;
    PairTrackerConfig config;
    Vector<PairItem> items;
    u64 next_key = 1;
    PairScene(const PairTrackerConfig& c, const SpatialSetConfig& sc = SpatialSetConfig()) : set(sc), tracker(c), config(c) {}

    void Add(Rng& rng, bool is_static) {
        PairItem p;
        p.live = true;
        p.is_static = is_static;
        p.item.key = next_key++;
        p.item.layer = rng.Below(3);
        p.item.box = BoxAt(rng.Point(-30, 30), Vec3(rng.Float(0.5f, 4), rng.Float(0.5f, 4), rng.Float(0.5f, 4)));
        p.handle = is_static ? set.AddStatic(p.item.box, p.item.key, p.item.layer) : set.AddDynamic(p.item.box, p.item.key, p.item.layer);
        REQUIRE(p.handle != kInvalidSetHandle);
        tracker.Touch(p.handle);
        items.Push(p);
    }
    usize PickLive(Rng& rng) const {
        for (u32 tries = 0; tries < 20; ++tries) {
            const usize i = rng.Below(static_cast<u32>(items.Size()));
            if (items[i].live) return i;
        }
        return items.Size();
    }
    void Frame(Rng& rng) {
        const u32 ops = rng.Below(10);
        for (u32 o = 0; o < ops; ++o) {
            const u32 kind = rng.Below(100);
            if (items.Size() == 0 || kind < 25) {
                if (items.Size() < 400) Add(rng, rng.Below(5) == 0);
                continue;
            }
            const usize i = PickLive(rng);
            if (i >= items.Size()) continue;
            PairItem& p = items[i];
            if (kind < 38) {
                tracker.Remove(p.item.key);
                set.Remove(p.handle);
                p.live = false;
            } else if (kind < 88) {
                if (p.is_static) continue;
                const Vec3 c = p.item.box.Center() + rng.Point(-3, 3);
                p.item.box = BoxAt(c, p.item.box.HalfExtents());
                set.Move(p.handle, p.item.box);
                tracker.Touch(p.handle);
            } else if (kind < 94) {
                if (p.is_static) {
                    set.MakeDynamic(p.handle);
                } else {
                    set.MakeStatic(p.handle);
                }
                p.is_static = !p.is_static;
                tracker.Touch(p.handle);
            } else if (kind < 97) { // the same key removed and added back (at a new place, maybe on another layer)
                tracker.Remove(p.item.key);
                set.Remove(p.handle);
                p.item.layer = rng.Below(3);
                p.item.box = BoxAt(p.item.box.Center() + rng.Point(-2, 2), p.item.box.HalfExtents());
                p.handle = p.is_static ? set.AddStatic(p.item.box, p.item.key, p.item.layer) : set.AddDynamic(p.item.box, p.item.key, p.item.layer);
                tracker.Touch(p.handle);
            } else if (rng.Below(3) == 0) {
                set.RebuildStatic();
            }
        }
    }
};

void PairApplyToContacts(const PairEvents& events, Vector<KeyPair>* contacts) {
    for (usize i = 0; i < events.end.Size(); ++i) {
        bool found = false;
        for (usize k = 0; k < contacts->Size(); ++k) {
            if ((*contacts)[k].a == events.end[i].a && (*contacts)[k].b == events.end[i].b) {
                (*contacts)[k] = (*contacts)[contacts->Size() - 1];
                contacts->Resize(contacts->Size() - 1);
                found = true;
                break;
            }
        }
        CHECK(found); // an End for a pair the narrow phase never had would corrupt it
    }
    for (usize i = 0; i < events.begin.Size(); ++i) {
        CHECK(!PairHas(*contacts, events.begin[i].a, events.begin[i].b));
        contacts->Push(events.begin[i]);
    }
    PairSort(contacts);
}

u64 PairMix(u64 h, u64 v) {
    return (h ^ v) * 0x100000001B3ull;
}

// Runs the random frames with a margin of 0 and checks everything; returns a hash of all the events (for the determinism test).
u64 RunPairsExact(u64 seed, SpatialSetConfig set_config) {
    Rng rng(seed);
    PairScene scene(PairMatrix(0.0f), set_config);
    PairTracker reference(PairMatrix(0.0f));
    Vector<KeyPair> before;
    Vector<KeyPair> now;
    Vector<KeyPair> held;
    Vector<KeyPair> want_begin;
    Vector<KeyPair> want_end;
    Vector<KeyPair> contacts;
    PairEvents events;
    PairEvents ref_events;
    u64 hash = 0xCBF29CE484222325ull;
    for (u32 frame = 0; frame < 2000; ++frame) {
        scene.Frame(rng);
        scene.tracker.Update(scene.set, &events);
        BrutePairs(scene.items, scene.config, 0.0f, &now);
        held.Clear();
        scene.tracker.ForEachPair([&](const KeyPair& p) { held.Push(p); });
        REQUIRE(PairSame(held, now));
        PairDifference(now, before, &want_begin);
        PairDifference(before, now, &want_end);
        REQUIRE(PairSame(events.begin, want_begin));
        REQUIRE(PairSame(events.end, want_end));
        PairApplyToContacts(events, &contacts);
        REQUIRE(PairSame(contacts, now));
        if (frame % 100 == 99) { // the full recompute agrees
            reference.Recompute(scene.set, &ref_events);
            held.Clear();
            reference.ForEachPair([&](const KeyPair& p) { held.Push(p); });
            REQUIRE(PairSame(held, now));
        }
        for (usize i = 0; i < events.begin.Size(); ++i) hash = PairMix(PairMix(hash, events.begin[i].a), events.begin[i].b);
        for (usize i = 0; i < events.end.Size(); ++i) hash = PairMix(PairMix(hash, ~events.end[i].a), events.end[i].b);
        before.Clear();
        for (usize i = 0; i < now.Size(); ++i) before.Push(now[i]);
    }
    return hash;
}

} // namespace

TEST_CASE("PairTracker: the incremental pairs equal a brute force after every frame, and Begin and End are the set difference", "[spatial][pairs]") {
    RunPairsExact(243001, SpatialSetConfig());
}

TEST_CASE("PairTracker: the same inputs give the same events, with a tree and with a grid", "[spatial][pairs]") {
    CHECK(RunPairsExact(243002, SpatialSetConfig()) == RunPairsExact(243002, SpatialSetConfig()));
    SpatialSetConfig grid;
    grid.dynamic = DynamicKind::Grid;
    grid.grid_cell_size = 4.0f;
    // The set's structure does not change which pairs exist or the order the events come out in.
    CHECK(RunPairsExact(243003, SpatialSetConfig()) == RunPairsExact(243003, grid));
}

TEST_CASE("PairTracker: with a margin the pairs sit between the touching pairs and the near_pairs pairs", "[spatial][pairs]") {
    Rng rng(243004);
    const f32 margin = 0.25f;
    PairScene scene(PairMatrix(margin));
    Vector<KeyPair> touching;
    Vector<KeyPair> near_pairs;
    Vector<KeyPair> held;
    Vector<KeyPair> before;
    Vector<KeyPair> want;
    Vector<KeyPair> contacts;
    PairEvents events;
    u64 kept_apart = 0;
    for (u32 frame = 0; frame < 2000; ++frame) {
        scene.Frame(rng);
        scene.tracker.Update(scene.set, &events);
        BrutePairs(scene.items, scene.config, 0.0f, &touching);
        BrutePairs(scene.items, scene.config, margin, &near_pairs);
        held.Clear();
        scene.tracker.ForEachPair([&](const KeyPair& p) { held.Push(p); });
        for (usize i = 0; i < touching.Size(); ++i) REQUIRE(PairHas(held, touching[i].a, touching[i].b));
        for (usize i = 0; i < held.Size(); ++i) REQUIRE(PairHas(near_pairs, held[i].a, held[i].b));
        kept_apart += held.Size() - touching.Size();
        PairDifference(held, before, &want);
        REQUIRE(PairSame(events.begin, want));
        PairDifference(before, held, &want);
        REQUIRE(PairSame(events.end, want));
        PairApplyToContacts(events, &contacts);
        REQUIRE(PairSame(contacts, held));
        before.Clear();
        for (usize i = 0; i < held.Size(); ++i) before.Push(held[i]);
    }
    CHECK(kept_apart > 0); // the margin did keep pairs that no longer touch, or the test proved nothing
}

TEST_CASE("PairTracker: two static items never pair, a static and a dynamic one do", "[spatial][pairs]") {
    SpatialSet set;
    PairTracker tracker;
    PairEvents events;
    const SetHandle a = set.AddStatic(BoxAt(Vec3(0, 0, 0), Vec3(1, 1, 1)), 1, 0);
    const SetHandle b = set.AddStatic(BoxAt(Vec3(0.5f, 0, 0), Vec3(1, 1, 1)), 2, 0);
    tracker.Touch(a);
    tracker.Touch(b);
    tracker.Update(set, &events);
    CHECK(events.begin.Size() == 0);
    CHECK(tracker.PairCount() == 0);
    const SetHandle c = set.AddDynamic(BoxAt(Vec3(0.2f, 0, 0), Vec3(1, 1, 1)), 3, 0);
    tracker.Touch(c);
    tracker.Update(set, &events);
    REQUIRE(events.begin.Size() == 2);
    CHECK(events.begin[0].a == 1);
    CHECK(events.begin[0].b == 3);
    CHECK(events.begin[1].a == 2);
    CHECK(events.begin[1].b == 3);
    // Making the dynamic one static ends both pairs: they cannot start to touch.
    set.MakeStatic(c);
    tracker.Touch(c);
    tracker.Update(set, &events);
    CHECK(events.end.Size() == 2);
    CHECK(tracker.PairCount() == 0);
}

TEST_CASE("PairTracker: removing an item ends its pairs in the same update, and a new key inherits nothing", "[spatial][pairs]") {
    SpatialSet set;
    PairTrackerConfig config;
    config.margin = 0.0f;
    PairTracker tracker(config);
    PairEvents events;
    const SetHandle a = set.AddDynamic(BoxAt(Vec3(0, 0, 0), Vec3(1, 1, 1)), 10, 0);
    const SetHandle b = set.AddDynamic(BoxAt(Vec3(0.5f, 0, 0), Vec3(1, 1, 1)), 20, 0);
    tracker.Touch(a);
    tracker.Touch(b);
    tracker.Update(set, &events);
    REQUIRE(events.begin.Size() == 1);
    CHECK(tracker.Has(10, 20));
    CHECK(tracker.Has(20, 10));
    tracker.Remove(20);
    set.Remove(b);
    tracker.Update(set, &events);
    REQUIRE(events.end.Size() == 1);
    CHECK(events.end[0].a == 10);
    CHECK(events.end[0].b == 20);
    CHECK(events.begin.Size() == 0);
    // A new item at the same place has a new key: a new pair, nothing inherited.
    const SetHandle c = set.AddDynamic(BoxAt(Vec3(0.5f, 0, 0), Vec3(1, 1, 1)), 30, 0);
    tracker.Touch(c);
    tracker.Update(set, &events);
    REQUIRE(events.begin.Size() == 1);
    CHECK(events.begin[0].b == 30);
    CHECK(events.end.Size() == 0);
    // The same key removed and added back between two updates, still overlapping: the pair never went away, so no events.
    tracker.Remove(30);
    set.Remove(c);
    const SetHandle d = set.AddDynamic(BoxAt(Vec3(0.5f, 0, 0), Vec3(1, 1, 1)), 30, 0);
    tracker.Touch(d);
    tracker.Update(set, &events);
    CHECK(events.begin.Size() == 0);
    CHECK(events.end.Size() == 0);
    CHECK(tracker.Has(10, 30));
    // Removing a key whose item and partner are both gone, and a stale touch, are harmless.
    tracker.Remove(10);
    tracker.Remove(30);
    tracker.Touch(a);
    set.Remove(a);
    set.Remove(d);
    tracker.Update(set, &events);
    CHECK(events.end.Size() == 1);
    CHECK(tracker.PairCount() == 0);
}

TEST_CASE("PairTracker: the layer matrix decides which pairs exist", "[spatial][pairs]") {
    SpatialSet set;
    PairTrackerConfig config = PairMatrix(0.0f);
    PairTracker tracker(config);
    PairEvents events;
    // Layers 1 and 1 do not meet; 0 and 2 do not meet; 0 and 1, 1 and 2, 0 and 0, 2 and 2 do.
    Array<u32, 5> layers;
    layers[0] = 0;
    layers[1] = 1;
    layers[2] = 1;
    layers[3] = 2;
    layers[4] = 2;
    for (u32 i = 0; i < 5; ++i) tracker.Touch(set.AddDynamic(BoxAt(Vec3(0.1f * static_cast<f32>(i), 0, 0), Vec3(1, 1, 1)), i + 1, layers[i]));
    tracker.Update(set, &events);
    u32 expected = 0;
    for (u32 i = 0; i < 5; ++i) {
        for (u32 j = i + 1; j < 5; ++j) expected += config.Meets(layers[i], layers[j]) ? 1u : 0u;
    }
    CHECK(events.begin.Size() == expected);
    for (usize i = 0; i < events.begin.Size(); ++i) CHECK(config.Meets(layers[events.begin[i].a - 1], layers[events.begin[i].b - 1]));
    CHECK_FALSE(config.Meets(40, 0)); // a layer of 32 or more meets nothing
}

TEST_CASE("PairTracker: 100,000 dynamic objects with 1 percent moving a frame (recorded, not gated)", "[spatial][pairs][benchmark]") {
    Rng rng(243400);
    constexpr u32 kObjects = 100000;
    SpatialSet set;
    PairTrackerConfig config;
    config.margin = 0.1f;
    PairTracker tracker(config);
    PairEvents events;
    Vector<SetHandle> handles;
    Vector<Aabb> boxes;
    for (u32 i = 0; i < kObjects; ++i) {
        const Aabb box = BoxAt(Vec3(rng.Float(0, 1000), 1, rng.Float(0, 1000)), Vec3(0.5f, 1, 0.5f));
        boxes.Push(box);
        handles.Push(set.AddDynamic(box, i + 1, 0));
        tracker.Touch(handles[i]);
    }
    u64 start = os::MonotonicTimeNanos();
    tracker.Update(set, &events);
    const f64 first_ms = static_cast<f64>(os::MonotonicTimeNanos() - start) / 1e6;
    u64 frame_ns = 0;
    u64 begun = 0;
    u64 ended = 0;
    for (u32 frame = 0; frame < 20; ++frame) {
        for (u32 k = 0; k < kObjects / 100; ++k) {
            const u32 i = rng.Below(kObjects);
            boxes[i] = BoxAt(boxes[i].Center() + Vec3(rng.Float(-1, 1), 0, rng.Float(-1, 1)), boxes[i].HalfExtents());
            set.Move(handles[i], boxes[i]);
            tracker.Touch(handles[i]);
        }
        start = os::MonotonicTimeNanos();
        tracker.Update(set, &events);
        frame_ns += os::MonotonicTimeNanos() - start;
        begun += events.begin.Size();
        ended += events.end.Size();
    }
    WARN("100,000 dynamic objects: first update (all touched) " << first_ms << " ms, " << tracker.PairCount() << " pairs; steady frame with 1% moving "
                                                                << static_cast<f64>(frame_ns) / 20.0 / 1e6 << " ms (" << begun / 20 << " begun, " << ended / 20 << " ended per frame)");
    // Repeated moves of one object within a frame are one touch.
    CHECK(tracker.Stats().touched <= kObjects + 20u * (kObjects / 100));
    CHECK(tracker.Stats().touched > kObjects + 19u * (kObjects / 100));
}

TEST_CASE("PairTracker: after a Recompute an Update still finds partners with large margins", "[spatial][pairs]") {
    // A is large (margin 2.5), B small (margin 0.25). B drifts to a gap of 1.0 from A: too far for the touched item's own margin, within A's.
    SpatialSetConfig tight_tree;
    tight_tree.bvh.fat_margin = 0.0f; // so the tree's own boxes do not reach B and hide the tracker's bound
    tight_tree.bvh.predictive = 0.0f;
    SpatialSet set(tight_tree);
    PairTrackerConfig config;
    config.margin = 0.25f;
    PairTracker tracker(config);
    PairEvents events;
    set.AddDynamic(BoxAt(Vec3(0, 0, 0), Vec3(5, 5, 5)), 1, 0);
    for (u32 i = 0; i < 16; ++i) set.AddDynamic(BoxAt(Vec3(100.0f + 10.0f * static_cast<f32>(i), 0, 50), Vec3(0.5f, 0.5f, 0.5f)), 100 + i, 0); // so the tree has structure
    const SetHandle b = set.AddDynamic(BoxAt(Vec3(5.5f, 0, 0), Vec3(0.5f, 0.5f, 0.5f)), 2, 0);
    tracker.Recompute(set, &events);
    REQUIRE(events.begin.Size() == 1);
    set.Move(b, BoxAt(Vec3(7.0f, 0, 0), Vec3(0.5f, 0.5f, 0.5f))); // its left face is 1.5 from A's right face at 5: gap 1.5? A.hi=5, B.lo=6.5
    tracker.Touch(b);
    tracker.Update(set, &events);
    CHECK(events.end.Size() == 0);
    CHECK(tracker.Has(1, 2));
}

TEST_CASE("PairTracker: a pair stays while the boxes are apart by less than the margin and ends at it", "[spatial][pairs]") {
    SpatialSet set;
    PairTrackerConfig config;
    config.margin = 0.25f; // boxes of extent 2: a margin of 0.5 each, so the pair ends at a gap of 1.0
    PairTracker tracker(config);
    PairEvents events;
    const SetHandle a = set.AddDynamic(BoxAt(Vec3(0, 0, 0), Vec3(1, 1, 1)), 1, 0);
    const SetHandle b = set.AddDynamic(BoxAt(Vec3(1.5f, 0, 0), Vec3(1, 1, 1)), 2, 0);
    tracker.Touch(a);
    tracker.Touch(b);
    tracker.Update(set, &events);
    CHECK(events.begin.Size() == 1);
    set.Move(b, BoxAt(Vec3(2.9f, 0, 0), Vec3(1, 1, 1))); // gap 0.9: apart, within the margins
    tracker.Touch(b);
    tracker.Update(set, &events);
    CHECK(events.end.Size() == 0);
    CHECK(tracker.Has(1, 2));
    set.Move(b, BoxAt(Vec3(3.1f, 0, 0), Vec3(1, 1, 1))); // gap 1.1: past both margins
    tracker.Touch(b);
    tracker.Update(set, &events);
    CHECK(events.end.Size() == 1);
    CHECK_FALSE(tracker.Has(1, 2));
    set.Move(b, BoxAt(Vec3(2.9f, 0, 0), Vec3(1, 1, 1))); // back within the margins but not touching: no pair is made
    tracker.Touch(b);
    tracker.Update(set, &events);
    CHECK(events.begin.Size() == 0);
}
