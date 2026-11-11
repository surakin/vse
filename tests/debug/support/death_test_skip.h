#pragma once

#include <catch2/catch_test_macros.hpp>

#include "debug/death_test.h"

// Skips the running Catch2 test, with a printed reason, on a platform that cannot start child processes.
#define ENGINE_REQUIRE_DEATH_TEST_SUPPORT()                                                                            \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!::engine_placeholder::debug::DeathTestsSupported())                                                       \
        {                                                                                                              \
            SKIP("this platform cannot start child processes, so death tests do not run here");                        \
        }                                                                                                              \
    } while (false)
