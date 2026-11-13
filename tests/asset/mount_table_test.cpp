// Mounts and the merged table (Phase 22 Day 315): priority and override, prefixes, shadowing, NotReady against
// NotFound, and the asynchronous refresh over slow and fast mounts in both orders.

#include <catch2/catch_test_macros.hpp>

#include "asset/faulty_backend.h"
#include "asset/memory_backend.h"
#include "asset/mount_table.h"
#include "support.h"
#include "sync/atomic.h"
#include "sync/thread.h"

namespace engine_placeholder::asset
{

namespace
{

Status AddWithPath(HostMemoryBackend& backend, const TypeRegistry& registry, u64 id, const char* path, usize payload,
                   u32 seed)
{
    const Vector<byte> blob = test::MakeTestBlob(registry, test::Rid(id), payload, seed);
    const StringView p = test::Vp(path);
    MemoryAddOptions options;
    options.paths = engine_placeholder::Span<const StringView>(&p, 1);
    return backend.Add(test::Rid(id), test::AsBytes(blob), options);
}

void WaitUntilReady(const MountTable& mounts)
{
    while (!mounts.TablesReady())
    {
        ThisThread::Yield();
    }
}

}  // namespace

TEST_CASE("The highest-priority mount that has the id serves it", "[asset][mount]")
{
    TypeRegistry registry = test::MakeBackendRegistry();
    HostMemoryBackend pak(registry, 1 << 20);
    HostMemoryBackend loose(registry, 1 << 20);
    REQUIRE(AddWithPath(pak, registry, 1, "/a.bk", 100, 1).Ok());
    REQUIRE(AddWithPath(pak, registry, 2, "/b.bk", 100, 2).Ok());
    REQUIRE(AddWithPath(loose, registry, 1, "/a.bk", 300, 3).Ok());  // overrides id 1

    MountTable mounts;
    MountId pak_id = 0;
    MountId loose_id = 0;
    REQUIRE(mounts.Mount(test::Vp("/"), &pak, 0, &pak_id).Ok());
    REQUIRE(mounts.Mount(test::Vp("/"), &loose, 10, &loose_id).Ok());

    ResourceInfo info;
    REQUIRE(mounts.Stat(test::Rid(1), &info).Ok());
    CHECK(info.size == test::MakeTestBlob(registry, test::Rid(1), 300, 3).Size());  // the loose copy
    UniquePtr<ResourceReader> reader;
    MountId which = 0;
    REQUIRE(mounts.Open(test::Rid(1), &reader, &which).Ok());
    CHECK(which == loose_id);
    REQUIRE(mounts.Open(test::Rid(2), &reader, &which).Ok());
    CHECK(which == pak_id);
    CHECK(mounts.Open(test::Rid(99), &reader).Is(ErrorCode::NotFound));

    // Unmounting the loose mount uncovers the pak's copy.
    REQUIRE(mounts.Unmount(loose_id).Ok());
    REQUIRE(mounts.Stat(test::Rid(1), &info).Ok());
    CHECK(info.size == test::MakeTestBlob(registry, test::Rid(1), 100, 1).Size());
    CHECK(mounts.Unmount(loose_id).Is(ErrorCode::NotFound));
    CHECK(mounts.MountCount() == 1);
}

TEST_CASE("A corrupt resource in a high mount is an error, not a fall through to a lower copy", "[asset][mount]")
{
    TypeRegistry registry = test::MakeBackendRegistry();
    HostMemoryBackend low(registry, 1 << 20);
    HostMemoryBackend high(registry, 1 << 20);
    REQUIRE(AddWithPath(low, registry, 1, "/a.bk", 100, 1).Ok());
    FaultyBackend faulty(high);
    faulty.FailNext(FaultOp::Stat, Status::Make(ErrorCode::Io, "injected"));
    MountTable mounts;
    MountId id = 0;
    REQUIRE(mounts.Mount(test::Vp("/"), &low, 0, &id).Ok());
    REQUIRE(mounts.Mount(test::Vp("/"), &faulty, 5, &id).Ok());
    ResourceInfo info;
    CHECK(mounts.Stat(test::Rid(1), &info).Is(ErrorCode::Io));
    CHECK(mounts.Stat(test::Rid(1), &info).Ok());  // the fault was one call
}

TEST_CASE("Path questions say NotReady until the tables arrive, then answer from the merged tables", "[asset][mount]")
{
    TypeRegistry registry = test::MakeBackendRegistry();
    HostMemoryBackend pak(registry, 1 << 20);
    HostMemoryBackend loose(registry, 1 << 20);
    REQUIRE(AddWithPath(pak, registry, 1, "/shared.bk", 50, 1).Ok());
    REQUIRE(AddWithPath(pak, registry, 2, "/only_pak.bk", 50, 2).Ok());
    REQUIRE(AddWithPath(loose, registry, 3, "/shared.bk", 50, 3).Ok());  // the same path, another id
    REQUIRE(AddWithPath(loose, registry, 4, "/only_loose.bk", 50, 4).Ok());

    MountTable mounts;
    MountId id = 0;
    REQUIRE(mounts.Mount(test::Vp("/"), &pak, 0, &id).Ok());
    REQUIRE(mounts.Mount(test::Vp("/"), &loose, 10, &id).Ok());

    ResourceId resolved;
    CHECK(mounts.Resolve(test::Vp("/shared.bk"), &resolved).Is(ErrorCode::NotReady));
    CHECK(!mounts.TablesReady());
    CHECK(mounts.Resolve(test::Vp("shared.bk"), &resolved).Is(ErrorCode::Invalid));

    REQUIRE(mounts.RefreshTables().Ok());
    CHECK(mounts.TablesReady());
    REQUIRE(mounts.Resolve(test::Vp("/shared.bk"), &resolved).Ok());
    CHECK(resolved == test::Rid(3));  // the loose mount wins
    REQUIRE(mounts.Resolve(test::Vp("/only_pak.bk"), &resolved).Ok());
    CHECK(resolved == test::Rid(2));
    CHECK(mounts.Resolve(test::Vp("/nothing.bk"), &resolved).Is(ErrorCode::NotFound));

    Vector<String<HeapAllocator>> paths;
    REQUIRE(mounts.PathsOf(test::Rid(1), &paths).Ok());
    CHECK(paths.Size() == 0);  // its only path is shadowed
    REQUIRE(mounts.PathsOf(test::Rid(3), &paths).Ok());
    REQUIRE(paths.Size() == 1);
    CHECK(paths[0].View() == test::Vp("/shared.bk"));

    ResourceTable merged;
    Vector<ShadowedEntry> shadowed;
    REQUIRE(mounts.Snapshot(&merged, &shadowed).Ok());
    CHECK(merged.Size() == 3);
    REQUIRE(shadowed.Size() == 1);
    CHECK(shadowed[0].path.View() == test::Vp("/shared.bk"));
    CHECK(shadowed[0].shadowed_id == test::Rid(1));
    CHECK(shadowed[0].winning_id == test::Rid(3));
}

TEST_CASE("A mount prefix puts its paths under the prefix", "[asset][mount]")
{
    TypeRegistry registry = test::MakeBackendRegistry();
    HostMemoryBackend mod(registry, 1 << 20);
    REQUIRE(AddWithPath(mod, registry, 1, "/hat.bk", 50, 1).Ok());
    MountTable mounts;
    MountId id = 0;
    REQUIRE(mounts.Mount(test::Vp("/mods/fancy"), &mod, 0, &id).Ok());
    CHECK(mounts.Mount(test::Vp("not/a/path"), &mod, 0, &id).Is(ErrorCode::Invalid));
    CHECK(mounts.Mount(test::Vp("/ok"), nullptr, 0, &id).Is(ErrorCode::Invalid));
    ResourceId resolved;
    CHECK(mounts.Resolve(test::Vp("/mods/fancy/hat.bk"), &resolved).Is(ErrorCode::NotReady));
    CHECK(
        mounts.Resolve(test::Vp("/elsewhere/hat.bk"), &resolved).Is(ErrorCode::NotFound));  // this mount cannot hold it
    REQUIRE(mounts.RefreshTables().Ok());
    REQUIRE(mounts.Resolve(test::Vp("/mods/fancy/hat.bk"), &resolved).Ok());
    CHECK(resolved == test::Rid(1));
    CHECK(mounts.Resolve(test::Vp("/hat.bk"), &resolved).Is(ErrorCode::NotFound));
}

TEST_CASE("The merged table does not depend on which mount's table arrives first", "[asset][mount][async]")
{
    for (int slow_is_high = 0; slow_is_high < 2; ++slow_is_high)
    {
        TypeRegistry registry = test::MakeBackendRegistry();
        HostMemoryBackend slow_inner(registry, 1 << 20);
        HostMemoryBackend fast(registry, 1 << 20);
        REQUIRE(AddWithPath(slow_inner, registry, 1, "/shared.bk", 50, 1).Ok());
        REQUIRE(AddWithPath(slow_inner, registry, 2, "/slow_only.bk", 50, 2).Ok());
        REQUIRE(AddWithPath(fast, registry, 3, "/shared.bk", 50, 3).Ok());
        REQUIRE(AddWithPath(fast, registry, 4, "/fast_only.bk", 50, 4).Ok());
        FaultyBackend slow(slow_inner);
        slow.SetLatencyMs(FaultOp::ReadTable, 150);

        MountTable mounts;
        MountId id = 0;
        REQUIRE(mounts.Mount(test::Vp("/"), &slow, slow_is_high ? 10 : 0, &id).Ok());
        REQUIRE(mounts.Mount(test::Vp("/"), &fast, slow_is_high ? 0 : 10, &id).Ok());

        mounts.RefreshTablesAsync();  // returns at once
        CHECK(!mounts.TablesReady());

        // The slow mount's paths are NotReady meanwhile, loads by id work, and the fast mount's path is answered
        // once the fast table is in (only when it outranks the slow one).
        ResourceId resolved;
        CHECK(mounts.Resolve(test::Vp("/slow_only.bk"), &resolved).Is(ErrorCode::NotReady));
        UniquePtr<ResourceReader> reader;
        CHECK(mounts.Open(test::Rid(2), &reader).Ok());
        CHECK(mounts.Open(test::Rid(4), &reader).Ok());

        WaitUntilReady(mounts);
        REQUIRE(mounts.Resolve(test::Vp("/shared.bk"), &resolved).Ok());
        CHECK(resolved == (slow_is_high ? test::Rid(1) : test::Rid(3)));
        REQUIRE(mounts.Resolve(test::Vp("/slow_only.bk"), &resolved).Ok());
        CHECK(resolved == test::Rid(2));
        REQUIRE(mounts.Resolve(test::Vp("/fast_only.bk"), &resolved).Ok());
        CHECK(resolved == test::Rid(4));
        ResourceTable merged;
        Vector<ShadowedEntry> shadowed;
        REQUIRE(mounts.Snapshot(&merged, &shadowed).Ok());
        CHECK(merged.Size() == 3);
        CHECK(shadowed.Size() == 1);
    }
}

namespace
{

struct Ready
{
    Atomic<int> calls{0};
};

void OnReady(void* user)
{
    static_cast<Ready*>(user)->calls.FetchAdd(1);
}

}  // namespace

TEST_CASE("The tables-ready callback fires once per refresh, and a failed table is reported", "[asset][mount][async]")
{
    TypeRegistry registry = test::MakeBackendRegistry();
    HostMemoryBackend good(registry, 1 << 20);
    HostMemoryBackend bad_inner(registry, 1 << 20);
    REQUIRE(AddWithPath(good, registry, 1, "/g.bk", 50, 1).Ok());
    REQUIRE(AddWithPath(bad_inner, registry, 2, "/b.bk", 50, 2).Ok());
    FaultyBackend bad(bad_inner);
    bad.FailNext(FaultOp::ReadTable, Status::Make(ErrorCode::Io, "injected table failure"));
    bad.SetLatencyMs(FaultOp::ReadTable, 30);

    Ready ready;
    MountTable mounts;
    mounts.SetTablesReadyCallback(&OnReady, &ready);
    MountId id = 0;
    REQUIRE(mounts.Mount(test::Vp("/"), &good, 0, &id).Ok());
    REQUIRE(mounts.Mount(test::Vp("/"), &bad, 5, &id).Ok());

    mounts.RefreshTablesAsync();
    WaitUntilReady(mounts);
    while (ready.calls.Load() < 1)
    {
        ThisThread::Yield();
    }
    CHECK(ready.calls.Load() == 1);
    ResourceId resolved;
    CHECK(mounts.Resolve(test::Vp("/b.bk"), &resolved).Is(ErrorCode::Io));  // the failed mount's own status
    ResourceTable merged;
    Vector<ShadowedEntry> shadowed;
    CHECK(mounts.Snapshot(&merged, &shadowed).Is(ErrorCode::Io));

    // A second refresh (the fault was one call) succeeds and fires the callback again.
    mounts.RefreshTablesAsync();
    WaitUntilReady(mounts);
    while (ready.calls.Load() < 2)
    {
        ThisThread::Yield();
    }
    REQUIRE(mounts.Resolve(test::Vp("/b.bk"), &resolved).Ok());
    CHECK(resolved == test::Rid(2));
}

TEST_CASE("Polling changes re-reads the table of the mount that changed", "[asset][mount]")
{
    TypeRegistry registry = test::MakeBackendRegistry();
    HostMemoryBackend backend(registry, 1 << 20);
    REQUIRE(AddWithPath(backend, registry, 1, "/one.bk", 50, 1).Ok());
    MountTable mounts;
    MountId id = 0;
    REQUIRE(mounts.Mount(test::Vp("/"), &backend, 0, &id).Ok());
    REQUIRE(mounts.RefreshTables().Ok());
    Vector<ChangeRecord> changes;
    REQUIRE(mounts.PollChanges(&changes).Ok());
    CHECK(changes.Size() == 1);  // the add that happened before the first poll
    WaitUntilReady(mounts);

    REQUIRE(AddWithPath(backend, registry, 2, "/two.bk", 50, 2).Ok());
    REQUIRE(mounts.PollChanges(&changes).Ok());
    REQUIRE(changes.Size() == 1);
    CHECK(changes[0].id == test::Rid(2));
    WaitUntilReady(mounts);
    ResourceId resolved;
    REQUIRE(mounts.Resolve(test::Vp("/two.bk"), &resolved).Ok());
    CHECK(resolved == test::Rid(2));
}

TEST_CASE("A refresh in flight answers NotReady even after an earlier read failed", "[asset][mount][async]")
{
    TypeRegistry registry = test::MakeBackendRegistry();
    HostMemoryBackend inner(registry, 1 << 20);
    REQUIRE(AddWithPath(inner, registry, 1, "/a.bk", 50, 1).Ok());
    FaultyBackend faulty(inner);
    faulty.FailNext(FaultOp::ReadTable, Status::Make(ErrorCode::Io, "injected"));
    MountTable mounts;
    MountId id = 0;
    REQUIRE(mounts.Mount(test::Vp("/"), &faulty, 0, &id).Ok());
    CHECK(mounts.RefreshTables().Is(ErrorCode::Io));
    ResourceId resolved;
    CHECK(mounts.Resolve(test::Vp("/a.bk"), &resolved).Is(ErrorCode::Io));

    faulty.SetLatencyMs(FaultOp::ReadTable, 100);
    mounts.RefreshTablesAsync();
    CHECK(mounts.Resolve(test::Vp("/a.bk"), &resolved).Is(ErrorCode::NotReady));  // not the stale failure
    WaitUntilReady(mounts);
    REQUIRE(mounts.Resolve(test::Vp("/a.bk"), &resolved).Ok());
}

TEST_CASE("Of two mounts with one priority the later one wins", "[asset][mount]")
{
    TypeRegistry registry = test::MakeBackendRegistry();
    HostMemoryBackend first(registry, 1 << 20);
    HostMemoryBackend second(registry, 1 << 20);
    REQUIRE(AddWithPath(first, registry, 1, "/a.bk", 100, 1).Ok());
    REQUIRE(AddWithPath(second, registry, 1, "/a.bk", 300, 2).Ok());
    MountTable mounts;
    MountId id = 0;
    REQUIRE(mounts.Mount(test::Vp("/"), &first, 5, &id).Ok());
    REQUIRE(mounts.Mount(test::Vp("/"), &second, 5, &id).Ok());
    ResourceInfo info;
    REQUIRE(mounts.Stat(test::Rid(1), &info).Ok());
    CHECK(info.size == test::MakeTestBlob(registry, test::Rid(1), 300, 2).Size());
}

}  // namespace engine_placeholder::asset
