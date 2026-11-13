// The pak (Phase 22 Day 316): round trip, the conformance suite, damage and truncation, determinism, targets, closure,
// patch paks and 64-bit offsets.

#include <catch2/catch_test_macros.hpp>

#include "asset/filesystem_backend.h"
#include "asset/pak.h"
#include "asset/pak_backend.h"
#include "backend_conformance.h"
#include "containers/array.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"
#include "os/os.h"
#include "memory_file.h"
#include "io/file_system.h"
#include "support.h"

namespace engine_placeholder::asset
{

namespace
{

Vector<byte> CopyOf(const Vector<byte>& bytes)
{
    Vector<byte> out;
    out.Resize(bytes.Size());
    MemCopy(out.Data(), bytes.Data(), bytes.Size());
    return out;
}

Vector<byte> PakBlob(const TypeRegistry& registry, ResourceType type, ResourceId id, u32 target, usize payload,
                     const Dependency* dependency = nullptr)
{
    test::Rng rng(static_cast<u32>(id.Value()));
    Vector<byte> bytes;
    for (usize i = 0; i < payload; ++i)
    {
        bytes.Push(static_cast<byte>(rng.Below(256)));
    }
    BlobWriter writer(registry, type, id, target);
    if (dependency != nullptr)
    {
        writer.AddDependency(*dependency);
    }
    REQUIRE(writer.AddSection(16, ByteSpan(bytes.Data(), bytes.Size())).Ok());
    Vector<byte> blob;
    REQUIRE(writer.Finish(&blob).Ok());
    return blob;
}

struct PakSet
{
    TypeRegistry registry = test::MakeBackendRegistry();
    Vector<ResourceId> ids;
    Vector<Vector<byte>> blobs;
    Vector<Array<char, 40>> paths;
};

PakSet MakePakSet(usize count, u32 target = 0)
{
    PakSet set;
    for (usize i = 0; i < count; ++i)
    {
        const ResourceId id = test::Rid(1000 + i * 7);
        set.ids.Push(id);
        set.blobs.Push(PakBlob(set.registry, test::kBackendTestType, id, target, 100 + (i * 37) % 900));
        Array<char, 40> path = {};
        FormatTo(path.Data(), path.Size(), "/pak/item_%04u.bin", static_cast<unsigned>(i));
        set.paths.Push(path);
    }
    return set;
}

Status WritePak(PakSet& set, const PakWriterOptions& options, test::MemoryFile& file, const PakReader* base = nullptr)
{
    PakWriter writer(set.registry, options);
    writer.SetBase(base);
    for (usize i = 0; i < set.ids.Size(); ++i)
    {
        ENGINE_TRY(
            writer.Add(set.ids[i], test::AsBytes(set.blobs[i]),
                       set.paths.Size() > i && options.include_names ? test::Vp(set.paths[i].Data()) : StringView()));
    }
    return writer.Finish(file);
}

Vector<byte> PakBytes(PakSet& set, const PakWriterOptions& options = PakWriterOptions())
{
    test::MemoryFile file;
    REQUIRE(WritePak(set, options, file).Ok());
    return file.Bytes();
}

Status OpenBytes(const Vector<byte>& bytes, const TypeRegistry& types, u32 target, PakReader* out)
{
    return PakReader::Open(test::FileOf(bytes), types, target, out);
}

}  // namespace

TEST_CASE("A pak round-trips its entries and names", "[asset][pak]")
{
    PakSet set = MakePakSet(20);
    const Vector<byte> bytes = PakBytes(set);
    PakReader reader;
    REQUIRE(OpenBytes(bytes, set.registry, 0, &reader).Ok());
    CHECK(reader.Count() == 20);
    CHECK(reader.Info().has_names);
    CHECK(reader.Info().entry_count == 20);
    CHECK(reader.Info().file_size == bytes.Size());
    CHECK_FALSE(reader.Info().has_base);
    for (usize i = 0; i < set.ids.Size(); ++i)
    {
        PakEntry entry;
        REQUIRE(reader.Find(set.ids[i], &entry));
        CHECK(entry.size == set.blobs[i].Size());
        CHECK(entry.offset % 64 == 0);
        Vector<byte> read;
        read.Resize(entry.size);
        REQUIRE(reader.ReadEntry(entry, 0, read.Data(), read.Size()).Ok());
        CHECK(MemCompare(read.Data(), set.blobs[i].Data(), read.Size()) == 0);
    }
    u32 version = 0;
    CHECK(reader.TypeVersionOf(test::kBackendTestType, &version));
    CHECK(version == 1);
    ResourceTable table;
    REQUIRE(reader.ReadNames(set.registry, &table).Ok());
    CHECK(table.Size() == 20);
    CHECK(reader.Verify(set.registry).Ok());
}

TEST_CASE("The pak backend passes the conformance suite", "[asset][pak][backend]")
{
    PakSet set = MakePakSet(12);
    const Vector<byte> bytes = PakBytes(set);
    UniquePtr<PakBackend> backend;
    REQUIRE(PakBackend::FromFile(test::FileOf(bytes), set.registry, 0, &backend).Ok());
    test::ConformanceFixture fx;
    for (usize i = 0; i < set.ids.Size(); ++i)
    {
        fx.ids.Push(set.ids[i]);
        fx.blobs.Push(CopyOf(set.blobs[i]));
        fx.paths.Push(test::Vp(set.paths[i].Data()));
    }
    test::RunBackendConformance(*backend, fx);
}

TEST_CASE("A pak without names reports no table", "[asset][pak]")
{
    PakSet set = MakePakSet(3);
    PakWriterOptions options;
    options.include_names = false;
    const Vector<byte> bytes = PakBytes(set, options);
    PakReader reader;
    REQUIRE(OpenBytes(bytes, set.registry, 0, &reader).Ok());
    CHECK_FALSE(reader.Info().has_names);
    ResourceTable table;
    CHECK(reader.ReadNames(set.registry, &table).Code() == ErrorCode::NotFound);
}

TEST_CASE("Packs are deterministic", "[asset][pak]")
{
    PakSet a = MakePakSet(30);
    PakSet b = MakePakSet(30);
    const Vector<byte> first = PakBytes(a);
    const Vector<byte> second = PakBytes(b);
    REQUIRE(first.Size() == second.Size());
    CHECK(MemCompare(first.Data(), second.Data(), first.Size()) == 0);
}

TEST_CASE("A writer can finish twice with the same result", "[asset][pak][writer]")
{
    // Finish sorts the items; the second call must not see stale positions (dependencies are looked up by id).
    TypeRegistry registry = test::MakeBackendRegistry();
    Dependency dependency;
    dependency.id = test::Rid(300);
    dependency.type = test::kBackendTestType;
    dependency.type_version = 1;
    dependency.role = 1;
    const Vector<byte> high = PakBlob(registry, test::kBackendTestType, test::Rid(900), 0, 32, &dependency);
    const Vector<byte> low = PakBlob(registry, test::kBackendTestType, test::Rid(300), 0, 32);
    PakWriter writer(registry, PakWriterOptions());
    REQUIRE(writer.Add(test::Rid(900), test::AsBytes(high)).Ok());  // added before the id it depends on
    REQUIRE(writer.Add(test::Rid(300), test::AsBytes(low)).Ok());
    test::MemoryFile first;
    test::MemoryFile second;
    REQUIRE(writer.Finish(first).Ok());
    REQUIRE(writer.Finish(second).Ok());
    const Vector<byte> a = first.Bytes();
    const Vector<byte> b = second.Bytes();
    REQUIRE(a.Size() == b.Size());
    CHECK(MemCompare(a.Data(), b.Data(), a.Size()) == 0);
}

TEST_CASE("Absent ids are not found", "[asset][pak]")
{
    PakSet set = MakePakSet(50);
    const Vector<byte> bytes = PakBytes(set);
    PakReader reader;
    REQUIRE(OpenBytes(bytes, set.registry, 0, &reader).Ok());
    PakEntry entry;
    CHECK_FALSE(reader.Find(ResourceId::None(), &entry));
    for (u64 v = 1; v < 3000; ++v)
    {
        const bool present = v >= 1000 && v < 1000 + 50 * 7 && (v - 1000) % 7 == 0;
        CHECK(reader.Find(test::Rid(v), &entry) == present);
    }
}

TEST_CASE("Every truncation of a pak is refused", "[asset][pak][corrupt]")
{
    PakSet set = MakePakSet(6);
    const Vector<byte> bytes = PakBytes(set);
    for (usize length = 0; length < bytes.Size(); ++length)
    {
        Vector<byte> cut;
        cut.Resize(length);
        MemCopy(cut.Data(), bytes.Data(), length);
        PakReader reader;
        CHECK(OpenBytes(cut, set.registry, 0, &reader).Failed());
    }
}

TEST_CASE("Damage to the header, index or footer is refused or caught by Verify", "[asset][pak][corrupt]")
{
    PakSet set = MakePakSet(5);
    const Vector<byte> bytes = PakBytes(set);
    PakReader good;
    REQUIRE(OpenBytes(bytes, set.registry, 0, &good).Ok());
    const usize index_start = bytes.Size() - kPakFooterBytes - good.Count() * kPakIndexEntryBytes;
    usize refused = 0;
    usize caught = 0;
    for (usize pos = 0; pos < bytes.Size(); ++pos)
    {
        const bool structural = pos < kPakHeaderBytes || pos >= index_start;
        if (!structural)
        {
            continue;  // entry bytes are the blobs' own business (BlobReader, Verify)
        }
        if (pos >= 56 && pos < 72)
        {
            continue;  // the base identity: a changed one names another base, which PakBackend refuses (patch test)
        }
        Vector<byte> bad = CopyOf(bytes);
        bad[pos] = static_cast<byte>(bad[pos] ^ 0x5a);
        PakReader reader;
        const Status status = OpenBytes(bad, set.registry, 0, &reader);
        if (status.Failed())
        {
            ++refused;
            continue;
        }
        // Accepted at open: only a hash or a padding-free field may differ, and Verify must notice.
        INFO("position " << pos);
        CHECK(reader.Verify(set.registry).Failed());
        ++caught;
    }
    CHECK(refused > 0);
    CHECK(refused + caught > 0);
}

TEST_CASE("An old format version or type version is refused naming the tool", "[asset][pak][version]")
{
    PakSet set = MakePakSet(2);
    Vector<byte> bytes = PakBytes(set);
    Vector<byte> old = CopyOf(bytes);
    old[4] = 0;  // format version in the header (u32 at +4)
    PakReader reader;
    const Status status = OpenBytes(old, set.registry, 0, &reader);
    CHECK(status.Code() == ErrorCode::UnsupportedVersion);
    CHECK(test::Contains(status.Message(), "asset_mint"));

    // The same pak against a registry whose current version of the type is newer.
    TypeRegistry newer;
    REQUIRE(newer.Register(test::kBackendTestType, 2, "backend test").Ok());
    PakReader other;
    CHECK(OpenBytes(bytes, newer, 0, &other).Code() == ErrorCode::UnsupportedVersion);
}

TEST_CASE("A pak packed for another target is refused", "[asset][pak][target]")
{
    PakSet set = MakePakSet(3, 7);
    PakWriterOptions options;
    options.target = 7;
    const Vector<byte> bytes = PakBytes(set, options);
    PakReader reader;
    const Status status = OpenBytes(bytes, set.registry, 0, &reader);
    CHECK(status.Code() == ErrorCode::WrongTarget);
    CHECK(test::Contains(reader.Detail(), "7"));
    PakReader right;
    CHECK(OpenBytes(bytes, set.registry, 7, &right).Ok());
}

TEST_CASE("A loose blob for another target is refused by the filesystem backend", "[asset][target]")
{
    TypeRegistry registry = test::MakeBackendRegistry();
    test::TempDir dir;
    const Vector<byte> blob = PakBlob(registry, test::kBackendTestType, test::Rid(77), 7, 64);
    REQUIRE(io::WriteFile(dir.Path().Join(test::Vp("a.bin")), blob.Data(), blob.Size()).Ok());
    UniquePtr<FilesystemBackend> backend;
    REQUIRE(FilesystemBackend::Create(dir.Path(), registry, &backend, 0).Ok());
    UniquePtr<ResourceReader> reader;
    CHECK(backend->Open(test::Rid(77), &reader).Code() == ErrorCode::WrongTarget);
    UniquePtr<FilesystemBackend> right;
    REQUIRE(FilesystemBackend::Create(dir.Path(), registry, &right, 7).Ok());
    CHECK(right->Open(test::Rid(77), &reader).Ok());
}

TEST_CASE("The writer refuses blobs it must not pack", "[asset][pak][writer]")
{
    PakSet set = MakePakSet(1);
    PakWriter writer(set.registry, PakWriterOptions());
    CHECK(writer.Add(ResourceId::None(), test::AsBytes(set.blobs[0])).Code() == ErrorCode::Invalid);
    CHECK(writer.Add(test::Rid(5), test::AsBytes(set.blobs[0])).Code() == ErrorCode::Corrupt);  // another id inside
    REQUIRE(writer.Add(set.ids[0], test::AsBytes(set.blobs[0]), "/a/b.bin").Ok());
    CHECK(writer.Add(set.ids[0], test::AsBytes(set.blobs[0])).Code() == ErrorCode::DuplicateId);

    const Vector<byte> other = PakBlob(set.registry, test::kBackendTestType, test::Rid(9), 0, 50);
    CHECK(writer.Add(test::Rid(9), test::AsBytes(other), "/a/b.bin").Code() == ErrorCode::DuplicateId);  // path

    const Vector<byte> foreign = PakBlob(set.registry, test::kBackendTestType, test::Rid(10), 3, 50);
    CHECK(writer.Add(test::Rid(10), test::AsBytes(foreign)).Code() == ErrorCode::WrongTarget);

    Vector<byte> damaged = CopyOf(other);
    damaged[damaged.Size() - 1] = static_cast<byte>(damaged[damaged.Size() - 1] ^ 1);
    CHECK(writer.Add(test::Rid(9), test::AsBytes(damaged)).Failed());

    TypeRegistry newer;
    REQUIRE(newer.Register(test::kBackendTestType, 2, "backend test").Ok());
    PakWriter strict(newer, PakWriterOptions());
    CHECK(strict.Add(set.ids[0], test::AsBytes(set.blobs[0])).Code() == ErrorCode::UnsupportedVersion);
}

TEST_CASE("The closure is enforced when the pak is written", "[asset][pak][closure]")
{
    TypeRegistry registry = test::MakeBackendRegistry();
    const ResourceId texture = test::Rid(100);
    const ResourceId material = test::Rid(200);

    Dependency dependency;
    dependency.id = texture;
    dependency.type = test::kBackendTestType;
    dependency.type_version = 1;
    dependency.role = 1;
    const Vector<byte> material_blob = PakBlob(registry, test::kBackendTestType, material, 0, 64, &dependency);
    const Vector<byte> texture_blob = PakBlob(registry, test::kBackendTestType, texture, 0, 64);

    {
        PakWriter writer(registry, PakWriterOptions());
        REQUIRE(writer.Add(material, test::AsBytes(material_blob)).Ok());
        test::MemoryFile file;
        CHECK(writer.Finish(file).Code() == ErrorCode::MissingDependency);
        REQUIRE(writer.Problems().Size() == 1);
        CHECK(writer.Problems()[0].blob == material);
        CHECK(writer.Problems()[0].dependency == texture);
    }
    {
        Dependency wrong = dependency;
        wrong.type = asset::types::kTexture;
        const Vector<byte> bad_material = PakBlob(registry, test::kBackendTestType, material, 0, 64, &wrong);
        PakWriter writer(registry, PakWriterOptions());
        REQUIRE(writer.Add(material, test::AsBytes(bad_material)).Ok());
        REQUIRE(writer.Add(texture, test::AsBytes(texture_blob)).Ok());
        test::MemoryFile file;
        CHECK(writer.Finish(file).Code() == ErrorCode::DependencyTypeMismatch);
    }
    {
        PakWriter writer(registry, PakWriterOptions());
        REQUIRE(writer.Add(material, test::AsBytes(material_blob)).Ok());
        REQUIRE(writer.Add(texture, test::AsBytes(texture_blob)).Ok());
        test::MemoryFile file;
        CHECK(writer.Finish(file).Ok());
    }
}

TEST_CASE("A patch pak needs its base", "[asset][pak][patch]")
{
    TypeRegistry registry = test::MakeBackendRegistry();
    const ResourceId texture = test::Rid(100);
    const ResourceId material = test::Rid(200);
    Dependency dependency;
    dependency.id = texture;
    dependency.type = test::kBackendTestType;
    dependency.type_version = 1;
    dependency.role = 1;

    // The base holds the texture.
    test::MemoryFile base_file;
    {
        PakWriter writer(registry, PakWriterOptions());
        const Vector<byte> blob = PakBlob(registry, test::kBackendTestType, texture, 0, 64);
        REQUIRE(writer.Add(texture, test::AsBytes(blob), "/t.tex").Ok());
        REQUIRE(writer.Finish(base_file).Ok());
    }
    const Vector<byte> base_bytes = base_file.Bytes();
    UniquePtr<PakBackend> base;
    REQUIRE(PakBackend::FromFile(test::FileOf(base_bytes), registry, 0, &base).Ok());

    // The patch holds a material that depends on it.
    const Vector<byte> material_blob = PakBlob(registry, test::kBackendTestType, material, 0, 64, &dependency);
    test::MemoryFile patch_file;
    {
        PakWriter without(registry, PakWriterOptions());
        REQUIRE(without.Add(material, test::AsBytes(material_blob)).Ok());
        test::MemoryFile scratch;
        CHECK(without.Finish(scratch).Code() == ErrorCode::MissingDependency);

        PakWriter writer(registry, PakWriterOptions());
        writer.SetBase(&base->Reader());
        REQUIRE(writer.Add(material, test::AsBytes(material_blob), "/m.mat").Ok());
        REQUIRE(writer.Finish(patch_file).Ok());
    }
    const Vector<byte> patch_bytes = patch_file.Bytes();
    UniquePtr<PakBackend> patch;
    REQUIRE(PakBackend::FromFile(test::FileOf(patch_bytes), registry, 0, &patch).Ok());
    CHECK(patch->Info().has_base);
    CHECK(patch->Info().base == base->Info().identity);

    ResourceInfo info;
    CHECK(patch->Stat(material, &info).Code() == ErrorCode::MissingDependency);  // not linked
    patch->LinkBase(patch.Get());                                                // a pak that is not the named base
    CHECK(patch->Stat(material, &info).Code() == ErrorCode::MissingDependency);
    patch->LinkBase(base.Get());
    CHECK(patch->Stat(material, &info).Ok());
    UniquePtr<ResourceReader> reader;
    CHECK(patch->Open(material, &reader).Ok());
}

TEST_CASE("A pak with offsets above 4 GiB reads back", "[asset][pak][large]")
{
    PakSet set = MakePakSet(4);
    PakWriterOptions options;
    options.first_entry_offset = u64(5) << 30;
    UniquePtr<test::MemoryFile> file = MakeUnique<test::MemoryFile>(true);
    REQUIRE(WritePak(set, options, *file).Ok());
    u64 size = 0;
    REQUIRE(file->Size(&size).Ok());
    CHECK(size > (u64(5) << 30));

    PakReader reader;
    REQUIRE(PakReader::Open(io::FilePtr(file.Release()), set.registry, 0, &reader).Ok());
    CHECK(reader.Count() == 4);
    for (usize i = 0; i < set.ids.Size(); ++i)
    {
        PakEntry entry;
        REQUIRE(reader.Find(set.ids[i], &entry));
        CHECK(entry.offset >= (u64(5) << 30));
        Vector<byte> read;
        read.Resize(entry.size);
        REQUIRE(reader.ReadEntry(entry, 0, read.Data(), read.Size()).Ok());
        CHECK(MemCompare(read.Data(), set.blobs[i].Data(), read.Size()) == 0);
    }
}

TEST_CASE("A pak of 100000 entries opens and finds every id", "[asset][pak][large]")
{
    TypeRegistry registry = test::MakeBackendRegistry();
    constexpr usize kCount = 100000;
    PakWriterOptions options;
    options.include_names = false;
    PakWriter writer(registry, options);
    Vector<Vector<byte>> blobs;
    blobs.Reserve(kCount);
    for (usize i = 0; i < kCount; ++i)
    {
        blobs.Push(PakBlob(registry, test::kBackendTestType, test::Rid(i + 1), 0, 16));
        REQUIRE(writer.Add(test::Rid(i + 1), test::AsBytes(blobs[i])).Ok());
    }
    test::MemoryFile file;
    REQUIRE(writer.Finish(file).Ok());
    const Vector<byte> bytes = file.Bytes();
    PakReader reader;
    REQUIRE(OpenBytes(bytes, registry, 0, &reader).Ok());
    CHECK(reader.Count() == kCount);
    PakEntry entry;
    for (usize i = 0; i < kCount; i += 97)
    {
        CHECK(reader.Find(test::Rid(i + 1), &entry));
    }
    CHECK_FALSE(reader.Find(test::Rid(kCount + 5), &entry));
}

TEST_CASE("Pak open and lookup speed", "[.][asset-bench]")
{
    TypeRegistry registry = test::MakeBackendRegistry();
    constexpr usize kCount = 100000;
    PakWriterOptions options;
    options.include_names = false;
    PakWriter writer(registry, options);
    Vector<Vector<byte>> blobs;
    for (usize i = 0; i < kCount; ++i)
    {
        blobs.Push(PakBlob(registry, test::kBackendTestType, test::Rid(i + 1), 0, 16));
        REQUIRE(writer.Add(test::Rid(i + 1), test::AsBytes(blobs[i])).Ok());
    }
    test::MemoryFile file;
    REQUIRE(writer.Finish(file).Ok());
    const Vector<byte> bytes = file.Bytes();
    const u64 t0 = os::MonotonicTimeNanos();
    PakReader reader;
    REQUIRE(OpenBytes(bytes, registry, 0, &reader).Ok());
    const u64 t1 = os::MonotonicTimeNanos();
    PakEntry entry;
    usize found = 0;
    for (usize i = 0; i < kCount; ++i)
    {
        found += reader.Find(test::Rid(i + 1), &entry) ? 1 : 0;
    }
    const u64 t2 = os::MonotonicTimeNanos();
    os::Print("open %.2f ms, lookup %.1f ns each (%u found)\n", double(t1 - t0) / 1e6, double(t2 - t1) / kCount,
              static_cast<unsigned>(found));
    CHECK(found == kCount);
}

}  // namespace engine_placeholder::asset
