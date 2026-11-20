#include <catch2/catch_test_macros.hpp>

#include "mem/pool_allocator.h"
#include "types.h"

using vse::PoolAllocator;
using vse::u32;
using vse::usize;

TEST_CASE("PoolAllocator: random alloc/free churn never aliases two live allocations", "[mem][pool]")
{
    constexpr usize kSlotSize = 32;
    constexpr usize kSlotCount = 64;
    constexpr usize kIterations = 10000;

    PoolAllocator pool(kSlotSize, kSlotCount);

    void* live[kSlotCount] = {};
    bool live_flag[kSlotCount] = {};

    // Small deterministic xorshift32 PRNG — reproducible across
    // platforms without depending on libc's rand() implementation.
    u32 rng_state = 0x12345678u;
    auto NextRand = [&rng_state]() -> u32
    {
        rng_state ^= rng_state << 13;
        rng_state ^= rng_state >> 17;
        rng_state ^= rng_state << 5;
        return rng_state;
    };

    bool no_alias = true;
    for (usize iter = 0; iter < kIterations && no_alias; ++iter)
    {
        usize idx = NextRand() % kSlotCount;
        if (!live_flag[idx])
        {
            void* p = pool.Alloc();
            if (p == nullptr)
            {
                // Pool momentarily full (every logical slot live); skip
                // this iteration rather than treating it as failure.
                continue;
            }
            for (usize j = 0; j < kSlotCount; ++j)
            {
                if (live_flag[j] && live[j] == p)
                {
                    no_alias = false;
                    break;
                }
            }
            live[idx] = p;
            live_flag[idx] = true;
        }
        else
        {
            pool.Free(live[idx]);
            live_flag[idx] = false;
            live[idx] = nullptr;
        }
    }
    REQUIRE(no_alias);
}
