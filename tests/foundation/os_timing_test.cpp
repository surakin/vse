#include <catch2/catch_test_macros.hpp>
#include <cstdlib>

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

TEST_CASE("os::EnvironmentEntry lists the process environment as NAME=value", "[os][environment]") {
    using engine_placeholder::StringView;
    using engine_placeholder::usize;
    setenv("RE_ENVIRONMENT_ENTRY_TEST", "abc=def", 1);
    const usize count = engine_placeholder::os::EnvironmentCount();
    REQUIRE(count > 0);
    bool found = false;
    usize found_index = 0;
    for (usize i = 0; i < count; ++i) {
        char entry[4096];
        usize length = 0;
        REQUIRE(engine_placeholder::os::EnvironmentEntry(i, entry, sizeof(entry), &length));
        REQUIRE(length < sizeof(entry));
        if (StringView(entry, length) == StringView("RE_ENVIRONMENT_ENTRY_TEST=abc=def")) {
            found = true;
            found_index = i;
        }
    }
    REQUIRE(found);

    // The caller's buffer: exact fit, one byte short, and no room at all.
    const StringView expected("RE_ENVIRONMENT_ENTRY_TEST=abc=def");
    char exact[64];
    usize length = 0;
    REQUIRE(engine_placeholder::os::EnvironmentEntry(found_index, exact, expected.Length() + 1, &length));
    CHECK(length == expected.Length());
    CHECK(StringView(exact, length) == expected);
    CHECK(exact[length] == '\0');

    char shorter[64];
    std::memset(shorter, 'x', sizeof(shorter));
    REQUIRE(engine_placeholder::os::EnvironmentEntry(found_index, shorter, expected.Length(), &length));
    CHECK(length == expected.Length()); // the full length is reported so the caller can retry
    CHECK(shorter[expected.Length() - 1] == '\0'); // cut short, still terminated
    CHECK(StringView(shorter) == expected.Substring(0, expected.Length() - 1));

    char untouched[4] = {'x', 'x', 'x', 'x'};
    REQUIRE(engine_placeholder::os::EnvironmentEntry(found_index, untouched, 0, &length));
    CHECK(length == expected.Length());
    CHECK(untouched[0] == 'x'); // capacity 0 writes nothing

    // Out of range: false, length 0, an empty string.
    char none[8] = {'x'};
    length = 99;
    CHECK_FALSE(engine_placeholder::os::EnvironmentEntry(count + 10, none, sizeof(none), &length));
    CHECK(length == 0);
    CHECK(none[0] == '\0');
    unsetenv("RE_ENVIRONMENT_ENTRY_TEST");
}
