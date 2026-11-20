// The rate limiter (Phase 20 Day 279): token buckets, the global bucket, strikes and bans, and a table that never
// grows.

#include <catch2/catch_test_macros.hpp>

#include "net/rate_limit.h"

namespace vse::net
{
namespace
{

constexpr u64 kSecond = 1'000'000'000ull;

Address RlAddr(u32 n)
{
    return Address::V4(static_cast<u8>(10 + (n >> 24)), static_cast<u8>(n >> 16), static_cast<u8>(n >> 8),
                       static_cast<u8>(n));
}

}  // namespace

TEST_CASE("RateLimiter: an address gets its burst, then its rate", "[net][limits]")
{
    RateLimiterConfig c;
    c.address_per_second = 10;
    c.address_burst = 5;
    RateLimiter limiter(c);
    u64 now = kSecond;
    u32 allowed = 0;
    for (u32 i = 0; i < 20; ++i)
    {
        allowed += limiter.Allow(RlAddr(1), now) ? 1 : 0;
    }
    CHECK(allowed == 5);
    now += kSecond / 2;  // five tokens come back
    allowed = 0;
    for (u32 i = 0; i < 20; ++i)
    {
        allowed += limiter.Allow(RlAddr(1), now) ? 1 : 0;
    }
    CHECK(allowed == 5);
    // Another address is not affected.
    CHECK(limiter.Allow(RlAddr(2), now));
    // A long silence refills to the burst and no further.
    now += 100 * kSecond;
    allowed = 0;
    for (u32 i = 0; i < 20; ++i)
    {
        allowed += limiter.Allow(RlAddr(1), now) ? 1 : 0;
    }
    CHECK(allowed == 5);
    CHECK(limiter.Counters().refused_address == 45);
}

TEST_CASE("RateLimiter: the global bucket bounds the total over many addresses", "[net][limits]")
{
    RateLimiterConfig c;
    c.total_per_second = 100;
    c.total_burst = 10;
    RateLimiter limiter(c);
    u32 allowed = 0;
    for (u32 i = 0; i < 1000; ++i)
    {
        allowed += limiter.Allow(RlAddr(i), kSecond) ? 1 : 0;
    }
    CHECK(allowed == 10);
    CHECK(limiter.Counters().refused_total == 990);
    // One second later: 100 per second refills up to the burst of 10.
    allowed = 0;
    for (u32 i = 0; i < 1000; ++i)
    {
        allowed += limiter.Allow(RlAddr(i), 2 * kSecond) ? 1 : 0;
    }
    CHECK(allowed == 10);
}

TEST_CASE("RateLimiter: no limit when the rates are zero", "[net][limits]")
{
    RateLimiter limiter;
    for (u32 i = 0; i < 10000; ++i)
    {
        REQUIRE(limiter.Allow(RlAddr(i % 7), kSecond));
    }
}

TEST_CASE("RateLimiter: strikes ban an address for a while, old strikes are forgotten", "[net][limits]")
{
    RateLimiterConfig c;
    c.strikes_to_ban = 3;
    c.strike_window_nanos = 10 * kSecond;
    c.ban_nanos = 100 * kSecond;
    RateLimiter limiter(c);
    const Address a = RlAddr(1);
    u64 now = kSecond;
    limiter.Strike(a, now);
    limiter.Strike(a, now + kSecond);
    CHECK_FALSE(limiter.IsBanned(a, now + kSecond));
    CHECK(limiter.Allow(a, now + kSecond));
    // The third strike comes after the window: the count starts over.
    limiter.Strike(a, now + 20 * kSecond);
    CHECK_FALSE(limiter.IsBanned(a, now + 20 * kSecond));
    limiter.Strike(a, now + 21 * kSecond);
    limiter.Strike(a, now + 22 * kSecond);
    CHECK(limiter.IsBanned(a, now + 22 * kSecond));
    CHECK_FALSE(limiter.Allow(a, now + 50 * kSecond));
    CHECK(limiter.Allow(RlAddr(2), now + 50 * kSecond));
    CHECK(limiter.BannedAddresses(now + 50 * kSecond) == 1);
    // The ban expires.
    CHECK(limiter.Allow(a, now + 130 * kSecond));
    CHECK(limiter.BannedAddresses(now + 130 * kSecond) == 0);
    CHECK(limiter.Counters().bans == 1);
}

TEST_CASE("RateLimiter: a hundred thousand addresses never grow the table, and a ban outlives the churn",
          "[net][limits]")
{
    RateLimiterConfig c;
    c.address_per_second = 1;
    c.address_burst = 1;
    c.max_addresses = 256;
    c.strikes_to_ban = 1;
    RateLimiter limiter(c);
    const usize capacity = limiter.Capacity();
    CHECK(capacity == 256);
    u64 now = kSecond;
    limiter.Strike(RlAddr(5'000'000), now);
    REQUIRE(limiter.IsBanned(RlAddr(5'000'000), now));
    for (u32 i = 0; i < 100000; ++i)
    {
        limiter.Allow(RlAddr(i), now + i);
        CHECK(limiter.Capacity() == capacity);
    }
    CHECK(limiter.IsBanned(RlAddr(5'000'000), now + 100000));
    CHECK(limiter.ActiveAddresses(now + 100000) <= capacity);
}

TEST_CASE("RateLimiter: a table of bans only still accepts a new ban", "[net][limits]")
{
    RateLimiterConfig c;
    c.max_addresses = 8;
    c.strikes_to_ban = 1;
    RateLimiter limiter(c);
    for (u32 i = 0; i < 200; ++i)
    {
        limiter.Strike(RlAddr(i), kSecond + i);
    }
    CHECK(limiter.BannedAddresses(kSecond + 200) <= limiter.Capacity());
    CHECK(limiter.IsBanned(RlAddr(199), kSecond + 200));
}

}  // namespace vse::net
