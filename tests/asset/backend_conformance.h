#pragma once

// The conformance suite every ResourceBackend passes (Phase 22 Day 315): written once against the interface; each
// backend's test file builds a fixture that holds the same resources and calls RunBackendConformance.

#include <catch2/catch_test_macros.hpp>

#include "asset/backend.h"
#include "cstr/mem_ops.h"
#include "support.h"
#include "sync/atomic.h"
#include "sync/thread.h"

namespace engine_placeholder::asset::test
{

/// What a backend under test holds: ids[i] has the bytes blobs[i] and the virtual path paths[i].
struct ConformanceFixture
{
    Vector<ResourceId> ids;
    Vector<Vector<byte>> blobs;
    Vector<StringView> paths;
    ResourceId missing = ResourceId::FromValue(0xdeadbeef);
};

struct AsyncRead
{
    Atomic<bool> done{false};
    Status status;
};

inline void OnAsyncRead(void* user, const Status& status)
{
    AsyncRead* read = static_cast<AsyncRead*>(user);
    read->status = status;
    read->done.Store(true);
}

inline void RunBackendConformance(ResourceBackend& backend, const ConformanceFixture& fx)
{
    REQUIRE(fx.ids.Size() == fx.blobs.Size());
    CHECK((backend.Capabilities() & kBackendRanged) != 0);
    CHECK(CStrLength(backend.Name()) > 0);

    for (usize i = 0; i < fx.ids.Size(); ++i)
    {
        const Vector<byte>& blob = fx.blobs[i];

        ResourceInfo info;
        REQUIRE(backend.Stat(fx.ids[i], &info).Ok());
        CHECK(info.size == blob.Size());
        if (info.has_hash)
        {
            BlobHeaderInfo header;
            REQUIRE(PeekBlobHeader(ByteSpan(blob.Data(), blob.Size()), &header).Ok());
            CHECK(info.hash == header.content_hash);
        }

        UniquePtr<ResourceReader> reader;
        REQUIRE(backend.Open(fx.ids[i], &reader).Ok());
        REQUIRE(reader->Size() == blob.Size());

        Vector<byte> whole;
        whole.Resize(blob.Size());
        REQUIRE(reader->ReadAt(0, whole.Data(), whole.Size()).Ok());
        CHECK(MemCompare(whole.Data(), blob.Data(), blob.Size()) == 0);

        // A range in the middle, and an empty range at the end.
        const usize from = blob.Size() / 3;
        const usize length = blob.Size() / 2;
        Vector<byte> part;
        part.Resize(length);
        REQUIRE(reader->ReadAt(from, part.Data(), length).Ok());
        CHECK(MemCompare(part.Data(), blob.Data() + from, length) == 0);
        byte none = 0;
        CHECK(reader->ReadAt(blob.Size(), &none, 0).Ok());

        // Past the end is refused, whatever the offset and length.
        Vector<byte> spill;
        spill.Resize(blob.Size() + 1);
        CHECK(reader->ReadAt(0, spill.Data(), blob.Size() + 1).Is(ErrorCode::Invalid));
        CHECK(reader->ReadAt(blob.Size() - 1, spill.Data(), 2).Is(ErrorCode::Invalid));
        CHECK(reader->ReadAt(blob.Size() + 1, spill.Data(), 0).Is(ErrorCode::Invalid));

        // The asynchronous read gives the same bytes.
        Vector<byte> async_bytes;
        async_bytes.Resize(length);
        AsyncRead read;
        reader->ReadAtAsync(from, async_bytes.Data(), length, &OnAsyncRead, &read);
        while (!read.done.Load())
        {
            ThisThread::Yield();
        }
        CHECK(read.status.Ok());
        CHECK(MemCompare(async_bytes.Data(), blob.Data() + from, length) == 0);
    }

    ResourceInfo missing_info;
    CHECK(backend.Stat(fx.missing, &missing_info).Is(ErrorCode::NotFound));
    UniquePtr<ResourceReader> missing_reader;
    CHECK(backend.Open(fx.missing, &missing_reader).Is(ErrorCode::NotFound));
    CHECK(!missing_reader);

    // The table: complete, both directions, blocking and asynchronous.
    ResourceTable table;
    REQUIRE(backend.ReadTable(&table).Ok());
    for (usize i = 0; i < fx.ids.Size(); ++i)
    {
        ResourceId resolved;
        REQUIRE(table.Resolve(fx.paths[i], &resolved));
        CHECK(resolved == fx.ids[i]);
    }

    UniquePtr<TableTicket> ticket = backend.ReadTableAsync();
    REQUIRE(ticket);
    ResourceTable async_table;
    REQUIRE(ticket->Finish(&async_table).Ok());
    CHECK(async_table.Size() == table.Size());
    CHECK(ticket->IsDone());

    UniquePtr<TableTicket> paged = backend.ReadTableAsync();
    ResourceTable pages;
    bool last = false;
    while (!last)
    {
        paged->TakePage(&pages, &last);
        ThisThread::Yield();
    }
    CHECK(pages.Size() == table.Size());

    Vector<ResourceId> listed;
    REQUIRE(backend.List(StringView("/"), 1000, &listed).Ok());
    CHECK(listed.Size() == table.Size());
    REQUIRE(backend.List(StringView("/"), 1, &listed).Ok());
    CHECK(listed.Size() == (table.Size() > 0 ? 1 : 0));
    REQUIRE(backend.List(StringView("/no-such-prefix"), 10, &listed).Ok());
    CHECK(listed.Size() == 0);

    Vector<ChangeRecord> changes;
    CHECK(backend.Watch(&changes).Ok());
}

}  // namespace engine_placeholder::asset::test
