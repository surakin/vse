// Linux-only checks of the crash context: the values come from /proc and from the binary (readelf).
#include <catch2/catch_test_macros.hpp>

#include "debug/crash_context.h"
#include "catch_engine_types.h"
#include "test_text.h"

#include <cstdio>
#include "cstr/cstr.h"

using namespace engine_placeholder;
using namespace engine_placeholder::debug;

TEST_CASE("The CPU brand is read from /proc/cpuinfo", "[crashreport][debug][linux]") {
    InitCrashContext(0, nullptr);
    FILE* f = std::fopen("/proc/cpuinfo", "r");
    REQUIRE(f != nullptr);
    char line[512];
    bool has_model_name = false;
    while (std::fgets(line, sizeof(line), f) != nullptr) {
        if (CStrCompareN(line, "model name", 10) == 0) {
            has_model_name = true;
        }
    }
    std::fclose(f);
    if (has_model_name) {
        CHECK(GetCrashContext().cpu_brand[0] != '\0');
    }
}

TEST_CASE("The build id in the crash context is the one the binary carries", "[crashreport][debug][linux]") {
    InitCrashContext(0, nullptr);
    const CrashContext& c = GetCrashContext();
    REQUIRE(c.executable[0] != '\0');
    const engine_test::Text command = engine_test::Format("readelf -n '%s' 2>/dev/null", c.executable);
    FILE* pipe = popen(command.CStr(), "r");
    REQUIRE(pipe != nullptr);
    engine_test::Text output = engine_test::MakeText();
    char buffer[256];
    while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output.Append(StringView(buffer));
    }
    pclose(pipe);
    const StringView key("Build ID: ");
    const StringView text = output.View();
    const usize at = text.Find(key);
    REQUIRE(at != text.Length());
    const StringView rest = text.Substring(at + key.Length());
    const usize end = rest.IndexOf('\n');
    const StringView expected = rest.Substring(0, end); // IndexOf gives Length() when there is no newline
    CHECK(StringView(c.build_id) == expected);
}
