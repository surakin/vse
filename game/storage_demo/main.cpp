// storage_demo: Phase 19 Day 264. A city of entities with transforms, a parent hierarchy and the spatial sets is saved
// to a SQLite file, loaded into a fresh world, and checked against the original: every stored component equal, the
// derived state (world transforms, spatial sets) rebuilt by the systems and answering the same queries as the original
// and as a brute-force scan. Then the world runs with the autosaver and the last autosave is loaded again. Headless:
// nothing is drawn (the render scene bridge needs the meshes of Phase 22's assets, KNOWN-ISSUES.md).
//
//   storage_demo [--entities N]  entity count (default 20000; a fifth are static blocks)
//                [--frames N]    frames to run with autosave (default 60)
//                [--file PATH]   the database file (default: a scratch file, removed afterwards); sets storage.path
//                [--columns]     keep the column tables too (storage.layout = columns)
#include "containers/vector.h"
#include "log/output.h"
#include "cstr/cstr.h"
#include "cstr/number.h"
#include "database/database_settings.h"
#include "algorithm.h"
#include "database/sqlite/sqlite_database.h"
#include "debug/program_setup.h"
#include "ecs/schedule.h"
#include "ecs/transform.h"
#include "ecs/world.h"
#include "io/file_system.h"
#include "io/path.h"
#include "jobs/scheduler.h"
#include "lang/scalar.h"
#include "math/geometry.h"
#include "os/console.h"
#include "os/os.h"
#include "settings/registry.h"
#include "spatial/components.h"
#include "spatial/entity_query.h"
#include "spatial/spatial_sync.h"
#include "spatial/spatial_world.h"
#include "storage/async_storage.h"
#include "storage/builtin_components.h"
#include "storage/identity.h"
#include "storage/local_storage.h"
#include "storage/storage_settings.h"

using namespace vse;

namespace
{

struct Rng
{
    u64 state = 0x9E3779B97F4A7C15ull;
    u32 Next()
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<u32>(state >> 33);
    }
    f32 Range(f32 lo, f32 hi)
    {
        return lo + (hi - lo) * (static_cast<f32>(Next() & 0xFFFFFF) / static_cast<f32>(1u << 24));
    }
};

// A world with the systems that derive its state.
struct City
{
    ecs::World world;
    ecs::Schedule schedule;
    storage::KeyIndex index;
    Vector<ecs::Entity> agents;

    bool Setup(jobs::Scheduler* jobs)
    {
        world.SetResource(ecs::TransformReport{});
        world.SetResource(spatial::SpatialWorld());
        schedule.Add(ecs::MakeTransformSystem("transforms"));
        schedule.Add(spatial::MakeSpatialSyncSystem("spatial_sync").InStage(ecs::kPostUpdate));
        schedule.SetScheduler(jobs);
        return schedule.Build().ok;
    }
    void Step(f32 dt) { schedule.Run(world, dt); }
};

Aabb BoxOf(const Vec3& c, const Vec3& h)
{
    return Aabb::FromMinMax(c - h, c + h);
}

// The stored components of every keyed entity, as bytes in key order: "key, then id and bytes of each stored
// component".
bool Image(const storage::ComponentRegistry& registry, City& city, Vector<byte>* out)
{
    Vector<storage::EntityKey> keys;
    Vector<ecs::Entity> entities;
    city.world.Query<ecs::Read<storage::EntityKeyComponent>>().ForEach(
        [&](ecs::Entity e, const storage::EntityKeyComponent& k)
        {
            keys.Push(k.key);
            entities.Push(e);
        });
    Vector<usize> order;
    for (usize i = 0; i < keys.Size(); ++i)
    {
        order.Push(i);
    }
    Sort(Span<usize>(order.Data(), order.Size()), [&](usize a, usize b) { return keys[a] < keys[b]; });
    const storage::CodecLimits limits;
    bytes::ByteWriter w;
    for (usize n = 0; n < order.Size(); ++n)
    {
        const usize i = order[n];
        w.WriteU64(keys[i].high);
        w.WriteU64(keys[i].low);
        for (u32 c = 0; c < registry.Count(); ++c)
        {
            const storage::RegisteredComponent& comp = registry.At(c);
            if (!comp.IsStored() || comp.EcsId() == ecs::kInvalidComponent ||
                !city.world.HasById(entities[i], comp.EcsId()))
            {
                continue;
            }
            w.WriteU32(comp.Id());
            if (comp.Encode(city.world.CellOf(entities[i], comp.EcsId()), w, limits).Failed())
            {
                return false;
            }
        }
        const ecs::Parent* p = city.world.Peek<ecs::Parent>(entities[i]);
        const storage::EntityKey pk = p != nullptr ? city.index.KeyOf(p->entity) : storage::EntityKey{};
        w.WriteU64(pk.high);
        w.WriteU64(pk.low);
    }
    const ByteSpan bytes = w.Bytes();
    for (usize i = 0; i < bytes.Size(); ++i)
    {
        out->Push(bytes[i]);
    }
    return true;
}

bool SameBytes(const Vector<byte>& a, const Vector<byte>& b)
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

// The keys of the entities a query returned, sorted.
void KeysOf(City& city, const Vector<ecs::Entity>& entities, Vector<storage::EntityKey>* out)
{
    for (usize i = 0; i < entities.Size(); ++i)
    {
        out->Push(city.index.KeyOf(entities[i]));
    }
    Sort(Span<storage::EntityKey>(out->Data(), out->Size()));
}

bool SameKeys(const Vector<storage::EntityKey>& a, const Vector<storage::EntityKey>& b)
{
    if (a.Size() != b.Size())
    {
        return false;
    }
    for (usize i = 0; i < a.Size(); ++i)
    {
        if (!(a[i] == b[i]))
        {
            return false;
        }
    }
    return true;
}

// The spatial sets of two cities answer the same, and the same as a scan of the world boxes. Returns the number of
// boxes that disagreed.
u32 CompareSpatial(City& a, City& b, f32 half, u32 probes)
{
    Rng rng;
    rng.state = 99;
    u32 bad = 0;
    for (u32 i = 0; i < probes; ++i)
    {
        const Vec3 center(rng.Range(-half, half), 2.0f, rng.Range(-half, half));
        const Aabbd box = BoxOf(center, Vec3(rng.Range(2, 40), 6, rng.Range(2, 40)));
        Vector<ecs::Entity> ha, hb;
        spatial::OverlapEntities(a.world.Resource<spatial::SpatialWorld>(), spatial::kRenderSet, box,
                                 spatial::QueryFilter(), &ha);
        spatial::OverlapEntities(b.world.Resource<spatial::SpatialWorld>(), spatial::kRenderSet, box,
                                 spatial::QueryFilter(), &hb);
        Vector<storage::EntityKey> ka, kb, scan;
        KeysOf(a, ha, &ka);
        KeysOf(b, hb, &kb);
        Vector<ecs::Entity> brute;
        b.world.Query<ecs::Read<spatial::WorldBounds>, ecs::Read<spatial::SpatialMembership>>().ForEach(
            [&](ecs::Entity e, const spatial::WorldBounds& wb, const spatial::SpatialMembership& m)
            {
                if ((m.sets & 1u) != 0 && Overlaps(wb.box, box))
                {
                    brute.Push(e);
                }
            });
        KeysOf(b, brute, &scan);
        if (!SameKeys(ka, kb) || !SameKeys(kb, scan))
        {
            ++bad;
        }
    }
    return bad;
}

bool Fail(const char* what)
{
    out::PrintError("storage_demo: FAILED: %s\n", what);
    return false;
}

bool Run(u32 entities, u32 frames, const char* file, bool columns)
{
    jobs::Scheduler jobs(jobs::Scheduler::Config{});
    storage::ComponentRegistry components;
    if (storage::RegisterBuiltinComponents(&components).Failed())
    {
        return Fail("the components did not register");
    }
    settings::Registry registry;
    storage::RegisterStorageSettings(&registry);
    database::RegisterDatabaseSettings(&registry);
    registry.Publish(registry.Find(StringView("storage.path")),
                     settings::Value::FromText(StringView(file, CStrLength(file))));
    registry.Publish(registry.Find(StringView("storage.layout")),
                     settings::Value::FromEnum(
                         static_cast<u32>(columns ? storage::StorageLayout::Columns : storage::StorageLayout::Blob)));
    registry.Publish(registry.Find(StringView("storage.autosave_seconds")), settings::Value::FromInt(1));

    // A city whose density is fixed: about one entity per 40 square units; every tenth agent carries a child.
    City original;
    if (!original.Setup(&jobs))
    {
        return Fail("the schedule did not build");
    }
    Rng rng;
    storage::KeyGenerator keys = storage::KeyGenerator::Seeded(2024);
    const f32 half = Sqrt(static_cast<f32>(entities) * 40.0f) * 0.5f;
    Vector<ecs::Entity> made;
    for (u32 i = 0; i < entities; ++i)
    {
        const bool block = i % 5 == 0;
        ecs::LocalTransform local;
        local.position = Vec3(rng.Range(-half, half), block ? 4.0f : 0.5f, rng.Range(-half, half));
        spatial::SpatialBounds bounds;
        bounds.local = block ? BoxOf(Vec3(0, 0, 0), Vec3(3, 4, 3)) : BoxOf(Vec3(0, 0, 0), Vec3(0.4f, 0.5f, 0.4f));
        spatial::SpatialMembership membership;
        membership.sets = 3;
        const ecs::Entity e = original.world.Create(storage::EntityKeyComponent{keys.Next()}, local,
                                                    ecs::WorldTransform{}, bounds, membership);
        if (block)
        {
            original.world.Add<spatial::SpatialStatic>(e, spatial::SpatialStatic{});
        }
        else
        {
            original.agents.Push(e);
            if (i % 10 == 1)
            {
                ecs::LocalTransform child_local;
                child_local.position = Vec3(1, 0, 1);
                const ecs::Entity child = original.world.Create(storage::EntityKeyComponent{keys.Next()}, child_local,
                                                                ecs::WorldTransform{}, bounds, membership);
                if (ecs::SetParent(original.world, child, e) != ecs::HierarchyStatus::Ok)
                {
                    return Fail("a parent was refused");
                }
                made.Push(child);
            }
        }
    }
    for (u32 i = 0; i < 3; ++i)
    {
        original.Step(0.016f);
    }
    if (original.index.Sync(original.world).Failed())
    {
        return Fail("the keys did not sync");
    }
    out::Print("storage_demo: %u entities (%u with a parent), a city of %.0f x %.0f units, file %s, layout %s\n",
               static_cast<u32>(original.index.Size()), static_cast<u32>(made.Size()), static_cast<double>(half * 2),
               static_cast<double>(half * 2), file, columns ? "columns" : "blob");

    // Save, load into a fresh world, compare.
    UniquePtr<storage::LocalStorage> local;
    database::SqliteDatabase backend;
    storage::Status s = storage::LocalStorage::OpenFromSettings(backend, registry, components, &local);
    if (s.Failed())
    {
        return Fail(s.Message());
    }
    storage::Report saved;
    s = local->Worlds().Save(original.world, original.index, storage::SaveOptions(), &saved);
    if (s.Failed())
    {
        return Fail(s.Message());
    }
    out::Print("storage_demo: saved %u entities, %llu bytes\n", static_cast<u32>(saved.created),
               static_cast<unsigned long long>(saved.bytes));

    City loaded;
    if (!loaded.Setup(&jobs))
    {
        return Fail("the second schedule did not build");
    }
    storage::Report loaded_report;
    s = local->Worlds().Load(loaded.world, loaded.index, storage::LoadOptions(), &loaded_report);
    if (s.Failed())
    {
        return Fail(s.Message());
    }
    for (u32 i = 0; i < 3; ++i)
    {
        loaded.Step(0.016f);
    }
    if (loaded.index.Sync(loaded.world).Failed())
    {
        return Fail("the keys of the loaded world did not sync");
    }
    Vector<byte> image_a, image_b;
    if (!Image(components, original, &image_a) || !Image(components, loaded, &image_b))
    {
        return Fail("a component did not encode");
    }
    if (!SameBytes(image_a, image_b))
    {
        return Fail("the loaded world differs from the original");
    }
    out::Print("storage_demo: the loaded world equals the original (%u bytes of components)\n",
               static_cast<u32>(image_a.Size()));
    original.Step(0.0f);  // the same frame on both: the world transforms and the sets are derived again, not stored
    loaded.Step(0.0f);
    const u32 bad = CompareSpatial(original, loaded, half, 200);
    if (bad != 0)
    {
        out::PrintError("storage_demo: %u of 200 boxes saw different entities\n", bad);
        return Fail("the spatial sets of the loaded world answer differently");
    }
    out::Print("storage_demo: 200 box queries agree between the original, the loaded world and a scan of the boxes\n");
    if (columns)
    {
        usize mismatches = 1;
        if (local->Records().VerifyColumns(&mismatches).Failed() || mismatches != 0)
        {
            return Fail("the column tables do not match the records");
        }
    }

    // Run with the autosaver and load the last autosave.
    storage::Autosaver autosaver(local->Worlds(), jobs, loaded.world, loaded.index,
                                 static_cast<f64>(local->AutosaveSeconds()) * 0.25);
    for (u32 frame = 0; frame < frames; ++frame)
    {
        for (usize i = 0; i < loaded.agents.Size() && i < 400; ++i)
        {
            ecs::LocalTransform* l = loaded.world.Get<ecs::LocalTransform>(loaded.agents[i]);
            if (l != nullptr)
            {
                l->position = l->position + Vec3(rng.Range(-0.1f, 0.1f), 0, rng.Range(-0.1f, 0.1f));
            }
        }
        loaded.Step(1.0f / 60.0f);
        s = autosaver.Update(1.0 / 60.0);
        if (s.Failed())
        {
            return Fail(s.Message());
        }
        if (frame % 15 == 14)
        {
            s = autosaver
                    .Flush();  // the loop here is faster than a disk; a game would simply collect it on a later frame
            if (s.Failed())
            {
                return Fail(s.Message());
            }
        }
    }
    s = autosaver.SaveNow();
    if (s.Failed())
    {
        return Fail(s.Message());
    }
    s = autosaver.Flush();
    if (s.Failed())
    {
        return Fail(s.Message());
    }
    out::Print("storage_demo: %u frames, %u autosaves\n", frames, static_cast<u32>(autosaver.Completed()));
    if (autosaver.Completed() < 2)
    {
        return Fail("the autosaver saved too rarely");
    }
    City again;
    if (!again.Setup(&jobs))
    {
        return Fail("the third schedule did not build");
    }
    s = local->Worlds().Load(again.world, again.index, storage::LoadOptions(), nullptr);
    if (s.Failed())
    {
        return Fail(s.Message());
    }
    if (again.index.Sync(again.world).Failed())
    {
        return Fail("the keys of the autosaved world did not sync");
    }
    Vector<byte> image_c, image_d;
    if (!Image(components, loaded, &image_c) || !Image(components, again, &image_d))
    {
        return Fail("a component did not encode");
    }
    if (!SameBytes(image_c, image_d))
    {
        return Fail("the autosaved world differs from the running one");
    }
    out::Print("storage_demo: the autosaved world equals the running one\n");
    return true;
}

}  // namespace

int main(int argc, char** argv)
{
    const debug::ProgramDebugSetup debug_setup = debug::StartProgramDebugging(argc, argv);
    debug::RunCrashTest(debug_setup.crash_test);
    u32 entities = 20000;
    u32 frames = 60;
    bool columns = false;
    const char* file = nullptr;
    static const os::CommandLineOption kCliOptions[] = {
        {"entities", true}, {"frames", true},           {"file", true},
        {"columns", false}, debug::kCrashHandlerOption, debug::kCrashTestOption};
    const os::CommandLine<HeapAllocator> line(HeapAllocator::Default(), argc, argv,
                                              Span<const os::CommandLineOption>(kCliOptions, 6));
    const char* program = line.ProgramName().Data();
    if (!line.Ok() || line.ArgCount() != 0)
    {
        out::PrintError("usage: %s [--entities N] [--frames N] [--file PATH] [--columns]\n", program);
        return 2;
    }
    StringView value;
    if (line.TryGetOption("entities", value))
    {
        entities = static_cast<u32>(CStrToLong(value.Data()));
    }
    if (line.TryGetOption("frames", value))
    {
        frames = static_cast<u32>(CStrToLong(value.Data()));
    }
    if (line.TryGetOption("file", value))
    {
        file = value.Data();
    }
    columns = line.HasFlag("columns");
    if (entities == 0 || entities > 2000000)
    {
        entities = 20000;
    }
    if (frames == 0 || frames > 100000)
    {
        frames = 60;
    }

    // Without --file the database lives in a scratch directory that is removed afterwards.
    io::Path scratch;
    String<HeapAllocator> scratch_file{HeapAllocator::Default()};
    if (file == nullptr)
    {
        io::Path tmp;
        if (io::TempDirectory(&tmp).Failed() || io::CreateTempDirectory(tmp, "re_storage_demo", &scratch).Failed())
        {
            out::PrintError("storage_demo: no scratch directory\n");
            return 1;
        }
        const io::Path path = scratch / StringView("city.db");
        scratch_file = String<HeapAllocator>(HeapAllocator::Default(), path.View());
        file = scratch_file.CStr();
    }
    const bool ok = Run(entities, frames, file, columns);
    if (scratch.View().Length() != 0)
    {
        io::RemoveAll(scratch);
    }
    if (!ok)
    {
        return 1;
    }
    out::Print("storage_demo: ok\n");
    return 0;
}
