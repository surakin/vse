// Linux-only checks of the crash context: the values come from /proc.
#include <catch2/catch_test_macros.hpp>

#include "debug/crash_context.h"

#include <cstdio>
#include <cstring>

using namespace engine_placeholder;
using namespace engine_placeholder::debug;

TEST_CASE("The CPU brand is read from /proc/cpuinfo", "[crashreport][debug][linux]") {
    InitCrashContext(0, nullptr);
    FILE* f = std::fopen("/proc/cpuinfo", "r");
    REQUIRE(f != nullptr);
    char line[512];
    bool has_model_name = false;
    while (std::fgets(line, sizeof(line), f) != nullptr) {
        if (std::strncmp(line, "model name", 10) == 0) {
            has_model_name = true;
        }
    }
    std::fclose(f);
    if (has_model_name) {
        CHECK(GetCrashContext().cpu_brand[0] != '\0');
    }
}

