// Incremental saves (Phase 19 Day 261): a save session writes only what changed, and the store always equals a fresh
// full save.

#include <catch2/catch_test_macros.hpp>

#include "ecs/transform.h"
#include "os/console.h"
#include "os/os.h"
#include "storage/builtin_components.h"
#include "storage/component_record.h"
#include "storage/memory_store.h"
#include "storage/save_session.h"
#include "storage/world_storage.h"

namespace vse::storage
{
namespace
{

using ecs::Entity;
using ecs::World;

struct IncA
{
    i32 a = 0;
    f32 b = 0;
};
struct IncB
{
    String<HeapAllocator> name{HeapAllocator::Default()};
};
struct IncTag
{
};

Status RegisterIncComponents(ComponentRegistry* r)
{
    ENGINE_TRY(RegisterBuiltinComponents(r));
    ENGINE_TRY(
        r->Register<IncA>({.id = 3000, .name = "inc_a", .fields = {Field<&IncA::a>("a"), Field<&IncA::b>("b")}}));
    ENGINE_TRY(r->Register<IncB>({.id = 3001, .name = "inc_b", .fields = {Field<&IncB::name>("name")}}));
    ENGINE_TRY(r->Register<IncTag>({.id = 3002, .name = "inc_tag"}));
    return Status::Success();
}

struct IncRng
{
    u64 s;
    u32 Next()
    {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<u32>(s >> 33);
    }
};

// Counts the writes that reach the wrapped store.
class CountingStore final : public Store
{
public:
    explicit CountingStore(Store& inner) : inner_(&inner) {}
    const char* Name() const override { return "counting"; }
    Status Put(const EntityRecord& record) override
    {
        ++puts;
        return inner_->Put(record);
    }
    Status Get(EntityKey key, EntityRecord* out) const override { return inner_->Get(key, out); }
    Status Remove(EntityKey key) override
    {
        ++removes;
        return inner_->Remove(key);
    }
    Status Scan(const KeyRange& range, RecordVisitor& visitor) const override { return inner_->Scan(range, visitor); }
    Status Count(usize* out) const override { return inner_->Count(out); }
    Status Begin() override { return inner_->Begin(); }
    Status Commit() override { return inner_->Commit(); }
    Status Rollback() override { return inner_->Rollback(); }
    Status GetMetadata(StringView name, String<HeapAllocator>* out) const override
    {
        return inner_->GetMetadata(name, out);
    }
    Status SetMetadata(StringView name, StringView value) override { return inner_->SetMetadata(name, value); }
    usize puts = 0;
    usize removes = 0;

private:
    Store* inner_;
};

class Dump final : public RecordVisitor
{
public:
    bool Visit(const EntityRecord& record) override
    {
        records.Push(record.Clone());
        return true;
    }
    Vector<EntityRecord> records;
};

bool SameStores(const Store& a, const Store& b)
{
    Dump da, db;
    if (a.Scan(KeyRange(), da).Failed() || b.Scan(KeyRange(), db).Failed() || da.records.Size() != db.records.Size())
    {
        return false;
    }
    for (usize i = 0; i < da.records.Size(); ++i)
    {
        const EntityRecord& x = da.records[i];
        const EntityRecord& y = db.records[i];
        if (!(x.key == y.key) || x.hash != y.hash || x.blob.Size() != y.blob.Size())
        {
            return false;
        }
    }
    return true;
}

// A fresh full save of the world into an empty store, to compare with.
bool EqualsFullSave(const ComponentRegistry& registry, World& world, KeyIndex& index, const Store& incremental)
{
    MemoryStore fresh;
    WorldStorage storage(registry, fresh);
    if (storage.Save(world, index, SaveOptions(), nullptr).Failed())
    {
        return false;
    }
    return SameStores(incremental, fresh);
}

struct Node
{
    Entity e;
    u32 seq = 0;
    bool has_children = false;
};

// A world that a test mutates at random: components come and go, entities appear and vanish, parents change.
struct Churn
{
    World world;
    KeyIndex index;
    KeyGenerator gen = KeyGenerator::Seeded(77);
    IncRng rng;
    Vector<Node> nodes;
    u32 next_seq = 0;

    explicit Churn(u64 seed) : rng{seed} {}

    void Create()
    {
        const Entity e = world.Create(EntityKeyComponent{gen.Next()});
        REQUIRE(e.IsValid());
        nodes.Push(Node{e, next_seq++, false});
        Mutate(nodes.Size() - 1);
    }
    void Mutate(usize n)
    {
        const Entity e = nodes[n].e;
        switch (rng.Next() % 8)
        {
        case 0:
            if (world.Has<IncA>(e))
            {
                REQUIRE(world.Set(e, IncA{static_cast<i32>(rng.Next()), 1.5f}) == ecs::Status::Ok);
            }
            else
            {
                REQUIRE(world.Add(e, IncA{static_cast<i32>(rng.Next()), 0.5f}) == ecs::Status::Ok);
            }
            break;
        case 1:
            if (world.Has<IncA>(e))
            {
                REQUIRE(world.Remove<IncA>(e) == ecs::Status::Ok);
            }
            break;
        case 2:
        {
            IncB b;
            b.name.Append(static_cast<char>('a' + rng.Next() % 26));
            if (world.Has<IncB>(e))
            {
                REQUIRE(world.Set(e, Move(b)) == ecs::Status::Ok);
            }
            else
            {
                REQUIRE(world.Add(e, Move(b)) == ecs::Status::Ok);
            }
            break;
        }
        case 3:
            if (world.Has<IncTag>(e))
            {
                REQUIRE(world.Remove<IncTag>(e) == ecs::Status::Ok);
            }
            else
            {
                REQUIRE(world.Add(e, IncTag{}) == ecs::Status::Ok);
            }
            break;
        case 4:
        {  // (re)parent to an older entity
            if (n > 0)
            {
                u32 tries = 0;
                usize p = rng.Next() % nodes.Size();
                while (nodes[p].seq >= nodes[n].seq && tries++ < 8)
                {
                    p = rng.Next() % nodes.Size();
                }
                if (nodes[p].seq < nodes[n].seq)
                {
                    REQUIRE(world.Set(e, ecs::Parent{nodes[p].e}) == ecs::Status::Ok);
                    nodes[p].has_children = true;
                }
            }
            break;
        }
        case 5:
            if (world.Has<ecs::Parent>(e))
            {
                REQUIRE(world.Remove<ecs::Parent>(e) == ecs::Status::Ok);
            }
            break;
        case 6:
            if (world.Has<ecs::LocalTransform>(e))
            {
                REQUIRE(world.Remove<ecs::LocalTransform>(e) == ecs::Status::Ok);
            }
            else
            {
                REQUIRE(world.Add(e, ecs::LocalTransform()) == ecs::Status::Ok);
            }
            break;
        default: break;  // untouched
        }
    }
    void Frame()
    {
        const u32 ops = rng.Next() % 4;
        for (u32 i = 0; i < ops; ++i)
        {
            const u32 what = rng.Next() % 10;
            if (what < 2 || nodes.Empty())
            {
                Create();
            }
            else if (what == 2 && nodes.Size() > 5)
            {
                const usize n = rng.Next() % nodes.Size();
                if (!nodes[n].has_children)
                {
                    REQUIRE(world.Destroy(nodes[n].e) == ecs::Status::Ok);
                    nodes.RemoveRange(n, 1);
                }
            }
            else
            {
                Mutate(rng.Next() % nodes.Size());
            }
        }
        world.EndFrame();
    }
};

void RunChurn(u32 save_every, u32 frames, u64 seed, usize* reconciles)
{
    ComponentRegistry registry;
    REQUIRE(RegisterIncComponents(&registry).Ok());
    MemoryStore store;
    WorldStorage storage(registry, store);
    SaveSession session;
    Churn churn(seed);
    for (u32 i = 0; i < 30; ++i)
    {
        churn.Create();
    }
    SaveOptions options;
    options.mode = SaveMode::Incremental;
    options.session = &session;
    *reconciles = 0;
    for (u32 frame = 1; frame <= frames; ++frame)
    {
        churn.Frame();
        if (frame % save_every == 0)
        {
            Report report;
            REQUIRE(storage.Save(churn.world, churn.index, options, &report).Ok());
            *reconciles += report.reconciled;
            REQUIRE(EqualsFullSave(registry, churn.world, churn.index, store));
        }
    }
}

}  // namespace

TEST_CASE("Incremental save: 500 random frames, a save every 7th, equal a fresh full save each time",
          "[storage][incremental]")
{
    usize reconciles = 0;
    RunChurn(7, 500, 5, &reconciles);
    CHECK(reconciles > 0);  // the removal log holds two frames, so these saves fall back to comparing with the store
}

TEST_CASE("Incremental save: a save every frame follows the removal log and never compares the whole world",
          "[storage][incremental]")
{
    usize reconciles = 0;
    RunChurn(1, 500, 9, &reconciles);
    CHECK(reconciles == 1);  // the first save of the session only
}

TEST_CASE("Incremental save: a save every second frame is still equal to a full save", "[storage][incremental]")
{
    usize reconciles = 0;
    RunChurn(2, 300, 13, &reconciles);
    CHECK(reconciles > 0);
}

TEST_CASE("Incremental save: an idle world touches no record and visits no unchanged chunk", "[storage][incremental]")
{
    ComponentRegistry registry;
    REQUIRE(RegisterIncComponents(&registry).Ok());
    MemoryStore inner;
    CountingStore store(inner);
    WorldStorage storage(registry, store);
    SaveSession session;
    Churn churn(21);
    for (u32 i = 0; i < 2000; ++i)
    {
        churn.Create();
    }
    SaveOptions options;
    options.mode = SaveMode::Incremental;
    options.session = &session;
    Report first;
    REQUIRE(storage.Save(churn.world, churn.index, options, &first).Ok());
    CHECK(first.reconciled == 1);
    CHECK(first.created == 2000);
    CHECK(session.TrackedRecords() == 2000);

    store.puts = 0;
    store.removes = 0;
    for (u32 round = 0; round < 3; ++round)
    {
        churn.world.EndFrame();
        Report idle;
        REQUIRE(storage.Save(churn.world, churn.index, options, &idle).Ok());
        CHECK(idle.reconciled == 0);
        CHECK(idle.encoded == 0);
        CHECK(idle.chunks_visited == 0);
        CHECK(idle.chunks_skipped > 0);
        CHECK(store.puts == 0);
        CHECK(store.removes == 0);
    }
    CHECK(EqualsFullSave(registry, churn.world, churn.index, inner));
}

TEST_CASE("Incremental save: a change rewrites only the records whose bytes changed", "[storage][incremental]")
{
    ComponentRegistry registry;
    REQUIRE(RegisterIncComponents(&registry).Ok());
    MemoryStore inner;
    CountingStore store(inner);
    WorldStorage storage(registry, store);
    SaveSession session;
    World world;
    KeyIndex index;
    KeyGenerator gen = KeyGenerator::Seeded(1);
    Vector<Entity> es;
    for (u32 i = 0; i < 50; ++i)
    {
        es.Push(world.Create(EntityKeyComponent{gen.Next()}, IncA{static_cast<i32>(i), 0}));
    }
    SaveOptions options;
    options.mode = SaveMode::Incremental;
    options.session = &session;
    REQUIRE(storage.Save(world, index, options, nullptr).Ok());
    store.puts = 0;
    world.EndFrame();

    // One entity changes; another is written with the value it had (the chunk is stamped, the record is the same).
    REQUIRE(world.Set(es[3], IncA{999, 0}) == ecs::Status::Ok);
    REQUIRE(world.Set(es[4], IncA{4, 0}) == ecs::Status::Ok);
    Report report;
    REQUIRE(storage.Save(world, index, options, &report).Ok());
    CHECK(report.chunks_visited == 1);
    CHECK(report.encoded == 50);
    CHECK(report.replaced == 1);
    CHECK(store.puts == 1);
    CHECK(EqualsFullSave(registry, world, index, inner));
}

TEST_CASE("Incremental save: removals come from the log, and a lost log falls back to the store",
          "[storage][incremental]")
{
    ComponentRegistry registry;
    REQUIRE(RegisterIncComponents(&registry).Ok());
    MemoryStore store;
    WorldStorage storage(registry, store);
    SaveSession session;
    Churn churn(31);
    for (u32 i = 0; i < 40; ++i)
    {
        churn.Create();
    }
    SaveOptions options;
    options.mode = SaveMode::Incremental;
    options.session = &session;
    REQUIRE(storage.Save(churn.world, churn.index, options, nullptr).Ok());
    churn.world.EndFrame();

    // Ten leaves vanish; the log still holds them.
    usize destroyed = 0;
    for (usize i = churn.nodes.Size(); i > 0 && destroyed < 10; --i)
    {
        if (!churn.nodes[i - 1].has_children)
        {
            REQUIRE(churn.world.Destroy(churn.nodes[i - 1].e) == ecs::Status::Ok);
            churn.nodes.RemoveRange(i - 1, 1);
            ++destroyed;
        }
    }
    REQUIRE(destroyed == 10);
    Report logged;
    REQUIRE(storage.Save(churn.world, churn.index, options, &logged).Ok());
    CHECK(logged.reconciled == 0);
    CHECK(logged.removed == 10);
    CHECK(EqualsFullSave(registry, churn.world, churn.index, store));

    // Ten more vanish, and three frames end before the next save: the log has dropped them.
    destroyed = 0;
    for (usize i = churn.nodes.Size(); i > 0 && destroyed < 10; --i)
    {
        if (!churn.nodes[i - 1].has_children)
        {
            REQUIRE(churn.world.Destroy(churn.nodes[i - 1].e) == ecs::Status::Ok);
            churn.nodes.RemoveRange(i - 1, 1);
            ++destroyed;
        }
    }
    churn.world.EndFrame();
    churn.world.EndFrame();
    churn.world.EndFrame();
    Report fallback;
    REQUIRE(storage.Save(churn.world, churn.index, options, &fallback).Ok());
    CHECK(fallback.reconciled == 1);
    CHECK(fallback.removed == 10);
    CHECK(EqualsFullSave(registry, churn.world, churn.index, store));
}

TEST_CASE("Incremental save: a key change and a stripped key move or drop the record", "[storage][incremental]")
{
    ComponentRegistry registry;
    REQUIRE(RegisterIncComponents(&registry).Ok());
    MemoryStore store;
    WorldStorage storage(registry, store);
    SaveSession session;
    Churn churn(41);
    for (u32 i = 0; i < 10; ++i)
    {
        churn.Create();
    }
    SaveOptions options;
    options.mode = SaveMode::Incremental;
    options.session = &session;
    REQUIRE(storage.Save(churn.world, churn.index, options, nullptr).Ok());
    churn.world.EndFrame();
    REQUIRE(churn.world.Set(churn.nodes[2].e, EntityKeyComponent{churn.gen.Next()}) == ecs::Status::Ok);
    REQUIRE(churn.world.Remove<EntityKeyComponent>(churn.nodes[5].e) == ecs::Status::Ok);
    Report report;
    REQUIRE(storage.Save(churn.world, churn.index, options, &report).Ok());
    CHECK(report.reconciled == 0);
    CHECK(report.removed == 2);
    CHECK(EqualsFullSave(registry, churn.world, churn.index, store));
}

TEST_CASE("Incremental save: a new session over the same store rewrites nothing", "[storage][incremental]")
{
    ComponentRegistry registry;
    REQUIRE(RegisterIncComponents(&registry).Ok());
    MemoryStore inner;
    CountingStore store(inner);
    WorldStorage storage(registry, store);
    Churn churn(51);
    for (u32 i = 0; i < 300; ++i)
    {
        churn.Create();
    }
    {
        SaveSession before;
        SaveOptions options;
        options.mode = SaveMode::Incremental;
        options.session = &before;
        REQUIRE(storage.Save(churn.world, churn.index, options, nullptr).Ok());
    }
    store.puts = 0;
    store.removes = 0;
    SaveSession after;  // a restarted process
    SaveOptions options;
    options.mode = SaveMode::Incremental;
    options.session = &after;
    Report report;
    REQUIRE(storage.Save(churn.world, churn.index, options, &report).Ok());
    CHECK(report.reconciled == 1);
    CHECK(report.encoded == 300);
    CHECK(store.puts == 0);
    CHECK(store.removes == 0);
    CHECK(report.created == 0);
    CHECK(report.replaced == 0);
    CHECK(after.TrackedRecords() == 300);
    // And the session is now a baseline for incremental saves.
    churn.world.EndFrame();
    REQUIRE(churn.world.Set(churn.nodes[7].e,
                            EntityKeyComponent{churn.world.Peek<EntityKeyComponent>(churn.nodes[7].e)->key}) ==
            ecs::Status::Ok);
    Report next;
    REQUIRE(storage.Save(churn.world, churn.index, options, &next).Ok());
    CHECK(next.reconciled == 0);
    CHECK(store.puts == 0);
}

namespace
{
struct CancelAfter
{
    CancelToken* token;
    usize at;
};
void CancelProgress(usize done, usize, void* user)
{
    CancelAfter* c = static_cast<CancelAfter*>(user);
    if (done >= c->at)
    {
        c->token->Cancel();
    }
}
}  // namespace

TEST_CASE("Incremental save: a cancelled save leaves the store at the previous save and the session usable",
          "[storage][incremental]")
{
    ComponentRegistry registry;
    REQUIRE(RegisterIncComponents(&registry).Ok());
    MemoryStore store;
    WorldStorage storage(registry, store);
    SaveSession session;
    Churn churn(61);
    for (u32 i = 0; i < 200; ++i)
    {
        churn.Create();
    }
    SaveOptions options;
    options.mode = SaveMode::Incremental;
    options.session = &session;
    REQUIRE(storage.Save(churn.world, churn.index, options, nullptr).Ok());
    MemoryStore snapshot;
    {
        Dump d;
        REQUIRE(store.Scan(KeyRange(), d).Ok());
        for (usize i = 0; i < d.records.Size(); ++i)
        {
            REQUIRE(snapshot.Put(d.records[i]).Ok());
        }
    }
    churn.world.EndFrame();
    for (usize i = 0; i < 100; ++i)
    {
        if (churn.world.Has<IncA>(churn.nodes[i].e))
        {
            REQUIRE(churn.world.Set(churn.nodes[i].e, IncA{static_cast<i32>(i) + 7000, 1}) == ecs::Status::Ok);
        }
        else
        {
            REQUIRE(churn.world.Add(churn.nodes[i].e, IncA{static_cast<i32>(i) + 7000, 1}) == ecs::Status::Ok);
        }
    }
    CancelToken token;
    CancelAfter hook{&token, 3};
    SaveOptions cancelled;
    cancelled.mode = SaveMode::Incremental;
    cancelled.session = &session;
    cancelled.cancel = &token;
    cancelled.progress = &CancelProgress;
    cancelled.user = &hook;
    cancelled.batch_size = 1;
    Report report;
    const Status s = storage.Save(churn.world, churn.index, cancelled, &report);
    CHECK(s.Code() == ErrorCode::Cancelled);
    CHECK(SameStores(store, snapshot));
    // The next save, not cancelled, still finds everything that changed.
    token.Reset();
    REQUIRE(storage.Save(churn.world, churn.index, options, nullptr).Ok());
    CHECK(EqualsFullSave(registry, churn.world, churn.index, store));
}

TEST_CASE("Incremental save: a save with the wrong arguments is refused", "[storage][incremental]")
{
    ComponentRegistry registry;
    REQUIRE(RegisterIncComponents(&registry).Ok());
    MemoryStore store;
    WorldStorage storage(registry, store);
    World world;
    KeyIndex index;
    SaveOptions options;
    options.mode = SaveMode::Incremental;
    CHECK(storage.Save(world, index, options, nullptr).Code() == ErrorCode::Invalid);  // no session
    SaveSession session;
    options.session = &session;
    options.selection.kind = Selection::Kind::Keys;
    CHECK(storage.Save(world, index, options, nullptr).Code() == ErrorCode::Invalid);
}

TEST_CASE("Benchmark: incremental save of 100,000 entities, idle and with 1 percent changed",
          "[storage][incremental][benchmark]")
{
    ComponentRegistry registry;
    REQUIRE(RegisterIncComponents(&registry).Ok());
    MemoryStore store;
    WorldStorage storage(registry, store);
    SaveSession session;
    World world;
    KeyIndex index;
    KeyGenerator gen = KeyGenerator::Seeded(3);
    Vector<Entity> es;
    for (u32 i = 0; i < 100000; ++i)
    {
        es.Push(world.Create(EntityKeyComponent{gen.Next()}, IncA{static_cast<i32>(i), 0}));
    }
    SaveOptions options;
    options.mode = SaveMode::Incremental;
    options.session = &session;
    const u64 t0 = os::MonotonicTimeNanos();
    REQUIRE(storage.Save(world, index, options, nullptr).Ok());
    const u64 t1 = os::MonotonicTimeNanos();
    world.EndFrame();
    Report idle;
    REQUIRE(storage.Save(world, index, options, &idle).Ok());
    const u64 t2 = os::MonotonicTimeNanos();
    CHECK(idle.encoded == 0);
    world.EndFrame();
    // 1 percent: every 100th entity, so the changes are spread over the chunks (the worst case for chunk skipping).
    for (usize i = 0; i < es.Size(); i += 100)
    {
        REQUIRE(world.Set(es[i], IncA{static_cast<i32>(i) + 1, 1}) == ecs::Status::Ok);
    }
    Report spread;
    const u64 t3 = os::MonotonicTimeNanos();
    REQUIRE(storage.Save(world, index, options, &spread).Ok());
    const u64 t4 = os::MonotonicTimeNanos();
    CHECK(spread.replaced == 1000);
    world.EndFrame();
    // 1 percent in one run: the first 1000 entities.
    for (usize i = 0; i < 1000; ++i)
    {
        REQUIRE(world.Set(es[i], IncA{static_cast<i32>(i) + 2, 2}) == ecs::Status::Ok);
    }
    Report run;
    const u64 t5 = os::MonotonicTimeNanos();
    REQUIRE(storage.Save(world, index, options, &run).Ok());
    const u64 t6 = os::MonotonicTimeNanos();
    CHECK(run.replaced == 1000);
    os::Print(
        "incremental save, 100000 entities: first %llu ms; idle %llu us (%zu chunks skipped); 1%% spread %llu ms (%zu chunks, %zu encoded); 1%% in one run %llu ms (%zu chunks, %zu encoded)\n",
        static_cast<unsigned long long>((t1 - t0) / 1000000), static_cast<unsigned long long>((t2 - t1) / 1000),
        idle.chunks_skipped, static_cast<unsigned long long>((t4 - t3) / 1000000), spread.chunks_visited,
        spread.encoded, static_cast<unsigned long long>((t6 - t5) / 1000000), run.chunks_visited, run.encoded);
}

}  // namespace vse::storage
