// The instance set (Phase 28 Day 495): random edits against a brute-force model, exact chunk bounds, stable and
// generation-checked ids, capacity and bad values refused by status, Rebuild, determinism, and a set far from the
// origin.

#include <catch2/catch_test_macros.hpp>

#include "algorithm.h"
#include "containers/vector.h"
#include "lang/limits.h"
#include "renderer/instance_scatter.h"
#include "renderer/instance_set.h"
#include "soak.h"

namespace vse::renderer
{

namespace
{

namespace instance_set_test
{

// A small deterministic generator (xorshift64*), so a run is the same everywhere.
struct Rng
{
    u64 s;
    explicit Rng(u64 seed) : s(seed * 2685821657736338717ull + 88172645463325252ull) {}
    u32 Next()
    {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
        return static_cast<u32>((s * 2685821657736338717ull) >> 32);
    }
    u32 Below(u32 n) { return Next() % n; }
    // A multiple of 1/64 in [lo, hi): exactly representable, so a position survives the offset round trip.
    f64 Grid(f64 lo, f64 hi)
    {
        const u32 steps = static_cast<u32>((hi - lo) * 64.0);
        return lo + static_cast<f64>(Below(steps)) / 64.0;
    }
};

struct Model
{
    InstanceId id;
    InstanceTransform t;
    InstanceExtra extra;
    bool alive = false;
};

InstanceTransform RandomTransform(Rng& rng, f64 lo, f64 hi, f64 base = 0.0)
{
    InstanceTransform t;
    t.position = Vec3d(base + rng.Grid(lo, hi), rng.Grid(0.0, 40.0), base + rng.Grid(lo, hi));
    t.rotation = Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
    t.rotation.y = static_cast<f32>(rng.Below(64)) / 128.0f;
    t.scale = Vec3(0.5f + static_cast<f32>(rng.Below(16)) / 8.0f, 1.0f, 0.5f + static_cast<f32>(rng.Below(8)) / 4.0f);
    return t;
}

bool SameTransform(const InstanceTransform& a, const InstanceTransform& b)
{
    return a.position.x == b.position.x && a.position.y == b.position.y && a.position.z == b.position.z &&
           a.rotation.x == b.rotation.x && a.rotation.y == b.rotation.y && a.rotation.z == b.rotation.z &&
           a.rotation.w == b.rotation.w && a.scale.x == b.scale.x && a.scale.y == b.scale.y && a.scale.z == b.scale.z;
}

// Everything the set must satisfy against the model; returns the first failure as a message index (0 = fine).
void CheckAgainstModel(const InstanceSet& set, const Vector<Model>& model)
{
    u32 alive = 0;
    for (usize i = 0; i < model.Size(); ++i)
    {
        const Model& m = model[i];
        if (!m.alive)
        {
            REQUIRE(!set.Contains(m.id));
            continue;
        }
        ++alive;
        InstanceTransform got;
        REQUIRE(set.Get(m.id, &got) == InstanceStatus::Ok);
        REQUIRE(SameTransform(got, m.t));
        if (set.HasData())
        {
            InstanceExtra e;
            REQUIRE(set.GetData(m.id, &e) == InstanceStatus::Ok);
            REQUIRE(e.custom == m.extra.custom);
            REQUIRE(e.tint.x == m.extra.tint.x);
        }
    }
    REQUIRE(set.Count() == alive);

    u32 in_chunks = 0;
    for (u32 c = 0; c < set.ChunkCount(); ++c)
    {
        const InstanceChunkInfo info = set.Chunk(c);
        REQUIRE(info.count <= kInstanceChunkSize);
        REQUIRE(info.first == c * kInstanceChunkSize);
        const Span<const InstanceRecord> records = set.Records(c);
        REQUIRE(records.Size() == info.count);
        in_chunks += info.count;
        if (info.count == 0)
        {
            REQUIRE(info.box.IsEmpty());
            continue;
        }
        Vec3 lo = records[0].position;
        Vec3 hi = records[0].position;
        f32 max_scale = 0.0f;
        for (usize i = 0; i < records.Size(); ++i)
        {
            const Vec3& p = records[i].position;
            lo = Vec3(Min(lo.x, p.x), Min(lo.y, p.y), Min(lo.z, p.z));
            hi = Vec3(Max(hi.x, p.x), Max(hi.y, p.y), Max(hi.z, p.z));
            max_scale = Max(max_scale, Max(records[i].scale.x, Max(records[i].scale.y, records[i].scale.z)));
        }
        REQUIRE(info.box.min.x == lo.x);
        REQUIRE(info.box.min.y == lo.y);
        REQUIRE(info.box.min.z == lo.z);
        REQUIRE(info.box.max.x == hi.x);
        REQUIRE(info.box.max.y == hi.y);
        REQUIRE(info.box.max.z == hi.z);
        REQUIRE(info.max_scale == max_scale);
    }
    REQUIRE(in_chunks == alive);
}

// Applies `ops` random edits to the set and the model, checking after each batch.
void RandomEdits(InstanceSet& set, Vector<Model>& model, Rng& rng, u32 ops, u32 batch, f64 span, bool check)
{
    for (u32 n = 0; n < ops; ++n)
    {
        const u32 what = rng.Below(100);
        if (what < 55 || model.Empty())
        {
            Model m;
            m.t = RandomTransform(rng, -span, span);
            m.extra.tint = Vec4(static_cast<f32>(rng.Below(8)), 0.5f, 0.25f, 1.0f);
            m.extra.custom = rng.Next();
            REQUIRE(set.Add(m.t, set.HasData() ? &m.extra : nullptr, &m.id) == InstanceStatus::Ok);
            m.alive = true;
            model.Push(m);
        }
        else
        {
            Model& m = model[rng.Below(static_cast<u32>(model.Size()))];
            if (!m.alive)
            {
                continue;
            }
            if (what < 80)
            {
                REQUIRE(set.Remove(m.id) == InstanceStatus::Ok);
                m.alive = false;
            }
            else if (what < 95)
            {
                // A small step most of the time, now and then a jump across the field.
                InstanceTransform t = rng.Below(10) == 0 ? RandomTransform(rng, -span, span) : m.t;
                if (t.position.x == m.t.position.x)
                {
                    t.position.x += rng.Grid(-2.0, 2.0);
                    t.position.z += rng.Grid(-2.0, 2.0);
                    t.scale = m.t.scale;
                }
                REQUIRE(set.SetTransform(m.id, t) == InstanceStatus::Ok);
                m.t = t;
            }
            else if (set.HasData())
            {
                m.extra.custom = rng.Next();
                m.extra.tint.x = static_cast<f32>(rng.Below(8));
                REQUIRE(set.SetData(m.id, m.extra) == InstanceStatus::Ok);
            }
        }
        if (check && (n + 1) % batch == 0)
        {
            CheckAgainstModel(set, model);
        }
    }
}

InstanceSetPtr MakeSet(bool data, u32 max = kMaxInstances)
{
    InstanceSetDesc d;
    d.has_data = data;
    d.max_instances = max;
    InstanceSetPtr set = InstanceSet::Create(d);
    REQUIRE(set.Get() != nullptr);
    return set;
}

}  // namespace instance_set_test

}  // namespace

using namespace instance_set_test;

TEST_CASE("The record is 40 bytes and a chunk holds 256", "[instance_set]")
{
    REQUIRE(sizeof(InstanceRecord) == 40);
    REQUIRE(kInstanceChunkSize == 256);
    REQUIRE(kMaxInstances == (1u << 24));
}

TEST_CASE("A set refuses a description that cannot work", "[instance_set]")
{
    InstanceSetDesc d;
    d.max_instances = 0;
    REQUIRE(InstanceSet::Create(d).Get() == nullptr);
    d = InstanceSetDesc();
    d.max_instances = kMaxInstances + 1;
    REQUIRE(InstanceSet::Create(d).Get() == nullptr);
    d = InstanceSetDesc();
    d.key_cell_size = 0.0;
    REQUIRE(InstanceSet::Create(d).Get() == nullptr);
    d = InstanceSetDesc();
    d.move_threshold = -1.0f;
    REQUIRE(InstanceSet::Create(d).Get() == nullptr);
}

TEST_CASE("100,000 random adds, removes and moves agree with a brute-force model after every batch", "[instance_set]")
{
    for (int with_data = 0; with_data < 2; ++with_data)
    {
        InstanceSetPtr set = MakeSet(with_data != 0);
        Vector<Model> model;
        Rng rng(7 + with_data);
        RandomEdits(*set, model, rng, 50000, 2500, 300.0, true);
        CheckAgainstModel(*set, model);
    }
}

TEST_CASE("An id stays valid while other instances are removed, and a stale id is refused", "[instance_set]")
{
    InstanceSetPtr set = MakeSet(true);
    Rng rng(3);
    Vector<Model> model;
    for (int i = 0; i < 2000; ++i)
    {
        Model m;
        m.t = RandomTransform(rng, -50.0, 50.0);
        REQUIRE(set->Add(m.t, &m.extra, &m.id) == InstanceStatus::Ok);
        m.alive = true;
        model.Push(m);
    }
    // Remove every other one, then every survivor still reads back.
    for (usize i = 0; i < model.Size(); i += 2)
    {
        REQUIRE(set->Remove(model[i].id) == InstanceStatus::Ok);
        model[i].alive = false;
    }
    CheckAgainstModel(*set, model);
    const InstanceId stale = model[1998].id;  // the last one removed: the free list is last in, first out
    InstanceTransform t;
    InstanceExtra e;
    REQUIRE(set->Remove(stale) == InstanceStatus::StaleId);
    REQUIRE(set->SetTransform(stale, model[1].t) == InstanceStatus::StaleId);
    REQUIRE(set->SetData(stale, e) == InstanceStatus::StaleId);
    REQUIRE(set->Get(stale, &t) == InstanceStatus::StaleId);
    REQUIRE(set->GetData(stale, &e) == InstanceStatus::StaleId);
    // The freed table entry is reused with a new generation: the old id stays stale.
    InstanceId fresh;
    REQUIRE(set->Add(model[1].t, nullptr, &fresh) == InstanceStatus::Ok);
    REQUIRE(fresh.index == stale.index);
    REQUIRE(fresh.generation != stale.generation);
    REQUIRE(!set->Contains(stale));
    REQUIRE(set->Contains(fresh));
    // An id from nowhere is stale too.
    InstanceId bogus;
    REQUIRE(set->Remove(bogus) == InstanceStatus::StaleId);
    // Clear stales everything and the set is usable again.
    set->Clear();
    REQUIRE(set->Count() == 0);
    REQUIRE(!set->Contains(fresh));
    REQUIRE(!set->Contains(model[1].id));
    REQUIRE(set->Add(model[1].t, nullptr, &fresh) == InstanceStatus::Ok);
    REQUIRE(set->Count() == 1);
}

TEST_CASE("The capacity and bad values are refused with a status and change nothing", "[instance_set]")
{
    InstanceSetPtr set = MakeSet(false, 300);
    Rng rng(5);
    Vector<InstanceId> ids;
    for (int i = 0; i < 300; ++i)
    {
        InstanceId id;
        REQUIRE(set->Add(RandomTransform(rng, -10.0, 10.0), nullptr, &id) == InstanceStatus::Ok);
        ids.Push(id);
    }
    REQUIRE(set->Add(RandomTransform(rng, -10.0, 10.0), nullptr, nullptr) == InstanceStatus::Full);
    REQUIRE(set->Count() == 300);
    REQUIRE(set->Remove(ids[10]) == InstanceStatus::Ok);
    REQUIRE(set->Add(RandomTransform(rng, -10.0, 10.0), nullptr, nullptr) == InstanceStatus::Ok);

    const u64 version = set->Version();
    InstanceTransform bad = RandomTransform(rng, -10.0, 10.0);
    bad.position.x = Limits<f64>::QuietNaN();
    InstanceSetPtr roomy = MakeSet(false);
    REQUIRE(roomy->Add(bad, nullptr, nullptr) == InstanceStatus::InvalidValue);
    bad = RandomTransform(rng, -10.0, 10.0);
    bad.position.y = 2.0 * kMaxInstanceCoordinate;
    REQUIRE(roomy->Add(bad, nullptr, nullptr) == InstanceStatus::InvalidValue);
    bad = RandomTransform(rng, -10.0, 10.0);
    bad.scale.y = 0.0f;
    REQUIRE(roomy->Add(bad, nullptr, nullptr) == InstanceStatus::InvalidValue);
    bad = RandomTransform(rng, -10.0, 10.0);
    bad.rotation.w = Limits<f32>::Infinity();
    REQUIRE(roomy->Add(bad, nullptr, nullptr) == InstanceStatus::InvalidValue);
    REQUIRE(roomy->Count() == 0);
    REQUIRE(roomy->ChunkCount() == 0);
    // A bad move leaves the instance where it was.
    InstanceId id;
    const InstanceTransform ok = RandomTransform(rng, -10.0, 10.0);
    REQUIRE(roomy->Add(ok, nullptr, &id) == InstanceStatus::Ok);
    REQUIRE(roomy->SetTransform(id, bad) == InstanceStatus::InvalidValue);
    InstanceTransform got;
    REQUIRE(roomy->Get(id, &got) == InstanceStatus::Ok);
    REQUIRE(SameTransform(got, ok));
    REQUIRE(set->Version() == version);
}

TEST_CASE("A set without data refuses data calls and a set with data holds them", "[instance_set]")
{
    InstanceSetPtr plain = MakeSet(false);
    InstanceId id;
    Rng rng(9);
    REQUIRE(plain->Add(RandomTransform(rng, -5.0, 5.0), nullptr, &id) == InstanceStatus::Ok);
    InstanceExtra e;
    REQUIRE(plain->SetData(id, e) == InstanceStatus::NoData);
    REQUIRE(plain->GetData(id, &e) == InstanceStatus::NoData);
    REQUIRE(plain->Extras(0).Size() == 0);

    InstanceSetPtr rich = MakeSet(true);
    e.tint = Vec4(0.25f, 0.5f, 0.75f, 1.0f);
    e.custom = 42;
    REQUIRE(rich->Add(RandomTransform(rng, -5.0, 5.0), &e, &id) == InstanceStatus::Ok);
    InstanceExtra back;
    REQUIRE(rich->GetData(id, &back) == InstanceStatus::Ok);
    REQUIRE(back.custom == 42);
    REQUIRE(rich->Extras(0).Size() == 1);
    e.tint.x = Limits<f32>::QuietNaN();
    REQUIRE(rich->SetData(id, e) == InstanceStatus::InvalidValue);
    REQUIRE(rich->GetData(id, &back) == InstanceStatus::Ok);
    REQUIRE(back.tint.x == 0.25f);
}

TEST_CASE("An edit stamps only the chunks it changed", "[instance_set]")
{
    InstanceSetPtr set = MakeSet(true);
    Rng rng(11);
    Vector<InstanceId> ids;
    for (int i = 0; i < 3000; ++i)
    {
        InstanceId id;
        REQUIRE(set->Add(RandomTransform(rng, -200.0, 200.0), nullptr, &id) == InstanceStatus::Ok);
        ids.Push(id);
    }
    Vector<u64> before;
    for (u32 c = 0; c < set->ChunkCount(); ++c)
    {
        before.Push(set->Chunk(c).version);
    }
    InstanceExtra e;
    e.custom = 7;
    REQUIRE(set->SetData(ids[100], e) == InstanceStatus::Ok);
    u32 changed = 0;
    for (u32 c = 0; c < set->ChunkCount(); ++c)
    {
        if (set->Chunk(c).version != before[c])
        {
            ++changed;
            REQUIRE(set->Chunk(c).version == set->Version());
        }
    }
    REQUIRE(changed == 1);
}

TEST_CASE("Rebuild keeps every instance and id, does not worsen the chunk volumes, and fills the chunks",
          "[instance_set]")
{
    InstanceSetPtr set = MakeSet(true);
    Vector<Model> model;
    Rng rng(21);
    RandomEdits(*set, model, rng, 30000, 30000, 400.0, false);
    CheckAgainstModel(*set, model);
    const f64 before = set->ChunkVolumeSum();
    Vector<u64> versions;
    for (u32 c = 0; c < set->ChunkCount(); ++c)
    {
        versions.Push(set->Chunk(c).version);
    }
    set->Rebuild();
    CheckAgainstModel(*set, model);
    REQUIRE(set->ChunkVolumeSum() <= before);
    // Every chunk but the last is full, and every version is new.
    u32 non_full = 0;
    for (u32 c = 0; c < set->ChunkCount(); ++c)
    {
        non_full += set->Chunk(c).count < kInstanceChunkSize ? 1 : 0;
        if (c < versions.Size())
        {
            REQUIRE(set->Chunk(c).version > versions[c]);
        }
    }
    REQUIRE(non_full <= 1);
    // Rebuilding a rebuilt set changes nothing in the layout.
    Vector<InstanceRecord> first;
    for (u32 c = 0; c < set->ChunkCount(); ++c)
    {
        for (usize i = 0; i < set->Records(c).Size(); ++i)
        {
            first.Push(set->Records(c)[i]);
        }
    }
    set->Rebuild();
    usize at = 0;
    for (u32 c = 0; c < set->ChunkCount(); ++c)
    {
        for (usize i = 0; i < set->Records(c).Size(); ++i, ++at)
        {
            REQUIRE(first[at].position.x == set->Records(c)[i].position.x);
            REQUIRE(first[at].position.z == set->Records(c)[i].position.z);
        }
    }
    REQUIRE(at == first.Size());
}

TEST_CASE("The same operations give the same chunk layout", "[instance_set]")
{
    InstanceSetPtr a = MakeSet(true);
    InstanceSetPtr b = MakeSet(true);
    Vector<Model> ma;
    Vector<Model> mb;
    Rng ra(33);
    Rng rb(33);
    RandomEdits(*a, ma, ra, 20000, 20000, 250.0, false);
    RandomEdits(*b, mb, rb, 20000, 20000, 250.0, false);
    for (int pass = 0; pass < 2; ++pass)
    {
        REQUIRE(a->ChunkCount() == b->ChunkCount());
        for (u32 c = 0; c < a->ChunkCount(); ++c)
        {
            const InstanceChunkInfo x = a->Chunk(c);
            const InstanceChunkInfo y = b->Chunk(c);
            REQUIRE(x.count == y.count);
            REQUIRE(x.version == y.version);
            REQUIRE(x.origin.x == y.origin.x);
            REQUIRE(x.box.min.x == y.box.min.x);
            REQUIRE(x.box.max.z == y.box.max.z);
            for (usize i = 0; i < a->Records(c).Size(); ++i)
            {
                REQUIRE(a->Records(c)[i].position.x == b->Records(c)[i].position.x);
                REQUIRE(a->Records(c)[i].position.y == b->Records(c)[i].position.y);
                REQUIRE(a->Records(c)[i].rotation.y == b->Records(c)[i].rotation.y);
            }
        }
        a->Rebuild();
        b->Rebuild();
    }
}

TEST_CASE("A set 10,000 km from the origin keeps exact offsets and bounds", "[instance_set]")
{
    const f64 base = 1.0e7;
    InstanceSetPtr set = MakeSet(false);
    Vector<Model> model;
    Rng rng(41);
    for (int i = 0; i < 5000; ++i)
    {
        Model m;
        m.t = RandomTransform(rng, -40.0, 40.0, base);
        REQUIRE(set->Add(m.t, nullptr, &m.id) == InstanceStatus::Ok);
        m.alive = true;
        model.Push(m);
    }
    CheckAgainstModel(*set, model);
    for (u32 c = 0; c < set->ChunkCount(); ++c)
    {
        const InstanceChunkInfo info = set->Chunk(c);
        // The offsets are small numbers, the origin carries the distance.
        REQUIRE(info.origin.x > base - 100.0);
        REQUIRE(info.origin.x < base + 100.0);
        REQUIRE(info.box.max.x < 200.0f);
        REQUIRE(info.box.min.x > -200.0f);
        // The box is exactly the instances' world extent minus the origin.
        f64 lo = info.origin.x + static_cast<f64>(info.box.min.x);
        f64 hi = info.origin.x + static_cast<f64>(info.box.max.x);
        f64 want_lo = 1.0e30;
        f64 want_hi = -1.0e30;
        for (usize i = 0; i < model.Size(); ++i)
        {
            InstanceTransform t;
            REQUIRE(set->Get(model[i].id, &t) == InstanceStatus::Ok);
            (void)t;
        }
        for (usize i = 0; i < set->Records(c).Size(); ++i)
        {
            const f64 w = info.origin.x + static_cast<f64>(set->Records(c)[i].position.x);
            want_lo = Min(want_lo, w);
            want_hi = Max(want_hi, w);
        }
        REQUIRE(lo == want_lo);
        REQUIRE(hi == want_hi);
    }
    set->Rebuild();
    CheckAgainstModel(*set, model);
}

TEST_CASE("An instance set holds and edits a million instances and agrees with a model", "[instance_set]")
{
    const u32 total = test_support::SoakCount(50000, 1000000);
    InstanceSetPtr set = MakeSet(true);
    Vector<Model> model;
    model.Reserve(total);
    Rng rng(77);
    for (u32 i = 0; i < total; ++i)
    {
        Model m;
        m.t = RandomTransform(rng, -2000.0, 2000.0);
        m.extra.custom = i;
        REQUIRE(set->Add(m.t, &m.extra, &m.id) == InstanceStatus::Ok);
        m.alive = true;
        model.Push(m);
    }
    REQUIRE(set->Count() == total);
    // Remove a tenth, move a tenth, rebuild, and compare every survivor.
    for (u32 i = 0; i < total; i += 10)
    {
        REQUIRE(set->Remove(model[i].id) == InstanceStatus::Ok);
        model[i].alive = false;
    }
    for (u32 i = 5; i < total; i += 10)
    {
        InstanceTransform t = model[i].t;
        t.position.x += rng.Grid(-30.0, 30.0);
        REQUIRE(set->SetTransform(model[i].id, t) == InstanceStatus::Ok);
        model[i].t = t;
    }
    CheckAgainstModel(*set, model);
    set->Rebuild();
    CheckAgainstModel(*set, model);
    REQUIRE(set->MemoryBytes() / static_cast<usize>(set->Count()) < 160);
}

TEST_CASE("Instance set: a scatter with a hostile rule places nothing and returns at once", "[instance_set]")
{
    InstanceSetDesc desc;
    desc.max_instances = 1000;
    InstanceSetPtr set = InstanceSet::Create(desc);
    ScatterRule rule;
    rule.count = 0xFFFFFFFFu;  // four billion points to try
    rule.min_x = rule.min_z = -10.0;
    rule.max_x = rule.max_z = 10.0;
    CHECK(Scatter(set.Get(), rule, nullptr, nullptr, nullptr).status == InstanceStatus::InvalidValue);
    rule.count = 10;
    rule.max_x = 1.0e300;
    CHECK(Scatter(set.Get(), rule, nullptr, nullptr, nullptr).status == InstanceStatus::InvalidValue);
    CHECK(set->Count() == 0);
}

}  // namespace vse::renderer
