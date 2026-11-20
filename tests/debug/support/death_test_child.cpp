// The child half of the death-test helper for Catch2 test executables (see cmake/EngineDeathTests.cmake). When the
// parent starts this executable with the filter `[.death-child]` and the environment variable ENGINE_DEATH_TEST_CHILD
// set, this test case runs the requested function and ends the process; run any other way it does nothing.
#include <catch2/catch_test_macros.hpp>

#include "debug/death_test.h"

TEST_CASE("death test child entry point", "[.death-child]")
{
    vse::debug::RunDeathTestChildIfRequested();  // does not return in a child
    SUCCEED("not started as a death-test child");
}
