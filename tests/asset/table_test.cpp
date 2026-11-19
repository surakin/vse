// The id <-> path table (Phase 22 Day 314): lookups both ways, rules, merge with shadowing, and the NAMES blob.

#include <catch2/catch_test_macros.hpp>

#include "asset/resource_table.h"
#include "cstr/format.h"
#include "bytes/byte_writer.h"
#include "support.h"

namespace engine_placeholder::asset
{

namespace
{

StringView V(const char* text)
{
    return StringView(text, CStrLength(text));
}

ResourceId Id(u64 value)
{
    return ResourceId::FromValue(value);
}

}  // namespace

TEST_CASE("A table resolves paths to ids and ids to paths", "[asset][table]")
{
    ResourceTable table;
    REQUIRE(table.Add(V("/textures/rock.tex"), Id(10), types::kTexture).Ok());
    REQUIRE(table.Add(V("/textures/stone.tex"), Id(10), types::kTexture).Ok());  // a second path for one id
    REQUIRE(table.Add(V("/meshes/rock.mesh"), Id(11), types::kMesh).Ok());
    CHECK(table.Size() == 3);

    ResourceId id;
    CHECK(table.Resolve(V("/textures/stone.tex"), &id));
    CHECK(id == Id(10));
    CHECK(!table.Resolve(V("/textures/missing.tex"), &id));
    CHECK(!table.Resolve(V("not/a/path"), &id));
    CHECK(!table.Resolve(V("/textures/../textures/rock.tex"), &id));

    Vector<StringView> paths;
    CHECK(table.PathsOf(Id(10), &paths) == 2);
    CHECK(paths[0] == V("/textures/rock.tex"));
    CHECK(paths[1] == V("/textures/stone.tex"));
    paths.Clear();
    CHECK(table.PathsOf(Id(99), &paths) == 0);

    ResourceType type;
    CHECK(table.TypeOf(Id(11), &type));
    CHECK(type == types::kMesh);
    CHECK(!table.TypeOf(Id(99), &type));
}

TEST_CASE("Paths are looked up in their normalized form", "[asset][table]")
{
    ResourceTable table;
    REQUIRE(table.Add(V("/caf\xc3\xa9"), Id(1), types::kMesh).Ok());
    ResourceId id;
    CHECK(table.Resolve(V("/caf\x65\xcc\x81"), &id));  // the decomposed spelling finds the same entry
    CHECK(id == Id(1));
}

TEST_CASE("The table refuses a path with two ids and an id with two types", "[asset][table]")
{
    ResourceTable table;
    REQUIRE(table.Add(V("/a"), Id(1), types::kMesh).Ok());
    CHECK(table.Add(V("/a"), Id(1), types::kMesh).Ok());  // repeating is accepted
    CHECK(table.Size() == 1);
    CHECK(table.Add(V("/a"), Id(2), types::kMesh).Is(ErrorCode::DuplicateId));      // one path, one id
    CHECK(table.Add(V("/b"), Id(1), types::kTexture).Is(ErrorCode::TypeMismatch));  // one id, one type
    CHECK(table.Add(V("/c"), ResourceId::None(), types::kMesh).Is(ErrorCode::Invalid));
    CHECK(table.Add(V("/c"), Id(3), ResourceType()).Is(ErrorCode::Invalid));
    CHECK(table.Add(V("bad"), Id(3), types::kMesh).Is(ErrorCode::Invalid));
    CHECK(table.Size() == 1);
}

TEST_CASE("Removing a path keeps the other entries reachable", "[asset][table]")
{
    ResourceTable table;
    for (u32 i = 0; i < 100; ++i)
    {
        StringBuilder<HeapAllocator> builder(HeapAllocator::Default());
        builder.Append("/dir/item").AppendInt(i);
        REQUIRE(table.Add(builder.View(), Id(1000 + i), types::kMesh).Ok());
    }
    CHECK(table.Remove(V("/dir/item50")));
    CHECK(!table.Remove(V("/dir/item50")));
    CHECK(table.Size() == 99);
    ResourceId id;
    CHECK(!table.Resolve(V("/dir/item50"), &id));
    CHECK(table.Resolve(V("/dir/item51"), &id));
    CHECK(id == Id(1051));
    CHECK(table.Resolve(V("/dir/item0"), &id));
}

TEST_CASE("A prefix listing is sorted by path", "[asset][table]")
{
    ResourceTable table;
    REQUIRE(table.Add(V("/b/2"), Id(2), types::kMesh).Ok());
    REQUIRE(table.Add(V("/a/1"), Id(1), types::kMesh).Ok());
    REQUIRE(table.Add(V("/b/1"), Id(3), types::kMesh).Ok());
    Vector<TableEntryView> list;
    CHECK(table.ListPrefix(V("/b/"), &list) == 2);
    CHECK(list[0].path == V("/b/1"));
    CHECK(list[1].path == V("/b/2"));
    list.Clear();
    CHECK(table.ListPrefix(V("/"), &list) == 3);
    CHECK(table.ListPrefix(V("/zzz"), &list) == 0);
}

TEST_CASE("A merge keeps the higher priority and reports what it shadowed", "[asset][table]")
{
    ResourceTable high;
    REQUIRE(high.Add(V("/shared"), Id(1), types::kMesh).Ok());
    REQUIRE(high.Add(V("/only/high"), Id(2), types::kMesh).Ok());
    ResourceTable low;
    REQUIRE(low.Add(V("/shared"), Id(100), types::kMesh).Ok());   // another id for one path: shadowed
    REQUIRE(low.Add(V("/only/low"), Id(3), types::kMesh).Ok());   // new
    REQUIRE(low.Add(V("/only/high"), Id(2), types::kMesh).Ok());  // the same: kept silently

    Vector<ShadowedEntry> shadowed;
    REQUIRE(high.Merge(low, &shadowed).Ok());
    CHECK(high.Size() == 3);
    ResourceId id;
    REQUIRE(high.Resolve(V("/shared"), &id));
    CHECK(id == Id(1));
    REQUIRE(high.Resolve(V("/only/low"), &id));
    CHECK(id == Id(3));
    REQUIRE(shadowed.Size() == 1);
    CHECK(shadowed[0].path.View() == V("/shared"));
    CHECK(shadowed[0].shadowed_id == Id(100));
    CHECK(shadowed[0].winning_id == Id(1));
}

TEST_CASE("A merge that finds one id with two types merges nothing", "[asset][table]")
{
    ResourceTable high;
    REQUIRE(high.Add(V("/a"), Id(1), types::kMesh).Ok());
    ResourceTable low;
    REQUIRE(low.Add(V("/z"), Id(50), types::kMesh).Ok());
    REQUIRE(low.Add(V("/b"), Id(1), types::kTexture).Ok());
    CHECK(high.Merge(low, nullptr).Is(ErrorCode::TypeMismatch));
    CHECK(high.Size() == 1);
}

TEST_CASE("A table survives the NAMES blob round trip, byte-identical across insertion orders", "[asset][table]")
{
    const TypeRegistry registry;
    ResourceTable a;
    REQUIRE(a.Add(V("/x/one"), Id(5), types::kMesh).Ok());
    REQUIRE(a.Add(V("/x/two"), Id(5), types::kMesh).Ok());
    REQUIRE(a.Add(V("/y/three"), Id(2), types::kTexture).Ok());
    ResourceTable b;
    REQUIRE(b.Add(V("/y/three"), Id(2), types::kTexture).Ok());
    REQUIRE(b.Add(V("/x/two"), Id(5), types::kMesh).Ok());
    REQUIRE(b.Add(V("/x/one"), Id(5), types::kMesh).Ok());

    Vector<byte> blob_a;
    Vector<byte> blob_b;
    REQUIRE(a.WriteNamesBlob(registry, &blob_a).Ok());
    REQUIRE(b.WriteNamesBlob(registry, &blob_b).Ok());
    REQUIRE(blob_a.Size() == blob_b.Size());
    CHECK(MemCompare(blob_a.Data(), blob_b.Data(), blob_a.Size()) == 0);

    BlobReader reader;
    REQUIRE(reader.Open(blob_a, registry).Ok());
    CHECK(reader.Verify().Ok());
    ResourceTable loaded;
    REQUIRE(loaded.ReadNamesBlob(reader).Ok());
    CHECK(loaded.Size() == 3);
    ResourceId id;
    REQUIRE(loaded.Resolve(V("/x/two"), &id));
    CHECK(id == Id(5));
    ResourceType type;
    REQUIRE(loaded.TypeOf(Id(2), &type));
    CHECK(type == types::kTexture);
}

TEST_CASE("A names section that is unsorted or not in canonical form is refused", "[asset][table]")
{
    const TypeRegistry registry;
    auto blob_with = [&](u64 id_a, const char* path_a, u64 id_b, const char* path_b)
    {
        bytes::ByteWriter writer;
        writer.WriteVarint(2);
        const u64 ids[2] = {id_a, id_b};
        const char* paths[2] = {path_a, path_b};
        for (u32 i = 0; i < 2; ++i)
        {
            writer.WriteU64(ids[i]);
            writer.WriteU32(types::kMesh.Tag());
            writer.WriteVarint(CStrLength(paths[i]));
            writer.WriteBytes(ByteSpan(reinterpret_cast<const byte*>(paths[i]), CStrLength(paths[i])));
        }
        BlobWriter blob(registry, types::kNames, ResourceId::None());
        REQUIRE(blob.AddSection(kFirstUserSection, writer.Bytes()).Ok());
        Vector<byte> out;
        REQUIRE(blob.Finish(&out).Ok());
        return out;
    };
    ResourceTable table;
    BlobReader reader;
    {
        const Vector<byte> ok = blob_with(1, "/a", 2, "/b");
        REQUIRE(reader.Open(ok, registry).Ok());
        CHECK(table.ReadNamesBlob(reader).Ok());
    }
    {
        const Vector<byte> unsorted = blob_with(2, "/b", 1, "/a");
        REQUIRE(reader.Open(unsorted, registry).Ok());
        CHECK(table.ReadNamesBlob(reader).Is(ErrorCode::Corrupt));
        CHECK(table.Size() == 0);
    }
    {
        const Vector<byte> decomposed = blob_with(1, "/caf\x65\xcc\x81", 2, "/b");  // not NFC
        REQUIRE(reader.Open(decomposed, registry).Ok());
        CHECK(table.ReadNamesBlob(reader).Is(ErrorCode::Corrupt));
    }
    {
        const Vector<byte> duplicate = blob_with(1, "/a", 1, "/a");  // not strictly increasing
        REQUIRE(reader.Open(duplicate, registry).Ok());
        CHECK(table.ReadNamesBlob(reader).Is(ErrorCode::Corrupt));
    }
}

TEST_CASE("A damaged names section is refused and leaves an empty table", "[asset][table]")
{
    const TypeRegistry registry;
    ResourceTable table;
    REQUIRE(table.Add(V("/a"), Id(1), types::kMesh).Ok());

    // A names section that claims an entry it does not have.
    BlobWriter writer(registry, types::kNames, ResourceId::None());
    byte bad[] = {static_cast<byte>(1)};
    REQUIRE(writer.AddSection(kFirstUserSection, ByteSpan(bad, 1)).Ok());
    Vector<byte> blob;
    REQUIRE(writer.Finish(&blob).Ok());
    BlobReader reader;
    REQUIRE(reader.Open(blob, registry).Ok());
    CHECK(table.ReadNamesBlob(reader).Is(ErrorCode::Corrupt));
    CHECK(table.Size() == 0);

    // A blob of another type is not a names blob.
    BlobWriter other(registry, types::kMesh, Id(7));
    Vector<byte> mesh;
    REQUIRE(other.Finish(&mesh).Ok());
    REQUIRE(reader.Open(mesh, registry).Ok());
    CHECK(table.ReadNamesBlob(reader).Is(ErrorCode::TypeMismatch));
}

TEST_CASE("Removing paths in any order keeps every other path and id findable", "[asset][table]")
{
    // Container audit Day 4: removal moves the last entry into the removed one's place and relinks the id chains.
    ResourceTable table;
    const u32 n = 300;
    char text[48];
    for (u32 i = 0; i < n; ++i)
    {
        FormatTo(text, sizeof(text), "/meshes/m%u.mesh", i);
        REQUIRE(table.Add(V(text), Id(1 + i / 3), types::kMesh).Ok());  // three paths per id
    }
    Vector<bool> gone;
    gone.Resize(n);
    u32 state = 7;
    for (u32 step = 0; step < n * 2 / 3; ++step)
    {
        state = state * 1103515245u + 12345u;
        const u32 i = (state >> 8) % n;
        FormatTo(text, sizeof(text), "/meshes/m%u.mesh", i);
        CHECK(table.Remove(V(text)) == !gone[i]);
        gone[i] = true;
    }
    for (u32 i = 0; i < n; ++i)
    {
        FormatTo(text, sizeof(text), "/meshes/m%u.mesh", i);
        ResourceId id;
        REQUIRE(table.Resolve(V(text), &id) == !gone[i]);
        if (!gone[i])
        {
            CHECK(id == Id(1 + i / 3));
        }
    }
    for (u32 g = 0; g < n / 3; ++g)
    {
        usize live = 0;
        for (u32 k = 0; k < 3; ++k)
        {
            live += gone[g * 3 + k] ? 0 : 1;
        }
        Vector<StringView> paths;
        CHECK(table.PathsOf(Id(1 + g), &paths) == live);
        ResourceType type;
        CHECK(table.TypeOf(Id(1 + g), &type) == (live > 0));
    }
}

}  // namespace engine_placeholder::asset
