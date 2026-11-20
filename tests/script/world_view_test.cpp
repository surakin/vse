// The world view, the command buffers and the ordered merge (Phase 27 Day 452): scripts on many threads read the
// tick-start world, record commands, and the merge gives the same world for any thread count.
#include <catch2/catch_test_macros.hpp>

#include "algorithm.h"
#include "ecs/query.h"
#include "ecs/world.h"
#include "os/console.h"
#include "os/os.h"
#include "script/luajit/luajit_backend.h"
#include "script/merge.h"
#include "script/partition_context.h"
#include "script/script_settings.h"
#include "script/script_world.h"
#include "sync/atomic.h"
#include "sync/thread.h"

using namespace vse;
using namespace vse::script;

namespace
{

struct SimPos
{
    Vec3 v;
};
struct SimVel
{
    Vec3 v;
};

enum : u16
{
    kSetVel = 1,
    kSpawn = 2,
    kDestroy = 3,
};
enum : u32
{
    kFieldVel = 1,
};

Atomic<u32> g_racy(0);
bool g_racy_enabled = false;

const PartitionContext& Ctx()
{
    return *CurrentPartitionContext();
}

Vec3 WPos(ecs::Entity e)
{
    const SimPos* p = Ctx().world->Get<SimPos>(e);
    return p != nullptr ? p->v : Vec3();
}

Vec3 WVel(ecs::Entity e)
{
    const Value* written = Ctx().overlay->Find(e, kFieldVel);
    if (written != nullptr)
    {
        return written->AsVec3();
    }
    const SimVel* v = Ctx().world->Get<SimVel>(e);
    return v != nullptr ? v->v : Vec3();
}

void WSetVel(ecs::Entity e, Vec3 v)
{
    Ctx().commands->Record(kSetVel, Ctx().current_entity_key, e, v);
    Ctx().overlay->Put(e, kFieldVel, Value::FromVec3(v));
}

ecs::Entity WSpawn(Vec3 position)
{
    const ecs::Entity placeholder = Ctx().commands->Reserve();
    Ctx().commands->Record(kSpawn, Ctx().current_entity_key, placeholder, position);
    return placeholder;
}

void WDestroy(ecs::Entity e)
{
    Ctx().commands->Record(kDestroy, Ctx().current_entity_key, e, nullptr, 0);
}

f32 WRacy()
{
    return g_racy_enabled ? static_cast<f32>(g_racy.FetchAdd(1, MemoryOrder::Relaxed)) : 0.0f;
}

bool ApplySetVel(MergeContext& context, ecs::Entity target, Span<const u8> payload)
{
    SimVel* v = context.World()->Get<SimVel>(target);
    if (v == nullptr)
    {
        return false;
    }
    MemCopy(&v->v, payload.Data(), sizeof(Vec3));
    return true;
}

bool ApplySpawn(MergeContext& context, ecs::Entity target, Span<const u8> payload)
{
    Vec3 position;
    MemCopy(&position, payload.Data(), sizeof(Vec3));
    context.Bind(target, context.World()->Create<SimPos, SimVel>(SimPos{position}, SimVel{}));
    return true;
}

bool ApplyDestroy(MergeContext& context, ecs::Entity target, Span<const u8>)
{
    return context.World()->Destroy(target) == ecs::Status::Ok;
}

SharedBytesPtr ViewText(const char* text)
{
    return CopyShared(ByteSpan(reinterpret_cast<const byte*>(text), CStrLength(text)));
}

const char kScript[] = "function on_update(e, n, i)\n"
                       "  local p, q = w.pos(e), w.pos(n)\n"
                       "  local d = (q - p) * 0.01\n"
                       "  w.set_vel(e, d)\n"
                       "  local v1 = w.vel(e)\n"
                       "  w.set_vel(e, v1 * 2)\n"
                       "  if w.vel(e).x ~= v1.x * 2 then error('the overlay lost a write') end\n"
                       "  if i % 5 == 0 then w.set_vel(n, d * -1) end\n"
                       "  if i % 97 == 0 then\n"
                       "    local c = w.spawn(p + vec3.new(1, 0, 0))\n"
                       "    w.set_vel(c, vec3.new(0, 1, 0))\n"
                       "  end\n"
                       "  if i % 101 == 0 and i > 100 then w.destroy(n) end\n"
                       "  local r = w.racy()\n"
                       "  if r ~= 0 then w.set_vel(e, vec3.new(r, 0, 0)) end\n"
                       "end\n";

u64 Mix(u64 x)
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

u32 Bits(f32 f)
{
    u32 b;
    MemCopy(&b, &f, sizeof(b));
    return b;
}

struct Sim
{
    ecs::World world;
    BindingRegistry bindings;
    CommandKindTable kinds;
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    UniquePtr<ScriptWorld> scripts;
    Vector<UniquePtr<CommandBuffer>> buffers;
    u32 partitions;
    MergeReport last_report;
    MergeReport total_report;
    ConflictPolicy policy = ConflictPolicy::LastWins;

    Sim(u32 entities, u32 partition_count = 64) : partitions(partition_count)
    {
        REQUIRE(RegisterStandardBindings(&bindings));
        REQUIRE(bindings.Add<&WPos>("w", "pos", ThreadRule::WorldRead, "e", "Position."));
        REQUIRE(bindings.Add<&WVel>("w", "vel", ThreadRule::WorldRead, "e", "Velocity."));
        REQUIRE(bindings.Add<&WSetVel>("w", "set_vel", ThreadRule::WorldWrite, "e, v", "Sets the velocity."));
        REQUIRE(bindings.Add<&WSpawn>("w", "spawn", ThreadRule::WorldWrite, "position", "Spawns."));
        REQUIRE(bindings.Add<&WDestroy>("w", "destroy", ThreadRule::WorldWrite, "e", "Destroys."));
        REQUIRE(bindings.Add<&WRacy>("w", "racy", ThreadRule::WorldRead, "", "A planted race."));
        CommandKind set{"set_vel", &ApplySetVel, false, true};
        CommandKind spawn{"spawn", &ApplySpawn, true, false};
        CommandKind destroy{"destroy", &ApplyDestroy, false, false};
        REQUIRE(kinds.Register(kSetVel, set));
        REQUIRE(kinds.Register(kSpawn, spawn));
        REQUIRE(kinds.Register(kDestroy, destroy));
        ScriptWorldConfig config;
        config.backend = backend.Get();
        config.bindings = &bindings;
        config.profile = InstallProfile::Partition(ContextEntityCheck());
        config.partitions = partitions;
        scripts = MakeUnique<ScriptWorld>(config);
        scripts->AddScript("sim", ViewText(kScript));
        for (u32 p = 0; p < partitions; ++p)
        {
            buffers.Push(MakeUnique<CommandBuffer>());
        }
        for (u32 i = 0; i < entities; ++i)
        {
            const f32 x = static_cast<f32>((i * 37) % 101);
            const f32 y = static_cast<f32>((i * 53) % 89);
            world.Create<SimPos, SimVel>(SimPos{Vec3(x, y, 0.0f)}, SimVel{});
        }
    }

    // One tick: the script phase on `threads` threads, then the merge and a single-threaded integration.
    void Tick(u32 threads)
    {
        Vector<u64> keys;
        world.Query<ecs::Read<SimPos>>().ForEach([&](ecs::Entity e, const SimPos&) { keys.Push(e.ToU64()); });
        Sort(Span<u64>(keys.Data(), keys.Size()), [](u64 a, u64 b) { return a < b; });
        Vector<Vector<u32>> lists;  // per partition: indexes into keys, ascending
        for (u32 p = 0; p < partitions; ++p)
        {
            lists.Push(Vector<u32>());
        }
        for (u32 i = 0; i < keys.Size(); ++i)
        {
            lists[PartitionOf(keys[i], partitions)].Push(i);
        }
        Atomic<u32> next(0);
        Atomic<u32> failures(0);
        {
            ReadOnlyPhase phase(&world);
            Vector<UniquePtr<Thread>> pool;
            for (u32 t = 0; t < threads; ++t)
            {
                pool.Push(MakeUnique<Thread>(
                    [&]()
                    {
                        for (;;)
                        {
                            const u32 p = next.FetchAdd(1, MemoryOrder::AcqRel);
                            if (p >= partitions)
                            {
                                return;
                            }
                            PartitionLease lease;
                            Outcome error;
                            if (scripts->Checkout(p, &lease, &error) != CheckoutStatus::Ok)
                            {
                                failures.FetchAdd(1, MemoryOrder::AcqRel);
                                continue;
                            }
                            CommandBuffer& buffer = *buffers[p];
                            buffer.Begin(p);
                            Overlay overlay;
                            PartitionContext context;
                            context.world = &world;
                            context.commands = &buffer;
                            context.overlay = &overlay;
                            ScopedPartitionContext scope(&context);
                            for (u32 k = 0; k < lists[p].Size(); ++k)
                            {
                                const u32 index = lists[p][k];
                                context.current_entity_key = keys[index];
                                overlay.Clear();
                                Value args[3] = {
                                    Value::FromEntity(ecs::Entity::FromU64(keys[index])),
                                    Value::FromEntity(ecs::Entity::FromU64(keys[(index + 37) % keys.Size()])),
                                    Value::FromInt(index)};
                                CallOutput out;
                                Budget budget;
                                budget.instructions = 5000000;
                                if (!lease.Call("on_update", Span<const Value>(args, 3), budget, &out).IsOk())
                                {
                                    failures.FetchAdd(1, MemoryOrder::AcqRel);
                                }
                            }
                        }
                    }));
            }
            for (usize t = 0; t < pool.Size(); ++t)
            {
                pool[t]->Join();
            }
        }
        REQUIRE(failures.Load(MemoryOrder::Acquire) == 0);
        Vector<CommandBuffer*> order;
        for (u32 p = 0; p < partitions; ++p)
        {
            order.Push(buffers[p].Get());
        }
        last_report = Merge(&world, Span<CommandBuffer* const>(order.Data(), order.Size()), kinds, policy);
        total_report.applied += last_report.applied;
        total_report.skipped += last_report.skipped;
        total_report.conflicts += last_report.conflicts;
        total_report.refused += last_report.refused;
        total_report.dangling += last_report.dangling;
        world.Query<ecs::Write<SimPos>, ecs::Read<SimVel>>().ForEach([](SimPos& p, const SimVel& v)
                                                                     { p.v = p.v + v.v; });
    }

    u64 Hash()
    {
        u64 sum = 0;
        world.Query<ecs::Read<SimPos>, ecs::Read<SimVel>>().ForEach(
            [&](ecs::Entity e, const SimPos& p, const SimVel& v)
            {
                u64 h = Mix(e.ToU64());
                h = Mix(h ^ Bits(p.v.x)) ^ Mix(h + Bits(p.v.y)) ^ Mix(h * 3 + Bits(v.v.x)) ^ Mix(h * 5 + Bits(v.v.y)) ^
                    Mix(h * 7 + Bits(v.v.z));
                sum += h;  // order-independent, so the hash does not depend on how the world stores its rows
            });
        return sum;
    }
};

u64 RunSim(u32 entities, u32 threads, u32 ticks, bool racy = false, ConflictPolicy policy = ConflictPolicy::LastWins,
           MergeReport* report = nullptr)
{
    g_racy_enabled = racy;
    g_racy.Store(0, MemoryOrder::Relaxed);
    Sim sim(entities);
    sim.policy = policy;
    for (u32 t = 0; t < ticks; ++t)
    {
        sim.Tick(threads);
    }
    g_racy_enabled = false;
    if (report != nullptr)
    {
        *report = sim.total_report;
    }
    return sim.Hash();
}

}  // namespace

TEST_CASE("World view: the state after several ticks is the same at 1, 2, 7 and 16 threads", "[script][worldview]")
{
    MergeReport report;
    const u64 reference = RunSim(10000, 1, 3, false, ConflictPolicy::LastWins, &report);
    CHECK(report.applied > 10000);
    CHECK(report.conflicts > 0);  // the neighbour writes cross partitions
    CHECK(report.dangling == 0);
    CHECK(RunSim(10000, 2, 3) == reference);
    CHECK(RunSim(10000, 7, 3) == reference);
    CHECK(RunSim(10000, 16, 3) == reference);
}

TEST_CASE("World view: 100 runs on 7 threads give one state", "[script][worldview]")
{
    const u64 reference = RunSim(1500, 1, 2);
    u32 different = 0;
    for (int run = 0; run < 100; ++run)
    {
        if (RunSim(1500, 7, 2) != reference)
        {
            ++different;
        }
    }
    CHECK(different == 0);
}

TEST_CASE("World view: a binding that reads a racing value is caught by the hash comparison", "[script][worldview]")
{
    const u64 serial = RunSim(4000, 1, 1, true);
    u32 differing = 0;
    for (int run = 0; run < 4; ++run)
    {
        if (RunSim(4000, 16, 1, true) != serial)
        {
            ++differing;
        }
    }
    CHECK(differing > 0);
}

TEST_CASE("World view: the conflict policy error refuses the later write and counts it", "[script][worldview]")
{
    MergeReport last_wins;
    MergeReport error;
    RunSim(3000, 4, 1, false, ConflictPolicy::LastWins, &last_wins);
    RunSim(3000, 4, 1, false, ConflictPolicy::Error, &error);
    CHECK(last_wins.conflicts > 0);
    CHECK(last_wins.refused == 0);
    CHECK(error.conflicts > 0);
    CHECK(error.refused == error.conflicts);
    CHECK(error.applied + error.refused == last_wins.applied);
}

TEST_CASE("World view: reads see the tick start, then the merge makes the changes", "[script][worldview]")
{
    ecs::World world;
    const ecs::Entity a = world.Create<SimPos, SimVel>(SimPos{Vec3(1, 0, 0)}, SimVel{});
    const ecs::Entity b = world.Create<SimPos, SimVel>(SimPos{Vec3(2, 0, 0)}, SimVel{});
    CommandBuffer buffer;
    buffer.Begin(0);
    PartitionContext context;
    context.world = &world;
    context.commands = &buffer;
    Overlay overlay;
    context.overlay = &overlay;
    CommandKindTable kinds;
    REQUIRE(kinds.Register(kDestroy, CommandKind{"destroy", &ApplyDestroy, false, false}));
    {
        ReadOnlyPhase phase(&world);
        ScopedPartitionContext scope(&context);
        // A script destroys b in this tick; it still reads b's tick-start value afterwards.
        buffer.Record(kDestroy, a.ToU64(), b, nullptr, 0);
        CHECK(world.Exists(b));
        CHECK(world.Get<SimPos>(b)->v.x == 2.0f);
        const EntityCheck check = ContextEntityCheck();
        CHECK(check.is_alive(nullptr, b));
        const ecs::Entity placeholder = buffer.Reserve();
        CHECK(check.is_alive(nullptr, placeholder));  // reserved this tick
        ecs::Entity other = placeholder;
        other.index += 1;
        CHECK_FALSE(check.is_alive(nullptr, other));  // not reserved
    }
    CHECK(CurrentPartitionContext() == nullptr);
    CommandBuffer* order[1] = {&buffer};
    const MergeReport report = Merge(&world, Span<CommandBuffer* const>(order, 1), kinds, ConflictPolicy::LastWins);
    CHECK(report.applied == 1);
    CHECK_FALSE(world.Exists(b));  // a handle to it now fails the entity check
    ScopedPartitionContext scope(&context);
    CHECK_FALSE(ContextEntityCheck().is_alive(nullptr, b));
}

TEST_CASE("Merge: the order is partition, entity key, sequence, whatever order the commands were recorded in",
          "[script][worldview]")
{
    ecs::World world;
    const ecs::Entity e = world.Create<SimPos, SimVel>(SimPos{}, SimVel{});
    CommandKindTable kinds;
    REQUIRE(kinds.Register(kSetVel, CommandKind{"set_vel", &ApplySetVel, false, false}));
    CommandBuffer shuffled;
    CommandBuffer sorted;
    shuffled.Begin(0);
    sorted.Begin(0);
    // Entity keys 9, 5, 7 recorded in that order; each writes x of the same entity: the highest key applies last.
    shuffled.Record(kSetVel, 9, e, Vec3(9, 0, 0));
    shuffled.Record(kSetVel, 5, e, Vec3(5, 0, 0));
    shuffled.Record(kSetVel, 7, e, Vec3(7, 0, 0));
    sorted.Record(kSetVel, 5, e, Vec3(5, 0, 0));
    sorted.Record(kSetVel, 7, e, Vec3(7, 0, 0));
    sorted.Record(kSetVel, 9, e, Vec3(9, 0, 0));
    CHECK_FALSE(shuffled.IsSorted());
    CHECK(sorted.IsSorted());
    CommandBuffer* a[1] = {&shuffled};
    CHECK(Merge(&world, Span<CommandBuffer* const>(a, 1), kinds, ConflictPolicy::LastWins).applied == 3);
    CHECK(shuffled.IsSorted());
    CHECK(world.Get<SimVel>(e)->v.x == 9.0f);
    ecs::World second;
    const ecs::Entity e2 = second.Create<SimPos, SimVel>(SimPos{}, SimVel{});
    CommandBuffer* b[1] = {&sorted};
    Merge(&second, Span<CommandBuffer* const>(b, 1), kinds, ConflictPolicy::LastWins);
    CHECK(second.Get<SimVel>(e2)->v.x == 9.0f);
}

TEST_CASE("Merge: placeholders, dangling handles and unknown kinds are counted, never crashes", "[script][worldview]")
{
    ecs::World world;
    CommandKindTable kinds;
    REQUIRE(kinds.Register(kSpawn, CommandKind{"spawn", &ApplySpawn, true, false}));
    REQUIRE(kinds.Register(kSetVel, CommandKind{"set_vel", &ApplySetVel, false, true}));
    CommandBuffer p0;
    CommandBuffer p3;
    p0.Begin(0);
    p3.Begin(3);
    const ecs::Entity first = p3.Reserve();
    CHECK(first.index == 3 * kPlaceholdersPerPartition);
    CHECK(first.generation == ecs::Entity::kPlaceholderGeneration);
    CHECK(p3.Reserve().index == 3 * kPlaceholdersPerPartition + 1);
    p3.Record(kSpawn, 1, first, Vec3(4, 0, 0));
    p3.Record(kSetVel, 1, first, Vec3(8, 0, 0));
    ecs::Entity never;
    never.index = 3 * kPlaceholdersPerPartition + 1;  // reserved but no spawn recorded
    never.generation = ecs::Entity::kPlaceholderGeneration;
    p3.Record(kSetVel, 1, never, Vec3(1, 0, 0));
    p0.Record(77, 1, ecs::Entity(), nullptr, 0);  // not registered
    CommandBuffer* order[2] = {&p0, &p3};
    const MergeReport report = Merge(&world, Span<CommandBuffer* const>(order, 2), kinds, ConflictPolicy::LastWins);
    CHECK(report.unknown == 1);
    CHECK(report.dangling == 1);
    CHECK(report.applied == 2);
    u32 count = 0;
    world.Query<ecs::Read<SimVel>>().ForEach(
        [&](const SimVel& v)
        {
            ++count;
            CHECK(v.v.x == 8.0f);
        });
    CHECK(count == 1);
}

TEST_CASE("Overlay: a later write replaces an earlier one, Clear forgets", "[script][worldview]")
{
    Overlay overlay;
    const ecs::Entity e = ecs::Entity::FromU64((u64{1} << 32) | 5);
    CHECK(overlay.Find(e, 1) == nullptr);
    overlay.Put(e, 1, Value::FromInt(1));
    overlay.Put(e, 2, Value::FromInt(2));
    overlay.Put(e, 1, Value::FromInt(3));
    CHECK(overlay.Size() == 2);
    CHECK(overlay.Find(e, 1)->Equals(Value::FromInt(3)));
    overlay.Clear();
    CHECK(overlay.Find(e, 1) == nullptr);
}

TEST_CASE("World view: the cost of a command", "[.bench]")
{
    CommandBuffer buffer;
    buffer.Begin(0);
    ecs::World world;
    constexpr u32 kN = 200000;
    Vector<ecs::Entity> entities;
    for (u32 i = 0; i < kN; ++i)
    {
        entities.Push(world.Create<SimPos, SimVel>(SimPos{}, SimVel{}));
    }
    CommandKindTable kinds;
    REQUIRE(kinds.Register(kSetVel, CommandKind{"set_vel", &ApplySetVel, false, true}));
    const u64 t0 = os::MonotonicTimeNanos();
    for (u32 i = 0; i < kN; ++i)
    {
        buffer.Record(kSetVel, entities[i].ToU64(), entities[i], Vec3(1, 2, 3));
    }
    const u64 t1 = os::MonotonicTimeNanos();
    CommandBuffer* order[1] = {&buffer};
    Merge(&world, Span<CommandBuffer* const>(order, 1), kinds, ConflictPolicy::LastWins);
    const u64 t2 = os::MonotonicTimeNanos();
    os::Print("record %.1f ns per command, merge %.1f ns per command\n", static_cast<double>(t1 - t0) / kN,
              static_cast<double>(t2 - t1) / kN);
}

TEST_CASE("CommandBuffer: a null payload with a size is refused, a zero size is fine", "[script][worldview]")
{
    CommandBuffer buffer;
    buffer.Begin(0);
    const ecs::Entity e = ecs::Entity::FromU64((u64{1} << 32) | 1);
    CHECK_FALSE(buffer.Record(kSetVel, 1, e, nullptr, 12));
    CHECK(buffer.Size() == 0);
    CHECK(buffer.Record(kDestroy, 1, e, nullptr, 0));
    CHECK(buffer.Record(kSetVel, 1, e, Vec3(1, 2, 3)));
    REQUIRE(buffer.Size() == 2);
    const Span<const u8> payload = buffer.PayloadOf(buffer.Commands()[1]);
    REQUIRE(payload.Size() == sizeof(Vec3));
    Vec3 back;
    MemCopy(&back, payload.Data(), sizeof(Vec3));
    CHECK(back == Vec3(1, 2, 3));
    buffer.Begin(kMaxPartitions - 1);  // the last partition's placeholders still fit 32 bits
    CHECK(buffer.Reserve().index == (kMaxPartitions - 1) * kPlaceholdersPerPartition);
}
