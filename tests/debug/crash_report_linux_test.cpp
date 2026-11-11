// Linux-only checks of the crash context: the values come from /proc and from the binary (readelf).
#include <catch2/catch_test_macros.hpp>

#include "debug/crash_context.h"
#include "catch_engine_types.h"
#include "test_text.h"

#include "cstr/cstr.h"
#include "support/test_support_posix.h"

using namespace engine_placeholder;
using namespace engine_placeholder::debug;

TEST_CASE("The CPU brand is read from /proc/cpuinfo", "[crashreport][debug][linux]") {
    InitCrashContext(0, nullptr);
    // The file reports a size of zero, so it is read through the descriptor primitives (sequential reads to the end).
    const int fd = os::Open("/proc/cpuinfo", O_RDONLY);
    REQUIRE(fd >= 0);
    engine_test::Text cpuinfo = engine_test::MakeText();
    char chunk[4096];
    for (long n = os::Read(fd, chunk, sizeof(chunk)); n > 0; n = os::Read(fd, chunk, sizeof(chunk))) {
        cpuinfo.AppendRawBytes(chunk, static_cast<usize>(n));
    }
    os::Close(fd);
    // A line that starts with "model name".
    const bool has_model_name = cpuinfo.View().StartsWith("model name") || cpuinfo.View().Contains("\nmodel name");
    if (has_model_name) {
        CHECK(GetCrashContext().cpu_brand[0] != '\0');
    }
}

TEST_CASE("The build id in the crash context is the one the binary carries", "[crashreport][debug][linux]") {
    InitCrashContext(0, nullptr);
    const CrashContext& c = GetCrashContext();
    REQUIRE(c.executable[0] != '\0');
    const engine_test::Text command = engine_test::Format("readelf -n '%s' 2>/dev/null", c.executable);
    const debug_test::ShellRun run = debug_test::RunShell(command.View());
    const engine_test::Text& output = run.output;
    const StringView key("Build ID: ");
    const StringView text = output.View();
    const usize at = text.Find(key);
    REQUIRE(at != text.Length());
    const StringView rest = text.Substring(at + key.Length());
    const usize end = rest.IndexOf('\n');
    const StringView expected = rest.Substring(0, end); // IndexOf gives Length() when there is no newline
    CHECK(StringView(c.build_id) == expected);
}
