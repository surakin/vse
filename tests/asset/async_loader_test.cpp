// Asynchronous loading (Phase 22 Day 330): requests read on workers, the owner's Update builds within a budget,
// priorities, the bounded queue, deduplication, cancellation at every stage, loads by path that wait for a table, a
// level with hundreds of dependencies, and shutdown with loads in flight.

#include <catch2/catch_test_macros.hpp>

#include "asset/async_loader.h"
#include "asset/faulty_backend.h"
#include "asset/level.h"
#include "asset/memory_backend.h"
#include "cstr/mem_ops.h"
#include "jobs/scheduler.h"
#include "support.h"
#include "sync/mutex.h"
#include "sync/thread.h"

namespace vse::asset
{

namespace
{

using test::AsBytes;
using test::Rid;

constexpr ResourceType kAlChainType = ResourceType::FromChars('A', 'L', 'C', 'H');

struct AlChain
{
    u32 value = 0;
    u32 dependency_count = 0;
};

// The "clock" the budget test measures with: every build advances it by a fixed step.
struct AlClock
{
    Atomic<u64> now{0};
    u64 build_step = 0;
};

u64 AlNow(void* user)
{
    return static_cast<AlClock*>(user)->now.Load();
}

Status LoadAlChain(const LoadContext& context, AlChain* out, void* user)
{
    ByteSpan section;
    if (!context.Blob().FindSection(16, &section) || section.Size() < 4)
    {
        return Status::Make(ErrorCode::Corrupt, "a chain blob has no value");
    }
    out->value = static_cast<u32>(section[0]) | (static_cast<u32>(section[1]) << 8);
    out->dependency_count = static_cast<u32>(context.Blob().DependencyCount());
    if (user != nullptr)
    {
        AlClock* clock = static_cast<AlClock*>(user);
        clock->now.FetchAdd(clock->build_step);
    }
    return Status::Success();
}

// A level with no content of its own: the object counts what it names.
struct AlLevel
{
    u32 dependency_count = 0;
};

Status LoadAlLevel(const LoadContext& context, AlLevel* out, void*)
{
    out->dependency_count = static_cast<u32>(context.Blob().DependencyCount());
    return Status::Success();
}

}  // namespace

template <> struct ResourceTraits<AlChain>
{
    static constexpr ResourceType kType = kAlChainType;
    static constexpr u32 kKind = ResourceType::FromChars('a', 'C', 'H', 'N').Tag();
};

template <> struct ResourceTraits<AlLevel>
{
    static constexpr ResourceType kType = types::kLevel;
    static constexpr u32 kKind = ResourceType::FromChars('a', 'L', 'V', 'L').Tag();
};

namespace
{

struct AlEdge
{
    u64 id;
    u32 role;
};

Vector<byte> AlBlob(const TypeRegistry& types, ResourceType type, u64 id, u32 value, const Vector<AlEdge>& deps)
{
    BlobWriter writer(types, type, Rid(id));
    byte payload[8] = {static_cast<byte>(value), static_cast<byte>(value >> 8), 0, 0, 0, 0, 0, 0};
    REQUIRE(writer.AddSection(16, ByteSpan(payload, 8)).Ok());
    for (usize i = 0; i < deps.Size(); ++i)
    {
        Dependency d;
        d.id = Rid(deps[i].id);
        d.type = kAlChainType;
        d.type_version = 1;
        d.role = deps[i].role;
        writer.AddDependency(d);
    }
    Vector<byte> blob;
    REQUIRE(writer.Finish(&blob).Ok());
    return blob;
}

// A backend that serves blobs, records the order they are opened in and may take its time over each one.
class AlBackend final : public ResourceBackend
{
public:
    class Reader final : public ResourceReader
    {
    public:
        Reader(AlBackend* owner, const Vector<byte>* bytes) : owner_(owner), bytes_(bytes) {}
        u64 Size() const override { return bytes_->Size(); }
        Status ReadAt(u64 offset, void* buffer, usize length) override
        {
            if (owner_->delay_ms != 0)
            {
                ThisThread::SleepForMilliseconds(owner_->delay_ms);
            }
            if (offset + length > bytes_->Size())
            {
                return Status::Make(ErrorCode::Invalid, "out of range");
            }
            MemCopy(buffer, bytes_->Data() + offset, length);
            return Status::Success();
        }

    private:
        AlBackend* owner_;
        const Vector<byte>* bytes_;
    };

    const char* Name() const override { return "al"; }
    u32 Capabilities() const override { return kBackendRanged; }
    Status Stat(ResourceId id, ResourceInfo* out) override
    {
        for (usize i = 0; i < entries_.Size(); ++i)
        {
            if (entries_[i].id == id)
            {
                out->size = entries_[i].bytes.Size();
                return Status::Success();
            }
        }
        return Status::Make(ErrorCode::NotFound, "no such resource");
    }
    Status Open(ResourceId id, UniquePtr<ResourceReader>* out) override
    {
        for (usize i = 0; i < entries_.Size(); ++i)
        {
            if (entries_[i].id == id)
            {
                {
                    LockGuard<Mutex> lock(mutex_);
                    opened_.Push(id.Value());
                }
                UniquePtr<Reader> reader = MakeUnique<Reader>(this, &entries_[i].bytes);
                *out = UniquePtr<ResourceReader>(reader.Release());
                return Status::Success();
            }
        }
        return Status::Make(ErrorCode::NotFound, "no such resource");
    }
    Status ReadTable(ResourceTable*) override { return Status::Success(); }

    void Put(u64 id, Vector<byte>&& bytes)
    {
        Entry entry;
        entry.id = Rid(id);
        entry.bytes = static_cast<Vector<byte>&&>(bytes);
        entries_.Push(static_cast<Entry&&>(entry));
    }
    // The ids opened so far, in order.
    Vector<u64> Opened() const
    {
        LockGuard<Mutex> lock(mutex_);
        Vector<u64> copy;
        for (usize i = 0; i < opened_.Size(); ++i)
        {
            copy.Push(opened_[i]);
        }
        return copy;
    }
    u64 delay_ms = 0;  // per read call

private:
    struct Entry
    {
        ResourceId id;
        Vector<byte> bytes;
    };
    Vector<Entry> entries_;
    mutable Mutex mutex_;
    Vector<u64> opened_;
};

// Waits until `ready` holds, for at most ten seconds; the tests REQUIRE it, so a stuck load fails instead of hanging.
template <class F> bool AlWaitFor(F ready)
{
    for (u32 i = 0; i < 10000; ++i)
    {
        if (ready())
        {
            return true;
        }
        ThisThread::SleepForMilliseconds(1);
    }
    return false;
}

jobs::Scheduler::Config AlConfig(usize participants)
{
    jobs::Scheduler::Config config;
    config.participants = participants;
    config.pin = false;
    return config;
}

struct AlRig
{
    TypeRegistry types;
    AlBackend backend;
    ManagerConfig config;
    UniquePtr<ResourceManager> manager;

    explicit AlRig(void* chain_user = nullptr, u32 max_inflight = 4, u32 max_queued = 64)
    {
        REQUIRE(types.Register(kAlChainType, 1, "alchain").Ok());
        config.buffer_pool_bytes = u64{32} << 20;
        config.max_inflight = max_inflight;
        config.max_queued = max_queued;
        manager = MakeUnique<ResourceManager>(types, config);
        manager->RegisterLoader<AlChain>(&LoadAlChain, chain_user);
        manager->RegisterLoader<AlLevel>(&LoadAlLevel);
        REQUIRE(manager->Mount(StringView("/"), &backend, 0).Ok());
    }
    void Add(u64 id, u32 value, const Vector<AlEdge>& deps = Vector<AlEdge>())
    {
        backend.Put(id, AlBlob(types, kAlChainType, id, value, deps));
    }
};

}  // namespace

TEST_CASE("async: a request reads on workers, the owner's Update builds it, and Finish gives the handle",
          "[asset][async]")
{
    AlRig rig;
    Vector<AlEdge> deps;
    deps.Push(AlEdge{2, 1});
    rig.Add(2, 22);
    rig.Add(1, 11, deps);
    jobs::Scheduler scheduler(AlConfig(3));
    AsyncLoader loader(*rig.manager, scheduler);
    LoadTicket ticket = loader.LoadAsync<AlChain>(Rid(1));
    REQUIRE(ticket.Valid());
    CHECK(loader.Wait(ticket).Ok());
    CHECK(ticket.IsDone());
    Handle<AlChain> handle;
    REQUIRE(ticket.Finish(&handle).Ok());
    REQUIRE(handle.Get() != nullptr);
    CHECK(handle.Get()->value == 11);
    CHECK(handle.Get()->dependency_count == 1);
    u32 read = 0;
    u32 total = 0;
    ticket.Progress(&read, &total);
    CHECK(read == 2);  // the resource and its dependency
    CHECK(total == 2);
    CHECK(rig.manager->Stats().resident == 2);
    // A resource that is loaded is answered at once.
    LoadTicket again = loader.LoadAsync<AlChain>(Rid(1));
    CHECK(again.IsDone());
    CHECK(again.Result().Ok());
}

TEST_CASE("async: two requests for one resource share one load, and a failed load is retried only when asked",
          "[asset][async]")
{
    AlRig rig;
    rig.Add(1, 11);
    jobs::Scheduler scheduler(AlConfig(3));
    AsyncLoader loader(*rig.manager, scheduler);
    LoadTicket a = loader.LoadAsync<AlChain>(Rid(1));
    LoadTicket b = loader.LoadAsync<AlChain>(Rid(1), LoadPriority::Immediate);
    CHECK(loader.Stats().shared == 1);
    CHECK(loader.Wait(a).Ok());
    CHECK(b.IsDone());
    Handle<AlChain> ha;
    Handle<AlChain> hb;
    REQUIRE(a.Finish(&ha).Ok());
    REQUIRE(b.Finish(&hb).Ok());
    CHECK(ha.Get() == hb.Get());  // one object
    CHECK(rig.manager->Stats().loads == 1);

    LoadTicket missing = loader.LoadAsync<AlChain>(Rid(404));
    CHECK(loader.Wait(missing).Is(ErrorCode::NotFound));
    rig.Add(404, 4);  // it appears: the earlier failure is not remembered, the next request is a new load
    LoadTicket retried = loader.LoadAsync<AlChain>(Rid(404));
    CHECK(loader.Wait(retried).Ok());
}

TEST_CASE("async: requests wait by priority for a reading slot, and past the queue limit they are refused",
          "[asset][async]")
{
    AlRig rig(nullptr, 1, 2);
    for (u64 id = 1; id <= 6; ++id)
    {
        rig.Add(id, static_cast<u32>(id));
    }
    jobs::Scheduler scheduler(AlConfig(1));  // no workers: nothing reads until the main thread helps
    AsyncLoader loader(*rig.manager, scheduler);
    LoadTicket first = loader.LoadAsync<AlChain>(Rid(1), LoadPriority::Background);  // takes the one slot
    LoadTicket background = loader.LoadAsync<AlChain>(Rid(2), LoadPriority::Background);
    LoadTicket normal = loader.LoadAsync<AlChain>(Rid(3), LoadPriority::Normal);
    LoadTicket refused = loader.LoadAsync<AlChain>(Rid(4), LoadPriority::Immediate);  // two are waiting already
    CHECK(refused.IsDone());
    CHECK(refused.Result().Is(ErrorCode::LimitExceeded));
    CHECK(loader.Stats().rejected == 1);
    CHECK(loader.Stats().queued == 2);
    CHECK(loader.Stats().reading == 1);
    CHECK(loader.Wait(first).Ok());
    CHECK(loader.Wait(normal).Ok());
    CHECK(loader.Wait(background).Ok());
    // The one that waited and was more urgent was read first.
    const Vector<u64> opened = rig.backend.Opened();
    REQUIRE(opened.Size() == 3);
    CHECK(opened[0] == 1);
    CHECK(opened[1] == 3);
    CHECK(opened[2] == 2);
}

TEST_CASE("async: Wait on a queued request runs the reads ahead of it on a scheduler without workers", "[asset][async]")
{
    AlRig rig(nullptr, 1, 8);
    rig.Add(1, 1);
    rig.Add(2, 2);
    jobs::Scheduler scheduler(AlConfig(1));
    AsyncLoader loader(*rig.manager, scheduler);
    LoadTicket first = loader.LoadAsync<AlChain>(Rid(1));   // takes the one slot; its read waits for this thread
    LoadTicket second = loader.LoadAsync<AlChain>(Rid(2));  // queued behind it
    CHECK(loader.Wait(second).Ok());                        // must not wait for a job nobody runs
    CHECK(first.IsDone());
}

TEST_CASE("async: a cancelled request is not joined and does not count against the queue limit", "[asset][async]")
{
    AlRig rig(nullptr, 1, 1);
    rig.Add(1, 1);
    rig.Add(2, 2);
    rig.Add(3, 3);
    jobs::Scheduler scheduler(AlConfig(1));  // nothing reads until this thread helps
    AsyncLoader loader(*rig.manager, scheduler);
    LoadTicket first = loader.LoadAsync<AlChain>(Rid(1));
    LoadTicket queued = loader.LoadAsync<AlChain>(Rid(2));  // the one place in the queue
    queued.Cancel();
    LoadTicket accepted = loader.LoadAsync<AlChain>(Rid(3));  // the cancelled one gave its place back
    CHECK_FALSE(accepted.Result().Is(ErrorCode::LimitExceeded));
    // A request for a resource whose only ticket was cancelled starts a new load instead of joining the dead one.
    LoadTicket dead = loader.LoadAsync<AlChain>(Rid(3));
    LoadTicket live = loader.LoadAsync<AlChain>(Rid(3));
    (void)dead;
    CHECK(loader.Wait(first).Ok());
    CHECK(loader.Wait(accepted).Ok());
    CHECK(loader.Wait(live).Ok());
    CHECK(queued.Result().Is(ErrorCode::Cancelled));
}

TEST_CASE("async: a cancelled ticket is not joined by the next request for the same resource", "[asset][async]")
{
    AlRig rig(nullptr, 1, 8);
    rig.Add(1, 1);
    rig.Add(2, 2);
    jobs::Scheduler scheduler(AlConfig(1));
    AsyncLoader loader(*rig.manager, scheduler);
    LoadTicket blocker = loader.LoadAsync<AlChain>(Rid(1));
    LoadTicket cancelled = loader.LoadAsync<AlChain>(Rid(2));
    cancelled.Cancel();  // the only ticket: the load is dead but still queued
    LoadTicket fresh = loader.LoadAsync<AlChain>(Rid(2));
    CHECK(loader.Wait(fresh).Ok());  // a new load, not the cancelled one
    CHECK(loader.Wait(blocker).Ok());
}

TEST_CASE("async: Update builds within the time and byte budgets and the rest waits", "[asset][async]")
{
    AlClock clock;
    clock.build_step = 400 * 1000;  // each build takes 400 microseconds of the fake clock
    AlRig rig(&clock);
    for (u64 id = 1; id <= 6; ++id)
    {
        rig.Add(id, static_cast<u32>(id));
    }
    jobs::Scheduler scheduler(AlConfig(3));
    AsyncLoaderOptions options;
    options.clock = &AlNow;
    options.clock_user = &clock;
    options.max_inflight = 6;
    AsyncLoader loader(*rig.manager, scheduler, options);
    LoadTicket tickets[6];
    for (u64 id = 1; id <= 6; ++id)
    {
        tickets[id - 1] = loader.LoadAsync<AlChain>(Rid(id));
    }
    // Wait until every blob is read, without building any.
    REQUIRE(AlWaitFor([&] { return loader.Stats().ready == 6; }));
    LoadUpdate first = loader.Update(1000, 0);  // 1000 microseconds: 400, 800, 1200 -> three builds
    CHECK(first.completed == 3);
    CHECK(first.backlog == 3);
    CHECK(loader.Stats().ready == 3);
    LoadUpdate second = loader.Update(0, 1);  // one byte: at least one request is built per call
    CHECK(second.completed == 1);
    CHECK(second.backlog == 2);
    LoadUpdate rest = loader.Update(0, 0);
    CHECK(rest.completed == 2);
    CHECK(rest.backlog == 0);
    for (usize i = 0; i < 6; ++i)
    {
        CHECK(tickets[i].IsDone());
        CHECK(tickets[i].Result().Ok());
    }
}

TEST_CASE("async: a request is cancelled before it starts, while it reads and after it was read", "[asset][async]")
{
    AlRig rig(nullptr, 1);
    Vector<AlEdge> deps;
    for (u64 id = 11; id <= 20; ++id)
    {
        rig.Add(id, 1);
        deps.Push(AlEdge{id, 1});
    }
    rig.Add(1, 1, deps);  // a blob with ten dependencies: ten more reads
    rig.Add(2, 2);
    rig.Add(3, 3);
    rig.backend.delay_ms = 20;
    jobs::Scheduler scheduler(AlConfig(3));
    AsyncLoader loader(*rig.manager, scheduler);
    storage::CancelToken token;
    LoadTicket reading = loader.LoadAsync<AlChain>(Rid(1), LoadPriority::Normal, &token);  // takes the one slot
    LoadTicket waiting = loader.LoadAsync<AlChain>(Rid(2));
    waiting.Cancel();  // before it starts
    ThisThread::SleepForMilliseconds(30);
    token.Cancel();  // while it reads
    CHECK(loader.Wait(reading).Is(ErrorCode::Cancelled));
    CHECK(loader.Wait(waiting).Is(ErrorCode::Cancelled));
    CHECK(rig.backend.Opened().Size() < 11);  // the reading stopped early
    // After it was read: cancelled before the build.
    rig.backend.delay_ms = 0;
    LoadTicket ready = loader.LoadAsync<AlChain>(Rid(3));
    REQUIRE(AlWaitFor([&] { return loader.Stats().ready == 1; }));
    ready.Cancel();
    loader.Update(0, 0);
    CHECK(ready.Result().Is(ErrorCode::Cancelled));
    CHECK(rig.manager->Stats().resident == 0);
    CHECK(rig.manager->Pool().BytesInUse() == 0);  // every buffer went back
    CHECK(loader.Stats().cancelled == 3);
}

TEST_CASE("async: many tickets for the same and for different resources, from the owner thread, all complete",
          "[asset][async][stress]")
{
    AlRig rig(nullptr, 3, 4096);
    for (u64 id = 1; id <= 40; ++id)
    {
        rig.Add(id, static_cast<u32>(id));
    }
    jobs::Scheduler scheduler(AlConfig(4));
    AsyncLoader loader(*rig.manager, scheduler);
    Vector<LoadTicket> tickets;
    for (u32 round = 0; round < 6; ++round)
    {
        for (u64 id = 1; id <= 40; ++id)
        {
            tickets.Push(loader.LoadAsync<AlChain>(Rid(id), static_cast<LoadPriority>((id + round) % 3)));
        }
    }
    for (usize i = 0; i < tickets.Size(); ++i)
    {
        CHECK(loader.Wait(tickets[i]).Ok());
    }
    CHECK(rig.manager->Stats().loads == 40);
    CHECK(loader.Stats().completed + loader.Stats().shared >= 240);
}

TEST_CASE("async: a level with 500 dependencies is read with the levels it includes first, and reports its progress",
          "[asset][async][level]")
{
    AlRig rig;
    Vector<AlEdge> deps;
    for (u64 id = 1000; id < 1500; ++id)
    {
        rig.Add(id, 1);
        deps.Push(AlEdge{id, id % 2 == 0 ? kLevelRoleInclude : kLevelRoleReference});
    }
    rig.backend.Put(7, AlBlob(rig.types, types::kLevel, 7, 0, deps));
    jobs::Scheduler scheduler(AlConfig(3));
    AsyncLoader loader(*rig.manager, scheduler);
    LoadTicket ticket = loader.LoadLevelAsync(Rid(7));
    CHECK(loader.Wait(ticket).Ok());
    u32 read = 0;
    u32 total = 0;
    ticket.Progress(&read, &total);
    CHECK(read == 501);
    CHECK(total == 501);
    const Vector<u64> opened = rig.backend.Opened();
    REQUIRE(opened.Size() == 501);
    CHECK(opened[0] == 7);
    bool references_started = false;
    for (usize i = 1; i < opened.Size(); ++i)
    {
        const bool include = opened[i] % 2 == 0;
        if (!include)
        {
            references_started = true;
        }
        CHECK((include ? !references_started : true));  // no include after a plain reference
    }
    Handle<AlLevel> level;
    REQUIRE(ticket.Finish(&level).Ok());
    CHECK(level.Get()->dependency_count == 500);
}

TEST_CASE("async: a load by path waits for the table, and fails with NotFound once every table is in",
          "[asset][async][path]")
{
    TypeRegistry types;
    REQUIRE(types.Register(kAlChainType, 1, "alchain").Ok());
    HostMemoryBackend inner(types, u64{1} << 20);
    const Vector<byte> blob = AlBlob(types, kAlChainType, 5, 55, Vector<AlEdge>());
    const StringView path("/a/five.chain");
    MemoryAddOptions options;
    options.paths = Span<const StringView>(&path, 1);
    REQUIRE(inner.Add(Rid(5), AsBytes(blob), options).Ok());
    FaultyBackend slow(inner);
    slow.SetLatencyMs(FaultOp::ReadTable, 60);  // the table arrives late
    ManagerConfig config;
    config.buffer_pool_bytes = u64{8} << 20;
    ResourceManager manager(types, config);
    manager.RegisterLoader<AlChain>(&LoadAlChain);
    REQUIRE(manager.Mount(StringView("/"), &slow, 0).Ok());
    manager.Mounts().RefreshTablesAsync();
    jobs::Scheduler scheduler(AlConfig(3));
    AsyncLoader loader(manager, scheduler);
    LoadTicket found = loader.LoadPathAsync<AlChain>(StringView("/a/five.chain"));
    LoadTicket absent = loader.LoadPathAsync<AlChain>(StringView("/a/none.chain"));
    CHECK_FALSE(found.IsDone());  // the table has not arrived: both wait
    CHECK_FALSE(absent.IsDone());
    CHECK(loader.Stats().waiting_table == 2);                                      // (the twin is added below)
    LoadTicket twin = loader.LoadPathAsync<AlChain>(StringView("/a/five.chain"));  // the same resource by the same path
    CHECK(loader.Wait(found).Ok());
    CHECK(loader.Wait(twin).Ok());
    CHECK(loader.Wait(absent).Is(ErrorCode::NotFound));
    CHECK(manager.Stats().loads == 1);  // one load served both
    Handle<AlChain> handle;
    REQUIRE(found.Finish(&handle).Ok());
    CHECK(handle.Get()->value == 55);
}

TEST_CASE("async: destroying the loader with loads in flight leaves nothing behind", "[asset][async][stress]")
{
    AlRig rig(nullptr, 2);
    for (u64 id = 1; id <= 30; ++id)
    {
        rig.Add(id, static_cast<u32>(id));
    }
    rig.backend.delay_ms = 3;
    jobs::Scheduler scheduler(AlConfig(3));
    Vector<LoadTicket> tickets;
    {
        AsyncLoader loader(*rig.manager, scheduler);
        for (u64 id = 1; id <= 30; ++id)
        {
            tickets.Push(loader.LoadAsync<AlChain>(Rid(id)));
        }
        ThisThread::SleepForMilliseconds(10);
        loader.Update(0, 0);
    }  // the loader goes with reads running and requests queued
    for (usize i = 0; i < tickets.Size(); ++i)
    {
        CHECK(tickets[i].IsDone());
        const Status result = tickets[i].Result();
        CHECK((result.Ok() || result.Is(ErrorCode::Cancelled)));
    }
    tickets.Clear();
    CHECK(rig.manager->Pool().BytesInUse() == rig.manager->Stats().bytes_resident);
}

}  // namespace vse::asset
