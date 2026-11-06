#include <catch2/catch_test_macros.hpp>

#include "debug/crash_context.h"
#include "debug/crash_report.h"
#include "debug/death_test.h"
#include "debug/fault_injection.h"
#include "debug/stack_trace.h"
#include "debug/thread_info.h"
#include "log/log.h"
#include "os/os.h"
#include "support/death_test_skip.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace engine_placeholder;
using namespace engine_placeholder::debug;

namespace {

LogChannel LogReportTest("LogReportTest", LogVerbosity::Info);

long ReportTestDiscard(int, const void*, usize len) {
    return static_cast<long>(len);
}

// Keeps the log output of a test away from the terminal for the life of the scope.
class QuietLog {
public:
    QuietLog() { SetWriteSinkForTesting(&ReportTestDiscard); }
    ~QuietLog() { SetWriteSinkForTesting(&os::Write); }
};

struct Lines {
    std::vector<std::string> text;
};

void CollectLine(const char* line, usize length, void* context) {
    static_cast<Lines*>(context)->text.emplace_back(line, length);
}

std::vector<std::string> RecentLines(usize max_lines = 0) {
    Lines lines;
    ReadRecentLogLines(&CollectLine, &lines, max_lines);
    return lines.text;
}

void LogNumbered(const char* prefix, int n) {
    char text[64];
    std::snprintf(text, sizeof(text), "%s%d", prefix, n);
    Log(LogReportTest, LogVerbosity::Info, StringView(text));
}

// A report collected in memory, with the sizes of the pieces it arrived in.
struct Collected {
    std::string text;
    std::vector<usize> piece_sizes;
};

void CollectPiece(void* context, const char* data, usize length) {
    Collected* c = static_cast<Collected*>(context);
    c->text.append(data, length);
    c->piece_sizes.push_back(length);
}

CrashReportSink SinkFor(Collected* c) {
    CrashReportSink sink;
    sink.write = &CollectPiece;
    sink.context = c;
    return sink;
}

void GpuProvider(void*, char* out, usize capacity) {
    std::snprintf(out, capacity, "Test GPU 9000, driver 1.2.3");
}

void FrameProvider(void* context, char* out, usize capacity) {
    std::snprintf(out, capacity, "frame %d", *static_cast<int*>(context));
}

// Misbehaves in the harmless direction: asks for far more than it may write, using a bounded copy as the rules require.
void VerboseProvider(void*, char* out, usize capacity) {
    const std::string big(1000, 'x');
    std::snprintf(out, capacity, "%s", big.c_str());
}

// Finds `needle` at or after `from`; returns its position or npos.
usize FindAfter(const std::string& text, const char* needle, usize from) {
    return text.find(needle, from);
}

} // namespace

TEST_CASE("The crash context describes the program and the machine", "[crashreport][debug]") {
    const char* argv[] = {"debug_tests", "--fake-flag", "value"};
    InitCrashContext(3, argv);
    REQUIRE(CrashContextInitialized());
    const CrashContext& c = GetCrashContext();
    CHECK(c.executable[0] != '\0');
    CHECK(c.os_name[0] != '\0');
    CHECK(c.os_version[0] != '\0');
    CHECK(c.architecture[0] != '\0');
    CHECK(c.engine_version[0] != '\0');
    CHECK(c.cpu_count > 0);
    CHECK(c.total_memory_bytes > 64ull * 1024 * 1024);
    CHECK(c.start_time_unix > 1700000000ull); // after November 2023
    // The build id is lowercase hex (even number of digits) when the binary has one.
    const usize id_length = std::strlen(c.build_id);
    CHECK(id_length % 2 == 0);
    for (usize i = 0; i < id_length; ++i) {
        CHECK(((c.build_id[i] >= '0' && c.build_id[i] <= '9') || (c.build_id[i] >= 'a' && c.build_id[i] <= 'f')));
    }
    if (std::strstr(c.command_line, "--fake-flag") == nullptr) {
        // Another test initialized the context first, without a command line.
        CHECK(c.command_line[0] == '\0');
    } else {
        CHECK(std::string(c.command_line) == "debug_tests --fake-flag value");
    }
    // A second call changes nothing.
    const std::string before = c.command_line;
    const char* other[] = {"different"};
    InitCrashContext(1, other);
    if (!before.empty()) {
        CHECK(before == c.command_line);
    }
}

ENGINE_DEATH_TEST_FN(dt_report_late_command_line) {
    InitCrashContext(0, nullptr); // as the crash handler or an assert does when the program has not
    const char* argv[] = {"prog", "--late"};
    InitCrashContext(2, argv);
    const char* other[] = {"other"};
    InitCrashContext(1, other); // does not replace a command line already there
    Collected report;
    WriteCrashContext(SinkFor(&report));
    os::Write(1, report.text.data(), report.text.size());
}

TEST_CASE("A command line given after an initialization without one is kept, once", "[crashreport][death][debug]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    const ChildResult r = RunInChild("dt_report_late_command_line");
    REQUIRE(r.exited);
    CHECK(r.StdoutContains("command line: prog --late\n"));
    CHECK_FALSE(r.StdoutContains("other"));
}

namespace {
void ReentrantProvider(void*, char* out, usize capacity) {
    // As if a check inside a provider failed and the assert handler wrote the context again.
    Collected inner;
    WriteCrashContext(SinkFor(&inner));
    std::snprintf(out, capacity, "inner report had %s", inner.text.find("--- state ---") != std::string::npos ? "state header" : "nothing");
}
} // namespace

TEST_CASE("A provider that writes a report itself does not recurse", "[crashreport][debug]") {
    ResetCrashStateProvidersForTesting();
    RegisterCrashStateProvider("again", &ReentrantProvider, nullptr);
    Collected report;
    WriteCrashContext(SinkFor(&report));
    ResetCrashStateProvidersForTesting();
    CHECK(report.text.find("again: inner report had state header\n") != std::string::npos);
}

TEST_CASE("The ring returns the newest 256 lines, oldest first", "[crashreport][log][debug]") {
    QuietLog quiet;
    ResetLogRingForTesting();
    for (int i = 0; i < 1000; ++i) {
        LogNumbered("ring-line-", i);
    }
    const std::vector<std::string> lines = RecentLines();
    REQUIRE(lines.size() == kLogRingSlots);
    for (usize i = 0; i < lines.size(); ++i) {
        const std::string expected = "ring-line-" + std::to_string(1000 - kLogRingSlots + i) + "\n";
        INFO(lines[i]);
        REQUIRE(lines[i].find(expected) != std::string::npos);
    }
    // Only the newest few.
    const std::vector<std::string> newest = RecentLines(3);
    REQUIRE(newest.size() == 3);
    CHECK(newest[0].find("ring-line-997") != std::string::npos);
    CHECK(newest[2].find("ring-line-999") != std::string::npos);
}

TEST_CASE("With fewer lines than slots the ring returns exactly those", "[crashreport][log][debug]") {
    QuietLog quiet;
    ResetLogRingForTesting();
    CHECK(RecentLines().empty());
    for (int i = 0; i < 10; ++i) {
        LogNumbered("few-", i);
    }
    const std::vector<std::string> lines = RecentLines();
    REQUIRE(lines.size() == 10);
    CHECK(lines.front().find("few-0\n") != std::string::npos);
    CHECK(lines.back().find("few-9\n") != std::string::npos);
}

TEST_CASE("A line longer than a slot is cut to the slot size", "[crashreport][log][debug]") {
    QuietLog quiet;
    ResetLogRingForTesting();
    Log(LogReportTest, LogVerbosity::Info, StringView(std::string(2000, 'q').c_str()));
    const std::vector<std::string> lines = RecentLines();
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].size() == kLogRingLineBytes);
}

TEST_CASE("Lines suppressed by the channel's verbosity are not kept", "[crashreport][log][debug]") {
    QuietLog quiet;
    ResetLogRingForTesting();
    Log(LogReportTest, LogVerbosity::Trace, "too detailed");
    CHECK(RecentLines().empty());
}

TEST_CASE("Eight threads logging while another reads: every line delivered is whole and each thread's lines stay in order", "[crashreport][log][debug]") {
    QuietLog quiet;
    ResetLogRingForTesting();
    std::atomic<bool> stop{false};
    std::atomic<int> bad{0};
    std::mutex bad_mutex;
    std::string first_bad;
    std::atomic<long> delivered{0};
    std::atomic<long> passes{0};
    std::thread reader([&] {
        while (!stop.load()) {
            const std::vector<std::string> lines = RecentLines();
            std::vector<int> last(8, -1);
            for (const std::string& line : lines) {
                // "[LogReportTest] INFO: T<thread>-<n>\n"
                const std::size_t t = line.find("INFO: T");
                int thread_id = -1;
                int n = -1;
                if (t == std::string::npos || std::sscanf(line.c_str() + t, "INFO: T%d-%d", &thread_id, &n) != 2 || thread_id < 0 ||
                    thread_id > 7 || line.back() != '\n') {
                    bad.fetch_add(1);
                    std::lock_guard<std::mutex> lock(bad_mutex);
                    if (first_bad.empty()) {
                        first_bad = "malformed: [" + line + "]";
                    }
                    continue;
                }
                if (n <= last[static_cast<usize>(thread_id)]) {
                    bad.fetch_add(1); // a thread's own lines went backwards
                    std::lock_guard<std::mutex> lock(bad_mutex);
                    if (first_bad.empty()) {
                        first_bad = "backwards: thread " + std::to_string(thread_id) + " n=" + std::to_string(n) + " after " +
                                    std::to_string(last[static_cast<usize>(thread_id)]);
                    }
                }
                last[static_cast<usize>(thread_id)] = n;
                delivered.fetch_add(1);
            }
            passes.fetch_add(1);
            std::this_thread::yield();
        }
    });
    while (passes.load() == 0) {
        std::this_thread::yield(); // the writers start once the reader is running, so the two overlap
    }
    std::thread writers[8];
    for (int t = 0; t < 8; ++t) {
        writers[t] = std::thread([t] {
            for (int n = 0; n < 2000; ++n) {
                char text[32];
                std::snprintf(text, sizeof(text), "T%d-%d", t, n);
                Log(LogReportTest, LogVerbosity::Info, StringView(text));
            }
        });
    }
    for (std::thread& w : writers) {
        w.join();
    }
    stop = true;
    reader.join();
    INFO(first_bad);
    CHECK(bad.load() == 0);
    CHECK(passes.load() > 0);
    // At rest: the ring holds the last 256 lines, all whole.
    const std::vector<std::string> lines = RecentLines();
    // A writer descheduled in the middle of its slot and then lapped by 256 newer lines leaves that one slot skipped, so
    // allow a few missing; what is delivered must still be whole.
    CHECK(lines.size() >= kLogRingSlots - 8);
    CHECK(lines.size() <= kLogRingSlots);
    for (const std::string& line : lines) {
        int t = -1;
        int n = -1;
        CHECK(std::sscanf(std::strstr(line.c_str(), "INFO: T"), "INFO: T%d-%d", &t, &n) == 2);
    }
}

TEST_CASE("State providers appear in the report in the order they were registered", "[crashreport][debug]") {
    ResetCrashStateProvidersForTesting();
    int frame = 41;
    CHECK(RegisterCrashStateProvider("gpu", &GpuProvider, nullptr));
    CHECK(RegisterCrashStateProvider("frame", &FrameProvider, &frame));
    Collected report;
    WriteCrashContext(SinkFor(&report));
    const usize state = report.text.find("--- state ---");
    REQUIRE(state != std::string::npos);
    const usize gpu = FindAfter(report.text, "gpu: Test GPU 9000, driver 1.2.3\n", state);
    const usize frame_line = FindAfter(report.text, "frame: frame 41\n", state);
    REQUIRE(gpu != std::string::npos);
    REQUIRE(frame_line != std::string::npos);
    CHECK(gpu < frame_line);
    frame = 42; // a provider reads live state each time
    Collected again;
    WriteCrashContext(SinkFor(&again));
    CHECK(again.text.find("frame: frame 42\n") != std::string::npos);
    ResetCrashStateProvidersForTesting();
}

TEST_CASE("The provider table holds sixteen and refuses nulls", "[crashreport][debug]") {
    ResetCrashStateProvidersForTesting();
    CHECK_FALSE(RegisterCrashStateProvider(nullptr, &GpuProvider, nullptr));
    CHECK_FALSE(RegisterCrashStateProvider("x", nullptr, nullptr));
    for (usize i = 0; i < kMaxCrashStateProviders; ++i) {
        CHECK(RegisterCrashStateProvider("p", &GpuProvider, nullptr));
    }
    CHECK_FALSE(RegisterCrashStateProvider("one too many", &GpuProvider, nullptr));
    ResetCrashStateProvidersForTesting();
}

TEST_CASE("A provider's text is cut to its buffer", "[crashreport][debug]") {
    ResetCrashStateProvidersForTesting();
    CHECK(RegisterCrashStateProvider("big", &VerboseProvider, nullptr));
    Collected report;
    WriteCrashContext(SinkFor(&report));
    ResetCrashStateProvidersForTesting();
    const usize at = report.text.find("big: ");
    REQUIRE(at != std::string::npos);
    const usize end = report.text.find('\n', at);
    REQUIRE(end != std::string::npos);
    CHECK(end - at - 5 == kCrashStateTextBytes - 1);
}

TEST_CASE("A crash report has every section, in order", "[crashreport][debug]") {
    InitDebugFacilities();
    InitCrashContext(0, nullptr);
    QuietLog quiet;
    ResetLogRingForTesting();
    ResetCrashStateProvidersForTesting();
    RegisterCrashStateProvider("gpu", &GpuProvider, nullptr);
    LogNumbered("before-the-crash-", 1);
    SetCrashThreadName("report-tester");

    CrashReportInfo info;
    info.reason = CrashReason::AccessViolation;
    info.has_fault_address = true;
    info.fault_address = 0x10;
    StackTrace trace;
    CaptureStackTrace(trace);
    info.trace = &trace;
    Collected report;
    WriteCrashReport(SinkFor(&report), info);
    SetCrashThreadName(nullptr);
    ResetCrashStateProvidersForTesting();

    const char* markers[] = {
        "=== CRASH REPORT ===\n",
        "reason: AccessViolation at 0x0000000000000010\n",
        "thread: ",
        "\"report-tester\"\n",
        "--- stack trace ---\n",
        "#0  0x",
        "--- build ---\n",
        "build id: ",
        "--- platform ---\n",
        "os: ",
        "cpu: ",
        "--- state ---\n",
        "gpu: Test GPU 9000, driver 1.2.3\n",
        "--- recent log (oldest first, last 32) ---\n",
        "before-the-crash-1\n",
        "=== END OF REPORT ===\n",
    };
    usize at = 0;
    for (const char* marker : markers) {
        INFO(marker);
        const usize found = report.text.find(marker, at);
        REQUIRE(found != std::string::npos);
        at = found;
    }
    // The report arrives in pieces, none larger than the writer's buffer.
    CHECK(report.piece_sizes.size() > 10);
    for (usize size : report.piece_sizes) {
        CHECK(size <= 512);
    }
}

TEST_CASE("A report for an assertion names the message instead of an address", "[crashreport][debug]") {
    CrashReportInfo info;
    info.reason = CrashReason::AssertFailed;
    info.message = "it broke";
    Collected report;
    WriteCrashReport(SinkFor(&report), info);
    CHECK(report.text.find("reason: AssertFailed: it broke\n") != std::string::npos);
    CHECK(report.text.find(" at 0x") == std::string::npos);
}

TEST_CASE("A report with no sink function writes nothing and does not crash", "[crashreport][debug]") {
    CrashReportInfo info;
    WriteCrashReport(CrashReportSink(), info);
    WriteCrashContext(CrashReportSink());
    SUCCEED();
}

// ---- assert and ensure output -----------------------------------------------------------------------------------------------

ENGINE_DEATH_TEST_FN(dt_report_assert_with_context) {
    InitCrashContext(0, nullptr);
    RegisterCrashStateProvider("gpu", &GpuProvider, nullptr);
    Log(LogReportTest, LogVerbosity::Info, "logged-before-the-assert");
    ENGINE_ASSERT(false, "context please");
}

ENGINE_DEATH_TEST_FN(dt_report_ensure_without_context) {
    InitCrashContext(0, nullptr);
    Log(LogReportTest, LogVerbosity::Info, "logged-before-the-ensure");
    ENGINE_ENSURE(false, "just a note");
}

TEST_CASE("A failed assert prints the same context sections as a crash report", "[crashreport][assert][death][debug]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    const ChildResult r = RunInChild("dt_report_assert_with_context");
    CHECK(r.reason == CrashReason::Abort);
    CHECK(r.StdoutContains("ASSERT FAILED: "));
    CHECK(r.StdoutContains("--- build ---"));
    CHECK(r.StdoutContains("build id: "));
    CHECK(r.StdoutContains("--- platform ---"));
    CHECK(r.StdoutContains("--- state ---"));
    CHECK(r.StdoutContains("gpu: Test GPU 9000, driver 1.2.3"));
    CHECK(r.StdoutContains("--- recent log"));
    CHECK(r.StdoutContains("logged-before-the-assert"));
}

TEST_CASE("A failed ensure prints only its message and trace, since the program goes on", "[crashreport][ensure][death][debug]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    const ChildResult r = RunInChild("dt_report_ensure_without_context");
    REQUIRE(r.exited);
    CHECK(r.StdoutContains("ENSURE FAILED: "));
    CHECK_FALSE(r.StdoutContains("--- build ---"));
    CHECK_FALSE(r.StdoutContains("--- recent log"));
}

// ---- a provider that faults ---------------------------------------------------------------------------------------------------

namespace {

void FaultingProvider(void*, char*, usize) {
    InjectFault(FaultKind::NullWrite);
}

void StdoutPiece(void*, const char* data, usize length) {
    os::Write(1, data, length);
}

} // namespace

ENGINE_DEATH_TEST_FN(dt_report_provider_faults) {
    InitCrashContext(0, nullptr);
    RegisterCrashStateProvider("bad", &FaultingProvider, nullptr);
    CrashReportInfo info;
    info.reason = CrashReason::Abort;
    CrashReportSink sink;
    sink.write = &StdoutPiece;
    WriteCrashReport(sink, info);
}

TEST_CASE("A provider that faults loses the rest of the report but not the part already written", "[crashreport][death][debug]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    const ChildResult r = RunInChild("dt_report_provider_faults");
    CHECK(r.terminated);
    CHECK(r.reason == CrashReason::AccessViolation);
    CHECK(r.StdoutContains("=== CRASH REPORT ==="));
    CHECK(r.StdoutContains("--- build ---"));
    CHECK(r.StdoutContains("--- state ---"));
    CHECK_FALSE(r.StdoutContains("=== END OF REPORT ==="));
}

TEST_CASE("The build id in the crash context is the one the binary carries", "[crashreport][debug][linux]") {
    InitCrashContext(0, nullptr);
    const CrashContext& c = GetCrashContext();
    REQUIRE(c.executable[0] != '\0');
    const std::string command = std::string("readelf -n '") + c.executable + "' 2>/dev/null";
    FILE* pipe = popen(command.c_str(), "r");
    REQUIRE(pipe != nullptr);
    std::string output;
    char buffer[256];
    while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
    pclose(pipe);
    const std::string key = "Build ID: ";
    const usize at = output.find(key);
    REQUIRE(at != std::string::npos);
    usize end = output.find('\n', at);
    const std::string expected = output.substr(at + key.size(), end - at - key.size());
    CHECK(std::string(c.build_id) == expected);
}
