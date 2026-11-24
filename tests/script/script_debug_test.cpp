// Debugging (Phase 27 Day 465): the text and the event of a script error, the console, the profiler, and the debugger:
// breakpoints, stepping and inspection through a transport, with the whole world held (or only one partition) while a
// partition is stopped, on one thread and on eight.
#include <catch2/catch_test_macros.hpp>

#include "cstr/cstr.h"
#include "cstr/format.h"
#include "ecs/schedule.h"
#include "ecs/transform.h"
#include "jobs/scheduler.h"
#include "log/log.h"
#include "script/backend_registry.h"
#include "script/bindings.h"
#include "script/component_catalog.h"
#include "script/luajit/luajit_backend.h"
#include "script/script_debug.h"
#include "script/script_runtime.h"
#include "storage/builtin_components.h"
#include "sync/atomic.h"
#include "sync/thread.h"

using namespace vse;
using namespace vse::script;

namespace
{

struct Tracer
{
    String<HeapAllocator> trail{HeapAllocator::Default()};
};

constexpr u32 kTracerId = 2300;

// The probes scripts call to say what ran (the test reads them from another thread).
Atomic<u32> g_hits{0};
Atomic<bool> g_release{false};

void ProbeHit()
{
    g_hits.FetchAdd(1);
}

// Waits (at most ten seconds) for the test to release the callbacks.
void ProbeWait()
{
    for (u32 i = 0; i < 10000 && !g_release.Load(); ++i)
    {
        ThisThread::SleepForMilliseconds(1);
    }
}

usize NameOf(void*, const ecs::World&, ecs::Entity, char* out, usize capacity)
{
    const char name[] = "Hero";
    if (capacity < sizeof(name) - 1)
    {
        return 0;
    }
    MemCopy(out, name, sizeof(name) - 1);
    return sizeof(name) - 1;
}

const char* const kErrorScript =
    "behavior('breaker', {\n"                            // 1
    "  on_update = function(self)\n"                     // 2
    "    error('boom')\n"                                // 3
    "  end,\n"                                           // 4
    "})\n"                                               // 5
    "behavior('varied', {\n"                             // 6
    "  on_update = function(self)\n"                     // 7
    "    self.n = (self.n or 0) + 1\n"                   // 8
    "    error('v' .. (self.n % 2))\n"                   // 9
    "  end,\n"                                           // 10
    "})\n"                                               // 11
    "behavior('listener', {\n"                           // 12
    "  on_create = function(self)\n"                     // 13
    "    event.on('on_script_error', function(me, p)\n"  // 14
    "      component.set(me.entity, 'tracer', { trail = p.behavior .. ':' .. p.callback .. ':' .. p.entity_name })\n"
    "    end)\n"
    "  end,\n"
    "})\n"
    "answer = 42\n"
    "function work(n)\n"
    "  local s = 0\n"
    "  for i = 1, n do s = s + i end\n"
    "  return s\n"
    "end\n"
    "behavior('worker', {\n"
    "  on_update = function(self) work(100) end,\n"
    "})\n";

const char* const kStepScript = "behavior('stepper', {\n"         // 1
                                "  on_update = function(self)\n"  // 2
                                "    local a = 1\n"               // 3
                                "    local b = a + 1\n"           // 4
                                "    probe.hit()\n"               // 5
                                "    local c = b + 1\n"           // 6
                                "  end,\n"                        // 7
                                "})\n";

// A debug instance stops in `stop_here` (line 2 of the chunk 'gate'); the others wait for the test and count.
const char* const kGateScript = "function stop_here()\n"  // 1
                                "  local marker = 1\n"    // 2
                                "end\n"                   // 3
                                "behavior('debugged', {\n"
                                "  on_update = function(self)\n"
                                "    stop_here()\n"
                                "    probe.hit()\n"
                                "  end,\n"
                                "})\n"
                                "behavior('plain', {\n"
                                "  on_update = function(self)\n"
                                "    probe.wait()\n"
                                "    probe.hit()\n"
                                "  end,\n"
                                "})\n";

jobs::Scheduler::Config ConfigFor(usize participants)
{
    jobs::Scheduler::Config config;
    config.participants = participants;
    config.pin = false;
    return config;
}

struct DebugRig
{
    storage::ComponentRegistry registry;
    ComponentCatalog catalog;
    BindingRegistry bindings;
    SceneServices scene;
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    ecs::World world;
    UniquePtr<ScriptRuntime> runtime;

    explicit DebugRig(u32 partitions = 4, OnScriptError on_error = OnScriptError::Disable, bool pause_all = true,
                      bool profile = false)
        : catalog(&registry)
    {
        g_hits.Store(0);
        g_release.Store(false);
        REQUIRE(storage::RegisterBuiltinComponents(&registry).Ok());
        REQUIRE(registry
                    .Register<Tracer>(
                        {.id = kTracerId, .name = "tracer", .fields = {storage::Field<&Tracer::trail>("trail")}})
                    .Ok());
        REQUIRE(RegisterStandardBindings(&bindings));
        REQUIRE(bindings.Add<&ProbeHit>("probe", "hit", ThreadRule::Pure, "", "Counts a call."));
        REQUIRE(bindings.Add<&ProbeWait>("probe", "wait", ThreadRule::Pure, "", "Waits for the test."));
        scene.name_of = &NameOf;
        ScriptRuntimeConfig config;
        config.backend = backend.Get();
        config.bindings = &bindings;
        config.partitions = partitions;
        config.on_error = on_error;
        config.components = &catalog;
        config.scene = &scene;
        config.debug_pause_all = pause_all;
        config.profile = profile;
        runtime = MakeUnique<ScriptRuntime>(config);
    }

    void Add(const char* name, const char* text)
    {
        runtime->AddScript(name, CopyShared(ByteSpan(reinterpret_cast<const byte*>(text), CStrLength(text))));
    }

    ecs::Entity Make(StringView behavior, bool with_tracer = true)
    {
        const ecs::Entity e = ecs::CreateTransformEntity(world, ecs::LocalTransform());
        if (with_tracer)
        {
            REQUIRE(world.Add(e, Tracer()) == ecs::Status::Ok);
        }
        REQUIRE(world.Add(e, ScriptComponent(behavior)) == ecs::Status::Ok);
        return e;
    }

    String<HeapAllocator> Trail(ecs::Entity e) const
    {
        const Tracer* t = static_cast<const ecs::World&>(world).Get<Tracer>(e);
        return t != nullptr ? String<HeapAllocator>(HeapAllocator::Default(), t->trail.View())
                            : String<HeapAllocator>(HeapAllocator::Default());
    }
};

// The log lines written since `from`, joined.
void CollectLine(const char* line, usize length, void* context)
{
    static_cast<String<HeapAllocator>*>(context)->Append(StringView(line, length));
}

String<HeapAllocator> LogSince(u64 from)
{
    String<HeapAllocator> text(HeapAllocator::Default());
    ReadLogLinesSince(from, &CollectLine, &text);
    return text;
}

bool Contains(const String<HeapAllocator>& text, const char* part)
{
    return text.View().Find(StringView(part)) < text.Length();
}

Value Cmd(const char* name)
{
    Value m = Value::MakeMap();
    m.Set("cmd", Value::FromText(name));
    return m;
}

Value BreakpointAt(const char* script, i64 line)
{
    Value command = Cmd("set_breakpoints");
    Value list = Value::MakeArray();
    if (line > 0)
    {
        Value entry = Value::MakeMap();
        entry.Set("script", Value::FromText(script));
        entry.Set("line", Value::FromInt(line));
        list.Push(Move(entry));
    }
    command.Set("breakpoints", Move(list));
    return command;
}

// Waits for the next event called `name` (other events are skipped); false after ten seconds.
bool WaitEvent(InProcessDebugTransport* t, const char* name, Value* out)
{
    for (u32 i = 0; i < 100; ++i)
    {
        Value v;
        if (!t->NextEvent(&v, 100))
        {
            continue;
        }
        const Value* kind = v.Find("event");
        if (kind != nullptr && kind->Text() == StringView(name))
        {
            *out = Move(v);
            return true;
        }
    }
    return false;
}

i64 IntOf(const Value& map, const char* key)
{
    const Value* v = map.Find(key);
    return v != nullptr && v->Kind() == ValueKind::Int ? v->AsInt() : -1;
}

}  // namespace

TEST_CASE("Script errors: the log carries the entity, the callback, the script line and the trace", "[script][debug]")
{
    DebugRig rig;
    rig.Add("errors", kErrorScript);
    const ecs::Entity e = rig.Make("breaker", false);
    const u64 from = LogLinesWritten();
    rig.runtime->Tick(&rig.world, 0.1);
    const String<HeapAllocator> log = LogSince(from);
    char head[160];
    FormatTo(head, sizeof(head), "behavior 'breaker' on entity %llu ('Hero'): on_update failed: errors:3: boom",
             static_cast<unsigned long long>(e.ToU64()));
    CHECK(Contains(log, head));
    CHECK(Contains(log, "stack traceback:"));
    REQUIRE(rig.runtime->RecentErrors().Size() == 1);
    const ScriptErrorRecord& r = rig.runtime->RecentErrors()[0];
    CHECK(r.behavior.View() == StringView("breaker"));
    CHECK(r.callback.View() == StringView("on_update"));
    CHECK(r.message.View() == StringView("errors:3: boom"));
    CHECK(r.entity_name.View() == StringView("Hero"));
    CHECK(r.entity_key == e.ToU64());
    CHECK(r.count == 1);
    CHECK(rig.runtime->Stats().errors == 1);
    CHECK(rig.runtime->Stats().distinct_errors == 1);
}

TEST_CASE("Script errors: a repeating error is logged once and counted, with the count shown at powers of ten",
          "[script][debug]")
{
    DebugRig rig(4, OnScriptError::Retry);
    rig.Add("errors", kErrorScript);
    rig.Make("breaker", false);
    const u64 from = LogLinesWritten();
    for (u32 i = 0; i < 12; ++i)
    {
        rig.runtime->Tick(&rig.world, 0.1);
    }
    CHECK(rig.runtime->Stats().errors == 12);
    CHECK(rig.runtime->Stats().distinct_errors == 1);
    REQUIRE(rig.runtime->RecentErrors().Size() == 1);
    CHECK(rig.runtime->RecentErrors()[0].count == 12);
    const String<HeapAllocator> log = LogSince(from);
    CHECK(Contains(log, "errors:3: boom (repeated 10 times)"));
    CHECK_FALSE(Contains(log, "(repeated 11 times)"));
}

TEST_CASE("Script errors: a different message is a new error, and every new error is an on_script_error event",
          "[script][debug]")
{
    DebugRig rig(4, OnScriptError::Retry);
    rig.Add("errors", kErrorScript);
    const ecs::Entity listener = rig.Make("listener");
    rig.Make("varied", false);
    for (u32 i = 0; i < 4; ++i)
    {
        rig.runtime->Tick(&rig.world, 0.1);
    }
    // 'varied' alternates 'v1', 'v0', 'v1', 'v0': each one differs from the one before.
    CHECK(rig.runtime->Stats().errors == 4);
    CHECK(rig.runtime->Stats().distinct_errors == 4);
    CHECK(rig.runtime->RecentErrors().Size() == 4);
    CHECK(rig.runtime->RecentErrors()[0].message.View() == StringView("errors:9: v1"));
    CHECK(rig.runtime->RecentErrors()[1].message.View() == StringView("errors:9: v0"));
    CHECK(rig.Trail(listener).View() == StringView("varied:on_update:Hero"));
}

TEST_CASE("Console: a chunk runs in a partition or in a scratch state and returns values or the script's error",
          "[script][debug][console]")
{
    DebugRig rig;
    rig.Add("errors", kErrorScript);
    ConsoleResult r;
    rig.runtime->ConsoleRun(kConsoleScratch, "1 + 2", &r);
    REQUIRE(r.ok);
    REQUIRE(r.values.Size() == 1);
    CHECK(r.values[0].AsInt() == 3);
    rig.runtime->ConsoleRun(kConsoleScratch, "local t = {} ; t.x = 5", &r);
    CHECK(r.ok);
    CHECK(r.values.Size() == 0);
    rig.runtime->ConsoleRun(kConsoleScratch, "error('bad')", &r);
    CHECK_FALSE(r.ok);
    CHECK(r.status == Status::RuntimeError);
    CHECK(Contains(r.message, "bad"));
    rig.runtime->ConsoleRun(kConsoleScratch, "1 +", &r);
    CHECK_FALSE(r.ok);
    CHECK(r.status == Status::SyntaxError);
    // The partition's own globals are there; the scratch state has none of them.
    rig.runtime->ConsoleRun(1, "answer", &r);
    REQUIRE(r.ok);
    REQUIRE(r.values.Size() == 1);
    CHECK(r.values[0].AsInt() == 42);
    rig.runtime->ConsoleRun(kConsoleScratch, "answer", &r);
    REQUIRE(r.ok);
    CHECK((r.values.Size() == 0 || r.values[0].IsNil()));
    rig.runtime->ConsoleRun(1, "wait(1)", &r);  // a chunk cannot wait: it fails, nothing is left suspended
    CHECK_FALSE(r.ok);
    rig.runtime->ConsoleRun(9999, "1", &r);
    CHECK_FALSE(r.ok);
}

TEST_CASE("Console: queued chunks run at the start of the next tick, in order", "[script][debug][console]")
{
    DebugRig rig;
    rig.Add("errors", kErrorScript);
    const u64 a = rig.runtime->ConsoleSubmit(0, "answer = answer + 1; return answer");
    const u64 b = rig.runtime->ConsoleSubmit(0, "answer = answer * 2; return answer");
    ConsoleResult r;
    CHECK_FALSE(rig.runtime->ConsoleTake(a, &r));
    rig.runtime->Tick(&rig.world, 0.1);
    REQUIRE(rig.runtime->ConsoleTake(a, &r));
    REQUIRE(r.ok);
    CHECK(r.values[0].AsInt() == 43);
    REQUIRE(rig.runtime->ConsoleTake(b, &r));
    CHECK(r.values[0].AsInt() == 86);
    CHECK_FALSE(rig.runtime->ConsoleTake(a, &r));  // taken already
}

TEST_CASE("Profiler: off by default, counts calls and time per function when on", "[script][debug][profile]")
{
    {
        DebugRig rig;
        rig.Add("errors", kErrorScript);
        rig.Make("worker", false);
        rig.runtime->Tick(&rig.world, 0.1);
        Vector<ProfileEntry> report;
        CHECK(rig.runtime->ProfileReport(&report, 10).IsOk());
        CHECK(report.Size() == 0);
    }
    DebugRig rig(4, OnScriptError::Disable, true, true);
    rig.Add("errors", kErrorScript);
    for (u32 i = 0; i < 5; ++i)
    {
        rig.Make("worker", false);
    }
    for (u32 i = 0; i < 3; ++i)
    {
        rig.runtime->Tick(&rig.world, 0.1);
    }
    Vector<ProfileEntry> report;
    REQUIRE(rig.runtime->ProfileReport(&report, 0).IsOk());
    REQUIRE(report.Size() > 0);
    const ProfileEntry* work = nullptr;
    for (usize i = 0; i < report.Size(); ++i)
    {
        if (report[i].name.View() == StringView("work"))
        {
            work = &report[i];
        }
        if (i > 0)
        {
            CHECK(report[i - 1].nanoseconds >= report[i].nanoseconds);
        }
    }
    REQUIRE(work != nullptr);
    CHECK(work->calls == 15);  // five instances, three ticks
    CHECK(work->chunk.View() == StringView("errors"));
    CHECK(work->line == 20);
    Vector<ProfileEntry> top;
    REQUIRE(rig.runtime->ProfileReport(&top, 2).IsOk());
    CHECK(top.Size() == 2);
}

TEST_CASE("Debug transport: the queue keeps the newest events and counts what it dropped", "[script][debug]")
{
    InProcessDebugTransport t(2);
    for (i64 i = 1; i <= 3; ++i)
    {
        Value v = Value::MakeMap();
        v.Set("n", Value::FromInt(i));
        t.PostEvent(Move(v));
    }
    CHECK(t.Dropped() == 1);
    Value v;
    REQUIRE(t.NextEvent(&v, 0));
    CHECK(IntOf(v, "n") == 2);
    REQUIRE(t.NextEvent(&v, 0));
    CHECK(IntOf(v, "n") == 3);
    CHECK_FALSE(t.NextEvent(&v, 0));
    CHECK_FALSE(t.Send(Cmd("pause")));  // no handler yet
}

TEST_CASE("Debugger: a breakpoint stops the script, which can be inspected and stepped", "[script][debug][debugger]")
{
    InProcessDebugTransport transport;  // outlives the runtime's debugger
    DebugRig rig(2);
    rig.Add("stepper", kStepScript);
    rig.Make("stepper", false);
    ScriptDebugger* debugger = rig.runtime->AttachDebugger(&transport);
    REQUIRE(debugger != nullptr);
    REQUIRE(transport.Send(BreakpointAt("stepper", 3)));
    Value ev;
    REQUIRE(WaitEvent(&transport, "breakpoints", &ev));
    CHECK(IntOf(ev, "count") == 1);

    struct Result
    {
        bool ok = true;
        i64 first_line = 0;
        i64 second_line = 0;
        i64 frames = 0;
        i64 frame_line = 0;
        bool saw_a = false;
        i64 a_value = 0;
        bool saw_self = false;
        bool step_out_left = false;
        u32 hits_at_first_stop = 99;
    } result;
    Thread client(
        [&]
        {
            Value e;
            if (!WaitEvent(&transport, "stopped", &e))
            {
                result.ok = false;
                transport.Send(Cmd("detach"));
                return;
            }
            result.first_line = IntOf(e, "line");
            result.hits_at_first_stop = g_hits.Load();
            transport.Send(Cmd("stack"));
            if (WaitEvent(&transport, "stack", &e))
            {
                const Value* frames = e.Find("frames");
                result.frames = frames != nullptr ? static_cast<i64>(frames->Size()) : -1;
                if (frames != nullptr && frames->Size() > 0)
                {
                    result.frame_line = IntOf(frames->At(0), "line");
                }
            }
            transport.Send(Cmd("step_over"));  // line 3 -> line 4
            if (!WaitEvent(&transport, "stopped", &e))
            {
                result.ok = false;
                transport.Send(Cmd("detach"));
                return;
            }
            result.second_line = IntOf(e, "line");
            Value locals = Cmd("locals");
            locals.Set("frame", Value::FromInt(0));
            transport.Send(locals);
            if (WaitEvent(&transport, "locals", &e))
            {
                const Value* vars = e.Find("variables");
                for (usize i = 0; vars != nullptr && i < vars->Size(); ++i)
                {
                    const Value* name = vars->At(i).Find("name");
                    const Value* value = vars->At(i).Find("value");
                    if (name != nullptr && name->Text() == StringView("a") && value != nullptr)
                    {
                        result.saw_a = true;
                        result.a_value = value->AsInt();
                    }
                    if (name != nullptr && name->Text() == StringView("self"))
                    {
                        result.saw_self = true;
                    }
                }
            }
            transport.Send(Cmd("step_out"));  // leaves the callback: the next stop is in the host
            if (WaitEvent(&transport, "stopped", &e))
            {
                const Value* script = e.Find("script");
                result.step_out_left = script != nullptr && script->Text() != StringView("stepper");
            }
            else
            {
                result.ok = false;
            }
            transport.Send(BreakpointAt("stepper", 0));  // no breakpoints
            transport.Send(Cmd("continue"));
        });
    rig.runtime->Tick(&rig.world, 0.1);
    client.Join();
    CHECK(result.ok);
    CHECK(result.first_line == 3);
    CHECK(result.hits_at_first_stop == 0);  // stopped before probe.hit()
    CHECK(result.frames >= 2);
    CHECK(result.frame_line == 3);
    CHECK(result.second_line == 4);
    CHECK(result.saw_self);
    CHECK(result.saw_a);
    CHECK(result.a_value == 1);
    CHECK(result.step_out_left);
    CHECK(g_hits.Load() == 1);  // the callback ran to its end
    CHECK(debugger->StopCount() == 3);
    CHECK(debugger->StoppedCount() == 0);
    rig.runtime->DetachDebugger();
}

TEST_CASE("Debugger: pause stops at the next line of a call that starts, and step_in stops at the line after",
          "[script][debug][debugger]")
{
    InProcessDebugTransport transport;  // outlives the runtime's debugger
    DebugRig rig(2);
    rig.Add("stepper", kStepScript);
    rig.Make("stepper", false);
    REQUIRE(rig.runtime->AttachDebugger(&transport) != nullptr);
    REQUIRE(transport.Send(Cmd("pause")));
    bool ok = true;
    bool first_is_pause = false;
    bool second_is_step = false;
    Thread client(
        [&]
        {
            Value e;
            ok = WaitEvent(&transport, "stopped", &e);
            first_is_pause = ok && e.Find("reason") != nullptr && e.Find("reason")->Text() == StringView("pause");
            transport.Send(Cmd("step_in"));
            ok = ok && WaitEvent(&transport, "stopped", &e);
            second_is_step = ok && e.Find("reason") != nullptr && e.Find("reason")->Text() == StringView("step");
            transport.Send(Cmd("detach"));
        });
    rig.runtime->Tick(&rig.world, 0.1);
    client.Join();
    CHECK(ok);
    CHECK(first_is_pause);
    CHECK(second_is_step);
    CHECK(g_hits.Load() == 1);  // detached: the callback ran to its end
    rig.runtime->DetachDebugger();
}

TEST_CASE("Debugger: a bad command is an error event, and detach lets a stopped script run on",
          "[script][debug][debugger]")
{
    InProcessDebugTransport transport;  // outlives the runtime's debugger
    DebugRig rig(2);
    rig.Add("stepper", kStepScript);
    rig.Make("stepper", false);
    REQUIRE(rig.runtime->AttachDebugger(&transport) != nullptr);
    Value ev;
    transport.Send(Cmd("fly"));
    REQUIRE(WaitEvent(&transport, "error", &ev));
    transport.Send(Cmd("continue"));  // nothing is stopped
    REQUIRE(WaitEvent(&transport, "error", &ev));
    Value bad = Cmd("set_breakpoints");
    transport.Send(bad);
    REQUIRE(WaitEvent(&transport, "error", &ev));
    // A line over 32 bits is an error, not line 4 after a cut to 32 bits.
    transport.Send(BreakpointAt("stepper", (i64{1} << 32) + 4));
    REQUIRE(WaitEvent(&transport, "error", &ev));
    REQUIRE(transport.Send(BreakpointAt("stepper", 4)));
    bool stopped = false;
    Thread client(
        [&]
        {
            Value e;
            stopped = WaitEvent(&transport, "stopped", &e);
            transport.Send(Cmd("detach"));
        });
    rig.runtime->Tick(&rig.world, 0.1);
    client.Join();
    CHECK(stopped);
    CHECK(g_hits.Load() == 1);
    rig.runtime->Tick(&rig.world, 0.1);  // the breakpoint is gone: no stop
    CHECK(g_hits.Load() == 2);
}

TEST_CASE("Debugger: destroying the runtime with a debugger attached is safe", "[script][debug][debugger]")
{
    InProcessDebugTransport transport;
    {
        DebugRig rig(2);
        rig.Add("stepper", kStepScript);
        rig.Make("stepper", false);
        REQUIRE(rig.runtime->AttachDebugger(&transport) != nullptr);
        rig.runtime->Tick(&rig.world, 0.1);
    }
    CHECK_FALSE(transport.Send(Cmd("pause")));  // the debugger removed its handler
}

namespace
{

struct GateResult
{
    bool ok = true;
    u32 after_release_1 = 0;
    u32 after_release_2 = 0;
};

// One debugged instance and 63 plain ones, in other partitions, that wait for the test.
GateResult RunGate(bool pause_all, usize participants)
{
    jobs::Scheduler scheduler(ConfigFor(participants));
    InProcessDebugTransport transport;  // outlives the runtime's debugger
    DebugRig rig(16, OnScriptError::Disable, pause_all);
    rig.Add("gate", kGateScript);
    const ecs::Entity debugged = rig.Make("debugged", false);
    const u32 held_partition = PartitionOf(debugged.ToU64(), 16);
    // The plain instances wait for the test, so they must neither share the debugged instance's partition (it could run
    // after one of them) nor fill every thread (its partition needs one to run on): they live in six other partitions.
    bool allowed[16] = {};
    for (u32 p = 0, taken = 0; p < 16 && taken < 6; ++p)
    {
        if (p != held_partition)
        {
            allowed[p] = true;
            ++taken;
        }
    }
    for (u32 made = 0; made < 63;)
    {
        const ecs::Entity e = ecs::CreateTransformEntity(rig.world, ecs::LocalTransform());
        if (allowed[PartitionOf(e.ToU64(), 16)])
        {
            REQUIRE(rig.world.Add(e, ScriptComponent("plain")) == ecs::Status::Ok);
            ++made;
        }
    }
    REQUIRE(rig.runtime->AttachDebugger(&transport) != nullptr);
    REQUIRE(transport.Send(BreakpointAt("gate", 2)));
    GateResult result;
    Thread client(
        [&]
        {
            Value e;
            if (!WaitEvent(&transport, "breakpoints", &e) || !WaitEvent(&transport, "stopped", &e))
            {
                result.ok = false;
                g_release.Store(true);
                transport.Send(Cmd("detach"));
                return;
            }
            g_release.Store(true);  // the plain callbacks that are running now finish
            ThisThread::SleepForMilliseconds(150);
            result.after_release_1 = g_hits.Load();
            ThisThread::SleepForMilliseconds(300);
            result.after_release_2 = g_hits.Load();
            transport.Send(BreakpointAt("gate", 0));
            transport.Send(Cmd("continue"));
        });
    rig.runtime->Tick(&rig.world, 0.1, &scheduler);
    client.Join();
    CHECK(g_hits.Load() == 64);
    rig.runtime->DetachDebugger();
    return result;
}

}  // namespace

TEST_CASE("Debugger: a stop holds every other partition at the end of its callback (pause_all) on 8 threads",
          "[script][debug][debugger][parallel]")
{
    const GateResult held = RunGate(true, 8);
    CHECK(held.ok);
    CHECK(held.after_release_1 == held.after_release_2);  // nothing moved while the world was held
    CHECK(held.after_release_2 <= 8);                     // only the callbacks already running finished
}

TEST_CASE("Debugger: without pause_all only the stopped partition waits", "[script][debug][debugger][parallel]")
{
    const GateResult one = RunGate(false, 8);
    CHECK(one.ok);
    CHECK(one.after_release_2 > 8);  // the other partitions ran on while one was stopped
    CHECK(one.after_release_2 < 64);
}

TEST_CASE("Debugger: stops, steps and continues on 8 threads without a deadlock", "[script][debug][debugger][parallel]")
{
    for (u32 round = 0; round < 3; ++round)
    {
        jobs::Scheduler scheduler(ConfigFor(8));
        InProcessDebugTransport transport;  // outlives the runtime's debugger
        DebugRig rig(16);
        rig.Add("stepper", kStepScript);
        for (u32 i = 0; i < 48; ++i)
        {
            rig.Make("stepper", false);
        }
        REQUIRE(rig.runtime->AttachDebugger(&transport) != nullptr);
        REQUIRE(transport.Send(BreakpointAt("stepper", 3)));
        bool ok = true;
        u32 stops = 0;
        Thread client(
            [&]
            {
                // Every instance stops at line 3, and several partitions may be stopped at once: answer each stop
                // on its own partition, with a step at line 3 and a continue at line 4.
                while (g_hits.Load() < 48)
                {
                    Value e;
                    bool got = false;
                    for (u32 t = 0; t < 500 && !got && g_hits.Load() < 48; ++t)
                    {
                        Value v;
                        if (transport.NextEvent(&v, 20))
                        {
                            const Value* kind = v.Find("event");
                            if (kind != nullptr && kind->Text() == StringView("stopped"))
                            {
                                e = Move(v);
                                got = true;
                            }
                        }
                    }
                    if (!got)
                    {
                        ok = g_hits.Load() >= 48;
                        break;
                    }
                    const i64 line = IntOf(e, "line");
                    Value command = Cmd(line == 3 ? "step_over" : "continue");
                    command.Set("partition", Value::FromInt(IntOf(e, "partition")));
                    transport.Send(command);
                    if (line == 3)
                    {
                        ++stops;
                    }
                }
                transport.Send(Cmd("detach"));
            });
        rig.runtime->Tick(&rig.world, 0.1, &scheduler);
        client.Join();
        CHECK(ok);
        CHECK(g_hits.Load() == 48);
        CHECK(stops >= 1);
        rig.runtime->DetachDebugger();
    }
}
