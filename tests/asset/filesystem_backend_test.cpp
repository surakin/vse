// The filesystem backend (Phase 22 Day 315): the conformance suite on a walked directory and on a directory with a
// stored table, identity rules (the file's own id wins over a table), files that vanish, a large directory, Watch.

#include <catch2/catch_test_macros.hpp>

#include "asset/filesystem_backend.h"
#include "cstr/format.h"
#include "backend_conformance.h"
#include "io/file.h"
#include "io/file_system.h"
#include "support.h"

namespace vse::asset
{

namespace
{

io::Path Under(const test::TempDir& dir, const char* relative)
{
    return dir.Path().Join(test::Vp(relative));
}

void WriteBlobFile(const test::TempDir& dir, const char* relative, const Vector<byte>& blob)
{
    const io::Path file = Under(dir, relative);
    REQUIRE(io::CreateDirectories(file.Parent()).Ok());
    REQUIRE(io::WriteFile(file, blob.Data(), blob.Size()).Ok());
}

struct Setup
{
    TypeRegistry registry = test::MakeBackendRegistry();
    test::TempDir dir;
    test::ConformanceFixture fixture;

    // Three blobs: two in subdirectories, one at the root, a non-blob file beside them.
    Setup()
    {
        Add("/textures/rock.bk", 0x101, 900, 1);
        Add("/meshes/deep/tree.bk", 0x102, 5000, 2);
        Add("/lone.bk", 0x103, 70, 3);
        const char* note = "not a blob";
        REQUIRE(io::WriteFile(Under(dir, "readme.txt"), note, CStrLength(note)).Ok());
    }

    void Add(const char* path, u64 id, usize payload, u32 seed)
    {
        Vector<byte> blob = test::MakeTestBlob(registry, test::Rid(id), payload, seed);
        WriteBlobFile(dir, path + 1, blob);
        fixture.ids.Push(test::Rid(id));
        fixture.paths.Push(test::Vp(path));
        fixture.blobs.Push(Move(blob));
    }

    UniquePtr<FilesystemBackend> Backend()
    {
        UniquePtr<FilesystemBackend> backend;
        REQUIRE(FilesystemBackend::Create(dir.Path(), registry, &backend).Ok());
        return backend;
    }

    void WriteStoredTable()
    {
        ResourceTable table;
        for (usize i = 0; i < fixture.ids.Size(); ++i)
        {
            REQUIRE(table.Add(fixture.paths[i], fixture.ids[i], test::kBackendTestType).Ok());
        }
        Vector<byte> bytes;
        REQUIRE(table.WriteNamesBlob(registry, &bytes).Ok());
        REQUIRE(io::WriteFile(Under(dir, kStoredTableFileName), bytes.Data(), bytes.Size()).Ok());
    }
};

}  // namespace

TEST_CASE("The filesystem backend passes the conformance suite on a walked directory", "[asset][backend][filesystem]")
{
    Setup s;
    UniquePtr<FilesystemBackend> backend = s.Backend();
    CHECK((backend->Capabilities() & kBackendStoredTable) == 0);
    test::RunBackendConformance(*backend, s.fixture);
}

TEST_CASE("The filesystem backend passes the conformance suite with a stored table", "[asset][backend][filesystem]")
{
    Setup s;
    s.WriteStoredTable();
    UniquePtr<FilesystemBackend> backend = s.Backend();
    CHECK((backend->Capabilities() & kBackendStoredTable) != 0);
    test::RunBackendConformance(*backend, s.fixture);
}

TEST_CASE("The table path is the file location, never an id from a file name", "[asset][backend][filesystem]")
{
    Setup s;
    // A file named like an id but holding another blob: the id inside decides.
    Vector<byte> blob = test::MakeTestBlob(s.registry, test::Rid(0x999), 40, 9);
    WriteBlobFile(s.dir, "0000000000000101.bk", blob);
    UniquePtr<FilesystemBackend> backend = s.Backend();
    ResourceTable table;
    REQUIRE(backend->ReadTable(&table).Ok());
    ResourceId id;
    REQUIRE(table.Resolve(test::Vp("/0000000000000101.bk"), &id));
    CHECK(id == test::Rid(0x999));
    REQUIRE(table.Resolve(test::Vp("/textures/rock.bk"), &id));
    CHECK(id == test::Rid(0x101));
    CHECK(!table.Resolve(test::Vp("/readme.txt"), &id));
}

TEST_CASE("Two files claiming one id are refused", "[asset][backend][filesystem]")
{
    Setup s;
    WriteBlobFile(s.dir, "copy/rock_copy.bk", s.fixture.blobs[0]);  // a copied blob: a duplicated id
    UniquePtr<FilesystemBackend> backend = s.Backend();
    ResourceTable table;
    CHECK(backend->ReadTable(&table).Is(ErrorCode::DuplicateId));
    ResourceInfo info;
    CHECK(backend->Stat(test::Rid(0x101), &info).Is(ErrorCode::DuplicateId));
}

TEST_CASE("A stale stored table cannot point an id at another resource's file", "[asset][backend][filesystem]")
{
    Setup s;
    s.WriteStoredTable();
    // The table still says /lone.bk is 0x103, but the file now holds a blob of 0x777.
    WriteBlobFile(s.dir, "lone.bk", test::MakeTestBlob(s.registry, test::Rid(0x777), 70, 7));
    UniquePtr<FilesystemBackend> backend = s.Backend();
    ResourceInfo info;
    const Status stat = backend->Stat(test::Rid(0x103), &info);
    CHECK(stat.Is(ErrorCode::Corrupt));
    CHECK(test::Contains(stat.Message(), "disagree"));
    UniquePtr<ResourceReader> reader;
    CHECK(backend->Open(test::Rid(0x103), &reader).Is(ErrorCode::Corrupt));
    CHECK(!reader);
    CHECK(backend->Stat(test::Rid(0x777), &info).Is(ErrorCode::NotFound));  // the table does not know 0x777
}

TEST_CASE("A table entry whose file is missing is not served", "[asset][backend][filesystem]")
{
    Setup s;
    s.WriteStoredTable();
    REQUIRE(io::Remove(Under(s.dir, "lone.bk")).Ok());
    UniquePtr<FilesystemBackend> backend = s.Backend();
    ResourceTable table;
    REQUIRE(backend->ReadTable(&table).Ok());
    CHECK(table.Size() == 2);
    ResourceInfo info;
    CHECK(backend->Stat(test::Rid(0x103), &info).Is(ErrorCode::NotFound));
}

TEST_CASE("A file that disappears after the index was built is NotFound", "[asset][backend][filesystem]")
{
    Setup s;
    UniquePtr<FilesystemBackend> backend = s.Backend();
    ResourceInfo info;
    REQUIRE(backend->Stat(test::Rid(0x103), &info).Ok());
    REQUIRE(io::Remove(Under(s.dir, "lone.bk")).Ok());
    UniquePtr<ResourceReader> reader;
    CHECK(backend->Open(test::Rid(0x103), &reader).Is(ErrorCode::NotFound));
    CHECK(!reader);
}

TEST_CASE("A blob of an old container version fails the index and names the remint tool",
          "[asset][backend][filesystem]")
{
    Setup s;
    Vector<byte> old = test::MakeTestBlob(s.registry, test::Rid(0x555), 40, 5);
    old[4] = static_cast<byte>(old[4] + 1);  // the container version
    WriteBlobFile(s.dir, "old.bk", old);
    UniquePtr<FilesystemBackend> backend = s.Backend();
    ResourceTable table;
    const Status status = backend->ReadTable(&table);
    CHECK(status.Is(ErrorCode::UnsupportedVersion));
    CHECK(test::Contains(status.Message(), "remint"));
}

TEST_CASE("The root must be a directory", "[asset][backend][filesystem]")
{
    Setup s;
    UniquePtr<FilesystemBackend> backend;
    CHECK(FilesystemBackend::Create(Under(s.dir, "no-such-dir"), s.registry, &backend).Is(ErrorCode::NotFound));
    CHECK(FilesystemBackend::Create(Under(s.dir, "lone.bk"), s.registry, &backend).Is(ErrorCode::NotFound));
}

TEST_CASE("A directory of ten thousand blobs is indexed and served", "[asset][backend][filesystem]")
{
    TypeRegistry registry = test::MakeBackendRegistry();
    test::TempDir dir;
    for (u32 i = 0; i < 10000; ++i)
    {
        const Vector<byte> blob = test::MakeTestBlob(registry, test::Rid(0x10000 + i), 16, i);
        char name[64];
        FormatTo(name, sizeof(name), "bulk/%u.bk", i);
        const io::Path file = dir.Path().Join(StringView(name, CStrLength(name)));
        if (i == 0)
        {
            REQUIRE(io::CreateDirectories(file.Parent()).Ok());
        }
        REQUIRE(io::WriteFile(file, blob.Data(), blob.Size()).Ok());
    }
    UniquePtr<FilesystemBackend> backend;
    REQUIRE(FilesystemBackend::Create(dir.Path(), registry, &backend).Ok());
    ResourceTable table;
    REQUIRE(backend->ReadTable(&table).Ok());
    CHECK(table.Size() == 10000);
    ResourceInfo info;
    CHECK(backend->Stat(test::Rid(0x10000 + 4321), &info).Ok());
    Vector<ResourceId> listed;
    REQUIRE(backend->List(StringView("/bulk/"), 50, &listed).Ok());
    CHECK(listed.Size() == 50);
}

TEST_CASE("Watch reports added, replaced and removed files", "[asset][backend][filesystem]")
{
    Setup s;
    UniquePtr<FilesystemBackend> backend = s.Backend();
    Vector<ChangeRecord> changes;
    REQUIRE(backend->Watch(&changes).Ok());  // the first call builds the index and reports nothing
    CHECK(changes.Size() == 0);
    REQUIRE(backend->Watch(&changes).Ok());
    CHECK(changes.Size() == 0);

    WriteBlobFile(s.dir, "new/added.bk", test::MakeTestBlob(s.registry, test::Rid(0x400), 30, 4));
    WriteBlobFile(s.dir, "lone.bk", test::MakeTestBlob(s.registry, test::Rid(0x103), 300, 33));  // a different size
    REQUIRE(io::Remove(Under(s.dir, "textures/rock.bk")).Ok());
    REQUIRE(backend->Watch(&changes).Ok());
    REQUIRE(changes.Size() == 3);
    bool added = false;
    bool replaced = false;
    bool removed = false;
    for (usize i = 0; i < changes.Size(); ++i)
    {
        added = added || (changes[i].id == test::Rid(0x400) && changes[i].kind == ChangeKind::Added);
        replaced = replaced || (changes[i].id == test::Rid(0x103) && changes[i].kind == ChangeKind::Replaced);
        removed = removed || (changes[i].id == test::Rid(0x101) && changes[i].kind == ChangeKind::Removed);
    }
    CHECK(added);
    CHECK(replaced);
    CHECK(removed);

    ResourceInfo info;
    CHECK(backend->Stat(test::Rid(0x400), &info).Ok());
    CHECK(backend->Stat(test::Rid(0x101), &info).Is(ErrorCode::NotFound));
    REQUIRE(backend->Watch(&changes).Ok());
    CHECK(changes.Size() == 0);
}

}  // namespace vse::asset
