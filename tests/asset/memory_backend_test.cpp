// The memory backend (Phase 22 Day 315): copied and borrowed bytes, the release callback, replace while a reader is
// open, verified hashes, the budget, watch records, dependents, and concurrent changes.

#include <catch2/catch_test_macros.hpp>

#include "asset/memory_backend.h"
#include "backend_conformance.h"
#include "support.h"
#include "sync/thread.h"

namespace vse::asset
{

namespace
{

struct Dependents
{
    Vector<ResourceId> dependents;  // of every id
    Vector<ResourceId> unloaded;
};

bool QueryDependents(void* user, ResourceId, Vector<ResourceId>* out)
{
    Dependents* d = static_cast<Dependents*>(user);
    for (usize i = 0; i < d->dependents.Size(); ++i)
    {
        out->Push(d->dependents[i]);
    }
    return d->dependents.Size() > 0;
}

void Unload(void* user, ResourceId id)
{
    static_cast<Dependents*>(user)->unloaded.Push(id);
}

constexpr u64 kBudget = 1 << 20;

}  // namespace

TEST_CASE("The memory backend passes the conformance suite", "[asset][backend][memory]")
{
    TypeRegistry registry = test::MakeBackendRegistry();
    HostMemoryBackend backend(registry, kBudget);
    test::ConformanceFixture fx;
    const char* paths[] = {"/a/one.bk", "/a/two.bk", "/three.bk"};
    for (u32 i = 0; i < 3; ++i)
    {
        fx.ids.Push(test::Rid(0x10 + i));
        fx.blobs.Push(test::MakeTestBlob(registry, test::Rid(0x10 + i), 100 + i * 400, i));
        fx.paths.Push(test::Vp(paths[i]));
        const StringView path = test::Vp(paths[i]);
        MemoryAddOptions options;
        options.paths = vse::Span<const StringView>(&path, 1);
        REQUIRE(backend.Add(fx.ids[i], test::AsBytes(fx.blobs[i]), options).Ok());
    }
    test::RunBackendConformance(backend, fx);
}

TEST_CASE("Copied bytes outlive the host's buffer", "[asset][backend][memory]")
{
    TypeRegistry registry = test::MakeBackendRegistry();
    HostMemoryBackend backend(registry, kBudget);
    Vector<byte> blob = test::MakeTestBlob(registry, test::Rid(1), 64, 1);
    const Vector<byte> original = test::MakeTestBlob(registry, test::Rid(1), 64, 1);
    REQUIRE(backend.Add(test::Rid(1), test::AsBytes(blob)).Ok());
    MemSet(blob.Data(), 0, blob.Size());  // the host reuses its buffer
    UniquePtr<ResourceReader> reader;
    REQUIRE(backend.Open(test::Rid(1), &reader).Ok());
    Vector<byte> read;
    read.Resize(blob.Size());
    REQUIRE(reader->ReadAt(0, read.Data(), read.Size()).Ok());
    CHECK(MemCompare(read.Data(), original.Data(), original.Size()) == 0);
}

TEST_CASE("A removed resource stays readable through a reader that is open", "[asset][backend][memory]")
{
    TypeRegistry registry = test::MakeBackendRegistry();
    HostMemoryBackend backend(registry, kBudget);
    const Vector<byte> blob = test::MakeTestBlob(registry, test::Rid(2), 64, 2);
    REQUIRE(backend.Add(test::Rid(2), test::AsBytes(blob)).Ok());
    UniquePtr<ResourceReader> reader;
    REQUIRE(backend.Open(test::Rid(2), &reader).Ok());
    REQUIRE(backend.Remove(test::Rid(2)).Ok());
    Vector<byte> read;
    read.Resize(blob.Size());
    REQUIRE(reader->ReadAt(0, read.Data(), read.Size()).Ok());  // still readable
    CHECK(MemCompare(read.Data(), blob.Data(), blob.Size()) == 0);
}

TEST_CASE("A refused add changes nothing", "[asset][backend][memory]")
{
    TypeRegistry registry = test::MakeBackendRegistry();
    const Vector<byte> blob = test::MakeTestBlob(registry, test::Rid(3), 64, 3);
    const Vector<byte> other = test::MakeTestBlob(registry, test::Rid(4), 64, 4);
    HostMemoryBackend backend(registry, kBudget);
    REQUIRE(backend.Add(test::Rid(3), test::AsBytes(blob)).Ok());
    const u64 held = backend.BytesHeld();
    CHECK(backend.Add(test::Rid(3), test::AsBytes(blob)).Is(ErrorCode::DuplicateId));  // the id is live
    CHECK(backend.Add(test::Rid(5), test::AsBytes(other)).Is(ErrorCode::Corrupt));     // the blob's own id is 4
    CHECK(backend.BytesHeld() == held);
}

TEST_CASE("A replace while a reader is open does not tear the read", "[asset][backend][memory]")
{
    TypeRegistry registry = test::MakeBackendRegistry();
    HostMemoryBackend backend(registry, kBudget);
    const Vector<byte> first = test::MakeTestBlob(registry, test::Rid(6), 200, 1);
    const Vector<byte> second = test::MakeTestBlob(registry, test::Rid(6), 500, 2);
    REQUIRE(backend.Add(test::Rid(6), test::AsBytes(first)).Ok());
    UniquePtr<ResourceReader> old_reader;
    REQUIRE(backend.Open(test::Rid(6), &old_reader).Ok());
    REQUIRE(backend.Replace(test::Rid(6), test::AsBytes(second)).Ok());

    Vector<byte> read;
    read.Resize(first.Size());
    REQUIRE(old_reader->ReadAt(0, read.Data(), read.Size()).Ok());
    CHECK(MemCompare(read.Data(), first.Data(), first.Size()) == 0);

    ResourceInfo info;
    REQUIRE(backend.Stat(test::Rid(6), &info).Ok());
    CHECK(info.size == second.Size());
    UniquePtr<ResourceReader> new_reader;
    REQUIRE(backend.Open(test::Rid(6), &new_reader).Ok());
    CHECK(new_reader->Size() == second.Size());
    CHECK(backend.BytesHeld() == second.Size());
}

TEST_CASE("A hash the host supplies is verified, and a damaged blob is refused", "[asset][backend][memory]")
{
    TypeRegistry registry = test::MakeBackendRegistry();
    HostMemoryBackend backend(registry, kBudget);
    Vector<byte> blob = test::MakeTestBlob(registry, test::Rid(7), 100, 1);
    BlobHeaderInfo header;
    REQUIRE(PeekBlobHeader(test::AsBytes(blob), &header).Ok());

    MemoryAddOptions wrong;
    ContentHash bad = header.content_hash;
    bad.bytes[0] = static_cast<u8>(bad.bytes[0] ^ 0xff);
    wrong.expected_hash = &bad;
    CHECK(backend.Add(test::Rid(7), test::AsBytes(blob), wrong).Is(ErrorCode::Corrupt));
    CHECK(backend.BytesHeld() == 0);

    MemoryAddOptions right;
    right.expected_hash = &header.content_hash;
    REQUIRE(backend.Add(test::Rid(7), test::AsBytes(blob), right).Ok());
    ResourceInfo info;
    REQUIRE(backend.Stat(test::Rid(7), &info).Ok());
    CHECK(info.has_hash);
    CHECK(info.hash == header.content_hash);

    Vector<byte> damaged = test::MakeTestBlob(registry, test::Rid(8), 100, 2);
    damaged[damaged.Size() - 1] = static_cast<byte>(damaged[damaged.Size() - 1] ^ 1);
    CHECK(backend.Add(test::Rid(8), test::AsBytes(damaged)).Is(ErrorCode::Corrupt));
    CHECK(backend.Add(ResourceId::None(), test::AsBytes(blob)).Is(ErrorCode::Invalid));
}

TEST_CASE("AddMany adds every item or none, and says which one was refused", "[asset][backend][memory][many]")
{
    TypeRegistry registry = test::MakeBackendRegistry();
    const Vector<byte> a = test::MakeTestBlob(registry, test::Rid(40), 100, 1);
    const Vector<byte> b = test::MakeTestBlob(registry, test::Rid(41), 100, 2);
    const Vector<byte> c = test::MakeTestBlob(registry, test::Rid(42), 100, 3);
    const StringView path_a = test::Vp("/many/a.bk");
    const StringView path_c = test::Vp("/many/c.bk");
    auto item = [](ResourceId id, const Vector<byte>& blob, const StringView* path)
    {
        MemoryAddItem one;
        one.id = id;
        one.data = blob.Data();
        one.size = blob.Size();
        if (path != nullptr)
        {
            one.options.paths = vse::Span<const StringView>(path, 1);
        }
        return one;
    };
    HostMemoryBackend backend(registry, kBudget);
    usize failed = 99;
    ResourceInfo info;

    SECTION("a batch is added in full, with its paths, and shows in the watch records")
    {
        const MemoryAddItem items[] = {item(test::Rid(40), a, &path_a), item(test::Rid(41), b, nullptr),
                                       item(test::Rid(42), c, &path_c)};
        REQUIRE(backend.AddMany(vse::Span<const MemoryAddItem>(items, 3), &failed).Ok());
        CHECK(backend.BytesHeld() == a.Size() + b.Size() + c.Size());
        for (u32 id = 40; id < 43; ++id)
        {
            CHECK(backend.Stat(test::Rid(id), &info).Ok());
        }
        ResourceTable table;
        REQUIRE(backend.ReadTable(&table).Ok());
        ResourceId found;
        CHECK((table.Resolve(path_a, &found) && found == test::Rid(40)));
        CHECK((table.Resolve(path_c, &found) && found == test::Rid(42)));
        Vector<ChangeRecord> changes;
        REQUIRE(backend.Watch(&changes).Ok());
        CHECK(changes.Size() == 3);
    }
    SECTION("an empty batch is nothing")
    {
        CHECK(backend.AddMany(vse::Span<const MemoryAddItem>(), &failed).Ok());
        CHECK(backend.BytesHeld() == 0);
    }
    SECTION("a damaged blob refuses the batch and names it")
    {
        Vector<byte> damaged = test::MakeTestBlob(registry, test::Rid(42), 100, 3);
        damaged[damaged.Size() - 1] = static_cast<byte>(damaged[damaged.Size() - 1] ^ 0x5A);
        const MemoryAddItem items[] = {item(test::Rid(40), a, &path_a), item(test::Rid(41), b, nullptr),
                                       item(test::Rid(42), damaged, nullptr)};
        CHECK(backend.AddMany(vse::Span<const MemoryAddItem>(items, 3), &failed).Is(ErrorCode::Corrupt));
        CHECK(failed == 2);
        CHECK(backend.BytesHeld() == 0);
        CHECK(backend.Stat(test::Rid(40), &info).Is(ErrorCode::NotFound));
        ResourceTable table;
        REQUIRE(backend.ReadTable(&table).Ok());
        CHECK(table.Size() == 0);
    }
    SECTION("a live id, a repeated id and a repeated path refuse the batch")
    {
        REQUIRE(backend.Add(test::Rid(41), test::AsBytes(b)).Ok());
        const u64 held = backend.BytesHeld();
        const MemoryAddItem live[] = {item(test::Rid(40), a, nullptr), item(test::Rid(41), b, nullptr)};
        CHECK(backend.AddMany(vse::Span<const MemoryAddItem>(live, 2), &failed).Is(ErrorCode::DuplicateId));
        CHECK(failed == 1);
        const MemoryAddItem twice[] = {item(test::Rid(40), a, nullptr), item(test::Rid(40), a, nullptr)};
        CHECK(backend.AddMany(vse::Span<const MemoryAddItem>(twice, 2), &failed).Is(ErrorCode::DuplicateId));
        CHECK(failed == 1);
        const MemoryAddItem same_path[] = {item(test::Rid(40), a, &path_a), item(test::Rid(42), c, &path_a)};
        CHECK(backend.AddMany(vse::Span<const MemoryAddItem>(same_path, 2), &failed).Is(ErrorCode::DuplicateId));
        CHECK(failed == 1);
        CHECK(backend.BytesHeld() == held);
        CHECK(backend.Stat(test::Rid(40), &info).Is(ErrorCode::NotFound));
    }
    SECTION("a path of another resource refuses the batch")
    {
        const MemoryAddItem first[] = {item(test::Rid(40), a, &path_a)};
        REQUIRE(backend.AddMany(vse::Span<const MemoryAddItem>(first, 1), &failed).Ok());
        const MemoryAddItem second[] = {item(test::Rid(41), b, nullptr), item(test::Rid(42), c, &path_a)};
        CHECK(backend.AddMany(vse::Span<const MemoryAddItem>(second, 2), &failed).Is(ErrorCode::DuplicateId));
        CHECK(failed == 1);
        CHECK(backend.Stat(test::Rid(41), &info).Is(ErrorCode::NotFound));
    }
    SECTION("the sum must fit the budget, and then the failed index is the count")
    {
        HostMemoryBackend small(registry, a.Size() + b.Size() + c.Size() - 1);
        const MemoryAddItem items[] = {item(test::Rid(40), a, nullptr), item(test::Rid(41), b, nullptr),
                                       item(test::Rid(42), c, nullptr)};
        CHECK(small.AddMany(vse::Span<const MemoryAddItem>(items, 3), &failed).Is(ErrorCode::LimitExceeded));
        CHECK(failed == 3);
        CHECK(small.BytesHeld() == 0);
        CHECK(small.Stat(test::Rid(40), &info).Is(ErrorCode::NotFound));
    }
    SECTION("a lent blob is refused: AddMany copies")
    {
        MemoryAddItem lent = item(test::Rid(40), a, nullptr);
        lent.options.release = [](void*, ByteSpan) {};
        CHECK(backend.AddMany(vse::Span<const MemoryAddItem>(&lent, 1), &failed).Is(ErrorCode::Invalid));
        CHECK(backend.BytesHeld() == 0);
    }
}

TEST_CASE("The byte budget is back-pressure, not eviction", "[asset][backend][memory]")
{
    TypeRegistry registry = test::MakeBackendRegistry();
    const Vector<byte> a = test::MakeTestBlob(registry, test::Rid(10), 1000, 1);
    const Vector<byte> b = test::MakeTestBlob(registry, test::Rid(11), 1000, 2);
    HostMemoryBackend backend(registry, a.Size() + b.Size() - 1);
    REQUIRE(backend.Add(test::Rid(10), test::AsBytes(a)).Ok());
    CHECK(backend.Add(test::Rid(11), test::AsBytes(b)).Is(ErrorCode::LimitExceeded));
    CHECK(backend.BytesHeld() == a.Size());
    ResourceInfo info;
    CHECK(backend.Stat(test::Rid(10), &info).Ok());  // the first is still there
    REQUIRE(backend.Remove(test::Rid(10)).Ok());
    REQUIRE(backend.Add(test::Rid(11), test::AsBytes(b)).Ok());  // room again
    // A refused replace leaves the old bytes.
    const Vector<byte> big = test::MakeTestBlob(registry, test::Rid(11), 5000, 3);
    CHECK(backend.Replace(test::Rid(11), test::AsBytes(big)).Is(ErrorCode::LimitExceeded));
    REQUIRE(backend.Stat(test::Rid(11), &info).Ok());
    CHECK(info.size == b.Size());
}

TEST_CASE("Every add, replace and remove leaves a watch record", "[asset][backend][memory]")
{
    TypeRegistry registry = test::MakeBackendRegistry();
    HostMemoryBackend backend(registry, kBudget);
    const Vector<byte> blob = test::MakeTestBlob(registry, test::Rid(20), 50, 1);
    const Vector<byte> blob2 = test::MakeTestBlob(registry, test::Rid(20), 60, 2);
    REQUIRE(backend.Add(test::Rid(20), test::AsBytes(blob)).Ok());
    REQUIRE(backend.Replace(test::Rid(20), test::AsBytes(blob2)).Ok());
    REQUIRE(backend.Remove(test::Rid(20)).Ok());
    CHECK(backend.Remove(test::Rid(20)).Is(ErrorCode::NotFound));
    CHECK(backend.Replace(test::Rid(20), test::AsBytes(blob2)).Is(ErrorCode::NotFound));  // refused: no record

    Vector<ChangeRecord> changes;
    REQUIRE(backend.Watch(&changes).Ok());
    REQUIRE(changes.Size() == 3);
    CHECK(changes[0].kind == ChangeKind::Added);
    CHECK(changes[1].kind == ChangeKind::Replaced);
    CHECK(changes[2].kind == ChangeKind::Removed);
    for (usize i = 0; i < 3; ++i)
    {
        CHECK(changes[i].id == test::Rid(20));
    }
    REQUIRE(backend.Watch(&changes).Ok());
    CHECK(changes.Size() == 0);
}

TEST_CASE("Paths: reachable by id only, or by the paths the host gave; a path belongs to one id",
          "[asset][backend][memory]")
{
    TypeRegistry registry = test::MakeBackendRegistry();
    HostMemoryBackend backend(registry, kBudget);
    const Vector<byte> a = test::MakeTestBlob(registry, test::Rid(30), 50, 1);
    const Vector<byte> b = test::MakeTestBlob(registry, test::Rid(31), 50, 2);
    const Vector<byte> a2 = test::MakeTestBlob(registry, test::Rid(30), 70, 3);
    const StringView paths[] = {test::Vp("/x/a.bk"), test::Vp("/x/alias.bk")};
    MemoryAddOptions with_paths;
    with_paths.paths = vse::Span<const StringView>(paths, 2);
    REQUIRE(backend.Add(test::Rid(30), test::AsBytes(a), with_paths).Ok());
    REQUIRE(backend.Add(test::Rid(31), test::AsBytes(b)).Ok());  // by id only

    MemoryAddOptions clash;
    const StringView taken = test::Vp("/x/a.bk");
    clash.paths = vse::Span<const StringView>(&taken, 1);
    const Vector<byte> c = test::MakeTestBlob(registry, test::Rid(32), 50, 4);
    CHECK(backend.Add(test::Rid(32), test::AsBytes(c), clash).Is(ErrorCode::DuplicateId));
    ResourceInfo info;
    CHECK(backend.Stat(test::Rid(32), &info).Is(ErrorCode::NotFound));  // nothing half-added

    MemoryAddOptions malformed;
    const StringView bad = test::Vp("x/../a.bk");
    malformed.paths = vse::Span<const StringView>(&bad, 1);
    CHECK(backend.Add(test::Rid(32), test::AsBytes(c), malformed).Failed());

    REQUIRE(backend.Replace(test::Rid(30), test::AsBytes(a2)).Ok());  // the paths stay
    ResourceTable table;
    REQUIRE(backend.ReadTable(&table).Ok());
    CHECK(table.Size() == 2);
    ResourceId id;
    REQUIRE(table.Resolve(test::Vp("/x/alias.bk"), &id));
    CHECK(id == test::Rid(30));
    REQUIRE(backend.Remove(test::Rid(30)).Ok());
    ResourceTable after;
    REQUIRE(backend.ReadTable(&after).Ok());
    CHECK(after.Size() == 0);
}

TEST_CASE("Removing or replacing a resource that a loaded one depends on is refused, naming the dependents",
          "[asset][backend][memory]")
{
    TypeRegistry registry = test::MakeBackendRegistry();
    HostMemoryBackend backend(registry, kBudget);
    const Vector<byte> blob = test::MakeTestBlob(registry, test::Rid(40), 50, 1);
    const Vector<byte> blob2 = test::MakeTestBlob(registry, test::Rid(40), 60, 2);
    REQUIRE(backend.Add(test::Rid(40), test::AsBytes(blob)).Ok());
    Dependents deps;
    deps.dependents.Push(test::Rid(900));
    deps.dependents.Push(test::Rid(901));
    ResourceInUseQuery query;
    query.dependents = &QueryDependents;
    query.unload = &Unload;
    query.user = &deps;
    backend.SetInUseQuery(query);

    Vector<ResourceId> named;
    CHECK(backend.Remove(test::Rid(40), false, &named).Is(ErrorCode::InUse));
    REQUIRE(named.Size() == 2);
    CHECK(named[0] == test::Rid(900));
    CHECK(named[1] == test::Rid(901));
    CHECK(backend.Replace(test::Rid(40), test::AsBytes(blob2), false, &named).Is(ErrorCode::InUse));
    ResourceInfo info;
    REQUIRE(backend.Stat(test::Rid(40), &info).Ok());
    CHECK(info.size == blob.Size());  // nothing changed

    // Forced: the dependents are unloaded first, then the change happens.
    REQUIRE(backend.Replace(test::Rid(40), test::AsBytes(blob2), true).Ok());
    CHECK(deps.unloaded.Size() == 2);
    REQUIRE(backend.Remove(test::Rid(40), true).Ok());
    CHECK(deps.unloaded.Size() == 4);
    CHECK(backend.Stat(test::Rid(40), &info).Is(ErrorCode::NotFound));
}

TEST_CASE("Concurrent add, replace, remove and read keep the backend consistent", "[asset][backend][memory][tsan]")
{
    TypeRegistry registry = test::MakeBackendRegistry();
    HostMemoryBackend backend(registry, 64 << 20);
    constexpr u32 kIds = 8;
    Vector<Vector<byte>> blobs;
    for (u32 i = 0; i < kIds; ++i)
    {
        blobs.Push(test::MakeTestBlob(registry, test::Rid(100 + i), 500 + i, i));
    }
    for (u32 i = 0; i < kIds / 2; ++i)
    {
        REQUIRE(backend.Add(test::Rid(100 + i), test::AsBytes(blobs[i])).Ok());
    }

    struct Shared
    {
        HostMemoryBackend* backend;
        Vector<Vector<byte>>* blobs;
        Atomic<bool> stop{false};
        Atomic<u32> bad{0};
    } shared{&backend, &blobs, {false}, {0}};

    auto writer = [](Shared* s, u32 seed)
    {
        test::Rng rng(seed);
        for (u32 n = 0; n < 400; ++n)
        {
            const u32 i = rng.Below(kIds);
            const ByteSpan bytes((*s->blobs)[i].Data(), (*s->blobs)[i].Size());
            switch (rng.Below(3))
            {
            case 0: s->backend->Add(test::Rid(100 + i), bytes).Ok(); break;
            case 1: s->backend->Replace(test::Rid(100 + i), bytes).Ok(); break;
            default: s->backend->Remove(test::Rid(100 + i)).Ok(); break;
            }
        }
    };
    auto reader = [](Shared* s)
    {
        test::Rng rng(99);
        while (!s->stop.Load())
        {
            const u32 i = rng.Below(kIds);
            UniquePtr<ResourceReader> r;
            if (s->backend->Open(test::Rid(100 + i), &r).Ok())
            {
                Vector<byte> bytes;
                bytes.Resize(r->Size());
                if (!r->ReadAt(0, bytes.Data(), bytes.Size()).Ok() ||
                    MemCompare(bytes.Data(), (*s->blobs)[i].Data(), bytes.Size()) != 0 ||
                    bytes.Size() != (*s->blobs)[i].Size())
                {
                    s->bad.FetchAdd(1);
                }
            }
            Vector<ChangeRecord> changes;
            s->backend->Watch(&changes).Ok();
        }
    };
    Thread w1(writer, &shared, 1u);
    Thread w2(writer, &shared, 2u);
    Thread r1(reader, &shared);
    Thread r2(reader, &shared);
    w1.Join();
    w2.Join();
    shared.stop.Store(true);
    r1.Join();
    r2.Join();
    CHECK(shared.bad.Load() == 0);
}

TEST_CASE("A forced replace that cannot happen unloads nothing", "[asset][backend][memory]")
{
    TypeRegistry registry = test::MakeBackendRegistry();
    const Vector<byte> small = test::MakeTestBlob(registry, test::Rid(70), 100, 1);
    const Vector<byte> huge = test::MakeTestBlob(registry, test::Rid(70), 5000, 2);
    HostMemoryBackend backend(registry, small.Size() + 100);
    REQUIRE(backend.Add(test::Rid(70), test::AsBytes(small)).Ok());
    Dependents deps;
    deps.dependents.Push(test::Rid(900));
    ResourceInUseQuery query;
    query.dependents = &QueryDependents;
    query.unload = &Unload;
    query.user = &deps;
    backend.SetInUseQuery(query);
    CHECK(backend.Replace(test::Rid(70), test::AsBytes(huge), true).Is(ErrorCode::LimitExceeded));
    CHECK(deps.unloaded.Size() == 0);  // the dependents were not unloaded for a change that did not happen
}

}  // namespace vse::asset
