#include <catch2/catch_test_macros.hpp>

#include "capture_log_device.h"
#include "cstr/mem_ops.h"
#include "log/log.h"
#include "log/log_device.h"
#include "log/output.h"
#include "os/os.h"
#include "string_view.h"
#include "sync/mutex.h"
#include "sync/thread.h"
#include "types.h"

using namespace vse;

namespace out_test
{

constexpr usize kSinkBytes = 256 * 1024;
char g_out_sink[kSinkBytes];
usize g_out_length = 0;
int g_last_fd = 0;
usize g_writes = 0;
char g_out_streams[4096];  // the descriptor of each write, as '1' or '2'

Mutex g_out_mutex;

long OutSinkWrite(int fd, const void* buf, usize len)
{
    LockGuard<Mutex> lock(g_out_mutex);  // two threads write in one test
    const usize room = kSinkBytes - g_out_length;
    const usize n = len < room ? len : room;
    MemCopy(g_out_sink + g_out_length, buf, n);
    g_out_length += n;
    g_last_fd = fd;
    if (g_writes < sizeof(g_out_streams))
    {
        g_out_streams[g_writes] = static_cast<char>('0' + fd);
    }
    ++g_writes;
    return static_cast<long>(len);
}

StringView OutSink()
{
    return StringView(g_out_sink, g_out_length);
}

struct OutGuard
{
    explicit OutGuard(ConsoleLogMode mode = ConsoleLogMode::Plain)
    {
        g_out_length = 0;
        g_writes = 0;
        SetWriteSinkForTesting(&OutSinkWrite);
        SetConsoleLogMode(mode);
    }
    ~OutGuard()
    {
        SetWriteSinkForTesting(&os::Write);
        SetConsoleLogMode(ConsoleLogMode::Auto);
    }
};

}  // namespace out_test

using namespace out_test;

TEST_CASE("out::Print writes the formatted text as it is, on standard output", "[log][out]")
{
    OutGuard guard;
    CHECK(out::Print("%d-%s\n", 7, "x") == 4);
    CHECK(OutSink() == "7-x\n");
    CHECK(g_last_fd == 1);
}

TEST_CASE("out::PrintError writes on standard error", "[log][out]")
{
    OutGuard guard;
    out::PrintError("bad %s\n", "thing");
    CHECK(OutSink() == "bad thing\n");
    CHECK(g_last_fd == 2);
    out::PrintErrorText("more");
    CHECK(g_last_fd == 2);
}

TEST_CASE("program output passes through unchanged on every console mode", "[log][out]")
{
    for (ConsoleLogMode mode : {ConsoleLogMode::Plain, ConsoleLogMode::Color})
    {
        OutGuard guard(mode);
        out::PrintText("a\tb\x1b[31mc\x7f\n");
        CHECK(OutSink() == "a\tb\x1b[31mc\x7f\n");
    }
}

TEST_CASE("PrintLine adds the newline, PrintText does not", "[log][out]")
{
    OutGuard guard;
    out::PrintLine("one");
    out::PrintText("two");
    CHECK(OutSink() == "one\ntwo");
}

TEST_CASE("PrintLine reaches the console as one write, text and newline together", "[log][out]")
{
    OutGuard guard;
    out::PrintLine("whole");
    CHECK(OutSink() == "whole\n");
    CHECK(g_writes == 1);
}

TEST_CASE("PrintBytes writes a NUL inside the data", "[log][out]")
{
    OutGuard guard;
    const char bytes[] = {'a', '\0', 'b'};
    CHECK(out::PrintBytes(bytes, 3) == 3);
    REQUIRE(g_out_length == 3);
    CHECK(g_out_sink[1] == '\0');
    CHECK(g_out_sink[2] == 'b');
}

TEST_CASE("a long PrintText arrives whole, in chunks cut at a newline", "[log][out]")
{
    OutGuard guard;
    static char big[100 * 1024 + 1];
    for (usize i = 0; i < sizeof(big) - 1; ++i)
    {
        big[i] = (i % 100 == 99) ? '\n' : static_cast<char>('a' + i % 26);
    }
    big[sizeof(big) - 1] = '\0';
    out::PrintText(big);
    CHECK(OutSink() == StringView(big, sizeof(big) - 1));
    CHECK(g_writes >= 7);  // 100 KiB in chunks of at most 16 KiB
    CHECK(g_writes < 20);
}

TEST_CASE("a formatted result over the buffer is cut with a marker", "[log][out]")
{
    OutGuard guard;
    static char filler[out::kMaxFormattedBytes + 100];
    MemSet(filler, 'z', sizeof(filler) - 1);
    filler[sizeof(filler) - 1] = '\0';
    const int needed = out::Print("%s", filler);
    CHECK(needed == static_cast<int>(sizeof(filler) - 1));
    CHECK(g_out_length == out::kMaxFormattedBytes - 1);
    CHECK(OutSink().EndsWith("...[cut]\n"));
}

TEST_CASE("an added device receives output as an Output record at level Info", "[log][out]")
{
    OutGuard guard(ConsoleLogMode::Off);
    engine_test::CaptureLogDevice everything;
    engine_test::CaptureLogDevice warnings;
    REQUIRE(AddLogDevice(&everything));
    REQUIRE(AddLogDevice(&warnings, LogVerbosity::Warn));
    out::Print("result %d\n", 42);
    out::PrintError("oops\n");
    RemoveLogDevice(&everything);
    RemoveLogDevice(&warnings);
    CHECK(g_out_length == 0);  // the console was off
    REQUIRE(everything.Count() == 2);
    CHECK(everything.At(0).kind == LogRecordKind::Output);
    CHECK(everything.At(0).line.View() == "result 42\n");
    CHECK(everything.At(0).stream == 1);
    CHECK(everything.At(0).level == LogVerbosity::Info);
    CHECK(everything.At(1).stream == 2);
    CHECK(warnings.Count() == 0);
}

TEST_CASE("output does not reach the observer or the ring", "[log][out]")
{
    OutGuard guard;
    static int observed;
    observed = 0;
    SetLogObserver(
        [](void*, const char*, LogVerbosity, StringView) -> bool
        {
            ++observed;
            return false;
        },
        nullptr);
    const u64 before = LogLinesWritten();
    out::Print("hello\n");
    SetLogObserver(nullptr, nullptr);
    CHECK(observed == 0);
    CHECK(LogLinesWritten() == before);
}

TEST_CASE("the console mode Off silences output on the console", "[log][out]")
{
    OutGuard guard(ConsoleLogMode::Off);
    out::Print("nobody hears this\n");
    CHECK(g_out_length == 0);
}

TEST_CASE("two threads printing whole lines do not split a line", "[log][out]")
{
    OutGuard guard;
    Thread a(
        []()
        {
            for (int i = 0; i < 200; ++i)
            {
                out::Print("AAAAAAAAAAAAAAAAAAAA\n");
            }
        });
    Thread b(
        []()
        {
            for (int i = 0; i < 200; ++i)
            {
                out::Print("BBBBBBBBBBBBBBBBBBBB\n");
            }
        });
    a.Join();
    b.Join();
    REQUIRE(g_out_length == 400 * 21);
    for (usize line = 0; line < 400; ++line)
    {
        const char first = g_out_sink[line * 21];
        for (usize i = 1; i < 20; ++i)
        {
            REQUIRE(g_out_sink[line * 21 + i] == first);
        }
        REQUIRE(g_out_sink[line * 21 + 20] == '\n');
    }
}

TEST_CASE("Flush flushes the added devices", "[log][out]")
{
    OutGuard guard(ConsoleLogMode::Off);
    engine_test::CaptureLogDevice device;
    REQUIRE(AddLogDevice(&device));
    out::Flush();
    out::FlushError();
    RemoveLogDevice(&device);
    CHECK(device.flushes_ == 2);
}
