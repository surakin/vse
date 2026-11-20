// The script world (Phase 27 Day 451): partitions with one state each, the one-holder rule, the memory cap per
// partition, the counters and the fixed entity-to-partition hash.
#include <catch2/catch_test_macros.hpp>

#include "script/backend_registry.h"
#include "script/luajit/luajit_backend.h"
#include "script/script_settings.h"
#include "script/script_world.h"
#include "sync/atomic.h"
#include "sync/thread.h"

using namespace vse;
using namespace vse::script;

namespace
{

SharedBytesPtr SharedText(const char* text)
{
    return CopyShared(ByteSpan(reinterpret_cast<const byte*>(text), CStrLength(text)));
}

struct WorldRig
{
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    ScriptWorldConfig config;
    UniquePtr<ScriptWorld> world;

    explicit WorldRig(u32 partitions = 64, u64 memory = 0)
    {
        config.backend = backend.Get();
        config.partitions = partitions;
        config.partition_memory = memory;
        world = MakeUnique<ScriptWorld>(config);
        world->AddScript("counter", SharedText("function bump(n) hits = (hits or 0) + n return hits end"));
    }
};

i64 BumpIn(PartitionLease& lease, i64 n)
{
    const Value arg = Value::FromInt(n);
    CallOutput out;
    const Outcome o = lease.Call("bump", Span<const Value>(&arg, 1), Budget(), &out);
    return o.IsOk() && out.results.Size() == 1 ? out.results[0].AsInt() : -1;
}

}  // namespace

TEST_CASE("PartitionOf: committed values, in range, independent of anything else", "[script][world]")
{
    struct Row
    {
        u64 id;
        u32 p1, p7, p64, p1024;
    };
    static const Row rows[] = {{0, 0, 2, 47, 431},
                               {1, 0, 2, 1, 193},
                               {2, 0, 4, 14, 718},
                               {12345, 0, 5, 32, 416},
                               {0xFFFFFFFFFFFFFFFFull, 0, 0, 32, 32},
                               {0x123456789ABCDEF0ull, 0, 4, 40, 232}};
    for (const Row& r : rows)
    {
        CHECK(PartitionOf(r.id, 1) == r.p1);
        CHECK(PartitionOf(r.id, 7) == r.p7);
        CHECK(PartitionOf(r.id, 64) == r.p64);
        CHECK(PartitionOf(r.id, 1024) == r.p1024);
    }
    u32 seen[64] = {};
    for (u64 id = 0; id < 6400; ++id)
    {
        ++seen[PartitionOf(id, 64)];
    }
    for (u32 count : seen)
    {
        CHECK(count > 50);  // a reasonable spread: 100 expected per partition
        CHECK(count < 150);
    }
}

TEST_CASE("ScriptWorld: the same script runs in 64 independent partitions", "[script][world]")
{
    WorldRig rig;
    CHECK(rig.world->PartitionCount() == 64);
    CHECK(rig.world->Total().memory_bytes == 0);  // no state is made before the first checkout
    for (u32 p = 0; p < 64; ++p)
    {
        PartitionLease lease;
        Outcome error;
        REQUIRE(rig.world->Checkout(p, &lease, &error) == CheckoutStatus::Ok);
        CHECK(lease.Partition() == p);
        CHECK(BumpIn(lease, 1) == 1);
        CHECK(BumpIn(lease, 2) == 3);  // each partition has its own global
    }
    // The states are kept between leases.
    PartitionLease again;
    REQUIRE(rig.world->TryCheckout(5, &again, nullptr) == CheckoutStatus::Ok);
    CHECK(BumpIn(again, 1) == 4);
    again.Release();
    CHECK_FALSE(again.Valid());
    const PartitionCounters total = rig.world->Total();
    CHECK(total.calls == 129);
    CHECK(total.memory_bytes > 0);
    u64 calls = 0;
    u64 memory = 0;
    for (u32 p = 0; p < 64; ++p)
    {
        calls += rig.world->Counters(p).calls;
        memory += rig.world->Counters(p).memory_bytes;
    }
    CHECK(calls == total.calls);
    CHECK(memory == total.memory_bytes);
    CHECK(rig.world->Counters(64).calls == 0);
}

TEST_CASE("ScriptWorld: scripts added later reach states that already exist", "[script][world]")
{
    WorldRig rig(4);
    {
        PartitionLease lease;
        REQUIRE(rig.world->TryCheckout(1, &lease, nullptr) == CheckoutStatus::Ok);
        CHECK(BumpIn(lease, 1) == 1);
    }
    rig.world->AddScript("more", SharedText("function twice(x) return x * 2 end"));
    PartitionLease lease;
    REQUIRE(rig.world->TryCheckout(1, &lease, nullptr) == CheckoutStatus::Ok);
    const Value arg = Value::FromInt(21);
    CallOutput out;
    REQUIRE(lease.Call("twice", Span<const Value>(&arg, 1), Budget(), &out).IsOk());
    CHECK(out.results[0].Equals(Value::FromInt(42)));
    CHECK(BumpIn(lease, 1) == 2);  // the state was kept, not rebuilt
}

TEST_CASE("ScriptWorld: exactly one of several threads gets a partition", "[script][world]")
{
    WorldRig rig(4);
    constexpr int kThreads = 8;
    Atomic<u32> ok(0);
    Atomic<u32> busy(0);
    Atomic<u32> attempted(0);
    Vector<UniquePtr<Thread>> threads;
    for (int t = 0; t < kThreads; ++t)
    {
        ScriptWorld* world = rig.world.Get();
        threads.Push(MakeUnique<Thread>(
            [world, &ok, &busy, &attempted]()
            {
                PartitionLease lease;
                const CheckoutStatus status = world->TryCheckout(2, &lease, nullptr);
                if (status == CheckoutStatus::Ok)
                {
                    ok.FetchAdd(1, MemoryOrder::AcqRel);
                    BumpIn(lease, 1);
                }
                else if (status == CheckoutStatus::Busy)
                {
                    busy.FetchAdd(1, MemoryOrder::AcqRel);
                }
                attempted.FetchAdd(1, MemoryOrder::AcqRel);
                // The winner keeps the partition until everybody has tried.
                while (attempted.Load(MemoryOrder::Acquire) < kThreads)
                {
                    ThisThread::Yield();
                }
            }));
    }
    for (usize i = 0; i < threads.Size(); ++i)
    {
        threads[i]->Join();
    }
    CHECK(ok.Load(MemoryOrder::Acquire) == 1);
    CHECK(busy.Load(MemoryOrder::Acquire) == kThreads - 1);
    PartitionLease lease;  // and it is free again
    CHECK(rig.world->TryCheckout(2, &lease, nullptr) == CheckoutStatus::Ok);
    CHECK(BumpIn(lease, 1) == 2);
}

TEST_CASE("ScriptWorld: a held partition is busy, a bad number is invalid", "[script][world]")
{
    WorldRig rig(2);
    PartitionLease a;
    PartitionLease b;
    Outcome error;
    REQUIRE(rig.world->TryCheckout(0, &a, &error) == CheckoutStatus::Ok);
    CHECK(rig.world->TryCheckout(0, &b, &error) == CheckoutStatus::Busy);
    CHECK_FALSE(b.Valid());
    CHECK(rig.world->TryCheckout(1, &b, &error) == CheckoutStatus::Ok);  // others are free
    CHECK(rig.world->TryCheckout(2, &b, &error) == CheckoutStatus::Invalid);
    a = static_cast<PartitionLease&&>(b);  // moving a lease releases what it held and takes the other
    CHECK(a.Partition() == 1);
    PartitionLease c;
    CHECK(rig.world->TryCheckout(0, &c, &error) == CheckoutStatus::Ok);
}

TEST_CASE("ScriptWorld: a script that does not load fails the checkout and leaves the partition free",
          "[script][world]")
{
    WorldRig rig(2);
    rig.world->AddScript("broken", SharedText("function ("));
    PartitionLease lease;
    Outcome error;
    CHECK(rig.world->TryCheckout(0, &lease, &error) == CheckoutStatus::Failed);
    CHECK(error.status == Status::SyntaxError);
    CHECK(rig.world->TryCheckout(0, &lease, &error) == CheckoutStatus::Failed);  // not Busy: it was handed back
}

TEST_CASE("ScriptWorld: a partition that runs out of memory is reset, the others are not touched", "[script][world]")
{
    WorldRig rig(3, u64{1} << 20);
    rig.world->AddScript(
        "bomb", SharedText("function bomb() local t = {} for i = 1, 100000000 do t[i] = {i} end return #t end"));
    {
        PartitionLease other;
        REQUIRE(rig.world->TryCheckout(1, &other, nullptr) == CheckoutStatus::Ok);
        CHECK(BumpIn(other, 5) == 5);
    }
    PartitionLease lease;
    REQUIRE(rig.world->TryCheckout(0, &lease, nullptr) == CheckoutStatus::Ok);
    CHECK(BumpIn(lease, 7) == 7);
    CallOutput out;
    const Outcome o = lease.Call("bomb", Span<const Value>(), Budget(), &out);
    CHECK(o.status == Status::OutOfMemory);
    CHECK(rig.world->Counters(0).out_of_memory == 1);
    CHECK(rig.world->Counters(0).resets == 1);
    CHECK(BumpIn(lease, 1) == 1);  // a fresh state with the scripts, the old global gone
    lease.Release();
    PartitionLease other;
    REQUIRE(rig.world->TryCheckout(1, &other, nullptr) == CheckoutStatus::Ok);
    CHECK(BumpIn(other, 1) == 6);  // untouched
    CHECK(rig.world->Counters(1).resets == 0);
    CHECK(rig.world->Total().resets == 1);
}

TEST_CASE("ScriptWorld: budget hits are counted", "[script][world]")
{
    WorldRig rig(1);
    rig.world->AddScript("spin", SharedText("function spin() while true do end end"));
    PartitionLease lease;
    REQUIRE(rig.world->TryCheckout(0, &lease, nullptr) == CheckoutStatus::Ok);
    Budget budget;
    budget.instructions = 100000;
    CallOutput out;
    CHECK(lease.Call("spin", Span<const Value>(), budget, &out).status == Status::BudgetExceeded);
    CHECK(rig.world->Counters(0).budget_hits == 1);
    CHECK(rig.world->Counters(0).calls == 1);
    CHECK(rig.world->Counters(0).nanoseconds > 0);
}

TEST_CASE("ScriptWorld: many threads work on different partitions and the counters add up", "[script][world]")
{
    WorldRig rig(64);
    constexpr int kThreads = 8;
    constexpr int kRounds = 20;
    Atomic<u32> wrong(0);
    Vector<UniquePtr<Thread>> threads;
    for (int t = 0; t < kThreads; ++t)
    {
        ScriptWorld* world = rig.world.Get();
        threads.Push(MakeUnique<Thread>(
            [world, t, &wrong]()
            {
                // Thread t owns the partitions t, t + 8, ... in every round.
                for (int round = 1; round <= kRounds; ++round)
                {
                    for (u32 p = static_cast<u32>(t); p < 64; p += kThreads)
                    {
                        PartitionLease lease;
                        if (world->Checkout(p, &lease, nullptr) != CheckoutStatus::Ok || BumpIn(lease, 1) != round)
                        {
                            wrong.FetchAdd(1, MemoryOrder::AcqRel);
                        }
                    }
                }
            }));
    }
    for (usize i = 0; i < threads.Size(); ++i)
    {
        threads[i]->Join();
    }
    CHECK(wrong.Load(MemoryOrder::Acquire) == 0);
    CHECK(rig.world->Total().calls == 64u * kRounds);
}

TEST_CASE("Script settings: partitions and the partition memory cap", "[script][world]")
{
    settings::Registry registry;
    RegisterScriptSettings(&registry);
    CHECK(ScriptPartitions(registry) == 64);
    CHECK(ScriptPartitionMemory(registry) == 67108864);
    CHECK(ScriptLogLimit(registry) == 64);
    CHECK(ScriptEventChainLimit(registry) == 8);
    CHECK(ScriptTaskStates(registry) == 0);
    CHECK(ScriptTaskQueueLimit(registry) == 1024);
    CHECK(ScriptTaskArgBytes(registry) == 16777216);
    CHECK(ScriptDevDirectory(registry).Length() == 0);  // off unless a development build sets it
    CHECK_FALSE(ScriptConflictsAreErrors(registry));
}

namespace
{

f32 Triple(f32 x)
{
    return x * 3.0f;
}

i32 OnlyMain()
{
    return 1;
}

}  // namespace

TEST_CASE("ScriptWorld: every partition gets the world's bindings and profile", "[script][world]")
{
    BindingRegistry registry;
    REQUIRE(registry.Add<&Triple>("t", "triple", ThreadRule::Pure, "x", "Triples."));
    REQUIRE(registry.Add<&OnlyMain>("t", "main", ThreadRule::MainThreadOnly, "", "Main thread only."));
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    ScriptWorldConfig config;
    config.backend = backend.Get();
    config.bindings = &registry;
    config.profile = InstallProfile::Partition(EntityCheck());
    config.partitions = 3;
    ScriptWorld world(config);
    world.AddScript("use", SharedText("function run() return t.triple(2) end function main() return t.main() end"));
    for (u32 p = 0; p < 3; ++p)
    {
        PartitionLease lease;
        REQUIRE(world.TryCheckout(p, &lease, nullptr) == CheckoutStatus::Ok);
        CallOutput out;
        REQUIRE(lease.Call("run", Span<const Value>(), Budget(), &out).IsOk());
        CHECK(out.results[0].ToDouble() == 6.0);
        const Outcome o = lease.Call("main", Span<const Value>(), Budget(), &out);
        CHECK(o.status == Status::RuntimeError);  // a partition state refuses main-thread-only bindings
        CHECK(o.message.View().Contains("main-thread-only"));
    }
}

TEST_CASE("ScriptWorld: without the reset policy an out-of-memory state is kept", "[script][world]")
{
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    ScriptWorldConfig config;
    config.backend = backend.Get();
    config.partitions = 1;
    config.partition_memory = u64{1} << 20;
    config.reset_on_out_of_memory = false;
    ScriptWorld world(config);
    world.AddScript("s", SharedText("function bomb() local t = {} for i = 1, 100000000 do t[i] = {i} end return #t end "
                                    "function mark() kept = 1 return kept end"));
    PartitionLease lease;
    REQUIRE(world.TryCheckout(0, &lease, nullptr) == CheckoutStatus::Ok);
    State* before = lease.GetState();
    CallOutput out;
    CHECK(lease.Call("bomb", Span<const Value>(), Budget(), &out).status == Status::OutOfMemory);
    CHECK(world.Counters(0).out_of_memory == 1);
    CHECK(world.Counters(0).resets == 0);
    CHECK(lease.GetState() == before);
}
