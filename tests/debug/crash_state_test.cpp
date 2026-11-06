// The ready-made crash state (debug/crash_state.h) and the error traces on the log (log/log.h).
#include <catch2/catch_test_macros.hpp>

#include "debug/crash_report.h"
#include "debug/crash_handler.h"
#include "debug/crash_state.h"
#include "debug/thread_info.h"
#include "log/log.h"
#include "os/os.h"

#include <cstring>
#include <string>

using namespace engine_placeholder;
using namespace engine_placeholder::debug;

namespace {

void StateSink(void* context, const char* data, usize length) {
    static_cast<std::string*>(context)->append(data, length);
}

std::string StateSection() {
    std::string text;
    CrashReportSink sink;
    sink.write = &StateSink;
    sink.context = &text;
    WriteCrashContext(sink);
    return text;
}

long CaptureWrite(int, const void* data, usize length);
std::string g_captured;
long CaptureWrite(int, const void* data, usize length) {
    g_captured.append(static_cast<const char*>(data), length);
    return static_cast<long>(length);
}

} // namespace

TEST_CASE("A text entry appears in the crash report and can be replaced", "[crashstate][debug]") {
    ResetCrashStateForTesting();
    // The table of entries is per process; use names no other test uses.
    REQUIRE(SetCrashStateText("test-gpu", "Test Adapter 9000, driver 1.2.3"));
    CHECK(StateSection().find("test-gpu: Test Adapter 9000, driver 1.2.3\n") != std::string::npos);
    REQUIRE(SetCrashStateText("test-gpu", "Replaced"));
    const std::string after = StateSection();
    CHECK(after.find("test-gpu: Replaced\n") != std::string::npos);
    CHECK(after.find("Test Adapter 9000") == std::string::npos);
    ResetCrashStateForTesting();
}

TEST_CASE("A counter entry shows its current value", "[crashstate][debug]") {
    ResetCrashStateForTesting();
    std::atomic<u64>* counter = CrashStateCounter("test-frame");
    REQUIRE(counter != nullptr);
    counter->store(0);
    CHECK(StateSection().find("test-frame: 0\n") != std::string::npos);
    counter->store(18446744073709551615ull);
    CHECK(StateSection().find("test-frame: 18446744073709551615\n") != std::string::npos);
    counter->store(41231);
    CHECK(StateSection().find("test-frame: 41231\n") != std::string::npos);
    CHECK(CrashStateCounter("test-frame") == counter); // the same entry on a second call
    ResetCrashStateForTesting();
}

TEST_CASE("A long text is cut, and a name cannot be both text and counter", "[crashstate][debug]") {
    ResetCrashStateForTesting();
    const std::string long_text(500, 'z');
    REQUIRE(SetCrashStateText("test-long", long_text.c_str()));
    const std::string section = StateSection();
    const usize at = section.find("test-long: ");
    REQUIRE(at != std::string::npos);
    CHECK(section.find('\n', at) - at - std::strlen("test-long: ") == kCrashStateEntryTextBytes - 1);
    CHECK(CrashStateCounter("test-long") == nullptr);
    CHECK_FALSE(SetCrashStateText(nullptr, "x"));
    CHECK(CrashStateCounter(nullptr) == nullptr);
    ResetCrashStateForTesting();
}

TEST_CASE("With error traces on, an Error line is followed by a trace and other lines are not", "[log][debug]") {
    static LogChannel channel("LogTraceTest", LogVerbosity::Info);
    SetWriteSinkForTesting(&CaptureWrite);
    g_captured.clear();
    CHECK_FALSE(LogErrorTraces());
    Log(channel, LogVerbosity::Error, "no trace please");
    const std::string plain = g_captured;
    g_captured.clear();
    SetLogErrorTraces(true);
    CHECK(LogErrorTraces());
    Log(channel, LogVerbosity::Warn, "a warning");
    Log(channel, LogVerbosity::Error, "with a trace");
    SetLogErrorTraces(false);
    const std::string traced = g_captured;
    SetWriteSinkForTesting(&os::Write);
    CHECK(plain == "[LogTraceTest] ERROR: no trace please\n");
    CHECK(traced.find("[LogTraceTest] WARN: a warning\n[LogTraceTest] ERROR: with a trace\n#0 ") != std::string::npos);
}

TEST_CASE("Registering a thread before the crash handler is installed sets its name and reports that nothing more was done", "[crash][debug]") {
    REQUIRE_FALSE(CrashHandlerInstalled());
    CHECK_FALSE(RegisterThreadForCrashHandling("early-thread"));
    CHECK(std::strcmp(CrashThreadName(), "early-thread") == 0);
    SetCrashThreadName("");
}

TEST_CASE("The name of a crash state entry is copied, so the caller's string may go away", "[crashstate][debug]") {
    ResetCrashStateForTesting();
    {
        char name[16];
        std::strcpy(name, "test-copied");
        REQUIRE(SetCrashStateText(name, "kept"));
        std::strcpy(name, "garbage!!!!");
    }
    const std::string section = StateSection();
    CHECK(section.find("test-copied: kept\n") != std::string::npos);
    CHECK(section.find("garbage") == std::string::npos);
    ResetCrashStateForTesting();
}
