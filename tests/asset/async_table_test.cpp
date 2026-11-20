// Asynchronous table reads and the fault-injecting wrapper (Phase 22 Day 315): ReadTableAsync returns at once,
// cancellation leaves nothing, pages arrive in order and complete, failures surface from Finish.

#include <catch2/catch_test_macros.hpp>

#include "asset/faulty_backend.h"
#include "asset/memory_backend.h"
#include "backend_conformance.h"
#include "cstr/format.h"
#include "support.h"
#include "sync/atomic.h"
#include "sync/thread.h"

namespace vse::asset
{

namespace
{

void AddMany(HostMemoryBackend& backend, const TypeRegistry& registry, u32 count)
{
    for (u32 i = 0; i < count; ++i)
    {
        char path[32];
        FormatTo(path, sizeof(path), "/res/%u.bk", i);
        const Vector<byte> blob = test::MakeTestBlob(registry, test::Rid(1000 + i), 20, i);
        const StringView p(path, CStrLength(path));
        MemoryAddOptions options;
        options.paths = vse::Span<const StringView>(&p, 1);
        REQUIRE(backend.Add(test::Rid(1000 + i), ByteSpan(blob.Data(), blob.Size()), options).Ok());
    }
}

}  // namespace

TEST_CASE("The fault wrapper passes the conformance suite, with reads in small chunks", "[asset][backend][faulty]")
{
    TypeRegistry registry = test::MakeBackendRegistry();
    HostMemoryBackend inner(registry, 1 << 20);
    test::ConformanceFixture fx;
    const char* paths[] = {"/a.bk", "/b.bk"};
    for (u32 i = 0; i < 2; ++i)
    {
        fx.ids.Push(test::Rid(0x50 + i));
        fx.blobs.Push(test::MakeTestBlob(registry, test::Rid(0x50 + i), 333 + i, i));
        fx.paths.Push(test::Vp(paths[i]));
        const StringView path = test::Vp(paths[i]);
        MemoryAddOptions options;
        options.paths = vse::Span<const StringView>(&path, 1);
        REQUIRE(inner.Add(fx.ids[i], ByteSpan(fx.blobs[i].Data(), fx.blobs[i].Size()), options).Ok());
    }
    FaultyBackend faulty(inner);
    faulty.SetReadChunk(7);
    test::RunBackendConformance(faulty, fx);
    CHECK(faulty.CallCount(FaultOp::Stat) > 0);
}

TEST_CASE("The fault wrapper injects failures after a number of good calls", "[asset][backend][faulty]")
{
    TypeRegistry registry = test::MakeBackendRegistry();
    HostMemoryBackend inner(registry, 1 << 20);
    AddMany(inner, registry, 1);
    FaultyBackend faulty(inner);
    faulty.FailNext(FaultOp::Open, Status::Make(ErrorCode::Io, "injected"), 2, 1);
    UniquePtr<ResourceReader> reader;
    CHECK(faulty.Open(test::Rid(1000), &reader).Ok());               // skipped
    CHECK(faulty.Open(test::Rid(1000), &reader).Is(ErrorCode::Io));  // injected
    CHECK(faulty.Open(test::Rid(1000), &reader).Is(ErrorCode::Io));  // injected
    CHECK(faulty.Open(test::Rid(1000), &reader).Ok());               // spent
    faulty.FailNext(FaultOp::Read, Status::Make(ErrorCode::Io, "injected read"));
    byte b = 0;
    CHECK(reader->ReadAt(0, &b, 1).Is(ErrorCode::Io));
    CHECK(reader->ReadAt(0, &b, 1).Ok());
}

TEST_CASE("ReadTableAsync returns at once and finishes later", "[asset][backend][async]")
{
    TypeRegistry registry = test::MakeBackendRegistry();
    HostMemoryBackend inner(registry, 1 << 20);
    AddMany(inner, registry, 20);
    FaultyBackend faulty(inner);
    faulty.SetLatencyMs(FaultOp::ReadTable, 100);

    UniquePtr<TableTicket> ticket = faulty.ReadTableAsync();
    REQUIRE(ticket);
    CHECK(!ticket->IsDone());
    ResourceTable early;
    bool last = true;
    CHECK(ticket->TakePage(&early, &last) == 0);  // nothing yet, and not the last page
    CHECK(!last);

    ResourceTable table;
    REQUIRE(ticket->Finish(&table).Ok());
    CHECK(ticket->IsDone());
    CHECK(table.Size() == 20);
    ResourceTable page;
    CHECK(ticket->TakePage(&page, &last) == 20);  // the page is still there for a consumer that paged
    CHECK(last);
    CHECK(ticket->TakePage(&page, &last) == 0);
    CHECK(last);
}

TEST_CASE("A cancelled table read leaves no table", "[asset][backend][async]")
{
    TypeRegistry registry = test::MakeBackendRegistry();
    HostMemoryBackend inner(registry, 1 << 20);
    AddMany(inner, registry, 5);
    FaultyBackend faulty(inner);
    faulty.SetLatencyMs(FaultOp::ReadTable, 80);

    UniquePtr<TableTicket> ticket = faulty.ReadTableAsync();
    ticket->Cancel();
    ResourceTable table;
    CHECK(ticket->Finish(&table).Is(ErrorCode::Cancelled));
    CHECK(table.Size() == 0);
    ResourceTable page;
    bool last = true;
    CHECK(ticket->TakePage(&page, &last) == 0);
    CHECK(!last);
}

TEST_CASE("A failed table read surfaces the backend's status from Finish", "[asset][backend][async]")
{
    TypeRegistry registry = test::MakeBackendRegistry();
    HostMemoryBackend inner(registry, 1 << 20);
    AddMany(inner, registry, 5);
    FaultyBackend faulty(inner);
    faulty.FailNext(FaultOp::ReadTable, Status::Make(ErrorCode::Offline, "the server is away"));
    UniquePtr<TableTicket> ticket = faulty.ReadTableAsync();
    ResourceTable table;
    const Status status = ticket->Finish(&table);
    CHECK(status.Is(ErrorCode::Offline));
    CHECK(test::Contains(status.Message(), "away"));
    CHECK(table.Size() == 0);  // never silently partial
}

TEST_CASE("Destroying a ticket whose read is running cancels and waits", "[asset][backend][async]")
{
    TypeRegistry registry = test::MakeBackendRegistry();
    HostMemoryBackend inner(registry, 1 << 20);
    AddMany(inner, registry, 5);
    FaultyBackend faulty(inner);
    faulty.SetLatencyMs(FaultOp::ReadTable, 60);
    {
        UniquePtr<TableTicket> ticket = faulty.ReadTableAsync();
        CHECK(!ticket->IsDone());
    }  // the destructor waits for the job
    CHECK(faulty.CallCount(FaultOp::ReadTable) == 1);
}

namespace
{

struct Producer
{
    TableTicket* ticket = nullptr;
    const TypeRegistry* registry = nullptr;
};

void Produce(Producer* p)
{
    for (u32 page = 0; page < 3; ++page)
    {
        ResourceTable table;
        for (u32 i = 0; i < 4; ++i)
        {
            char path[32];
            FormatTo(path, sizeof(path), "/p%u/%u.bk", page, i);
            REQUIRE(table.Add(StringView(path, CStrLength(path)), test::Rid(page * 10 + i + 1), test::kBackendTestType)
                        .Ok());
        }
        REQUIRE(p->ticket->Publish(table).Ok());
        ThisThread::SleepForMilliseconds(15);
    }
    p->ticket->Complete(Status::Success());
}

}  // namespace

TEST_CASE("Pages of a natively asynchronous table arrive in order and complete", "[asset][backend][async]")
{
    TypeRegistry registry = test::MakeBackendRegistry();
    UniquePtr<TableTicket> ticket = MakeUnique<TableTicket>();
    Producer producer{ticket.Get(), &registry};
    Thread thread(&Produce, &producer);

    ResourceTable all;
    bool last = false;
    usize taken = 0;
    while (!last)
    {
        ResourceTable page;
        const usize n = ticket->TakePage(&page, &last);
        // Each page holds whole published pages: a multiple of four entries, never a fragment.
        CHECK(n % 4 == 0);
        for (usize i = 0; i < page.Size(); ++i)
        {
            const TableEntryView e = page.At(i);
            REQUIRE(all.Add(e.path, e.id, e.type).Ok());
        }
        taken += n;
        ThisThread::Yield();
    }
    thread.Join();
    CHECK(taken == 12);
    CHECK(all.Size() == 12);
    ResourceTable whole;
    REQUIRE(ticket->Finish(&whole).Ok());
    CHECK(whole.Size() == 12);
}

namespace
{

Atomic<int> g_completions{0};

void CountCompletion(void*)
{
    g_completions.FetchAdd(1);
}

}  // namespace

TEST_CASE("The completion function runs once, or SetOnComplete says the read already ended", "[asset][backend][async]")
{
    g_completions.Store(0);
    UniquePtr<TableTicket> ticket = MakeUnique<TableTicket>();
    CHECK(ticket->SetOnComplete(&CountCompletion, nullptr));
    ticket->Complete(Status::Success());
    CHECK(g_completions.Load() == 1);
    CHECK(!ticket->SetOnComplete(&CountCompletion, nullptr));
    CHECK(g_completions.Load() == 1);
}

}  // namespace vse::asset
