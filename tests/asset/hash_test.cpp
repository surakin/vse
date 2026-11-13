// The content hash (Phase 22 Day 314): the FIPS 180-4 vectors (the hash is SHA-256 truncated to 128 bits), streaming
// in any chunking, and the hex form.

#include <catch2/catch_test_macros.hpp>

#include "asset/content_hash.h"
#include "support.h"

namespace engine_placeholder::asset
{

namespace
{

bool HexIs(const ContentHash& hash, const char* expected)
{
    char hex[33];
    FormatContentHash(hash, hex);
    return CStrCompare(hex, expected) == 0;
}

}  // namespace

TEST_CASE("The content hash matches the SHA-256 vectors, truncated to 128 bits", "[asset][hash]")
{
    CHECK(HexIs(HashBytes(ByteSpan()), "e3b0c44298fc1c149afbf4c8996fb924"));
    CHECK(HexIs(HashBytes(test::BytesOf("abc")), "ba7816bf8f01cfea414140de5dae2223"));
    CHECK(HexIs(HashBytes(test::BytesOf("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")),
                "248d6a61d20638b8e5c026930c3e6039"));
}

TEST_CASE("The content hash of one million 'a' matches the vector, streamed in chunks", "[asset][hash]")
{
    Vector<byte> chunk;
    chunk.Resize(1000);
    for (usize i = 0; i < chunk.Size(); ++i)
    {
        chunk[i] = static_cast<byte>('a');
    }
    ContentHasher hasher;
    for (u32 i = 0; i < 1000; ++i)
    {
        hasher.Update(chunk);
    }
    CHECK(HexIs(hasher.Finish(), "cdc76e5c9914fb9281a1c7e284d73e67"));
}

TEST_CASE("Streaming equals one call for every split of every length up to 200", "[asset][hash]")
{
    Vector<byte> data;
    test::Rng rng(7);
    for (u32 i = 0; i < 200; ++i)
    {
        data.Push(static_cast<byte>(rng.Below(256)));
    }
    for (usize length = 0; length <= data.Size(); ++length)
    {
        const ByteSpan whole(data.Data(), length);
        const ContentHash expected = HashBytes(whole);
        for (usize split = 0; split <= length; split += 7)
        {
            ContentHasher hasher;
            hasher.Update(whole.First(split));
            hasher.Update(whole.Subspan(split));
            REQUIRE(hasher.Finish() == expected);
        }
    }
}

TEST_CASE("A hasher can be reset and reused; a zero hash is recognised", "[asset][hash]")
{
    ContentHasher hasher;
    hasher.Update(test::BytesOf("abc"));
    (void)hasher.Finish();
    hasher.Reset();
    hasher.Update(test::BytesOf("abc"));
    CHECK(hasher.Finish() == HashBytes(test::BytesOf("abc")));
    CHECK(ContentHash().IsZero());
    CHECK(!HashBytes(ByteSpan()).IsZero());
}

}  // namespace engine_placeholder::asset
