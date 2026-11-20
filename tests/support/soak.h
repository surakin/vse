#pragma once

#include "os/console.h"  // os::GetEnvironment
#include "types.h"

// The short and the soak form of a long test (maintenance "shorter test runs", Day 4). A test that is long only because
// of a count (resets in a loop, a sweep of failure points, a minute of hostile scripts) runs a short count in the
// ordinary run and the full count when ENGINE_TEST_SOAK=1. The full form is a second ctest, registered with
// engine_soak_catch_test() in CMake, which sets the variable and carries the `soak` label, so scripts/test-phase.sh
// runs it only with --all or --soak. Both forms run the same checks; only the count differs. See
// docs/guide/building-and-testing.md, "Long tests".
namespace test_support
{

using vse::u32;

/// Whether this run is a test's soak form (ENGINE_TEST_SOAK=1).
inline bool Soak()
{
    const char* value = vse::os::GetEnvironment("ENGINE_TEST_SOAK");
    return value != nullptr && value[0] == '1' && value[1] == '\0';
}

/// The count to use: `full` in the soak form, `short_count` otherwise.
inline u32 SoakCount(u32 short_count, u32 full)
{
    return Soak() ? full : short_count;
}

}  // namespace test_support
