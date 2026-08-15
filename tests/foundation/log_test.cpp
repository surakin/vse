#include <fcntl.h>

#include <catch2/catch_test_macros.hpp>

#include "log/log.h"
#include "mem/mem.h"
#include "os/os.h"
#include "string/string.h"
#include "types.h"

using engine_placeholder::Log;
using engine_placeholder::LogChannel;
using engine_placeholder::LogCore;
using engine_placeholder::LogVerbosity;
using engine_placeholder::LoadConfig;
using engine_placeholder::SetExitSinkForTesting;
using engine_placeholder::SetWriteSinkForTesting;
using engine_placeholder::StringView;
using engine_placeholder::usize;
using engine_placeholder::os::Close;
using engine_placeholder::os::Open;
using engine_placeholder::os::Write;

namespace {

char g_capture_buffer[1024];
usize g_capture_len = 0;

long CapturingWriteSink(int fd, const void* buf, usize len) {
    (void)fd;
    usize copy_len = len;
    if (g_capture_len + copy_len > sizeof(g_capture_buffer)) {
        copy_len = sizeof(g_capture_buffer) - g_capture_len;
    }
    memcpy(g_capture_buffer + g_capture_len, buf, copy_len);
    g_capture_len += copy_len;
    return static_cast<long>(len);
}

// Exceptions are enabled for this test target (unlike the `foundation`
// production library, see /tests/foundation/CMakeLists.txt), so the
// exit sink can unwind the stack by throwing instead of the
// setjmp/longjmp trick production code without exceptions would need.
struct TestAssertExit {
    int code;
};

[[noreturn]] void TestExitSink(int code) {
    throw TestAssertExit{code};
}

} // namespace

TEST_CASE("Error-level messages emit by default; Info-level is suppressed", "[log]") {
    g_capture_len = 0;
    SetWriteSinkForTesting(&CapturingWriteSink);

    Log(LogCore, LogVerbosity::Error, "error visible by default");
    Log(LogCore, LogVerbosity::Info, "info hidden by default");

    SetWriteSinkForTesting(&Write);

    StringView captured(g_capture_buffer, g_capture_len);
    REQUIRE(captured.Contains(StringView("error visible by default")));
    REQUIRE_FALSE(captured.Contains(StringView("info hidden by default")));
}

TEST_CASE("ENGINE_ASSERT on a false condition exits with code 1 and logs a labeled message", "[log][assert]") {
    g_capture_len = 0;
    SetWriteSinkForTesting(&CapturingWriteSink);
    SetExitSinkForTesting(&TestExitSink);

    bool reached_after_assert = false;
    int exit_code = 0;
    try {
        ENGINE_ASSERT(false, "intentional test failure");
        reached_after_assert = true; // should never execute
    } catch (const TestAssertExit& e) {
        exit_code = e.code;
    }

    SetWriteSinkForTesting(&Write);
    SetExitSinkForTesting(&engine_placeholder::os::Exit);

    REQUIRE_FALSE(reached_after_assert);
    REQUIRE(exit_code == 1);

    StringView captured(g_capture_buffer, g_capture_len);
    REQUIRE(captured.Contains(StringView("intentional test failure")));
    REQUIRE(captured.Contains(StringView("ASSERT FAILED")));
}

TEST_CASE("LoadConfig overrides a channel's verbosity/enabled state from a file", "[log][config]") {
    static LogChannel test_channel("TestChannel", LogVerbosity::Warn);
    test_channel.SetVerbosity(LogVerbosity::Warn);
    test_channel.SetEnabled(true);

    const char* config_path = "/tmp/engine_test_log_config.txt";
    int fd = Open(config_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    const char content[] = "TestChannel.verbosity=Info\nTestChannel.enabled=true\n";
    Write(fd, content, sizeof(content) - 1);
    Close(fd);

    bool loaded = LoadConfig(config_path);
    REQUIRE(loaded);
    REQUIRE(test_channel.Verbosity() == LogVerbosity::Info);
    REQUIRE(test_channel.Enabled());

    g_capture_len = 0;
    SetWriteSinkForTesting(&CapturingWriteSink);
    Log(test_channel, LogVerbosity::Info, "emitted message");
    Log(test_channel, LogVerbosity::Debug, "suppressed message");
    SetWriteSinkForTesting(&Write);

    StringView captured(g_capture_buffer, g_capture_len);
    REQUIRE(captured.Contains(StringView("emitted message")));
    REQUIRE_FALSE(captured.Contains(StringView("suppressed message")));

    // Disable the channel entirely; even Error-level must be suppressed.
    test_channel.SetEnabled(false);
    g_capture_len = 0;
    SetWriteSinkForTesting(&CapturingWriteSink);
    Log(test_channel, LogVerbosity::Error, "should not appear when disabled");
    SetWriteSinkForTesting(&Write);

    StringView captured2(g_capture_buffer, g_capture_len);
    REQUIRE(captured2.Length() == 0);
}
