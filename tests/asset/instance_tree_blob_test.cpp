// Instance trees as assets (Phase 28 Day 499): the blob, the source text, loading, hostile blobs and determinism.

#include <catch2/catch_test_macros.hpp>

#include "asset/blob.h"
#include "asset/instance_tree_blob.h"
#include "cstr/mem_ops.h"
#include "instance_tree_source.h"
#include "support.h"

namespace vse::asset
{

namespace
{

using test::Rid;

const char* const kTreeText = "# a tree with two branches\n"
                              "id 0000000000000009\n"
                              "mesh -0.5 0 -0.5 0.5 3 0.5\n"
                              "mesh -0.2 0 -0.2 0.2 1 0.2\n"
                              "node - 1000000.25 0 20 0 0 0 1 1 1 1 0 0\n"
                              "node 0 0 3 0 0 0 0.7071068 0.7071068 0.5 0.5 0.5 1 1\n"
                              "node 0 0 2 0 0 0 0 1 0.5 0.5 0.5 1 1\n"
                              "node 1 0 1 0 0 0 0 1 0.5 0.5 0.5 - 2\n";

Vector<byte> ItBlob()
{
    TypeRegistry types;
    Vector<byte> blob;
    InstanceTreeSourceError error;
    REQUIRE(MintInstanceTree(StringView(kTreeText), types, &blob, &error).Ok());
    return blob;
}

Vector<byte> ItCopy(const Vector<byte>& v)
{
    Vector<byte> out;
    for (usize i = 0; i < v.Size(); ++i)
    {
        out.Push(v[i]);
    }
    return out;
}

}  // namespace

TEST_CASE("instance tree blob: the source mints a blob that loads into the tree", "[asset][instance_tree]")
{
    const Vector<byte> blob = ItBlob();
    TypeRegistry types;
    InstanceTreeBlobView view;
    REQUIRE(view.Open(CopyShared(ByteSpan(blob.Data(), blob.Size())), types).Ok());
    CHECK(view.NodeCount() == 4);
    CHECK(view.MeshCount() == 2);
    renderer::InstanceTreePtr tree;
    REQUIRE(MakeInstanceTree(view, nullptr, &tree).Ok());
    REQUIRE(tree.Get() != nullptr);
    CHECK(tree->NodeCount() == 4);
    CHECK(tree->Validate());
    CHECK(tree->RootCount() == 1);
    CHECK(tree->Node(0).origin.x == 1000000.25);
    CHECK(tree->Node(0).subtree_size == 4);
    CHECK(tree->Node(2).mesh == renderer::kTreeNoMesh);  // depth first: ids 0, 1, 3, 2
}

TEST_CASE("instance tree blob: minting twice gives the same bytes", "[asset][instance_tree]")
{
    const Vector<byte> a = ItBlob();
    const Vector<byte> b = ItBlob();
    REQUIRE(a.Size() == b.Size());
    CHECK(MemCompare(a.Data(), b.Data(), a.Size()) == 0);
}

TEST_CASE("instance tree blob: a hostile blob is refused, never trusted", "[asset][instance_tree]")
{
    const Vector<byte> blob = ItBlob();
    TypeRegistry types;
    u32 refused = 0;
    for (usize cut = 0; cut < blob.Size(); cut += 17)
    {
        Vector<byte> part;
        for (usize i = 0; i < cut; ++i)
        {
            part.Push(blob[i]);
        }
        InstanceTreeBlobView view;
        renderer::InstanceTreePtr tree;
        if (view.Open(CopyShared(ByteSpan(part.Data(), part.Size())), types).Failed() ||
            MakeInstanceTree(view, nullptr, &tree).Failed())
        {
            ++refused;
        }
    }
    CHECK(refused > 0);
    // Every flipped byte either opens and builds a valid tree, or is refused; nothing crashes.
    for (usize at = 0; at < blob.Size(); ++at)
    {
        Vector<byte> bad = ItCopy(blob);
        bad[at] = static_cast<byte>(bad[at] ^ 0xFF);
        InstanceTreeBlobView view;
        renderer::InstanceTreePtr tree;
        if (view.Open(CopyShared(ByteSpan(bad.Data(), bad.Size())), types).Ok() &&
            MakeInstanceTree(view, nullptr, &tree).Ok())
        {
            CHECK(tree->Validate());
        }
    }
}

TEST_CASE("instance tree source: bad lines are refused with a line number", "[asset][instance_tree]")
{
    struct Case
    {
        const char* text;
        u32 line;
    };
    const Case cases[] = {
        {"node - 0 0 0 0 0 0 1 1 1 1 - 0\n", 1},                                         // no id first
        {"id 0000000000000009\n", 0},                                                    // no node
        {"id 0000000000000009\nnode 0 0 0 0 0 0 0 1 1 1 1 - 0\n", 2},                    // parent not earlier
        {"id 0000000000000009\nnode - 0 0 0 0 0 0 1 0 1 1 - 0\n", 2},                    // scale 0
        {"id 0000000000000009\nnode - 0 0 0 0 0 0 2 1 1 1 - 0\n", 2},                    // not a unit rotation
        {"id 0000000000000009\nnode - 0 0 0 0 0 0 1 1 1 1 0 0\n", 2},                    // mesh not listed
        {"id 0000000000000009\nnode - 0 0 0 0 0 0 1 1 1 1 - 256\n", 2},                  // slot too high
        {"id 0000000000000009\nnode - 0 0 0 0 0 0 1 1 1\n", 2},                          // wrong count
        {"id 0000000000000009\nmesh 1 0 0 0 1 1\nnode - 0 0 0 0 0 0 1 1 1 1 0 0\n", 2},  // inverted box
        {"id 0000000000000009\nnode - 0 0 0 0 0 0 1 1 1 1 - 0\nmesh 0 0 0 1 1 1\n", 3},  // mesh after node
        {"id 0000000000000009\nnode - 0 0 0 0 0 0 1 1 1 1 - 0 1 1 1 1 3\n"
         "node - 0 0 0 0 0 0 1 1 1 1 - 0\n",
         3},  // data mixed with none
    };
    for (const Case& c : cases)
    {
        TypeRegistry types;
        Vector<byte> blob;
        InstanceTreeSourceError error;
        INFO(c.text);
        CHECK(MintInstanceTree(StringView(c.text), types, &blob, &error).Failed());
        CHECK(error.line == c.line);
    }
}

}  // namespace vse::asset
