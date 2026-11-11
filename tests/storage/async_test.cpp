// Asynchronous saves and loads, the autosaver and the storage settings (Phase 19 Day 264).

#include <catch2/catch_test_macros.hpp>

#include "database/database_settings.h"
#include "database/sqlite/sqlite_database.h"
#include "ecs/transform.h"
#include "jobs/scheduler.h"
#include "settings/registry.h"
#include "storage/async_storage.h"
#include "storage/builtin_components.h"
#include "storage/local_storage.h"
#include "storage/memory_store.h"
#include "storage/save_session.h"
#include "storage/storage_settings.h"
#include "storage/world_storage.h"
#include "temp_dir.h"
#include "world_compare.h"

namespace engine_placeholder::storage {
namespace {

struct AsyA {
    i32 a = 0;
    f32 b = 0;
};
struct AsyB {
    String<HeapAllocator> name{HeapAllocator::Default()};
};

Status RegisterAsyComponents(ComponentRegistry* r) {
    ENGINE_TRY(RegisterBuiltinComponents(r));
    ENGINE_TRY(r->Register<AsyA>({.id = 3100, .name = "asy_a", .flags = kComponentQueryable, .fields = {Field<&AsyA::a>("a"), Field<&AsyA::b>("b")}}));
    ENGINE_TRY(r->Register<AsyB>({.id = 3101, .name = "asy_b", .fields = {Field<&AsyB::name>("name")}}));
    return Status::Success();
}

struct AsyRng {
    u64 s;
    u32 Next() {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<u32>(s >> 33);
    }
};

struct AsyWorld {
    ecs::World world;
    KeyIndex index;
    KeyGenerator gen = KeyGenerator::Seeded(4242);
    Vector<ecs::Entity> entities;
    Vector<u32> depth;
};

void AsyBuild(AsyWorld* w, u64 seed, u32 count) {
    AsyRng rng{seed};
    for (u32 i = 0; i < count; ++i) {
        const ecs::Entity e = w->world.Create(EntityKeyComponent{w->gen.Next()});
        REQUIRE(e.IsValid());
        if (rng.Next() % 3 != 0) {
            REQUIRE(w->world.Add(e, AsyA{static_cast<i32>(rng.Next()), static_cast<f32>(rng.Next() % 1000) * 0.5f}) == ecs::Status::Ok);
        }
        if (rng.Next() % 4 == 0) {
            AsyB b;
            for (u32 k = rng.Next() % 12; k > 0; --k) {
                b.name.Append(static_cast<char>('a' + rng.Next() % 26));
            }
            REQUIRE(w->world.Add(e, Move(b)) == ecs::Status::Ok);
        }
        u32 my_depth = 0;
        if (!w->entities.Empty() && rng.Next() % 3 == 0) {
            const usize p = rng.Next() % w->entities.Size();
            if (w->depth[p] < 5) { // deeper chains are refused by a load
                my_depth = w->depth[p] + 1;
                REQUIRE(w->world.Set(e, ecs::Parent{w->entities[p]}) == ecs::Status::Ok);
                REQUIRE(w->world.Set(e, ecs::HierarchyDepth{my_depth}) == ecs::Status::Ok);
            }
        }
        w->entities.Push(e);
        w->depth.Push(my_depth);
    }
}

// Writes into the components of some entities, and adds a few.
void AsyChange(AsyWorld* w, u64 seed) {
    AsyRng rng{seed};
    for (usize i = 0; i < w->entities.Size(); i += 5) {
        if (w->world.Has<AsyA>(w->entities[i])) {
            REQUIRE(w->world.Set(w->entities[i], AsyA{static_cast<i32>(rng.Next()), 1.0f}) == ecs::Status::Ok);
        }
    }
    AsyBuild(w, seed + 1, 3);
}

class AsyDump final : public RecordVisitor {
public:
    bool Visit(const EntityRecord& record) override {
        records.Push(record.Clone());
        return true;
    }
    Vector<EntityRecord> records;
};

bool AsySameStores(const Store& a, const Store& b) {
    AsyDump da, db;
    if (a.Scan(KeyRange(), da).Failed() || b.Scan(KeyRange(), db).Failed() || da.records.Size() != db.records.Size()) {
        return false;
    }
    for (usize i = 0; i < da.records.Size(); ++i) {
        const EntityRecord& x = da.records[i];
        const EntityRecord& y = db.records[i];
        if (!(x.key == y.key) || x.hash != y.hash || x.blob.Size() != y.blob.Size()) {
            return false;
        }
        for (usize j = 0; j < x.blob.Size(); ++j) {
            if (x.blob[j] != y.blob[j]) {
                return false;
            }
        }
    }
    return true;
}

jobs::Scheduler::Config AsyConfig(usize participants) {
    jobs::Scheduler::Config c;
    c.participants = participants;
    c.pin = false;
    return c;
}

struct AsyCancel {
    CancelToken token;
    usize calls = 0;
    usize cancel_at = 0;
    bool fired = false;
};

void AsyProgress(usize, usize, void* user) {
    AsyCancel* c = static_cast<AsyCancel*>(user);
    if (c->calls++ == c->cancel_at) {
        c->fired = true;
        c->token.Cancel();
    }
}

} // namespace

TEST_CASE("Async save: equals the synchronous save byte for byte, over 100 worlds and 1 to 8 participants", "[storage][async]") {
    ComponentRegistry registry;
    REQUIRE(RegisterAsyComponents(&registry).Ok());
    u32 worlds = 0;
    for (usize participants = 1; participants <= 8; ++participants) {
        jobs::Scheduler scheduler(AsyConfig(participants));
        for (u32 n = 0; n < 13; ++n, ++worlds) {
            AsyWorld w;
            AsyBuild(&w, 100 + worlds, (worlds * 37) % 150);
            MemoryStore sync_store;
            MemoryStore async_store;
            WorldStorage sync_storage(registry, sync_store);
            WorldStorage async_storage(registry, async_store);
            SaveOptions options;
            options.batch_size = 1 + worlds % 29; // several rounds, whatever the world's size
            Report sync_report;
            REQUIRE(sync_storage.Save(w.world, w.index, options, &sync_report).Ok());
            UniquePtr<AsyncSave> handle;
            REQUIRE(async_storage.SaveAsync(scheduler, w.world, w.index, options, &handle).Ok());
            Report async_report;
            REQUIRE(handle->Wait(&async_report).Ok());
            CHECK(AsySameStores(sync_store, async_store));
            CHECK(async_report.created == sync_report.created);
            CHECK(async_report.bytes == sync_report.bytes);
            CHECK(async_report.encoded == sync_report.encoded);
        }
    }
    CHECK(worlds == 104);
}

TEST_CASE("Async save: the world may change as soon as the call returns; the store holds the world as it was", "[storage][async]") {
    ComponentRegistry registry;
    REQUIRE(RegisterAsyComponents(&registry).Ok());
    jobs::Scheduler scheduler(AsyConfig(4));
    AsyWorld w;
    AsyBuild(&w, 7, 400);
    MemoryStore expected;
    WorldStorage(registry, expected).Save(w.world, w.index, SaveOptions(), nullptr).Ok();
    MemoryStore store;
    WorldStorage storage(registry, store);
    UniquePtr<AsyncSave> handle;
    REQUIRE(storage.SaveAsync(scheduler, w.world, w.index, SaveOptions(), &handle).Ok());
    for (u32 round = 0; round < 5; ++round) { // while the writer job runs
        AsyChange(&w, 90 + round);
    }
    REQUIRE(handle->Wait(nullptr).Ok());
    CHECK(AsySameStores(expected, store));
}

TEST_CASE("Async save: cancelling at any progress callback leaves the store as it was", "[storage][async]") {
    ComponentRegistry registry;
    REQUIRE(RegisterAsyComponents(&registry).Ok());
    jobs::Scheduler scheduler(AsyConfig(3));
    usize cancelled = 0;
    for (usize cancel_at = 0; cancel_at < 40; ++cancel_at) {
        AsyWorld before;
        AsyBuild(&before, 5, 60);
        MemoryStore store;
        WorldStorage storage(registry, store);
        REQUIRE(storage.Save(before.world, before.index, SaveOptions(), nullptr).Ok());
        MemoryStore old;
        WorldStorage(registry, old).Save(before.world, before.index, SaveOptions(), nullptr).Ok();

        AsyChange(&before, 50);
        MemoryStore expected;
        WorldStorage(registry, expected).Save(before.world, before.index, SaveOptions(), nullptr).Ok();

        AsyCancel cancel;
        cancel.cancel_at = cancel_at;
        SaveOptions options;
        options.batch_size = 8;
        options.cancel = &cancel.token;
        options.progress = &AsyProgress;
        options.user = &cancel;
        UniquePtr<AsyncSave> handle;
        Status s = storage.SaveAsync(scheduler, before.world, before.index, options, &handle);
        if (s.Ok()) {
            s = handle->Wait(nullptr);
            handle.Reset();
        }
        if (s.Ok()) {
            CHECK(AsySameStores(expected, store)); // the cancel came after the commit (the last callback)
        } else {
            CHECK(s.Is(ErrorCode::Cancelled));
            CHECK(AsySameStores(old, store));
            ++cancelled;
        }
        if (!cancel.fired) {
            break;
        }
    }
    CHECK(cancelled >= 8);
}

TEST_CASE("Async save: a second writer gets Busy until the first is collected", "[storage][async]") {
    ComponentRegistry registry;
    REQUIRE(RegisterAsyComponents(&registry).Ok());
    jobs::Scheduler scheduler(AsyConfig(2));
    AsyWorld w;
    AsyBuild(&w, 3, 50);
    MemoryStore store;
    WorldStorage storage(registry, store);
    UniquePtr<AsyncSave> first;
    REQUIRE(storage.SaveAsync(scheduler, w.world, w.index, SaveOptions(), &first).Ok());
    UniquePtr<AsyncSave> second;
    CHECK(storage.SaveAsync(scheduler, w.world, w.index, SaveOptions(), &second).Is(ErrorCode::Busy));
    CHECK(storage.Save(w.world, w.index, SaveOptions(), nullptr).Is(ErrorCode::Busy));
    CHECK(second.Get() == nullptr);
    REQUIRE(first->Wait(nullptr).Ok());
    CHECK(first->Finish(nullptr).Ok()); // idempotent
    CHECK(storage.Save(w.world, w.index, SaveOptions(), nullptr).Ok());
    UniquePtr<AsyncSave> third;
    CHECK(storage.SaveAsync(scheduler, w.world, w.index, SaveOptions(), &third).Ok());
    CHECK(third->Wait(nullptr).Ok());
}

TEST_CASE("Async save: a handle dropped without Wait finishes the write, even on a scheduler with no workers", "[storage][async]") {
    ComponentRegistry registry;
    REQUIRE(RegisterAsyComponents(&registry).Ok());
    for (usize participants = 1; participants <= 3; participants += 2) {
        jobs::Scheduler scheduler(AsyConfig(participants));
        AsyWorld w;
        AsyBuild(&w, 3, 40);
        MemoryStore store;
        WorldStorage storage(registry, store);
        {
            UniquePtr<AsyncSave> handle;
            REQUIRE(storage.SaveAsync(scheduler, w.world, w.index, SaveOptions(), &handle).Ok());
        } // destroyed here: nobody called Wait or Finish
        usize count = 0;
        REQUIRE(store.Count(&count).Ok());
        CHECK(count == 40);
        CHECK(storage.Save(w.world, w.index, SaveOptions(), nullptr).Ok()); // the writer slot was released
    }
}

TEST_CASE("Async save: a handle dropped without Wait still brings the session up to date", "[storage][async][incremental]") {
    ComponentRegistry registry;
    REQUIRE(RegisterAsyComponents(&registry).Ok());
    jobs::Scheduler scheduler(AsyConfig(2));
    AsyWorld w;
    AsyBuild(&w, 5, 60);
    MemoryStore store;
    WorldStorage storage(registry, store);
    SaveSession session;
    SaveOptions options;
    options.mode = SaveMode::Incremental;
    options.session = &session;
    {
        UniquePtr<AsyncSave> handle;
        REQUIRE(storage.SaveAsync(scheduler, w.world, w.index, options, &handle).Ok());
    } // dropped: the destructor waits and adopts
    Report report;
    REQUIRE(storage.Save(w.world, w.index, options, &report).Ok());
    CHECK(report.encoded == 0); // nothing changed since the dropped save, and the session knows it
}

TEST_CASE("Async save: a store failure arrives from Finish and leaves the store as it was", "[storage][async]") {
    ComponentRegistry registry;
    REQUIRE(RegisterAsyComponents(&registry).Ok());
    jobs::Scheduler scheduler(AsyConfig(2));
    AsyWorld w;
    AsyBuild(&w, 3, 50);
    MemoryStore store;
    WorldStorage storage(registry, store);
    REQUIRE(storage.Save(w.world, w.index, SaveOptions(), nullptr).Ok());
    MemoryStore old;
    WorldStorage(registry, old).Save(w.world, w.index, SaveOptions(), nullptr).Ok();
    AsyChange(&w, 77);
    store.FailAfter(3);
    UniquePtr<AsyncSave> handle;
    REQUIRE(storage.SaveAsync(scheduler, w.world, w.index, SaveOptions(), &handle).Ok());
    CHECK(handle->Wait(nullptr).Is(ErrorCode::Io));
    CHECK(AsySameStores(old, store));
}

TEST_CASE("Async incremental save: the store equals a fresh full save after every round", "[storage][async][incremental]") {
    ComponentRegistry registry;
    REQUIRE(RegisterAsyComponents(&registry).Ok());
    jobs::Scheduler scheduler(AsyConfig(4));
    AsyWorld w;
    AsyBuild(&w, 11, 300);
    MemoryStore store;
    WorldStorage storage(registry, store);
    SaveSession session;
    SaveOptions options;
    options.mode = SaveMode::Incremental;
    options.session = &session;
    for (u32 round = 0; round < 12; ++round) {
        if (round != 5) { // one round with nothing written
            AsyChange(&w, 200 + round);
        }
        UniquePtr<AsyncSave> handle;
        REQUIRE(storage.SaveAsync(scheduler, w.world, w.index, options, &handle).Ok());
        Report report;
        REQUIRE(handle->Wait(&report).Ok());
        if (round == 5) {
            CHECK(report.encoded == 0);
        }
        MemoryStore fresh;
        WorldStorage(registry, fresh).Save(w.world, w.index, SaveOptions(), nullptr).Ok();
        CHECK(AsySameStores(fresh, store));
        w.world.EndFrame();
    }
}

TEST_CASE("Autosaver: saves at the interval, writes only what changed, and the file loads back equal", "[storage][async][autosave]") {
    ComponentRegistry registry;
    REQUIRE(RegisterAsyComponents(&registry).Ok());
    jobs::Scheduler scheduler(AsyConfig(4));
    AsyWorld w;
    AsyBuild(&w, 21, 500);
    MemoryStore store;
    WorldStorage storage(registry, store);
    Autosaver autosaver(storage, scheduler, w.world, w.index, 0.5);
    for (u32 frame = 0; frame < 120; ++frame) {
        if (frame % 7 == 0) {
            AsyChange(&w, 300 + frame);
        }
        REQUIRE(autosaver.Update(1.0 / 60.0).Ok());
        w.world.EndFrame();
        if (frame % 30 == 29) {
            REQUIRE(autosaver.Flush().Ok()); // the frame loop here is much faster than a disk
        }
    }
    REQUIRE(autosaver.Flush().Ok());
    CHECK(autosaver.Completed() >= 3);
    REQUIRE(autosaver.SaveNow().Ok());
    REQUIRE(autosaver.Flush().Ok());
    MemoryStore fresh;
    WorldStorage(registry, fresh).Save(w.world, w.index, SaveOptions(), nullptr).Ok();
    CHECK(AsySameStores(fresh, store));

    ecs::World loaded;
    KeyIndex loaded_index;
    REQUIRE(storage.Load(loaded, loaded_index, LoadOptions(), nullptr).Ok());
    test::RequireSameWorlds(registry, w.world, loaded);

    Autosaver off(storage, scheduler, w.world, w.index, 0);
    for (u32 frame = 0; frame < 30; ++frame) {
        REQUIRE(off.Update(1.0).Ok());
    }
    CHECK(off.Completed() == 0); // 0 seconds is off
}

TEST_CASE("Async load: equals Load, and the world is not touched until Finish", "[storage][async]") {
    ComponentRegistry registry;
    REQUIRE(RegisterAsyComponents(&registry).Ok());
    jobs::Scheduler scheduler(AsyConfig(3));
    AsyWorld w;
    AsyBuild(&w, 31, 250);
    MemoryStore store;
    WorldStorage storage(registry, store);
    REQUIRE(storage.Save(w.world, w.index, SaveOptions(), nullptr).Ok());

    ecs::World loaded;
    KeyIndex loaded_index;
    UniquePtr<AsyncLoad> handle;
    REQUIRE(storage.LoadAsync(scheduler, LoadOptions(), &handle).Ok());
    while (!handle->IsDone()) {
        scheduler.HelpUntil([](void* user) { return static_cast<AsyncLoad*>(user)->IsDone(); }, handle.Get());
    }
    CHECK(test::KeyedEntities(loaded).Size() == 0);
    Report report;
    REQUIRE(handle->Finish(loaded, loaded_index, &report).Ok());
    CHECK(report.created == 250);
    test::RequireSameWorlds(registry, w.world, loaded);

    // Cancelled before it reads: the world stays as it was.
    CancelToken token;
    token.Cancel();
    LoadOptions options;
    options.cancel = &token;
    UniquePtr<AsyncLoad> cancelled;
    REQUIRE(storage.LoadAsync(scheduler, options, &cancelled).Ok());
    ecs::World untouched;
    KeyIndex untouched_index;
    CHECK(cancelled->Wait(untouched, untouched_index, nullptr).Is(ErrorCode::Cancelled));
    CHECK(test::KeyedEntities(untouched).Size() == 0);

    // Selected keys.
    LoadOptions some;
    some.selection.kind = Selection::Kind::Keys;
    some.missing = MissingPolicy::NullReference; // parents outside the selection
    const Vector<test::EntityImage> all = test::KeyedEntities(w.world);
    for (usize i = 0; i < all.Size(); i += 10) {
        some.selection.keys.Push(all[i].key);
    }
    UniquePtr<AsyncLoad> partial;
    REQUIRE(storage.LoadAsync(scheduler, some, &partial).Ok());
    ecs::World part;
    KeyIndex part_index;
    Report part_report;
    REQUIRE(partial->Wait(part, part_index, &part_report).Ok());
    CHECK(part_report.created == some.selection.keys.Size());
}

TEST_CASE("Storage settings: path, layout and autosave are settings and open the storage", "[storage][settings]") {
    settings::Registry registry;
    RegisterStorageSettings(&registry);
    database::RegisterDatabaseSettings(&registry);
    CHECK(PathFromSettings(registry).Length() == 0);
    CHECK_FALSE(ColumnsFromSettings(registry));
    CHECK(AutosaveSecondsFromSettings(registry) == 0);

    database_test::TempDir dir;
    const io::Path file = dir / StringView("world.db");
    registry.Publish(registry.Find(StringView("storage.path")), settings::Value::FromText(file.View()));
    registry.Publish(registry.Find(StringView("storage.layout")), settings::Value::FromEnum(static_cast<u32>(StorageLayout::Columns)));
    registry.Publish(registry.Find(StringView("storage.autosave_seconds")), settings::Value::FromInt(30));
    CHECK(PathFromSettings(registry).View() == file.View());
    CHECK(ColumnsFromSettings(registry));
    CHECK(AutosaveSecondsFromSettings(registry) == 30);

    ComponentRegistry components;
    REQUIRE(RegisterAsyComponents(&components).Ok());
    database::SqliteDatabase backend;
    AsyWorld w;
    AsyBuild(&w, 41, 200);
    {
        UniquePtr<LocalStorage> local;
        REQUIRE(LocalStorage::OpenFromSettings(backend, registry, components, &local).Ok());
        CHECK(local->AutosaveSeconds() == 30);
        CHECK(local->Path() == file.View());
        REQUIRE(local->Worlds().Save(w.world, w.index, SaveOptions(), nullptr).Ok());
        usize mismatches = 1;
        REQUIRE(local->Records().VerifyColumns(&mismatches).Ok()); // the layout setting reached the store
        CHECK(mismatches == 0);
    }
    UniquePtr<LocalStorage> again;
    REQUIRE(LocalStorage::OpenFromSettings(backend, registry, components, &again).Ok());
    ecs::World loaded;
    KeyIndex loaded_index;
    REQUIRE(again->Worlds().Load(loaded, loaded_index, LoadOptions(), nullptr).Ok());
    test::RequireSameWorlds(components, w.world, loaded);

    // A limit set low reaches the codec through the opened storage.
    again.Reset();
    registry.Publish(registry.Find(StringView("storage.max_components_per_entity")), settings::Value::FromInt(1));
    REQUIRE(LocalStorage::OpenFromSettings(backend, registry, components, &again).Ok());
    ecs::World limited;
    KeyIndex limited_index;
    CHECK(again->Worlds().Load(limited, limited_index, LoadOptions(), nullptr).Failed());
    CHECK(test::KeyedEntities(limited).Size() == 0);
}

} // namespace engine_placeholder::storage
