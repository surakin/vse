#include <catch2/catch_test_macros.hpp>

#include "containers/vector.h"
#include "cstr/mem_ops.h"
#include "log/diagnostics_hooks.h"
#include "log/log.h"
#include "log/log_device.h"
#include "log/tty_log_device.h"
#include "os/os.h"
#include "capture_log_device.h"
#include "string_view.h"
#include "sync/atomic.h"
#include "sync/mutex.h"
#include "sync/thread.h"
#include "types.h"

using namespace vse;
using engine_test::CaptureLogDevice;
using engine_test::Seen;
using engine_test::Text;

namespace
{

char g_sink[8192];
usize g_sink_length = 0;

long SinkWrite(int, const void* buf, usize len)
{
    const usize room = sizeof(g_sink) - g_sink_length;
    const usize n = len < room ? len : room;
    MemCopy(g_sink + g_sink_length, buf, n);
    g_sink_length += n;
    return static_cast<long>(len);
}

StringView Sink()
{
    return StringView(g_sink, g_sink_length);
}

// Sends the console to a buffer and back, whatever the test does.
struct SinkGuard
{
    SinkGuard()
    {
        g_sink_length = 0;
        SetWriteSinkForTesting(&SinkWrite);
        SetConsoleLogMode(ConsoleLogMode::Plain);
    }
    ~SinkGuard()
    {
        SetWriteSinkForTesting(&os::Write);
        SetConsoleLogMode(ConsoleLogMode::Auto);
    }
};

LogChannel TestChannel("DeviceTest", LogVerbosity::Trace);

}  // namespace

TEST_CASE("a line reaches every device whose level admits it", "[log][device]")
{
    SinkGuard guard;
    CaptureLogDevice everything;
    CaptureLogDevice errors_only;
    REQUIRE(AddLogDevice(&everything));
    REQUIRE(AddLogDevice(&errors_only, LogVerbosity::Error));

    Log(TestChannel, LogVerbosity::Info, "hello");
    Log(TestChannel, LogVerbosity::Error, "broken");

    CHECK(everything.Count() == 2);
    REQUIRE(errors_only.Count() == 1);
    CHECK(StringView(errors_only.At(0).message.View()) == "broken");
    CHECK(errors_only.At(0).level == LogVerbosity::Error);

    RemoveLogDevice(&everything);
    RemoveLogDevice(&errors_only);
}

TEST_CASE("the record carries the channel, message, line and a timestamp", "[log][device]")
{
    SinkGuard guard;
    CaptureLogDevice device;
    REQUIRE(AddLogDevice(&device));
    const u64 before = os::MonotonicTimeNanos();
    Log(TestChannel, LogVerbosity::Warn, "careful");
    const u64 after = os::MonotonicTimeNanos();
    RemoveLogDevice(&device);

    REQUIRE(device.Count() == 1);
    const Seen& s = device.At(0);
    CHECK(s.channel.View() == "DeviceTest");
    CHECK(s.message.View() == "careful");
    CHECK(s.line.View() == "[DeviceTest] WARN: careful\n");
    CHECK(s.trace.View().Length() == 0);
    CHECK(s.timestamp >= before);
    CHECK(s.timestamp <= after);
}

TEST_CASE("a channel that filters a line keeps it from the devices", "[log][device]")
{
    SinkGuard guard;
    CaptureLogDevice device;
    REQUIRE(AddLogDevice(&device));
    LogChannel quiet("DeviceQuiet", LogVerbosity::Warn);
    Log(quiet, LogVerbosity::Info, "dropped");
    CHECK(device.Count() == 0);
    CHECK(Sink().Length() == 0);
    RemoveLogDevice(&device);
}

TEST_CASE("the registry refuses a null, a duplicate and a ninth device", "[log][device]")
{
    SinkGuard guard;
    CHECK_FALSE(AddLogDevice(nullptr));
    CaptureLogDevice devices[kMaxLogDevices + 1];
    for (usize i = 0; i < kMaxLogDevices; ++i)
    {
        REQUIRE(AddLogDevice(&devices[i]));
    }
    CHECK_FALSE(AddLogDevice(&devices[kMaxLogDevices]));
    CHECK_FALSE(AddLogDevice(&devices[0]));
    RemoveLogDevice(&devices[3]);
    CHECK(AddLogDevice(&devices[kMaxLogDevices]));  // the freed slot is reused
    for (CaptureLogDevice& d : devices)
    {
        RemoveLogDevice(&d);
    }
    RemoveLogDevice(&devices[0]);  // not added: nothing happens
}

TEST_CASE("a removed device receives nothing more", "[log][device]")
{
    SinkGuard guard;
    CaptureLogDevice device;
    REQUIRE(AddLogDevice(&device));
    Log(TestChannel, LogVerbosity::Info, "one");
    RemoveLogDevice(&device);
    Log(TestChannel, LogVerbosity::Info, "two");
    CHECK(device.Count() == 1);
}

TEST_CASE("FlushLogDevices flushes every added device", "[log][device]")
{
    SinkGuard guard;
    CaptureLogDevice a;
    CaptureLogDevice b;
    REQUIRE(AddLogDevice(&a));
    REQUIRE(AddLogDevice(&b));
    FlushLogDevices();
    CHECK(a.flushes_ == 1);
    CHECK(b.flushes_ == 1);
    RemoveLogDevice(&a);
    RemoveLogDevice(&b);
}

TEST_CASE("removing a device while another thread logs is safe", "[log][device]")
{
    SinkGuard guard;
    SetConsoleLogMode(ConsoleLogMode::Off);
    for (int round = 0; round < 50; ++round)
    {
        CaptureLogDevice device;
        REQUIRE(AddLogDevice(&device));
        Atomic<bool> stop{false};
        Thread logger(
            [&stop]()
            {
                while (!stop.Load(MemoryOrder::Relaxed))
                {
                    Log(TestChannel, LogVerbosity::Info, "spin");
                }
            });
        while (device.Count() == 0)
        {
            ThisThread::Yield();
        }
        RemoveLogDevice(&device);  // returns only when no Write is running: `device` may then die
        stop.Store(true, MemoryOrder::Relaxed);
        logger.Join();
    }
}

TEST_CASE("the plain console device writes the line exactly as before", "[log][device]")
{
    SinkGuard guard;
    Log(TestChannel, LogVerbosity::Info, "plain");
    CHECK(Sink() == "[DeviceTest] INFO: plain\n");
}

TEST_CASE("the console mode Off writes nothing but the devices still get lines", "[log][device]")
{
    SinkGuard guard;
    SetConsoleLogMode(ConsoleLogMode::Off);
    CHECK(GetConsoleLogMode() == ConsoleLogMode::Off);
    CaptureLogDevice device;
    REQUIRE(AddLogDevice(&device));
    Log(TestChannel, LogVerbosity::Info, "quiet");
    RemoveLogDevice(&device);
    CHECK(Sink().Length() == 0);
    CHECK(device.Count() == 1);
}

namespace
{
bool Handle(void*, const char*, LogVerbosity, StringView)
{
    return true;
}
}  // namespace

TEST_CASE("an observer that handles a line keeps it off the console only", "[log][device]")
{
    SinkGuard guard;
    CaptureLogDevice device;
    REQUIRE(AddLogDevice(&device));
    SetLogObserver(&Handle, nullptr);
    Log(TestChannel, LogVerbosity::Info, "handled");
    SetLogObserver(nullptr, nullptr);
    RemoveLogDevice(&device);
    CHECK(Sink().Length() == 0);
    CHECK(device.Count() == 1);
}

TEST_CASE("the ring holds a line whatever the devices are", "[log][device]")
{
    SinkGuard guard;
    SetConsoleLogMode(ConsoleLogMode::Off);
    const u64 before = LogLinesWritten();
    Log(TestChannel, LogVerbosity::Info, "ringed");
    CHECK(LogLinesWritten() == before + 1);
}

TEST_CASE("the coloured console device colours by level and resets before the newline", "[log][tty]")
{
    SinkGuard guard;
    SetConsoleLogMode(ConsoleLogMode::Color);
    Log(TestChannel, LogVerbosity::Error, "bad");
    CHECK(Sink() == "\x1b[1;31m[DeviceTest] ERROR: bad\x1b[0m\n");
    g_sink_length = 0;
    Log(TestChannel, LogVerbosity::Warn, "hmm");
    CHECK(Sink() == "\x1b[33m[DeviceTest] WARN: hmm\x1b[0m\n");
    g_sink_length = 0;
    Log(TestChannel, LogVerbosity::Info, "fine");
    CHECK(Sink() == "[DeviceTest] INFO: fine\n");
    g_sink_length = 0;
    Log(TestChannel, LogVerbosity::Debug, "dim");
    CHECK(Sink() == "\x1b[2m[DeviceTest] DEBUG: dim\x1b[0m\n");
}

TEST_CASE("the coloured device shows control bytes instead of sending them", "[log][tty]")
{
    SinkGuard guard;
    SetConsoleLogMode(ConsoleLogMode::Color);
    Log(TestChannel, LogVerbosity::Info,
        "a\x1b[31mb\x7f"
        "c\td");
    CHECK(Sink() == "[DeviceTest] INFO: a^[[31mb^?c\td\n");
}

namespace
{
Text StripEscapes(StringView text)
{
    Text out;
    for (usize i = 0; i < text.Length(); ++i)
    {
        if (text[i] == '\x1b')
        {
            while (i < text.Length() && text[i] != 'm')
            {
                ++i;
            }
            continue;
        }
        out.Append(text.Substring(i, 1));
    }
    return out;
}
}  // namespace

TEST_CASE("the coloured device's line without its escapes is the plain line", "[log][tty]")
{
    SinkGuard guard;
    Log(TestChannel, LogVerbosity::Error, "same");
    Text plain(Sink());
    g_sink_length = 0;
    SetConsoleLogMode(ConsoleLogMode::Color);
    Log(TestChannel, LogVerbosity::Error, "same");
    CHECK(StripEscapes(Sink()).View() == plain.View());
}

TEST_CASE("Auto picks the plain device when the write sink is replaced", "[log][tty]")
{
    SinkGuard guard;
    SetConsoleLogMode(ConsoleLogMode::Auto);
    Log(TestChannel, LogVerbosity::Error, "auto");
    CHECK(Sink() == "[DeviceTest] ERROR: auto\n");
}

TEST_CASE("an Error line carries its trace to the console and the devices when traces are on", "[log][device]")
{
    SinkGuard guard;
    static const FailureReportHooks hooks{[](char* buffer, usize capacity, u32) -> usize
                                          {
                                              const char text[] = "  #0 fake_frame\n";
                                              const usize n = sizeof(text) - 1 < capacity ? sizeof(text) - 1 : capacity;
                                              MemCopy(buffer, text, n);
                                              return n;
                                          },
                                          nullptr, nullptr};
    const FailureReportHooks* previous = SetFailureReportHooks(&hooks);
    SetLogErrorTraces(true);
    CaptureLogDevice device;
    REQUIRE(AddLogDevice(&device));
    Log(TestChannel, LogVerbosity::Error, "traced");
    Log(TestChannel, LogVerbosity::Warn, "untraced");
    RemoveLogDevice(&device);
    SetLogErrorTraces(false);
    SetFailureReportHooks(previous);

    CHECK(Sink() == "[DeviceTest] ERROR: traced\n  #0 fake_frame\n[DeviceTest] WARN: untraced\n");
    REQUIRE(device.Count() == 2);
    CHECK(device.At(0).trace.View() == "  #0 fake_frame\n");
    CHECK(device.At(1).trace.View().Length() == 0);
}

TEST_CASE("a failed ensure still reaches the write sink with no device and the console off", "[log][device]")
{
    SinkGuard guard;
    SetConsoleLogMode(ConsoleLogMode::Off);
    ResetEnsureStateForTesting();
    ENGINE_ENSURE(false, "device test ensure");
    CHECK(Sink().Contains("device test ensure"));
    ResetEnsureStateForTesting();
}

TEST_CASE("the capture device keeps lines and clears them", "[log][device]")
{
    SinkGuard guard;
    CaptureLogDevice device;
    REQUIRE(AddLogDevice(&device));
    Log(TestChannel, LogVerbosity::Info, "kept");
    CHECK(device.Count() == 1);
    device.Clear();
    CHECK(device.Count() == 0);
    Log(TestChannel, LogVerbosity::Info, "again");
    CHECK(device.Count() == 1);
    RemoveLogDevice(&device);
}

TEST_CASE("the coloured device cuts a record longer than any Log line instead of overrunning", "[log][tty]")
{
    SinkGuard guard;
    Text big;
    for (int i = 0; i < 3000; ++i)
    {
        big.Append("\x01");  // the worst case: every byte grows to two
    }
    LogRecord record;
    record.level = LogVerbosity::Error;
    record.line = big.View();
    TtyLogDevice device;
    device.Write(record);
    CHECK(g_sink_length > 0);
    CHECK(g_sink_length < 1200);
    CHECK(g_sink[g_sink_length - 1] == '\n');
}

TEST_CASE("a line and its trace in a Log call reach the console in one write", "[log][device]")
{
    SinkGuard guard;
    static const FailureReportHooks hooks{[](char* buffer, usize capacity, u32) -> usize
                                          {
                                              // More than a record can carry: the hook is asked for no more than that.
                                              const usize n = capacity < 6000 ? capacity : 6000;
                                              MemSet(buffer, 'x', n);
                                              return n;
                                          },
                                          nullptr, nullptr};
    const FailureReportHooks* previous = SetFailureReportHooks(&hooks);
    SetLogErrorTraces(true);
    CaptureLogDevice device;
    REQUIRE(AddLogDevice(&device));
    Log(TestChannel, LogVerbosity::Error, "long trace");
    RemoveLogDevice(&device);
    SetLogErrorTraces(false);
    SetFailureReportHooks(previous);
    REQUIRE(device.Count() == 1);
    CHECK(device.At(0).trace.length <= kLogTraceBytes);
}
