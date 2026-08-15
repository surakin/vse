#include <catch2/catch_test_macros.hpp>

#include "os/os.h"
#include "types.h"

using engine_placeholder::u64;
using engine_placeholder::os::MonotonicTimeNanos;
using engine_placeholder::os::SleepNanos;

TEST_CASE("MonotonicTimeNanos never goes backwards across consecutive calls", "[os][timing]") {
    u64 t1 = MonotonicTimeNanos();
    u64 t2 = MonotonicTimeNanos();
    REQUIRE(t2 >= t1);
}

TEST_CASE("SleepNanos blocks for at least (most of) the requested duration", "[os][timing]") {
    constexpr u64 kRequestedNanos = 20'000'000; // 20ms
    u64 t1 = MonotonicTimeNanos();
    SleepNanos(kRequestedNanos);
    u64 t2 = MonotonicTimeNanos();
    // Lower-bound only (80% of requested) to avoid flakiness from
    // scheduler/clock granularity on a loaded CI machine; there's no
    // meaningful upper bound to assert since the OS is free to delay a
    // sleeping thread arbitrarily longer.
    REQUIRE(t2 - t1 >= kRequestedNanos * 8 / 10);
}
