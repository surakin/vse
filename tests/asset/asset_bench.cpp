// Phase 22 Day 340: the asset benchmarks, hidden (`[.][asset-bench]`), meant for a Release build. Each prints its
// figures with os::Print; the table in docs/superpowers/specs/2026-11-13-asset-pipeline-design.md quotes them against
// the pass conditions.

#include <catch2/catch_test_macros.hpp>

#include "asset/block_compress.h"
#include "asset/blob.h"
#include "asset/memory_backend.h"
#include "asset/resource_table.h"
#include "asset/texture_mint.h"
#include "cstr/format.h"
#include "jobs/scheduler.h"
#include "os/console.h"
#include "os/os.h"
#include "support.h"

namespace engine_placeholder::asset
{
namespace
{

f64 Seconds(u64 start)
{
    return static_cast<f64>(os::MonotonicTimeNanos() - start) / 1e9;
}

Vector<byte> Gradient(u32 size)
{
    Vector<byte> pixels;
    pixels.Resize(static_cast<usize>(size) * size * 4);
    for (u32 y = 0; y < size; ++y)
    {
        for (u32 x = 0; x < size; ++x)
        {
            byte* p = pixels.Data() + (static_cast<usize>(y) * size + x) * 4;
            p[0] = static_cast<byte>(x * 255 / size);
            p[1] = static_cast<byte>(y * 255 / size);
            p[2] = static_cast<byte>((x ^ y) & 255);
            p[3] = 255;
        }
    }
    return pixels;
}

}  // namespace

TEST_CASE("bench: block compression speed per megapixel at 1 and 8 workers", "[.][asset-bench]")
{
    const Vector<byte> pixels = Gradient(1024);
    const ByteSpan image(pixels.Data(), pixels.Size());
    jobs::Scheduler::Config config;
    config.participants = 8;
    config.pin = false;
    jobs::Scheduler scheduler(config);
    const struct
    {
        const char* name;
        BcKind kind;
    } kinds[] = {{"BC1", BcKind::Bc1}, {"BC4", BcKind::Bc4}, {"BC5", BcKind::Bc5}, {"BC7", BcKind::Bc7}};
    for (const auto& k : kinds)
    {
        for (const u32 workers : {1u, 8u})
        {
            Vector<byte> blocks;
            const u64 start = os::MonotonicTimeNanos();
            REQUIRE(
                EncodeImage(k.kind, 1024, 1024, image, BcEncodeOptions(), workers == 1 ? nullptr : &scheduler, &blocks)
                    .Ok());
            const f64 seconds = Seconds(start);
            os::Print("bench: %s encode, %u worker(s): %.2f megapixels per second\n", k.name,
                      static_cast<unsigned>(workers), 1.048576 / seconds);
        }
    }
}

TEST_CASE("bench: the texture minter (mips and BC7) on a 1024 by 1024 image", "[.][asset-bench]")
{
    const Vector<byte> pixels = Gradient(1024);
    const ByteSpan layer(pixels.Data(), pixels.Size());
    TypeRegistry types;
    TextureMintSpec spec;
    spec.format = rhi::Format::BC7_UNorm;
    spec.drop_alpha = true;
    spec.alpha = MipAlpha::Ignore;
    Vector<byte> blob;
    const u64 start = os::MonotonicTimeNanos();
    REQUIRE(MintTextureFromPixels(types, test::Rid(1), spec, 1024, 1024, Span<const ByteSpan>(&layer, 1), &blob).Ok());
    os::Print("bench: mint of a 1024 x 1024 BC7 texture with its mip chain, 1 worker: %.2f s, %u bytes\n",
              Seconds(start), static_cast<unsigned>(blob.Size()));
}

TEST_CASE("bench: table lookups at 100,000 entries and blob validation speed", "[.][asset-bench]")
{
    ResourceTable table;
    constexpr u32 kEntries = 100000;
    for (u32 i = 0; i < kEntries; ++i)
    {
        char path[48];
        FormatTo(path, sizeof(path), "/bench/group%u/item%u", i % 100, i);
        REQUIRE(table
                    .Add(StringView(path, CStrLength(path)), test::Rid(1000 + i),
                         ResourceType::FromChars('B', 'K', 'N', 'D'))
                    .Ok());
    }
    u64 hits = 0;
    u64 start = os::MonotonicTimeNanos();
    constexpr u32 kLookups = 200000;
    for (u32 i = 0; i < kLookups; ++i)
    {
        ResourceType type;
        hits += table.TypeOf(test::Rid(1000 + (i * 7919) % kEntries), &type) ? 1 : 0;
    }
    os::Print("bench: lookup by id in a table of %u entries: %.0f ns each (%u hits)\n", static_cast<unsigned>(kEntries),
              Seconds(start) * 1e9 / kLookups, static_cast<unsigned>(hits));
    start = os::MonotonicTimeNanos();
    for (u32 i = 0; i < kLookups; ++i)
    {
        char path[48];
        const u32 n = (i * 7919) % kEntries;
        FormatTo(path, sizeof(path), "/bench/group%u/item%u", n % 100, n);
        ResourceId id;
        hits += table.Resolve(StringView(path, CStrLength(path)), &id) ? 1 : 0;
    }
    os::Print("bench: lookup by path in the same table (including formatting the path): %.0f ns each\n",
              Seconds(start) * 1e9 / kLookups);

    TypeRegistry types = test::MakeBackendRegistry();
    const Vector<byte> blob = test::MakeTestBlob(types, test::Rid(7), 8u << 20, 3);
    start = os::MonotonicTimeNanos();
    constexpr u32 kOpens = 200;
    for (u32 i = 0; i < kOpens; ++i)
    {
        BlobReader reader;
        REQUIRE(reader.Open(ByteSpan(blob.Data(), blob.Size()), types).Ok());
    }
    os::Print("bench: opening (validating) an 8 MiB blob: %.1f us each\n", Seconds(start) * 1e6 / kOpens);
}

TEST_CASE("bench: ten thousand resources through the memory backend", "[.][asset-bench]")
{
    TypeRegistry types = test::MakeBackendRegistry();
    HostMemoryBackend backend(types, u64{1} << 30);
    constexpr u32 kCount = 10000;
    u64 start = os::MonotonicTimeNanos();
    for (u32 i = 0; i < kCount; ++i)
    {
        const Vector<byte> blob = test::MakeTestBlob(types, test::Rid(1 + i), 4096, i);
        REQUIRE(backend.Add(test::Rid(1 + i), ByteSpan(blob.Data(), blob.Size())).Ok());
    }
    os::Print("bench: adding %u blobs of 4 KiB to the memory backend: %.1f ms\n", static_cast<unsigned>(kCount),
              Seconds(start) * 1e3);
    ResourceTable table;
    start = os::MonotonicTimeNanos();
    REQUIRE(backend.ReadTable(&table).Ok());
    os::Print("bench: reading the table of %u entries: %.2f ms\n", static_cast<unsigned>(table.Size()),
              Seconds(start) * 1e3);
}

}  // namespace engine_placeholder::asset
