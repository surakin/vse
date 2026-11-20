// Script tasks (Phase 27 Day 461): parallel work started from a script, its results, its limits, and that it cannot
// reach the world.
#include <catch2/catch_test_macros.hpp>

#include "cstr/cstr.h"
#include "cstr/format.h"
#include "ecs/transform.h"
#include "jobs/scheduler.h"
#include "os/console.h"
#include "os/os.h"
#include "script/backend_registry.h"
#include "script/bindings.h"
#include "script/component_catalog.h"
#include "script/luajit/luajit_backend.h"
#include "script/script_runtime.h"
#include "storage/builtin_components.h"

using namespace vse;
using namespace vse::script;

namespace
{

struct TaskTrace
{
    String<HeapAllocator> text{HeapAllocator::Default()};
    i32 value = 0;
};

constexpr u32 kTaskTraceId = 2131;

const char* const kTaskScripts = R"LUA(
local function put(self, s)
  local t = component.get(self.entity, 'task_trace')
  component.set(self.entity, 'task_trace', { text = t.text .. s, value = t.value + 1 })
end

behavior('adder', {
  on_create = function(self)
    local h = task.run('return args.a + args.b', { a = 1, b = 2 })
    local r, err = wait_task(h)
    put(self, tostring(r))
  end,
})
behavior('mapper', {
  on_create = function(self)
    local r, err = wait_task(task.map('return item * 2 + index', { 10, 20, 30, 40, 50 }, 2))
    put(self, err or table.concat(r, ','))
  end,
})
behavior('summer', {
  on_create = function(self)
    local h = task.parallel_for(1000000, 'local s = 0 for i = first, last do s = s + i end return s', nil, 4096)
    local parts, err = wait_task(h)
    local total = 0
    for i = 1, #parts do total = total + parts[i] end
    put(self, err or string.format('%d chunks, sum %.0f', #parts, total))
  end,
})
behavior('failer', {
  on_create = function(self)
    local r, err = wait_task(task.run("error('boom')"))
    put(self, 'E:' .. tostring(err))
  end,
})
behavior('syntaxer', {
  on_create = function(self)
    local r, err = wait_task(task.run('this is not lua'))
    put(self, 'S:' .. tostring(err))
  end,
})
behavior('chunk_failer', {
  on_create = function(self)
    local r, err = wait_task(task.map("if index == 5 then error('bad five') end return item", { 1, 2, 3, 4, 5, 6, 7, 8 }, 2))
    put(self, 'C:' .. tostring(err))
  end,
})
behavior('globaler', {
  on_create = function(self)
    local a = wait_task(task.run('leak = (leak or 0) + 1 math_copy = math return leak'))
    local b = wait_task(task.run('leak = (leak or 0) + 1 return leak'))
    local c = wait_task(task.run('random = 5 return type(random)'))
    local d = wait_task(task.run('return type(random)'))
    put(self, 'G:' .. tostring(a) .. ',' .. tostring(b) .. ',' .. c .. ',' .. d)
  end,
})
behavior('worldly', {
  on_create = function(self)
    local r, err = wait_task(task.run('return entity.create()'))
    put(self, 'W:' .. tostring(err))
  end,
})
behavior('definer', {
  on_create = function(self)
    local a, ea = wait_task(task.run('return 1 end) __task_define(1, 1, function() return 99 end) --'))
    local b, eb = wait_task(task.run('__task_define(5, 1, function() return 1 end) return 2'))
    local c, ec = wait_task(task.run('__task_expect(5) return 3'))
    local d, ed = wait_task(task.run('return 4 end) __task_expect(7) __task_define(7, 1, function() return 99 end) --'))
    local e1 = wait_task(task.run('random.float = function() return 42 end return 1'))
    local e2 = wait_task(task.run('return random.float()'))
    put(self, (e2 ~= 42 and 'fresh|' or 'stale|') .. tostring(d) .. ':' .. tostring(ed) .. '|')
    put(self, tostring(a) .. ':' .. tostring(ea) .. '|' .. tostring(b) .. ':' .. tostring(eb) .. '|' .. tostring(c) .. ':' .. tostring(ec))
  end,
})
behavior('napper', {
  on_create = function(self)
    -- A task asked for after a timed wait resumes: it runs at the end of that tick, so its waiter is not ready in the same one.
    wait_frames(1)
    local r, err = wait_task(task.run('return args + 1', 41))
    put(self, tostring(r) .. ':' .. tostring(err))
  end,
})
behavior('closer', {
  on_create = function(self)
    local ok, err = pcall(task.run, function() return 1 end)
    put(self, ok and 'accepted' or 'refused:' .. tostring(err))
  end,
})
behavior('clocker', {
  on_create = function(self)
    local r, err = wait_task(task.run('return time.real()'))
    put(self, 'T:' .. tostring(err))
  end,
})
behavior('randomer', {
  on_create = function(self)
    local a = wait_task(task.run('return { random.float(), random.int(1, 1000), math.random() }', nil, 7))
    local b = wait_task(task.run('return { random.float(), random.int(1, 1000), math.random() }', nil, 7))
    local c = wait_task(task.run('return { random.float(), random.int(1, 1000), math.random() }', nil, 8))
    put(self, (a[1] == b[1] and a[2] == b[2] and a[3] == b[3]) and 'same' or 'different')
    put(self, (a[1] ~= c[1] or a[2] ~= c[2]) and ',seeds differ' or ',seeds equal')
  end,
})
behavior('flooder', {
  on_create = function(self)
    local ok = 0
    local refused
    for i = 1, 5 do
      local h, err = task.run('return 1')
      if h then ok = ok + 1 else refused = err end
    end
    put(self, ok .. ' queued; ' .. tostring(refused))
  end,
})
behavior('fat', {
  on_create = function(self)
    local big = {}
    for i = 1, 20000 do big[i] = 'x' end
    local h, err = task.run('return #args', big)
    put(self, h and 'accepted' or 'refused:' .. tostring(err))
  end,
})
behavior('abandoner', {
  on_create = function(self)
    shared_handle = task.run('return 42')
  end,
})
behavior('thief', {
  on_update = function(self)
    if shared_handle ~= nil then
      local r, err = task.result(shared_handle)
      put(self, 'R:' .. tostring(r) .. ':' .. tostring(err))
      shared_handle = nil
    end
  end,
})
behavior('poller', {
  on_create = function(self)
    self.h = task.run('return 7')
    local r, err = task.result(self.h)
    put(self, tostring(r) .. ':' .. tostring(err))
  end,
  on_update = function(self)
    if self.h ~= nil and time.tick() >= 1 then
      local r, err = task.result(self.h)
      put(self, ' ' .. tostring(r))
      local r2, err2 = task.result(self.h)
      put(self, ' ' .. tostring(r2) .. ':' .. tostring(err2))
      self.h = nil
    end
  end,
})
behavior('noisy', {
  on_create = function(self)
    local r = wait_task(task.run('log.info("from a task") return 1'))
    put(self, tostring(r))
  end,
})
behavior('hasher', {
  on_create = function(self)
    local r = wait_task(task.map('local x = 0 for i = 1, 100 do x = x + math.random(1, 10) + item end return x', { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 }, 3, 99))
    put(self, table.concat(r, ','))
  end,
})
)LUA";

struct TaskRig
{
    storage::ComponentRegistry registry;
    ComponentCatalog catalog;
    BindingRegistry bindings;
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    ecs::World world;
    UniquePtr<ScriptRuntime> runtime;

    explicit TaskRig(u32 partitions = 8, u32 queue_limit = 1024, u64 arg_bytes = 16777216, u32 task_states = 0)
        : catalog(&registry)
    {
        REQUIRE(storage::RegisterBuiltinComponents(&registry).Ok());
        REQUIRE(registry
                    .Register<TaskTrace>({.id = kTaskTraceId,
                                          .name = "task_trace",
                                          .fields = {storage::Field<&TaskTrace::text>("text"),
                                                     storage::Field<&TaskTrace::value>("value")}})
                    .Ok());
        REQUIRE(RegisterStandardBindings(&bindings));
        ScriptRuntimeConfig config;
        config.backend = backend.Get();
        config.bindings = &bindings;
        config.partitions = partitions;
        config.components = &catalog;
        config.budget.instructions = 200000;
        config.task_queue_limit = queue_limit;
        config.task_arg_bytes = arg_bytes;
        config.task_states = task_states;
        runtime = MakeUnique<ScriptRuntime>(config);
        runtime->AddScript("task_scripts",
                           CopyShared(ByteSpan(reinterpret_cast<const byte*>(kTaskScripts), CStrLength(kTaskScripts))));
    }

    ecs::Entity Make(StringView behavior)
    {
        const ecs::Entity e = ecs::CreateTransformEntity(world, ecs::LocalTransform());
        REQUIRE(world.Add(e, TaskTrace()) == ecs::Status::Ok);
        REQUIRE(world.Add(e, ScriptComponent(behavior)) == ecs::Status::Ok);
        return e;
    }

    String<HeapAllocator> Text(ecs::Entity e) const
    {
        const TaskTrace* t = static_cast<const ecs::World&>(world).Get<TaskTrace>(e);
        return String<HeapAllocator>(HeapAllocator::Default(), t->text.View());
    }

    // The task runs at the end of the tick that asks for it and the waiter resumes at the start of the next one.
    void Tick(u32 count = 3, jobs::Scheduler* scheduler = nullptr)
    {
        for (u32 i = 0; i < count; ++i)
        {
            runtime->Tick(&world, 0.1, scheduler);
        }
    }

    String<HeapAllocator> Run(StringView behavior, u32 ticks = 3)
    {
        const ecs::Entity e = Make(behavior);
        Tick(ticks);
        return Text(e);
    }
};

}  // namespace

TEST_CASE("Tasks: run returns a result through wait_task", "[script][task]")
{
    TaskRig rig;
    CHECK(rig.Run("adder").View() == StringView("3"));
    CHECK(rig.runtime->Stats().tasks_run == 1);
    CHECK(rig.runtime->Stats().tasks_held == 0);
    CHECK(rig.runtime->Stats().errors == 0);
}

TEST_CASE("Tasks: a task asked for after a timed wait is waited for until it has run", "[script][task]")
{
    TaskRig rig;
    CHECK(rig.Run("napper", 6).View() == StringView("42:nil"));
    CHECK(rig.runtime->Stats().errors == 0);
}

TEST_CASE("Tasks: map returns the results in index order", "[script][task]")
{
    TaskRig rig;
    CHECK(rig.Run("mapper").View() == StringView("21,42,63,84,105"));
    CHECK(rig.runtime->Stats().task_chunks == 3);
}

TEST_CASE("Tasks: parallel_for over a million indices equals the serial loop", "[script][task]")
{
    TaskRig rig(8, 1024, 16777216, 4);
    const String<HeapAllocator> text = rig.Run("summer");
    CHECK(text.View() == StringView("245 chunks, sum 500000500000"));
}

TEST_CASE("Tasks: errors come back as values", "[script][task]")
{
    TaskRig rig;
    CHECK(rig.Run("failer").View().Contains(StringView("boom")));
    CHECK(rig.Run("syntaxer").View().StartsWith(StringView("S:")));
    CHECK(rig.Run("syntaxer").View().Length() > 5);
    const String<HeapAllocator> c = rig.Run("chunk_failer");
    CHECK(c.View().Contains(StringView("chunk starting at index 5")));
    CHECK(c.View().Contains(StringView("bad five")));
    CHECK(rig.runtime->Stats().tasks_failed == 4);
    CHECK(rig.runtime->Stats().errors == 0);  // a failing task is not a script error
}

TEST_CASE("Tasks: a task cannot leave globals behind, reach the world or read the clock", "[script][task]")
{
    TaskRig rig(8, 1024, 16777216, 1);  // one state: every task runs on the same one
    CHECK(rig.Run("globaler", 6).View() == StringView("G:1,1,number,table"));
    CHECK(rig.Run("worldly").View().Contains(StringView("entity")));
    CHECK(rig.Run("clocker").View().Contains(StringView("wall clock")));
    CHECK(rig.runtime->Stats().errors == 0);
}

TEST_CASE("Tasks: a task's source cannot define another task's function", "[script][task]")
{
    TaskRig rig(8, 1024, 16777216, 1);
    const String<HeapAllocator> text = rig.Run("definer", 12);
    CHECK(text.View().Contains(StringView("nil:")));
    CHECK(text.View().Contains(StringView("unexpected task definition")));
    CHECK(text.View().Contains(StringView("not available in a task")));
    CHECK_FALSE(text.View().Contains(StringView("99")));
}

TEST_CASE("Tasks: a Lua closure cannot be a task", "[script][task]")
{
    TaskRig rig;
    CHECK(rig.Run("closer").View().Contains(StringView("source text")));
}

TEST_CASE("Tasks: random numbers depend only on the seed, whatever the state ran before", "[script][task]")
{
    TaskRig rig(8, 1024, 16777216, 1);  // one state: the second task runs on the state the first one used
    CHECK(rig.Run("randomer", 5).View() == StringView("same,seeds differ"));
}

TEST_CASE("Tasks: the queue and the argument size are limited", "[script][task]")
{
    TaskRig rig(8, 3);
    const String<HeapAllocator> text = rig.Run("flooder");
    CHECK(text.View().StartsWith(StringView("3 queued; ")));
    CHECK(text.View().Contains(StringView("queue is full")));
    TaskRig small(8, 1024, 1000);
    CHECK(small.Run("fat").View().Contains(StringView("too large")));
}

TEST_CASE("Tasks: a result is taken once, and only by its owner", "[script][task]")
{
    TaskRig rig(1);
    rig.Make("abandoner");
    const ecs::Entity thief = rig.Make("thief");
    rig.Tick(4);
    CHECK(rig.Text(thief).View() == StringView("R:nil:that task belongs to another script"));
    TaskRig rig2;
    const ecs::Entity e = rig2.Make("poller");
    rig2.Tick(4);
    // the same tick: pending; the next tick: the result; taken: gone
    CHECK(rig2.Text(e).View() == StringView("nil:pending 7 nil:no such task"));
}

TEST_CASE("Tasks: a task whose owner is destroyed is dropped", "[script][task]")
{
    TaskRig rig;
    const ecs::Entity e = rig.Make("abandoner");
    rig.Tick(1);
    CHECK(rig.runtime->Stats().tasks_held == 1);
    REQUIRE(rig.world.Destroy(e) == ecs::Status::Ok);
    rig.Tick(2);
    CHECK(rig.runtime->Stats().tasks_held == 0);
}

TEST_CASE("Tasks: the same results at 1, 2, 7 and 16 threads and any number of states", "[script][task][parallel]")
{
    String<HeapAllocator> reference(HeapAllocator::Default());
    bool first = true;
    for (const usize threads : {usize{1}, usize{2}, usize{7}, usize{16}})
    {
        for (const u32 states : {1u, 3u, 16u})
        {
            jobs::Scheduler::Config sc;
            sc.participants = threads;
            sc.pin = false;
            jobs::Scheduler scheduler(sc);
            TaskRig rig(16, 1024, 16777216, states);
            Vector<ecs::Entity> all;
            for (u32 i = 0; i < 60; ++i)
            {
                all.Push(rig.Make(i % 3 == 0 ? "hasher" : (i % 3 == 1 ? "mapper" : "adder")));
            }
            rig.Tick(4, &scheduler);
            String<HeapAllocator> all_text(HeapAllocator::Default());
            for (usize i = 0; i < all.Size(); ++i)
            {
                all_text.Append(rig.Text(all[i]).View());
                all_text.Append(StringView("|"));
            }
            CHECK(rig.runtime->Stats().errors == 0);
            if (first)
            {
                reference = Move(all_text);
                first = false;
                CHECK(reference.Length() > 100);
            }
            else
            {
                CHECK(all_text.View() == reference.View());
            }
        }
    }
}

TEST_CASE("Tasks: a million-element parallel_for against the serial loop (printed, not asserted)",
          "[.][script][task][cost]")
{
    for (const usize threads : {usize{1}, usize{4}, usize{8}})
    {
        jobs::Scheduler::Config sc;
        sc.participants = threads;
        sc.pin = false;
        jobs::Scheduler scheduler(sc);
        TaskRig rig(8, 1024, 16777216, static_cast<u32>(threads));
        const ecs::Entity e = rig.Make("summer");
        const u64 start = os::MonotonicTimeNanos();
        rig.Tick(3, &scheduler);
        const u64 end = os::MonotonicTimeNanos();
        String<HeapAllocator> line(HeapAllocator::Default(), "parallel_for of 1,000,000 on ");
        line.Append(StringView(threads == 1 ? "1 thread" : (threads == 4 ? "4 threads" : "8 threads")));
        line.Append(StringView(": "));
        char ms[32];
        FormatTo(ms, sizeof(ms), "%.2f ms", static_cast<f64>(end - start) / 1.0e6);
        line.Append(StringView(ms));
        line.Append(StringView(" -> "));
        line.Append(rig.Text(e).View());
        line.Append(StringView("\n"));
        os::Print("%s", line.CStr());
    }
}
