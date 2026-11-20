// The ready-made crash state (debug/crash_state.h) and the error traces on the log (log/log.h).
#include <catch2/catch_test_macros.hpp>

#include "debug/crash_report.h"
#include "debug/crash_handler.h"
#include "debug/crash_state.h"
#include "debug/thread_info.h"
#include "log/log.h"
#include "os/os.h"

#include "sync/atomic.h"
#include "cstr/cstr.h"
#include "catch_engine_types.h"
#include "test_text.h"

using namespace vse;
using namespace vse::debug;
using engine_test::Text;

namespace
{

void StateSink(void* context, const char* data, usize length)
{
    static_cast<Text*>(context)->AppendRawBytes(data, length);
}

Text StateSection()
{
    Text text = engine_test::MakeText();
    CrashReportSink sink;
    sink.write = &StateSink;
    sink.context = &text;
    WriteCrashContext(sink);
    return text;
}

long CaptureWrite(int, const void* data, usize length);
Text g_captured = engine_test::MakeText();
long CaptureWrite(int, const void* data, usize length)
{
    g_captured.AppendRawBytes(static_cast<const char*>(data), length);
    return static_cast<long>(length);
}

}  // namespace

TEST_CASE("A text entry appears in the crash report and can be replaced", "[crashstate][debug]")
{
    ResetCrashStateForTesting();
    // The table of entries is per process; use names no other test uses.
    REQUIRE(SetCrashStateText("test-gpu", "Test Adapter 9000, driver 1.2.3"));
    CHECK(StateSection().View().Contains("test-gpu: Test Adapter 9000, driver 1.2.3\n"));
    REQUIRE(SetCrashStateText("test-gpu", "Replaced"));
    const Text after = StateSection();
    CHECK(after.View().Contains("test-gpu: Replaced\n"));
    CHECK_FALSE(after.View().Contains("Test Adapter 9000"));
    ResetCrashStateForTesting();
}

TEST_CASE("A counter entry shows its current value", "[crashstate][debug]")
{
    ResetCrashStateForTesting();
    Atomic<u64>* counter = CrashStateCounter("test-frame");
    REQUIRE(counter != nullptr);
    counter->Store(0);
    CHECK(StateSection().View().Contains("test-frame: 0\n"));
    counter->Store(18446744073709551615ull);
    CHECK(StateSection().View().Contains("test-frame: 18446744073709551615\n"));
    counter->Store(41231);
    CHECK(StateSection().View().Contains("test-frame: 41231\n"));
    CHECK(CrashStateCounter("test-frame") == counter);  // the same entry on a second call
    ResetCrashStateForTesting();
}

TEST_CASE("A long text is cut, and a name cannot be both text and counter", "[crashstate][debug]")
{
    ResetCrashStateForTesting();
    const Text long_text = engine_test::Repeated(500, 'z');
    REQUIRE(SetCrashStateText("test-long", long_text.CStr()));
    const Text section = StateSection();
    const usize at = section.View().Find("test-long: ");
    REQUIRE(at != section.Length());
    const usize line_end = at + section.View().Substring(at).IndexOf('\n');
    CHECK(line_end - at - CStrLength("test-long: ") == kCrashStateEntryTextBytes - 1);
    CHECK(CrashStateCounter("test-long") == nullptr);
    CHECK_FALSE(SetCrashStateText(nullptr, "x"));
    CHECK(CrashStateCounter(nullptr) == nullptr);
    ResetCrashStateForTesting();
}

TEST_CASE("With error traces on, an Error line is followed by a trace and other lines are not", "[log][debug]")
{
    static LogChannel channel("LogTraceTest", LogVerbosity::Info);
    SetWriteSinkForTesting(&CaptureWrite);
    g_captured = engine_test::MakeText();
    CHECK_FALSE(LogErrorTraces());
    Log(channel, LogVerbosity::Error, "no trace please");
    const Text plain = engine_test::MakeText(g_captured.View());
    g_captured = engine_test::MakeText();
    SetLogErrorTraces(true);
    CHECK(LogErrorTraces());
    Log(channel, LogVerbosity::Warn, "a warning");
    Log(channel, LogVerbosity::Error, "with a trace");
    SetLogErrorTraces(false);
    const Text traced = engine_test::MakeText(g_captured.View());
    SetWriteSinkForTesting(&os::Write);
    CHECK(plain == "[LogTraceTest] ERROR: no trace please\n");
    CHECK(traced.View().Contains("[LogTraceTest] WARN: a warning\n[LogTraceTest] ERROR: with a trace\n#0 "));
}

TEST_CASE(
    "Registering a thread before the crash handler is installed sets its name and reports that nothing more was done",
    "[crash][debug]")
{
    REQUIRE_FALSE(CrashHandlerInstalled());
    CHECK_FALSE(RegisterThreadForCrashHandling("early-thread"));
    CHECK(CStrCompare(CrashThreadName(), "early-thread") == 0);
    SetCrashThreadName("");
}

TEST_CASE("The name of a crash state entry is copied, so the caller's string may go away", "[crashstate][debug]")
{
    ResetCrashStateForTesting();
    {
        char name[16];
        CStrCopy(name, "test-copied");
        REQUIRE(SetCrashStateText(name, "kept"));
        CStrCopy(name, "garbage!!!!");
    }
    const Text section = StateSection();
    CHECK(section.View().Contains("test-copied: kept\n"));
    CHECK_FALSE(section.View().Contains("garbage"));
    ResetCrashStateForTesting();
}

TEST_CASE("A log message longer than the line buffer is cut but the line still ends with a newline", "[log][debug]")
{
    static LogChannel channel("LogLongTest", LogVerbosity::Info);
    SetWriteSinkForTesting(&CaptureWrite);
    g_captured = engine_test::MakeText();
    const Text long_message = engine_test::Repeated(2000, 'm');
    Log(channel, LogVerbosity::Info, long_message.View());
    Log(channel, LogVerbosity::Info, "next");
    SetWriteSinkForTesting(&os::Write);
    const usize first_end = g_captured.View().IndexOf('\n');
    REQUIRE(first_end != g_captured.Length());
    CHECK(first_end == 511);  // 511 bytes of text, then the newline
    CHECK(g_captured.View().Substring(first_end + 1) == "[LogLongTest] INFO: next\n");  // not glued onto the long line
}
