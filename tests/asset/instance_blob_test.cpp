// Instance sets as assets (Phase 28 Day 498): the blob, the source text, loading without per-instance adds, hostile
// blobs and determinism.

#include <catch2/catch_test_macros.hpp>

#include "asset/blob.h"
#include "asset/instance_blob.h"
#include "cstr/mem_ops.h"
#include "cstr/format.h"
#include "instance_source.h"
#include "support.h"

namespace vse::asset
{

namespace
{

using test::Rid;

renderer::InstanceTransform IbMake(f64 x, f64 y, f64 z, f32 s)
{
    renderer::InstanceTransform t;
    t.position = Vec3d(x, y, z);
    t.rotation = Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
    t.scale = Vec3(s, s, s);
    return t;
}

Vector<renderer::InstanceTransform> IbMany(u32 n)
{
    Vector<renderer::InstanceTransform> v;
    for (u32 i = 0; i < n; ++i)
    {
        v.Push(IbMake(static_cast<f64>((i * 37u) % 311u) * 1.5, 0.25 * static_cast<f64>(i % 7),
                      static_cast<f64>((i * 91u) % 509u) * 0.5 + 1.0e6, 1.0f + static_cast<f32>(i % 3)));
    }
    return v;
}

Vector<byte> IbBlob(const Vector<renderer::InstanceTransform>& t, const Vector<renderer::InstanceExtra>& d,
                    bool has_data)
{
    TypeRegistry types;
    renderer::InstanceSetDesc desc;
    desc.has_data = has_data;
    Vector<byte> blob;
    REQUIRE(BuildInstanceBlob(types, Rid(5), desc, Span<const renderer::InstanceTransform>(t.Data(), t.Size()),
                              Span<const renderer::InstanceExtra>(d.Data(), d.Size()), &blob)
                .Ok());
    return blob;
}

}  // namespace

TEST_CASE("instance blob: a set round-trips through the blob without per-instance adds", "[asset][instances]")
{
    const Vector<renderer::InstanceTransform> t = IbMany(1000);
    Vector<renderer::InstanceExtra> d;
    for (u32 i = 0; i < t.Size(); ++i)
    {
        renderer::InstanceExtra e;
        e.tint = Vec4(static_cast<f32>(i % 4) * 0.25f, 0.5f, 0.25f, 1.0f);
        e.custom = i;
        d.Push(e);
    }
    const Vector<byte> blob = IbBlob(t, d, true);
    TypeRegistry types;
    InstanceBlobView view;
    REQUIRE(view.Open(CopyShared(ByteSpan(blob.Data(), blob.Size())), types).Ok());
    REQUIRE(view.Header().instance_count == 1000);
    REQUIRE(view.Header().has_data);
    renderer::InstanceSetPtr set;
    REQUIRE(MakeInstanceSet(view, &set).Ok());
    REQUIRE(set);
    CHECK(set->Count() == 1000);
    CHECK(set->AddCalls() == 0);
    // The same blob again from the made set gives the same bytes.
    Vector<byte> again;
    REQUIRE(BuildInstanceBlobFromSet(types, Rid(5), *set, &again).Ok());
    CHECK(again.Size() == blob.Size());
    CHECK(MemCompare(again.Data(), blob.Data(), blob.Size()) == 0);
}

TEST_CASE("instance blob: the blob is a function of the input", "[asset][instances]")
{
    const Vector<renderer::InstanceTransform> t = IbMany(700);
    const Vector<renderer::InstanceExtra> none;
    const Vector<byte> a = IbBlob(t, none, false);
    const Vector<byte> b = IbBlob(t, none, false);
    REQUIRE(a.Size() == b.Size());
    CHECK(MemCompare(a.Data(), b.Data(), a.Size()) == 0);
}

TEST_CASE("instance blob: a hostile blob is refused, never trusted", "[asset][instances]")
{
    const Vector<renderer::InstanceTransform> t = IbMany(300);
    const Vector<renderer::InstanceExtra> none;
    const Vector<byte> blob = IbBlob(t, none, false);
    TypeRegistry types;
    // Every truncation fails to open or to make a set.
    u32 refused = 0;
    for (usize cut = 0; cut < blob.Size(); cut += 61)
    {
        Vector<byte> part;
        for (usize i = 0; i < cut; ++i)
        {
            part.Push(blob[i]);
        }
        InstanceBlobView view;
        renderer::InstanceSetPtr set;
        if (view.Open(CopyShared(ByteSpan(part.Data(), part.Size())), types).Failed() ||
            MakeInstanceSet(view, &set).Failed())
        {
            ++refused;
        }
    }
    CHECK(refused > 0);
    // A flipped byte anywhere never crashes; the result is either refused or a valid set.
    for (usize at = 0; at < blob.Size(); at += 29)
    {
        Vector<byte> bad;
        for (usize i = 0; i < blob.Size(); ++i)
        {
            bad.Push(blob[i]);
        }
        bad[at] = static_cast<byte>(bad[at] ^ 0xFF);
        InstanceBlobView view;
        renderer::InstanceSetPtr set;
        if (view.Open(CopyShared(ByteSpan(bad.Data(), bad.Size())), types).Ok())
        {
            (void)MakeInstanceSet(view, &set);
        }
    }
}

TEST_CASE("instance source: the minter reads the text", "[asset][instances]")
{
    const char* text = "# a few trees\n"
                       "id 0000000000000007\n"
                       "cell_size 2\n"
                       "instance 1000000.5 0 20 0 0 0 1 1 1 1\n"
                       "instance 3 0 4 0 0 0 1 2 2 2\n";
    TypeRegistry types;
    Vector<byte> blob;
    InstanceSourceError error;
    REQUIRE(MintInstances(StringView(text), types, &blob, &error).Ok());
    InstanceBlobView view;
    REQUIRE(view.Open(CopyShared(ByteSpan(blob.Data(), blob.Size())), types).Ok());
    CHECK(view.Header().instance_count == 2);
    CHECK(view.Header().key_cell_size == 2.0);
    CHECK_FALSE(view.Header().has_data);
}

TEST_CASE("instance source: bad lines are refused with a line number", "[asset][instances]")
{
    struct Case
    {
        const char* text;
        u32 line;
    };
    const Case cases[] = {
        {"instance 0 0 0 0 0 0 1 1 1 1\n", 1},                         // no id first
        {"id 0000000000000007\n", 0},                                  // no instance
        {"id 0000000000000007\ninstance 0 0 0 0 0 0 1 0 1 1\n", 2},    // scale 0
        {"id 0000000000000007\ninstance 0 0 0 0 0 0 2 1 1 1\n", 2},    // not a unit rotation
        {"id 0000000000000007\ninstance 0 0 0 0 0 0 1 1 1\n", 2},      // wrong count
        {"id 0000000000000007\ninstance nan 0 0 0 0 0 1 1 1 1\n", 2},  // not finite
        {"id 0000000000000007\ninstance 0 0 0 0 0 0 1 1 1 1 1 1 1 1 3\n"
         "instance 0 0 0 0 0 0 1 1 1 1\n",
         3},                                                                      // data mixed with none
        {"id 0000000000000007\ncell_size 0\ninstance 0 0 0 0 0 0 1 1 1 1\n", 2},  // cell size 0
        {"id 0000000000000007\nfoo\n", 2},                                        // unknown record
    };
    for (const Case& c : cases)
    {
        TypeRegistry types;
        Vector<byte> blob;
        InstanceSourceError error;
        INFO(c.text);
        CHECK(MintInstances(StringView(c.text), types, &blob, &error).Failed());
        CHECK(error.line == c.line);
    }
}

}  // namespace vse::asset
