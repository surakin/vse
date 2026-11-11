#include <fcntl.h>

#include <catch2/catch_test_macros.hpp>

#include "log/log.h"
#include "os/os.h"
#include "string/string.h"
#include "types.h"
#include "cstr/mem_ops.h"

using engine_placeholder::AssertAction;
using engine_placeholder::AssertHandler;
using engine_placeholder::AssertInfo;
using engine_placeholder::Log;
using engine_placeholder::LogChannel;
using engine_placeholder::LogCore;
using engine_placeholder::LogVerbosity;
using engine_placeholder::SetAssertHandler;
using engine_placeholder::SetWriteSinkForTesting;
using engine_placeholder::StringView;
using engine_placeholder::usize;
using engine_placeholder::os::Close;
using engine_placeholder::os::Open;
using engine_placeholder::os::Write;

namespace
{

char g_capture_buffer[1024];
usize g_capture_len = 0;

long CapturingWriteSink(int fd, const void* buf, usize len)
{
    (void)fd;
    usize copy_len = len;
    if (g_capture_len + copy_len > sizeof(g_capture_buffer))
    {
        copy_len = sizeof(g_capture_buffer) - g_capture_len;
    }
    engine_placeholder::MemCopy(g_capture_buffer + g_capture_len, buf, copy_len);
    g_capture_len += copy_len;
    return static_cast<long>(len);
}

// Exceptions are enabled for this test target (unlike the `foundation`
// production library, see /tests/foundation/CMakeLists.txt), so the
// assert handler can unwind the stack by throwing instead of aborting.
struct TestAssertFailure
{
    int line;
};

// Prints the usual line through the default handler (so the output is captured), then unwinds.
AssertAction ThrowAfterPrinting(const AssertInfo& info)
{
    engine_placeholder::DefaultAssertHandler(info);
    throw TestAssertFailure{info.line};
}

}  // namespace

TEST_CASE("Error-level messages emit by default; Info-level is suppressed", "[log]")
{
    g_capture_len = 0;
    SetWriteSinkForTesting(&CapturingWriteSink);

    Log(LogCore, LogVerbosity::Error, "error visible by default");
    Log(LogCore, LogVerbosity::Info, "info hidden by default");

    SetWriteSinkForTesting(&Write);

    StringView captured(g_capture_buffer, g_capture_len);
    REQUIRE(captured.Contains(StringView("error visible by default")));
    REQUIRE_FALSE(captured.Contains(StringView("info hidden by default")));
}

TEST_CASE("ENGINE_ASSERT on a false condition reports through the handler and logs a labeled message", "[log][assert]")
{
    g_capture_len = 0;
    SetWriteSinkForTesting(&CapturingWriteSink);
    const AssertHandler previous = SetAssertHandler(&ThrowAfterPrinting);

    bool reached_after_assert = false;
    int reported_line = 0;
    try
    {
        ENGINE_ASSERT(false, "intentional test failure");
        reached_after_assert = true;  // should never execute
    }
    catch (const TestAssertFailure& e)
    {
        reported_line = e.line;
    }

    SetAssertHandler(previous);
    SetWriteSinkForTesting(&Write);

    REQUIRE_FALSE(reached_after_assert);
    REQUIRE(reported_line > 0);

    StringView captured(g_capture_buffer, g_capture_len);
    REQUIRE(captured.Contains(StringView("intentional test failure")));
    REQUIRE(captured.Contains(StringView("ASSERT FAILED")));
}
