// Phase 27 Day 464: hot reload. A changed script replaces the running one in every partition at one point between
// ticks, keeps what its hooks keep, and a broken edit leaves the old version running.
#include <catch2/catch_test_macros.hpp>

#include "cstr/cstr.h"
#include "cstr/format.h"
#include "ecs/transform.h"
#include "io/file.h"
#include "io/file_system.h"
#include "jobs/scheduler.h"
#include "script/backend_registry.h"
#include "script/bindings.h"
#include "script/component_catalog.h"
#include "script/luajit/luajit_backend.h"
#include "script/script_reload.h"
#include "script/script_runtime.h"
#include "storage/builtin_components.h"
#include "sync/thread.h"
#include "temp_dir.h"

using namespace engine_placeholder;
using namespace engine_placeholder::script;

namespace
{

struct RlTrace
{
    String<HeapAllocator> text{HeapAllocator::Default()};
    i32 value = 0;
};

constexpr u32 kRlTraceId = 2131;

// Version `v` of the counter behavior: on_update adds `step` to `n` and writes the version and `n` into the trace.
String<HeapAllocator> Counter(StringView version, i32 step, bool hooks)
{
    char text[900];
    FormatTo(text, sizeof(text),
             "behavior('counter', {\n"
             "  on_create = function(self) self.n = 0 end,\n"
             "  on_update = function(self)\n"
             "    self.n = self.n + %d\n"
             "    component.set(self.entity, 'rl_trace', { text = '%.*s', value = self.n })\n"
             "  end,\n"
             "%s"
             "})\n",
             static_cast<int>(step), static_cast<int>(version.Length()), version.Data(),
             hooks ? "  on_reload_save = function(self) return { n = self.n } end,\n"
                     "  on_reload_restore = function(self, saved) self.n = saved.n + 1000 end,\n"
                   : "");
    return String<HeapAllocator>(HeapAllocator::Default(), StringView(text, CStrLength(text)));
}

struct ReloadRig
{
    storage::ComponentRegistry registry;
    ComponentCatalog catalog;
    BindingRegistry bindings;
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    ecs::World world;
    UniquePtr<ScriptRuntime> runtime;
    UniquePtr<ScriptReloader> reloader;
    u64 tick = 0;

    explicit ReloadRig(u32 partitions = 4, bool enabled = true) : catalog(&registry)
    {
        REQUIRE(storage::RegisterBuiltinComponents(&registry).Ok());
        REQUIRE(registry
                    .Register<RlTrace>(
                        {.id = kRlTraceId,
                         .name = "rl_trace",
                         .fields = {storage::Field<&RlTrace::text>("text"), storage::Field<&RlTrace::value>("value")}})
                    .Ok());
        REQUIRE(RegisterStandardBindings(&bindings));
        ScriptRuntimeConfig config;
        config.backend = backend.Get();
        config.bindings = &bindings;
        config.partitions = partitions;
        config.components = &catalog;
        config.budget.instructions = 200000;
        runtime = MakeUnique<ScriptRuntime>(config);
        reloader = MakeUnique<ScriptReloader>(&runtime->Scripts(), backend.Get(), enabled);
    }

    void Module(StringView key, StringView text, StringView deps = StringView(), bool entry = true)
    {
        SharedBytesPtr bytes = CopyShared(ByteSpan(reinterpret_cast<const byte*>(text.Data()), text.Length()));
        runtime->Scripts().AddModule(key, key, bytes, 0, text.Length(), deps, entry);
    }

    ecs::Entity Make(StringView behavior)
    {
        const ecs::Entity e = ecs::CreateTransformEntity(world, ecs::LocalTransform());
        REQUIRE(world.Add(e, RlTrace()) == ecs::Status::Ok);
        REQUIRE(world.Add(e, ScriptComponent(behavior)) == ecs::Status::Ok);
        return e;
    }

    const RlTrace& Trace(ecs::Entity e) const { return *static_cast<const ecs::World&>(world).Get<RlTrace>(e); }

    void Tick(u32 count = 1, jobs::Scheduler* scheduler = nullptr)
    {
        for (u32 i = 0; i < count; ++i)
        {
            runtime->Tick(&world, 0.1, scheduler);
        }
    }

    // Reloads one module with the next tick number.
    Outcome Reload(StringView key, const String<HeapAllocator>& text, ReloadReport* report)
    {
        Vector<ModuleReplacement> changes;
        ModuleReplacement r;
        r.key = String<HeapAllocator>(HeapAllocator::Default(), key);
        r.bytes = CopyShared(ByteSpan(reinterpret_cast<const byte*>(text.CStr()), text.Length()));
        r.size = text.Length();
        changes.Push(Move(r));
        return reloader->Reload(Span<const ModuleReplacement>(changes.Data(), changes.Size()), ++tick, report);
    }
};

}  // namespace

TEST_CASE("Hot reload: the new version runs, and the hooks carry the state over", "[script][reload]")
{
    ReloadRig rig;
    rig.Module("counter", Counter("v1", 1, true).View());
    const ecs::Entity e = rig.Make("counter");
    rig.Tick(3);
    CHECK(rig.Trace(e).text.View() == StringView("v1"));
    const i32 before = rig.Trace(e).value;
    REQUIRE(before >= 2);
    ReloadReport report;
    REQUIRE(rig.Reload("counter", Counter("v2", 10, true), &report).IsOk());
    CHECK(report.applied);
    CHECK(report.result.instances_rebound == 1);
    CHECK(report.result.hook_errors == 0);
    rig.Tick(1);
    CHECK(rig.Trace(e).text.View() == StringView("v2"));
    CHECK(rig.Trace(e).value == before + 1000 + 10);  // restore added 1000, then v2's step
    CHECK(rig.runtime->Stats().errors == 0);
}

TEST_CASE("Hot reload: without hooks an instance keeps its own fields and gets the new callbacks", "[script][reload]")
{
    ReloadRig rig;
    rig.Module("counter", Counter("v1", 1, false).View());
    const ecs::Entity e = rig.Make("counter");
    rig.Tick(3);
    const i32 before = rig.Trace(e).value;
    ReloadReport report;
    REQUIRE(rig.Reload("counter", Counter("v2", 5, false), &report).IsOk());
    rig.Tick(1);
    CHECK(rig.Trace(e).text.View() == StringView("v2"));
    CHECK(rig.Trace(e).value == before + 5);
}

TEST_CASE("Hot reload: a syntax error is reported and the old version keeps running", "[script][reload]")
{
    ReloadRig rig;
    rig.Module("counter", Counter("v1", 1, false).View());
    const ecs::Entity e = rig.Make("counter");
    rig.Tick(2);
    ReloadReport report;
    String<HeapAllocator> broken(HeapAllocator::Default(),
                                 StringView("behavior('counter', {\n  on_update = function(self)\n"));
    REQUIRE(rig.Reload("counter", broken, &report).IsOk());  // a compile failure is in the report, not the outcome
    CHECK_FALSE(report.applied);
    REQUIRE(report.compile_failures.Size() == 1);
    CHECK(report.compile_failures[0].key.View() == StringView("counter"));
    CHECK(report.compile_failures[0].line != 0);
    rig.Tick(2);
    CHECK(rig.Trace(e).text.View() == StringView("v1"));
    CHECK(rig.runtime->Stats().errors == 0);
}

TEST_CASE("Hot reload: a text that fails when it runs leaves every partition on the old version", "[script][reload]")
{
    ReloadRig rig(4);
    rig.Module("counter", Counter("v1", 1, false).View());
    Vector<ecs::Entity> entities;
    for (u32 i = 0; i < 8; ++i)
    {
        entities.Push(rig.Make("counter"));
    }
    rig.Tick(2);
    ReloadReport report;
    const Outcome failed = rig.Reload(
        "counter", String<HeapAllocator>(HeapAllocator::Default(), StringView("error('boom')")), &report);
    CHECK_FALSE(failed.IsOk());
    CHECK_FALSE(report.applied);
    CHECK(report.run_failure.message.View().Contains(StringView("boom")));
    rig.Tick(1);
    for (usize i = 0; i < entities.Size(); ++i)
    {
        CHECK(rig.Trace(entities[i]).text.View() == StringView("v1"));
    }
    // the world is still usable: a good reload now works
    REQUIRE(rig.Reload("counter", Counter("v3", 1, false), &report).IsOk());
    CHECK(report.applied);
    rig.Tick(1);
    CHECK(rig.Trace(entities[0]).text.View() == StringView("v3"));
}

TEST_CASE("Hot reload: a coroutine waiting across a reload resumes", "[script][reload]")
{
    ReloadRig rig;
    rig.Module("sleeper", "behavior('sleeper', {\n"
                          "  on_create = function(self)\n"
                          "    wait(0.25)\n"
                          "    component.set(self.entity, 'rl_trace', { text = 'woke', value = 1 })\n"
                          "  end,\n"
                          "})\n");
    const ecs::Entity e = rig.Make("sleeper");
    rig.Tick(1);
    CHECK(rig.runtime->Stats().coroutines_waiting == 1);
    ReloadReport report;
    REQUIRE(rig.Reload("sleeper",
                       String<HeapAllocator>(HeapAllocator::Default(),
                                             StringView("behavior('sleeper', { on_update = function(self) end })\n")),
                       &report)
                .IsOk());
    CHECK(report.applied);
    rig.Tick(5);
    CHECK(rig.Trace(e).text.View() == StringView("woke"));
    CHECK(rig.runtime->Stats().errors == 0);
}

TEST_CASE("Hot reload: reloading a dependency runs its dependents again, in order", "[script][reload]")
{
    ReloadRig rig;
    rig.Module("lib", "return { value = 1 }\n", StringView(), false);
    rig.Module("user",
               "local lib = __require('lib')\n"
               "behavior('user', {\n"
               "  on_update = function(self)\n"
               "    component.set(self.entity, 'rl_trace', { text = 'u', value = lib.value })\n"
               "  end,\n"
               "})\n",
               "lib", true);
    const ecs::Entity e = rig.Make("user");
    rig.Tick(2);
    CHECK(rig.Trace(e).value == 1);
    ReloadReport report;
    REQUIRE(rig.Reload("lib", String<HeapAllocator>(HeapAllocator::Default(), StringView("return { value = 2 }\n")),
                       &report)
                .IsOk());
    CHECK(report.applied);
    CHECK(report.result.modules_run == 2);  // lib and user
    rig.Tick(1);
    CHECK(rig.Trace(e).value == 2);
}

TEST_CASE("Hot reload: with hot reload off nothing happens", "[script][reload]")
{
    ReloadRig rig(2, false);
    rig.Module("counter", Counter("v1", 1, false).View());
    const ecs::Entity e = rig.Make("counter");
    rig.Tick(2);
    ReloadReport report;
    REQUIRE(rig.Reload("counter", Counter("v2", 1, false), &report).IsOk());
    CHECK_FALSE(report.enabled);
    CHECK_FALSE(report.applied);
    rig.Tick(1);
    CHECK(rig.Trace(e).text.View() == StringView("v1"));
}

TEST_CASE("Hot reload: a script reloaded twice in a tick waits, and a key given twice takes the last text",
          "[script][reload]")
{
    ReloadRig rig;
    rig.Module("counter", Counter("v1", 1, false).View());
    const ecs::Entity e = rig.Make("counter");
    rig.Tick(2);
    ReloadReport report;
    // two texts for one key in one call: the last one
    Vector<ModuleReplacement> changes;
    for (const char* v : {"v2", "v3"})
    {
        const String<HeapAllocator> text = Counter(StringView(v, 2), 1, false);
        ModuleReplacement r;
        r.key = String<HeapAllocator>(HeapAllocator::Default(), StringView("counter"));
        r.bytes = CopyShared(ByteSpan(reinterpret_cast<const byte*>(text.CStr()), text.Length()));
        r.size = text.Length();
        changes.Push(Move(r));
    }
    REQUIRE(rig.reloader->Reload(Span<const ModuleReplacement>(changes.Data(), changes.Size()), 10, &report).IsOk());
    CHECK(report.requested == 1);
    CHECK(report.applied);
    rig.Tick(1);
    CHECK(rig.Trace(e).text.View() == StringView("v3"));
    // the same key again in the same tick number: deferred
    REQUIRE(rig.reloader->Reload(Span<const ModuleReplacement>(changes.Data(), changes.Size()), 10, &report).IsOk());
    CHECK_FALSE(report.applied);
    CHECK(report.deferred.Size() == 1);
    // a later tick takes it
    REQUIRE(rig.reloader->Reload(Span<const ModuleReplacement>(changes.Data(), changes.Size()), 11, &report).IsOk());
    CHECK(report.applied);
}

TEST_CASE("Hot reload: a reload leaves no instance's identity behind", "[script][reload]")
{
    ReloadRig rig(1);
    // `probe` reports whether `spawn` works outside any callback: it does only when some instance's identity is
    // still set in the behavior host.
    rig.Module("counter", Counter("v1", 1, true).View());
    rig.Module("probe", "function probe() return (pcall(spawn, function() end)) end\n");
    rig.Module("prober", "behavior('prober', { on_update = function(self)\n"
                         "  component.set(self.entity, 'rl_trace', { text = 'p', value = probe() and 1 or 0 })\n"
                         "end })\n");
    rig.Make("counter");
    const ecs::Entity prober = rig.Make("prober");
    rig.Tick(1);
    CHECK(rig.Trace(prober).value == 1);  // inside a callback the probe works: it can see an identity
    ReloadReport report;
    REQUIRE(rig.Reload("counter", Counter("v2", 1, true), &report).IsOk());
    REQUIRE(report.result.instances_rebound == 1);
    PartitionLease lease;
    Outcome error;
    REQUIRE(rig.runtime->Scripts().Checkout(0, &lease, &error) == CheckoutStatus::Ok);
    CallOutput out;
    REQUIRE(lease.Call("probe", Span<const Value>(), Budget(), &out).IsOk());
    REQUIRE(out.results.Size() == 1);
    CHECK_FALSE(out.results[0].AsBool());
}

TEST_CASE("Hot reload: 100 reloads while ticks run on 16 threads keep every instance consistent",
          "[script][reload][threads]")
{
    jobs::Scheduler::Config config;
    config.participants = 16;
    config.pin = false;
    jobs::Scheduler scheduler(config);
    ReloadRig rig(8);
    rig.Module("counter", Counter("v0", 1, true).View());
    Vector<ecs::Entity> entities;
    for (u32 i = 0; i < 64; ++i)
    {
        entities.Push(rig.Make("counter"));
    }
    rig.Tick(1, &scheduler);
    u32 applied = 0;
    for (u32 k = 1; k <= 100; ++k)
    {
        char version[16];
        FormatTo(version, sizeof(version), "v%u", k);
        ReloadReport report;
        // the hooks add 1000 on each restore, so the values are n + 1000 * reloads; every instance must agree
        REQUIRE(rig.Reload("counter", Counter(StringView(version, CStrLength(version)), 1, true), &report).IsOk());
        applied += report.applied ? 1u : 0u;
        rig.Tick(1, &scheduler);
    }
    CHECK(applied == 100);
    const i32 first = rig.Trace(entities[0]).value;
    for (usize i = 0; i < entities.Size(); ++i)
    {
        CHECK(rig.Trace(entities[i]).value == first);
        CHECK(rig.Trace(entities[i]).text.View() == StringView("v100"));
    }
    CHECK(rig.runtime->Stats().errors == 0);
}

TEST_CASE("Hot reload: the development watcher reports a changed file once", "[script][reload][watch]")
{
    io_test::TempDir dir;
    ScriptDevWatcher off((StringView()));
    CHECK_FALSE(off.Active());
    ScriptDevWatcher watcher(dir.Path().View());
    REQUIRE(watcher.Active());
    const asset::ResourceId id = asset::ResourceId::FromValue(0xA1);
    char hex[17];
    asset::FormatResourceId(id, hex);
    String<HeapAllocator> name(HeapAllocator::Default(), StringView(hex, 16));
    name.Append(StringView(".lua"));
    const io::Path file = dir.Path().Join(name.View());
    REQUIRE(io::WriteFile(file, "x = 1", 5).Ok());
    watcher.Watch(id);
    Vector<DevChange> changes;
    ThisThread::SleepForMilliseconds(50);
    watcher.Poll(&changes);
    CHECK(changes.Size() == 0);  // the baseline is not a change
    REQUIRE(io::WriteFile(file, "x = 2", 5).Ok());
    for (int i = 0; i < 40 && changes.Size() == 0; ++i)
    {
        ThisThread::SleepForMilliseconds(25);
        watcher.Poll(&changes);
    }
    REQUIRE(changes.Size() == 1);
    CHECK(changes[0].id == id);
    watcher.Poll(&changes);
    CHECK(changes.Size() == 1);  // not handled yet (say, deferred): reported again
    watcher.Applied(changes[0]);
    // the same bytes written again are not a change
    REQUIRE(io::WriteFile(file, "x = 2", 5).Ok());
    ThisThread::SleepForMilliseconds(100);
    watcher.Poll(&changes);
    CHECK(changes.Size() == 0);
}
