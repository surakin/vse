// Phase 27 Day 463: minted scripts load through the resource manager with their closure and run as modules in a script
// world.

#include <catch2/catch_test_macros.hpp>

#include "asset/cpu_loaders.h"
#include "asset/memory_backend.h"
#include "asset/script_blob.h"
#include "cstr/cstr.h"
#include "cstr/format.h"
#include "script/luajit/luajit_backend.h"
#include "script/script_assets.h"
#include "sync/thread.h"

namespace vse::script
{
namespace
{

using asset::ResourceId;

struct LoadRig
{
    asset::TypeRegistry types;
    asset::HostMemoryBackend backend;
    UniquePtr<asset::ResourceManager> manager;
    UniquePtr<Backend> language = luajit::CreateLuaJitBackend();

    LoadRig() : types(), backend(types, u64{16} << 20)
    {
        asset::ManagerConfig config;
        config.buffer_pool_bytes = u64{16} << 20;
        manager = MakeUnique<asset::ResourceManager>(types, config);
        asset::RegisterCpuLoaders(manager.Get());
        REQUIRE(manager->Mount(StringView("/"), &backend, 0).Ok());
    }

    UniquePtr<ScriptWorld> World(u32 partitions)
    {
        ScriptWorldConfig config;
        config.backend = language.Get();
        config.partitions = partitions;
        return MakeUnique<ScriptWorld>(config);
    }

    // Mints a script blob straight from source text that already uses `__require("<hex>")`.
    void Add(u64 id, StringView source, const u64* deps = nullptr, usize count = 0, const char* entry = nullptr)
    {
        asset::ScriptBuild build;
        build.language = String<HeapAllocator>(HeapAllocator::Default(), StringView("luajit"));
        build.language_version = String<HeapAllocator>(HeapAllocator::Default(), StringView("2.1"));
        build.source = String<HeapAllocator>(HeapAllocator::Default(), source);
        for (usize i = 0; i < count; ++i)
        {
            build.dependencies.Push(ResourceId::FromValue(deps[i]));  // strictly increasing
        }
        if (entry != nullptr)
        {
            build.entries.Push(String<HeapAllocator>(HeapAllocator::Default(), StringView(entry, CStrLength(entry))));
        }
        Vector<byte> blob;
        REQUIRE(asset::BuildScriptBlob(types, ResourceId::FromValue(id), build, &blob).Ok());
        REQUIRE(backend.Add(ResourceId::FromValue(id), ByteSpan(blob.Data(), blob.Size())).Ok());
    }
};

String<HeapAllocator> Key(u64 id)
{
    char hex[17];
    asset::FormatResourceId(ResourceId::FromValue(id), hex);
    return String<HeapAllocator>(HeapAllocator::Default(), StringView(hex, 16));
}

// "local m = __require("<key>")" lines for each id.
String<HeapAllocator> Requires(const u64* ids, usize count)
{
    String<HeapAllocator> text(HeapAllocator::Default());
    for (usize i = 0; i < count; ++i)
    {
        text.Append(StringView("local m"));
        char n[4] = {static_cast<char>('0' + i), 0, 0, 0};
        text.Append(StringView(n, 1));
        text.Append(StringView(" = __require(\""));
        text.Append(Key(ids[i]).View());
        text.Append(StringView("\")\n"));
    }
    return text;
}

i64 Total(ScriptWorld* world, u32 partition, const char* function = "total")
{
    PartitionLease lease;
    Outcome error;
    REQUIRE(world->Checkout(partition, &lease, &error) == CheckoutStatus::Ok);
    CallOutput out;
    const Outcome o = lease.Call(function, Span<const Value>(), Budget(), &out);
    return o.IsOk() && out.results.Size() == 1 ? out.results[0].AsInt() : -1;
}

// A script that requires `deps` and defines the global function `total` as its own number plus theirs.
String<HeapAllocator> Source(i64 own, const u64* deps, usize count, const char* name)
{
    String<HeapAllocator> text = Requires(deps, count);
    char line[160];
    FormatTo(line, sizeof(line), "local own = %lld\n", static_cast<long long>(own));
    text.Append(StringView(line, CStrLength(line)));
    text.Append(StringView("local sum = own\n"));
    for (usize i = 0; i < count; ++i)
    {
        char add[64];
        FormatTo(add, sizeof(add), "sum = sum + m%c.sum\n", static_cast<char>('0' + i));
        text.Append(StringView(add, CStrLength(add)));
    }
    FormatTo(line, sizeof(line), "%s_sum = sum\nfunction %s_total() return sum end\n", name, name);
    text.Append(StringView(line, CStrLength(line)));
    text.Append(StringView("return { sum = sum }\n"));
    return text;
}

// 1 requires 2 and 3; 2 requires 4; 3 requires 4 and 5. The value of a script is its number plus its requires'.
void AddFive(LoadRig* rig)
{
    rig->Add(5, Source(5, nullptr, 0, "s5").View());
    rig->Add(4, Source(4, nullptr, 0, "s4").View());
    const u64 d2[1] = {4};
    rig->Add(2, Source(2, d2, 1, "s2").View(), d2, 1);
    const u64 d3[2] = {4, 5};
    rig->Add(3, Source(3, d3, 2, "s3").View(), d3, 2);
    const u64 d1[2] = {2, 3};
    rig->Add(1, Source(1, d1, 2, "s1").View(), d1, 2, "main");
}

}  // namespace

TEST_CASE("script assets: a closure of five scripts loads dependencies first and runs in every partition",
          "[script][assets][load]")
{
    LoadRig rig;
    AddFive(&rig);
    UniquePtr<ScriptWorld> world = rig.World(4);
    ScriptAssets assets(world.Get(), rig.manager.Get());
    REQUIRE(assets.Add(ResourceId::FromValue(1)).Ok());
    REQUIRE(assets.Count() == 5);
    // Each script is added after the scripts it requires.
    for (usize i = 0; i < assets.Count(); ++i)
    {
        const u64 id = assets.Added()[i].Value();
        for (usize j = i + 1; j < assets.Count(); ++j)
        {
            // 4 and 5 come before 2 and 3, which come before 1.
            CHECK(!(id == 1 && assets.Added()[j].Value() != 1));
        }
    }
    CHECK(assets.Added()[4] == ResourceId::FromValue(1));
    CHECK(assets.ScriptOf(StringView("main")) == ResourceId::FromValue(1));
    CHECK(assets.ScriptOf(StringView("nobody")).IsNone());
    // total of 1 = 1 + (2 + 4) + (3 + 4 + 5) = 19: a shared dependency (4) counts once per path, runs once per state.
    for (u32 p = 0; p < 4; ++p)
    {
        CHECK(Total(world.Get(), p, "s1_total") == 19);
        CHECK(Total(world.Get(), p, "s4_total") == 4);
    }
    // Adding it again changes nothing.
    REQUIRE(assets.Add(ResourceId::FromValue(1)).Ok());
    CHECK(assets.Count() == 5);
    CHECK(rig.manager->Stats().loads == 5);
}

TEST_CASE("script assets: the result is the same for any partition count and thread count", "[script][assets][load]")
{
    i64 reference = 0;
    for (u32 partitions : {1u, 3u, 8u})
    {
        LoadRig rig;
        AddFive(&rig);
        UniquePtr<ScriptWorld> world = rig.World(partitions);
        ScriptAssets assets(world.Get(), rig.manager.Get());
        REQUIRE(assets.Add(ResourceId::FromValue(1)).Ok());
        struct Job
        {
            ScriptWorld* world;
            u32 partition;
            i64 result;
        };
        Vector<Job> jobs;
        for (u32 p = 0; p < partitions; ++p)
        {
            jobs.Push(Job{world.Get(), p, 0});
        }
        Vector<Thread> threads;
        for (usize i = 0; i < jobs.Size(); ++i)
        {
            Job* job = &jobs[i];
            threads.Push(Thread(
                [job]()
                {
                    PartitionLease lease;
                    Outcome error;
                    if (job->world->Checkout(job->partition, &lease, &error) == CheckoutStatus::Ok)
                    {
                        CallOutput out;
                        if (lease.Call("s1_total", Span<const Value>(), Budget(), &out).IsOk() &&
                            out.results.Size() == 1)
                        {
                            job->result = out.results[0].AsInt();
                        }
                    }
                }));
        }
        for (usize i = 0; i < threads.Size(); ++i)
        {
            threads[i].Join();
        }
        for (usize i = 0; i < jobs.Size(); ++i)
        {
            if (reference == 0)
            {
                reference = jobs[i].result;
            }
            CHECK(jobs[i].result == reference);
        }
    }
    CHECK(reference == 19);
}

TEST_CASE("script assets: a missing dependency fails the load and adds nothing", "[script][assets][load]")
{
    LoadRig rig;
    const u64 deps[2] = {7, 9};  // 9 is nowhere
    rig.Add(7, Source(7, nullptr, 0, "s7").View());
    rig.Add(1, Source(1, deps, 2, "s1").View(), deps, 2);
    UniquePtr<ScriptWorld> world = rig.World(2);
    ScriptAssets assets(world.Get(), rig.manager.Get());
    asset::LoadFailure failure;
    const asset::Status status = assets.Add(ResourceId::FromValue(1), &failure);
    CHECK(status.Is(asset::ErrorCode::MissingDependency));
    CHECK(assets.Count() == 0);
    PartitionLease lease;
    Outcome error;
    REQUIRE(world->Checkout(0, &lease, &error) == CheckoutStatus::Ok);
    CallOutput out;
    CHECK_FALSE(lease.Call("s7_total", Span<const Value>(), Budget(), &out).IsOk());  // 7 was not added either
}

TEST_CASE("script assets: a cycle in hostile content is refused", "[script][assets][load]")
{
    LoadRig rig;
    const u64 a[1] = {2};
    const u64 b[1] = {1};
    rig.Add(1, Source(1, a, 1, "s1").View(), a, 1);
    rig.Add(2, Source(2, b, 1, "s2").View(), b, 1);
    UniquePtr<ScriptWorld> world = rig.World(1);
    ScriptAssets assets(world.Get(), rig.manager.Get());
    CHECK(assets.Add(ResourceId::FromValue(1)).Is(asset::ErrorCode::DependencyCycle));
    CHECK(assets.Count() == 0);
}

TEST_CASE("script assets: a require outside the declared dependencies, or after loading, is an error",
          "[script][assets][load]")
{
    LoadRig rig;
    // 2 exists but 1 does not declare it.
    rig.Add(2, StringView("return { sum = 2 }\n"));
    String<HeapAllocator> sneaky(HeapAllocator::Default());
    sneaky.Append(StringView("local x = __require(\""));
    sneaky.Append(Key(2).View());
    sneaky.Append(StringView("\")\n"));
    rig.Add(1, sneaky.View());
    UniquePtr<ScriptWorld> world = rig.World(1);
    ScriptAssets assets(world.Get(), rig.manager.Get());
    REQUIRE(assets.Add(ResourceId::FromValue(1)).Ok());
    PartitionLease lease;
    Outcome error;
    CHECK(world->Checkout(0, &lease, &error) == CheckoutStatus::Failed);
    CHECK(error.message.View().Find(StringView("not a declared dependency")) < error.message.View().Length());

    // A require from a function that runs after the script loaded.
    LoadRig later;
    later.Add(2, StringView("return { sum = 2 }\n"));
    String<HeapAllocator> late(HeapAllocator::Default());
    late.Append(StringView("function total() return __require(\""));
    late.Append(Key(2).View());
    late.Append(StringView("\").sum end\n"));
    const u64 d[1] = {2};
    later.Add(1, late.View(), d, 1);
    UniquePtr<ScriptWorld> world2 = later.World(1);
    ScriptAssets assets2(world2.Get(), later.manager.Get());
    REQUIRE(assets2.Add(ResourceId::FromValue(1)).Ok());
    PartitionLease lease2;
    REQUIRE(world2->Checkout(0, &lease2, &error) == CheckoutStatus::Ok);
    CallOutput out;
    const Outcome o = lease2.Call("total", Span<const Value>(), Budget(), &out);
    CHECK_FALSE(o.IsOk());
    CHECK(o.message.View().Find(StringView("only available while a script loads")) < o.message.View().Length());
}

TEST_CASE("script assets: a script with a syntax error or an error at load fails the checkout by value",
          "[script][assets][load]")
{
    LoadRig rig;
    rig.Add(1, StringView("local x = = 1\n"));
    UniquePtr<ScriptWorld> world = rig.World(1);
    ScriptAssets assets(world.Get(), rig.manager.Get());
    REQUIRE(assets.Add(ResourceId::FromValue(1)).Ok());
    PartitionLease lease;
    Outcome error;
    CHECK(world->Checkout(0, &lease, &error) == CheckoutStatus::Failed);
    CHECK(error.status == Status::SyntaxError);

    LoadRig runs;
    runs.Add(1, StringView("error('boom')\n"));
    UniquePtr<ScriptWorld> world2 = runs.World(1);
    ScriptAssets assets2(world2.Get(), runs.manager.Get());
    REQUIRE(assets2.Add(ResourceId::FromValue(1)).Ok());
    CHECK(world2->Checkout(0, &lease, &error) == CheckoutStatus::Failed);
    CHECK(error.status == Status::RuntimeError);
    // The failure is sticky: a second checkout reports the same error, not a follow-on one.
    const String<HeapAllocator> first(HeapAllocator::Default(), error.message.View());
    CHECK(world2->Checkout(0, &lease, &error) == CheckoutStatus::Failed);
    CHECK(error.status == Status::RuntimeError);
    CHECK(error.message.View() == first.View());
    CHECK(first.View().Find(StringView("boom")) < first.View().Length());
}

TEST_CASE("script assets: a script cannot reach or replace the module host", "[script][assets][load]")
{
    LoadRig rig;
    rig.Add(3, StringView("return { sum = 3 }\n"));
    // Script 1 tries every way it knows to break the declared-dependency rule for the scripts loaded after it.
    rig.Add(1, StringView(
                   "function probe() return type(__module_run) .. type(__module_define) .. type(__module_store) end\n"
                   "__require = function() return { sum = 999 } end\n"
                   "_G.__require = function() return { sum = 999 } end\n"
                   "__module_run = nil\n"));
    // Script 2 declares nothing and requires 3: the rule must still refuse it.
    String<HeapAllocator> text(HeapAllocator::Default());
    text.Append(StringView("local x = __require(\""));
    text.Append(Key(3).View());
    text.Append(StringView("\")\n"));
    rig.Add(2, text.View());
    UniquePtr<ScriptWorld> world = rig.World(1);
    ScriptAssets assets(world.Get(), rig.manager.Get());
    REQUIRE(assets.Add(ResourceId::FromValue(3)).Ok());
    REQUIRE(assets.Add(ResourceId::FromValue(1)).Ok());
    REQUIRE(assets.Add(ResourceId::FromValue(2)).Ok());
    PartitionLease lease;
    Outcome error;
    CHECK(world->Checkout(0, &lease, &error) == CheckoutStatus::Failed);
    CHECK(error.message.View().Find(StringView("not a declared dependency")) < error.message.View().Length());
    // The host's functions are not globals at all.
    LoadRig plain;
    plain.Add(1,
              StringView(
                  "function probe() return type(__module_run) .. type(__module_define) .. type(__module_store) end\n"));
    UniquePtr<ScriptWorld> world2 = plain.World(1);
    ScriptAssets assets2(world2.Get(), plain.manager.Get());
    REQUIRE(assets2.Add(ResourceId::FromValue(1)).Ok());
    PartitionLease lease2;
    REQUIRE(world2->Checkout(0, &lease2, &error) == CheckoutStatus::Ok);
    CallOutput out;
    REQUIRE(lease2.Call("probe", Span<const Value>(), Budget(), &out).IsOk());
    REQUIRE(out.results.Size() == 1);
    CHECK(out.results[0].Text() == StringView("nilnilnil"));
}

TEST_CASE("script assets: a script that waits while it loads fails instead of staying half run",
          "[script][assets][load]")
{
    LoadRig rig;
    rig.Add(1, StringView("wait_frames(1)\nfunction total() return 1 end\n"));
    UniquePtr<ScriptWorld> world = rig.World(1);
    ScriptAssets assets(world.Get(), rig.manager.Get());
    REQUIRE(assets.Add(ResourceId::FromValue(1)).Ok());
    PartitionLease lease;
    Outcome error;
    CHECK(world->Checkout(0, &lease, &error) == CheckoutStatus::Failed);
    CHECK(error.message.View().Find(StringView("cannot wait")) < error.message.View().Length());
}

TEST_CASE("script assets: scripts unload when released and load again", "[script][assets][load]")
{
    LoadRig rig;
    AddFive(&rig);
    {
        UniquePtr<ScriptWorld> world = rig.World(2);
        ScriptAssets assets(world.Get(), rig.manager.Get());
        REQUIRE(assets.Add(ResourceId::FromValue(1)).Ok());
        CHECK(rig.manager->Stats().resident == 5);
        // A state keeps what it compiled after the manager lets go.
        CHECK(Total(world.Get(), 0, "s1_total") == 19);
        assets.Release();
    }
    CHECK(rig.manager->Stats().resident == 0);
    UniquePtr<ScriptWorld> again = rig.World(2);
    ScriptAssets reload(again.Get(), rig.manager.Get());
    REQUIRE(reload.Add(ResourceId::FromValue(1)).Ok());
    CHECK(Total(again.Get(), 1, "s1_total") == 19);
    CHECK(rig.manager->Stats().loads == 10);
}

TEST_CASE("script assets: a script whose dependency is not a script is refused", "[script][assets][load]")
{
    LoadRig rig;
    asset::BlobWriter writer(rig.types, asset::types::kScript, ResourceId::FromValue(1), 0);
    asset::Dependency d;
    d.id = ResourceId::FromValue(2);
    d.type = asset::types::kMesh;
    d.type_version = 1;
    writer.AddDependency(d);
    const byte bytes[1] = {byte{0}};
    REQUIRE(writer.AddSection(16, ByteSpan(bytes, 1)).Ok());
    Vector<byte> blob;
    REQUIRE(writer.Finish(&blob).Ok());
    asset::ScriptView view;
    CHECK(view.Open(CopyShared(blob), rig.types).Failed());
}

}  // namespace vse::script
