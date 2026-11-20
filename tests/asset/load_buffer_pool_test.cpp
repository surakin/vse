// The load buffer pool (Phase 22 Day 315): alignment, size classes, reuse without allocating, the byte budget.

#include <catch2/catch_test_macros.hpp>

#include "asset/asset_settings.h"
#include "asset/load_buffer_pool.h"
#include "support.h"

namespace vse::asset
{

TEST_CASE("Buffers are aligned and pre-touched", "[asset][pool]")
{
    LoadBufferPool pool(u64(64) << 20);
    LoadBuffer small;
    REQUIRE(pool.Acquire(1000, &small).Ok());
    CHECK(small.Size() == 1000);
    CHECK(small.Capacity() == 1024);
    CHECK(reinterpret_cast<usize>(small.Data()) % 64 == 0);
    for (usize i = 0; i < small.Capacity(); ++i)
    {
        REQUIRE(small.Data()[i] == 0);
    }

    LoadBuffer large;
    REQUIRE(pool.Acquire((usize(1) << 20) + 1, &large).Ok());
    CHECK(large.Capacity() == (usize(2) << 20));
    CHECK(reinterpret_cast<usize>(large.Data()) % 4096 == 0);
}

TEST_CASE("A second load of the same size reuses a buffer and allocates nothing", "[asset][pool]")
{
    LoadBufferPool pool(u64(64) << 20);
    {
        LoadBuffer first;
        REQUIRE(pool.Acquire(300000, &first).Ok());
        CHECK(pool.AllocationCount() == 1);
        CHECK(pool.BytesInUse() == first.Capacity());
    }
    CHECK(pool.BytesInUse() == 0);
    CHECK(pool.BytesIdle() > 0);
    LoadBuffer second;
    REQUIRE(pool.Acquire(290000, &second).Ok());  // same size class
    CHECK(pool.AllocationCount() == 1);
    CHECK(pool.ReuseCount() == 1);
}

TEST_CASE("The pool is bounded by its budget and drops idle buffers to make room", "[asset][pool]")
{
    LoadBufferPool pool(1 << 20);
    LoadBuffer a;
    REQUIRE(pool.Acquire(600000, &a).Ok());  // 1 MiB class
    LoadBuffer b;
    CHECK(pool.Acquire(1000, &b).Is(ErrorCode::LimitExceeded));  // the whole budget is in use
    CHECK(!b.IsValid());
    a.Release();
    CHECK(pool.BytesIdle() == (1u << 20));
    REQUIRE(pool.Acquire(1000, &b).Ok());  // the idle 1 MiB buffer was dropped for it
    CHECK(pool.BytesIdle() == 0);
    CHECK(pool.Acquire(0, &a).Is(ErrorCode::Invalid));
    CHECK(pool.Acquire(usize(1) << 40, &a).Is(ErrorCode::TooLarge));
}

TEST_CASE("A moved buffer is returned once", "[asset][pool]")
{
    LoadBufferPool pool(u64(8) << 20);
    LoadBuffer a;
    REQUIRE(pool.Acquire(5000, &a).Ok());
    LoadBuffer b = Move(a);
    CHECK(!a.IsValid());
    CHECK(b.IsValid());
    b.Release();
    CHECK(pool.BytesInUse() == 0);
    CHECK(pool.BytesIdle() == 8192);
}

TEST_CASE("The asset settings declare the two budgets, and declaring twice is harmless", "[asset][pool][settings]")
{
    settings::Registry registry;
    RegisterAssetSettings(&registry);
    RegisterAssetSettings(&registry);
    CHECK(BufferPoolBytesFromSettings(registry) == (u64(256) << 20));
    CHECK(MemoryBackendBytesFromSettings(registry) == (u64(512) << 20));
}

}  // namespace vse::asset
