// The block hash (Phase 26 Day 415): FNV-1a on known inputs, the byte order, the zero rule, the invalid cases,
// independence of the block boundaries.
#include <catch2/catch_test_macros.hpp>

#include "audio/block_hash.h"
#include "containers/vector.h"
#include "lang/limits.h"

using namespace vse;
using namespace vse::audio;

TEST_CASE("BlockHasher: known values", "[audio][hash]")
{
    BlockHasher empty;
    CHECK(empty.Value() == BlockHasher::kOffsetBasis);
    CHECK(empty.Valid());

    BlockHasher zero;
    zero.AddSample(0.0f);
    CHECK(zero.Value() == 0x4d25767f9dce13f5ull);  // FNV-1a 64 of the four bytes 00 00 00 00

    BlockHasher one;
    one.AddSample(1.0f);
    CHECK(one.Value() == 0x4b72477f9c5c2f98ull);  // bytes 00 00 80 3f: the little-endian bit pattern of 1.0f

    const f32 pair[2] = {0.5f, -0.25f};
    CHECK(HashBlock(Span<const f32>(pair, 2), nullptr) == 0x52f8f165251391c2ull);
}

TEST_CASE("BlockHasher: negative zero hashes as positive zero", "[audio][hash]")
{
    BlockHasher positive;
    positive.AddSample(0.0f);
    BlockHasher negative;
    negative.AddSample(-0.0f);
    CHECK(positive.Value() == negative.Value());
    CHECK(negative.Valid());
}

TEST_CASE("BlockHasher: NaN and infinity make the hash invalid", "[audio][hash]")
{
    const f32 nan = Limits<f32>::QuietNaN();
    const f32 inf = Limits<f32>::Infinity();
    bool valid = true;
    const f32 with_nan[3] = {0.1f, nan, 0.2f};
    HashBlock(Span<const f32>(with_nan, 3), &valid);
    CHECK_FALSE(valid);

    const f32 with_inf[1] = {-inf};
    HashBlock(Span<const f32>(with_inf, 1), &valid);
    CHECK_FALSE(valid);

    const f32 fine[2] = {0.1f, -0.2f};
    HashBlock(Span<const f32>(fine, 2), &valid);
    CHECK(valid);

    BlockHasher hasher;
    hasher.AddSample(nan);
    hasher.AddSample(1.0f);
    CHECK_FALSE(hasher.Valid());  // stays invalid after finite samples
}

TEST_CASE("BlockHasher: different samples and different order give different hashes", "[audio][hash]")
{
    const f32 ab[2] = {0.25f, 0.5f};
    const f32 ba[2] = {0.5f, 0.25f};
    const f32 near[2] = {0.25f, 0.5000001f};
    const u64 hash = HashBlock(Span<const f32>(ab, 2), nullptr);
    CHECK(hash != HashBlock(Span<const f32>(ba, 2), nullptr));
    CHECK(hash != HashBlock(Span<const f32>(near, 2), nullptr));
}

TEST_CASE("RenderHash: block boundaries do not change the hash", "[audio][hash]")
{
    Vector<f32> samples;
    for (u32 i = 0; i < 1000; ++i)
    {
        samples.Push(static_cast<f32>(i % 17) * 0.0625f - 0.5f);
    }
    RenderHash whole;
    whole.AddBlock(Span<const f32>(samples.Data(), samples.Size()));

    RenderHash pieces;
    usize first = 0;
    const usize sizes[4] = {64, 100, 256, 580};
    for (usize size : sizes)
    {
        pieces.AddBlock(Span<const f32>(samples.Data() + first, size));
        first += size;
    }
    CHECK(first == samples.Size());
    CHECK(pieces.Value() == whole.Value());
    CHECK(pieces.Blocks() == 4);
    CHECK(whole.Blocks() == 1);
    CHECK(pieces.Samples() == 1000);
    CHECK(pieces.Valid());
}
