// The versioned blob container (Phase 22 Day 314): round trips, alignment, the dependency section, versions, every
// truncation, every single-byte corruption of a golden blob, and a seeded fuzz of random bytes.

#include <catch2/catch_test_macros.hpp>

#include "asset/blob.h"
#include "support.h"

namespace engine_placeholder::asset
{

namespace
{

constexpr ResourceType kTest = ResourceType::FromChars('T', 'E', 'S', 'T');

TypeRegistry MakeRegistry(u32 test_version = 1)
{
    TypeRegistry registry;
    REQUIRE(registry.Register(kTest, test_version, "test").Ok());
    return registry;
}

Vector<byte> Filled(usize size, u32 seed)
{
    Vector<byte> bytes;
    test::Rng rng(seed);
    for (usize i = 0; i < size; ++i)
    {
        bytes.Push(static_cast<byte>(rng.Below(256)));
    }
    return bytes;
}

// A small blob with three sections and two dependencies: the golden blob of the corruption tests.
Vector<byte> MakeGolden(const TypeRegistry& registry)
{
    BlobWriter writer(registry, kTest, ResourceId::FromValue(0x1111));
    writer.AddDependency(Dependency{ResourceId::FromValue(0x30), types::kTexture, 1, 7});
    writer.AddDependency(Dependency{ResourceId::FromValue(0x20), types::kMesh, 1, 3});
    REQUIRE(writer.AddSection(16, Filled(100, 1)).Ok());
    REQUIRE(writer.AddSection(17, Filled(1, 2)).Ok());
    REQUIRE(writer.AddSection(40, Filled(130, 3)).Ok());
    Vector<byte> blob;
    REQUIRE(writer.Finish(&blob).Ok());
    return blob;
}

}  // namespace

TEST_CASE("A blob round-trips with 0 to 16 sections, each section aligned", "[asset][blob]")
{
    const TypeRegistry registry = MakeRegistry();
    for (u32 count = 0; count <= 16; ++count)
    {
        BlobWriter writer(registry, kTest, ResourceId::FromValue(0xabcdef), 5);
        Vector<Vector<byte>> contents;
        for (u32 i = 0; i < count; ++i)
        {
            contents.Push(Filled(i * 37 + 1, i + 100));
            REQUIRE(writer.AddSection(kFirstUserSection + i, contents[i]).Ok());
        }
        Vector<byte> blob;
        REQUIRE(writer.Finish(&blob).Ok());
        CHECK(blob.Size() % kSectionAlignment == 0);

        BlobReader reader;
        REQUIRE(reader.Open(blob, registry).Ok());
        CHECK(reader.Type() == kTest);
        CHECK(reader.Id().Value() == 0xabcdef);
        CHECK(reader.Target() == 5);
        CHECK(reader.TypeVersion() == 1);
        CHECK(reader.SectionCount() == count + 1);
        CHECK(reader.DependencyCount() == 0);
        CHECK(reader.Verify().Ok());
        for (u32 i = 0; i < count; ++i)
        {
            ByteSpan section;
            REQUIRE(reader.FindSection(kFirstUserSection + i, &section));
            CHECK(section.Size() == contents[i].Size());
            CHECK(MemCompare(section.Data(), contents[i].Data(), section.Size()) == 0);
            CHECK((section.Data() - blob.Data()) % kSectionAlignment == 0);
        }
        CHECK(!reader.FindSection(kFirstUserSection + count, nullptr));
    }
}

TEST_CASE("A large section starts on a page boundary", "[asset][blob]")
{
    const TypeRegistry registry = MakeRegistry();
    BlobWriter writer(registry, kTest, ResourceId::FromValue(9));
    REQUIRE(writer.AddSection(16, Filled(10, 1)).Ok());
    REQUIRE(writer.AddSection(17, Filled(kPageAlignedSectionBytes, 2)).Ok());
    Vector<byte> blob;
    REQUIRE(writer.Finish(&blob).Ok());
    BlobReader reader;
    REQUIRE(reader.Open(blob, registry).Ok());
    ByteSpan large;
    REQUIRE(reader.FindSection(17, &large));
    CHECK((large.Data() - blob.Data()) % kPageAlignment == 0);
    CHECK(large.Size() == kPageAlignedSectionBytes);
}

TEST_CASE("Writing the same input twice gives the same bytes", "[asset][blob]")
{
    const TypeRegistry registry = MakeRegistry();
    const Vector<byte> a = MakeGolden(registry);
    const Vector<byte> b = MakeGolden(registry);
    REQUIRE(a.Size() == b.Size());
    CHECK(MemCompare(a.Data(), b.Data(), a.Size()) == 0);
}

TEST_CASE("Dependencies are sorted by id and read back", "[asset][blob]")
{
    const TypeRegistry registry = MakeRegistry();
    const Vector<byte> blob = MakeGolden(registry);
    BlobReader reader;
    REQUIRE(reader.Open(blob, registry).Ok());
    REQUIRE(reader.DependencyCount() == 2);
    const Dependency first = reader.DependencyAt(0);
    const Dependency second = reader.DependencyAt(1);
    CHECK(first.id.Value() == 0x20);
    CHECK(first.type == types::kMesh);
    CHECK(first.type_version == 1);
    CHECK(first.role == 3);
    CHECK(second.id.Value() == 0x30);
    CHECK(second.type == types::kTexture);
    CHECK(second.role == 7);
}

TEST_CASE("The writer refuses what a blob must not contain", "[asset][blob]")
{
    const TypeRegistry registry = MakeRegistry();
    Vector<byte> out;
    {
        BlobWriter writer(registry, ResourceType::FromChars('N', 'O', 'P', 'E'), ResourceId::FromValue(1));
        CHECK(writer.Finish(&out).Is(ErrorCode::TypeMismatch));
    }
    {
        BlobWriter writer(registry, kTest, ResourceId::None());
        CHECK(writer.Finish(&out).Is(ErrorCode::Invalid));  // a resource has an id
    }
    {
        BlobWriter writer(registry, kTest, ResourceId::FromValue(1));
        writer.AddDependency(Dependency{ResourceId::FromValue(1), types::kMesh, 1, 0});  // itself
        CHECK(writer.Finish(&out).Is(ErrorCode::Invalid));
    }
    {
        BlobWriter writer(registry, kTest, ResourceId::FromValue(1));
        writer.AddDependency(Dependency{ResourceId::None(), types::kMesh, 1, 0});
        CHECK(writer.Finish(&out).Is(ErrorCode::Invalid));
    }
    {
        BlobWriter writer(registry, kTest, ResourceId::FromValue(1));
        writer.AddDependency(Dependency{ResourceId::FromValue(2), types::kMesh, 1, 0});
        writer.AddDependency(Dependency{ResourceId::FromValue(2), types::kMesh, 1, 1});  // twice
        CHECK(writer.Finish(&out).Is(ErrorCode::Invalid));
    }
    {
        BlobWriter writer(registry, kTest, ResourceId::FromValue(1));
        writer.AddDependency(Dependency{ResourceId::FromValue(2), types::kMesh, 0, 0});  // version 0
        CHECK(writer.Finish(&out).Is(ErrorCode::Invalid));
    }
    {
        BlobWriter writer(registry, kTest, ResourceId::FromValue(1));
        CHECK(writer.AddSection(1, ByteSpan()).Is(ErrorCode::Invalid));  // reserved
        CHECK(writer.AddSection(16, ByteSpan()).Ok());
        CHECK(writer.AddSection(16, ByteSpan()).Is(ErrorCode::Invalid));  // repeated
    }
    {
        BlobWriter writer(registry, kTest, ResourceId::FromValue(1));
        for (u32 i = 0; i < kMaxDependencies + 1; ++i)
        {
            writer.AddDependency(Dependency{ResourceId::FromValue(100 + i), types::kMesh, 1, 0});
        }
        CHECK(writer.Finish(&out).Is(ErrorCode::LimitExceeded));
    }
}

TEST_CASE("A NAMES blob may carry the null id; no other type may", "[asset][blob]")
{
    const TypeRegistry registry = MakeRegistry();
    BlobWriter writer(registry, types::kNames, ResourceId::None());
    Vector<byte> blob;
    REQUIRE(writer.Finish(&blob).Ok());
    BlobReader reader;
    CHECK(reader.Open(blob, registry).Ok());
    CHECK(reader.Id().IsNone());

    // Rewrite a resource blob's id to zero: refused.
    Vector<byte> golden = MakeGolden(registry);
    for (u32 i = 0; i < 8; ++i)
    {
        golden[16 + i] = 0;
    }
    CHECK(reader.Open(golden, registry).Is(ErrorCode::Corrupt));
}

TEST_CASE("Another type version or container version is refused naming remint", "[asset][blob]")
{
    const TypeRegistry written = MakeRegistry(1);
    const Vector<byte> golden = MakeGolden(written);
    BlobReader reader;
    {
        const TypeRegistry newer = MakeRegistry(2);  // the runtime moved on: the old blob is refused, never converted
        const Status status = reader.Open(golden, newer);
        CHECK(status.Is(ErrorCode::UnsupportedVersion));
        CHECK(test::Contains(status.Message(), "asset_mint remint"));
    }
    {
        const TypeRegistry same = MakeRegistry(1);  // the type version matches; only the container version is wrong
        Vector<byte> copy;
        for (usize i = 0; i < golden.Size(); ++i)
        {
            copy.Push(golden[i]);
        }
        copy[4] = static_cast<byte>(kContainerVersion + 1);  // a newer container
        const Status status = reader.Open(copy, same);
        CHECK(status.Is(ErrorCode::UnsupportedVersion));
        CHECK(test::Contains(status.Message(), "asset_mint remint"));
    }
    {
        TypeRegistry unknown;  // a registry that does not know the type
        CHECK(reader.Open(golden, unknown).Is(ErrorCode::TypeMismatch));
    }
}

TEST_CASE("A flag the container version does not define is refused", "[asset][blob]")
{
    const TypeRegistry registry = MakeRegistry();
    Vector<byte> blob = MakeGolden(registry);
    blob[28] = static_cast<byte>(1);
    BlobReader reader;
    CHECK(reader.Open(blob, registry).Is(ErrorCode::Corrupt));
}

TEST_CASE("Every truncation of a golden blob is refused", "[asset][blob]")
{
    const TypeRegistry registry = MakeRegistry();
    const Vector<byte> golden = MakeGolden(registry);
    BlobReader reader;
    for (usize length = 0; length < golden.Size(); ++length)
    {
        REQUIRE(reader.Open(ByteSpan(golden.Data(), length), registry).Failed());
    }
    CHECK(reader.Open(golden, registry).Ok());
}

TEST_CASE("Every single-byte corruption is refused by Open or caught by the hash", "[asset][blob]")
{
    const TypeRegistry registry = MakeRegistry();
    Vector<byte> blob = MakeGolden(registry);
    BlobReader reader;
    for (usize i = 0; i < blob.Size(); ++i)
    {
        const byte original = blob[i];
        for (u32 flip : {0x01u, 0x80u, 0xffu})
        {
            blob[i] = static_cast<byte>(original ^ flip);
            if (reader.Open(blob, registry).Ok())
            {
                REQUIRE(reader.Verify().Failed());
            }
        }
        blob[i] = original;
    }
    REQUIRE(reader.Open(blob, registry).Ok());
    CHECK(reader.Verify().Ok());
}

TEST_CASE("Section table attacks are refused", "[asset][blob]")
{
    const TypeRegistry registry = MakeRegistry();
    const Vector<byte> golden = MakeGolden(registry);
    BlobReader reader;
    auto patch_u64 = [&](usize offset, u64 value)
    {
        Vector<byte> copy;
        for (usize i = 0; i < golden.Size(); ++i)
        {
            copy.Push(golden[i]);
        }
        for (u32 i = 0; i < 8; ++i)
        {
            copy[offset + i] = static_cast<byte>((value >> (8 * i)) & 0xff);
        }
        return copy;
    };
    const usize second_entry = kBlobHeaderBytes + kSectionEntryBytes;
    // A section past the end, an offset that overflows, a size that overflows, an unaligned offset, an overlap.
    CHECK(reader.Open(patch_u64(second_entry + 8, golden.Size()), registry).Is(ErrorCode::Corrupt));
    CHECK(reader.Open(patch_u64(second_entry + 8, ~0ull - 63), registry).Is(ErrorCode::Corrupt));
    CHECK(reader.Open(patch_u64(second_entry + 16, ~0ull), registry).Is(ErrorCode::Corrupt));
    CHECK(reader.Open(patch_u64(second_entry + 8, 130), registry).Is(ErrorCode::Corrupt));
    CHECK(reader.Open(patch_u64(second_entry + 8, kBlobHeaderBytes), registry).Is(ErrorCode::Corrupt));
    // The recorded total size must equal the buffer.
    CHECK(reader.Open(patch_u64(40, golden.Size() + 64), registry).Is(ErrorCode::Corrupt));
}

TEST_CASE("Random bytes and mutated blobs never crash the reader", "[asset][blob][fuzz]")
{
    const TypeRegistry registry = MakeRegistry();
    const Vector<byte> golden = MakeGolden(registry);
    test::Rng rng(20261113);
    BlobReader reader;
    u32 accepted = 0;
    for (u32 i = 0; i < 1000000; ++i)
    {
        Vector<byte> bytes;
        if ((i & 1) == 0)
        {
            const u32 length = rng.Below(300);
            for (u32 k = 0; k < length; ++k)
            {
                bytes.Push(static_cast<byte>(rng.Below(256)));
            }
        }
        else
        {
            for (usize k = 0; k < golden.Size(); ++k)
            {
                bytes.Push(golden[k]);
            }
            const u32 mutations = 1 + rng.Below(4);
            for (u32 m = 0; m < mutations; ++m)
            {
                bytes[rng.Below(static_cast<u32>(bytes.Size()))] = static_cast<byte>(rng.Below(256));
            }
        }
        if (reader.Open(bytes, registry).Ok())
        {
            ++accepted;
            // Whatever was accepted must be fully readable.
            for (usize d = 0; d < reader.DependencyCount(); ++d)
            {
                (void)reader.DependencyAt(d);
            }
            ByteSpan section;
            for (u32 id = 0; id < 48; ++id)
            {
                (void)reader.FindSection(id, &section);
            }
        }
    }
    CHECK(accepted > 0);  // mutations that touch only section data are valid blobs
}

}  // namespace engine_placeholder::asset
