#include <catch2/catch_test_macros.hpp>

#include "io/file.h"
#include "io/file_log_device.h"
#include "io/file_system.h"
#include "log/log.h"
#include "log/log_device.h"
#include "sync/thread.h"
#include "temp_dir.h"
#include "types.h"

using namespace vse;
using namespace vse::io;

namespace
{

LogChannel FileChannel("FileDevice", LogVerbosity::Trace);

String<HeapAllocator> Read(const Path& path)
{
    String<HeapAllocator> text(HeapAllocator::Default());
    ReadTextFile(path, &text);
    return text;
}

UniquePtr<FileLogDevice> OpenDevice(const Path& path, const FileLogOptions& options = FileLogOptions())
{
    UniquePtr<FileLogDevice> device;
    REQUIRE(FileLogDevice::Open(path, options, &device).Ok());
    return device;
}

// Sends one line to a device without going through the console.
void Send(FileLogDevice& device, LogVerbosity level, StringView message)
{
    char line[600];
    usize n = 0;
    const StringView prefix("[FileDevice] X: ");
    for (usize i = 0; i < prefix.Length(); ++i)
    {
        line[n++] = prefix[i];
    }
    for (usize i = 0; i < message.Length() && n < 598; ++i)
    {
        line[n++] = message[i];
    }
    line[n++] = '\n';
    LogRecord record;
    record.channel = "FileDevice";
    record.level = level;
    record.message = message;
    record.line = StringView(line, n);
    device.Write(record);
}

}  // namespace

TEST_CASE("a file log device appends lines in order and survives a reopen", "[log][file]")
{
    io_test::TempDir dir;
    const Path path = dir / "app.log";
    {
        UniquePtr<FileLogDevice> device = OpenDevice(path);
        Send(*device, LogVerbosity::Info, "one");
        Send(*device, LogVerbosity::Info, "two");
    }  // the destructor flushes
    {
        UniquePtr<FileLogDevice> device = OpenDevice(path);
        Send(*device, LogVerbosity::Info, "three");
    }
    CHECK(Read(path).View() == "[FileDevice] X: one\n[FileDevice] X: two\n[FileDevice] X: three\n");
}

TEST_CASE("an error line is on disk at once, a lower one after a flush", "[log][file]")
{
    io_test::TempDir dir;
    const Path path = dir / "app.log";
    UniquePtr<FileLogDevice> device = OpenDevice(path);
    Send(*device, LogVerbosity::Info, "buffered");
    CHECK(Read(path).View().Length() == 0);
    Send(*device, LogVerbosity::Error, "urgent");
    CHECK(Read(path).View() == "[FileDevice] X: buffered\n[FileDevice] X: urgent\n");
    Send(*device, LogVerbosity::Warn, "later");
    CHECK(Read(path).View() == "[FileDevice] X: buffered\n[FileDevice] X: urgent\n");
    device->Flush();
    CHECK(Read(path).View().Contains("later"));
}

TEST_CASE("the trace of a record follows its line", "[log][file]")
{
    io_test::TempDir dir;
    const Path path = dir / "app.log";
    UniquePtr<FileLogDevice> device = OpenDevice(path);
    LogRecord record;
    record.level = LogVerbosity::Error;
    record.line = "[C] ERROR: x\n";
    record.trace = "  #0 frame\n";
    device->Write(record);
    CHECK(Read(path).View() == "[C] ERROR: x\n  #0 frame\n");
}

TEST_CASE("the file rotates by size and keeps the newest files", "[log][file]")
{
    io_test::TempDir dir;
    const Path path = dir / "app.log";
    FileLogOptions options;
    options.max_bytes = 100;
    options.keep = 2;
    options.flush_level = LogVerbosity::Trace;
    UniquePtr<FileLogDevice> device = OpenDevice(path, options);
    // Each line is 24 + 1 bytes; four fit in 100. Ten lines make two rotations.
    for (int i = 0; i < 10; ++i)
    {
        char message[8] = {'m', 's', 'g', static_cast<char>('0' + i), '-', '-', '-', '\0'};
        Send(*device, LogVerbosity::Info, StringView(message, 7));
    }
    device->Flush();
    CHECK(device->Rotations() == 2);
    CHECK(device->DroppedLines() == 0);
    const auto current = Read(path);
    const auto first = Read(dir / "app.log.1");
    const auto second = Read(dir / "app.log.2");
    CHECK_FALSE(Exists(dir / "app.log.3"));
    // The three files hold lines 0-9 in order, whole, none lost across a rotation: the oldest file is the first four.
    CHECK(second.View().Contains("msg0"));
    CHECK(first.View().Contains("msg4"));
    CHECK(current.View().Contains("msg9"));
    // No line is split: every file ends with a newline.
    CHECK(second.View()[second.View().Length() - 1] == '\n');
    CHECK(first.View()[first.View().Length() - 1] == '\n');
    CHECK(current.View()[current.View().Length() - 1] == '\n');
}

TEST_CASE("with keep 0 a rotation empties the file", "[log][file]")
{
    io_test::TempDir dir;
    const Path path = dir / "app.log";
    FileLogOptions options;
    options.max_bytes = 60;
    options.keep = 0;
    options.flush_level = LogVerbosity::Trace;
    UniquePtr<FileLogDevice> device = OpenDevice(path, options);
    for (int i = 0; i < 6; ++i)
    {
        Send(*device, LogVerbosity::Info, "aaaaaaa");
    }
    CHECK(device->Rotations() >= 1);
    CHECK_FALSE(Exists(dir / "app.log.1"));
    CHECK(Read(path).View().Length() <= 60);
}

TEST_CASE("a line larger than the buffer is written whole", "[log][file]")
{
    io_test::TempDir dir;
    const Path path = dir / "app.log";
    UniquePtr<FileLogDevice> device = OpenDevice(path);
    String<HeapAllocator> big(HeapAllocator::Default());
    for (int i = 0; i < 20000; ++i)
    {
        big.Append('x');
    }
    LogRecord record;
    record.level = LogVerbosity::Info;
    record.line = big.View();
    device->Write(record);
    device->Flush();
    CHECK(Read(path).View().Length() == 20000);
}

TEST_CASE("opening a file log device in a missing directory fails by value", "[log][file]")
{
    io_test::TempDir dir;
    UniquePtr<FileLogDevice> device;
    const Status status = FileLogDevice::Open(dir / "nope" / "app.log", FileLogOptions(), &device);
    CHECK_FALSE(status.Ok());
    CHECK(device == nullptr);
}

TEST_CASE("four threads write whole lines", "[log][file]")
{
    io_test::TempDir dir;
    const Path path = dir / "app.log";
    FileLogOptions options;
    options.max_bytes = 0;
    UniquePtr<FileLogDevice> device = OpenDevice(path, options);
    constexpr int kThreads = 4;
    constexpr int kLines = 1000;
    Thread threads[kThreads];
    for (int t = 0; t < kThreads; ++t)
    {
        threads[t] = Thread(
            [&device]()
            {
                for (int i = 0; i < kLines; ++i)
                {
                    Send(*device, LogVerbosity::Info, "payload");
                }
            });
    }
    for (Thread& t : threads)
    {
        t.Join();
    }
    device->Flush();
    const auto text = Read(path);
    usize lines = 0;
    for (usize i = 0; i < text.View().Length(); ++i)
    {
        lines += text.View()[i] == '\n' ? 1 : 0;
    }
    CHECK(lines == static_cast<usize>(kThreads * kLines));
    CHECK(text.View().Length() == lines * StringView("[FileDevice] X: payload\n").Length());
}

TEST_CASE("a removed device then destroyed has flushed its file", "[log][file]")
{
    SetConsoleLogMode(ConsoleLogMode::Off);
    io_test::TempDir dir;
    const Path path = dir / "app.log";
    {
        UniquePtr<FileLogDevice> device = OpenDevice(path);
        REQUIRE(AddLogDevice(device.Get(), LogVerbosity::Info));
        Log(FileChannel, LogVerbosity::Info, "through the registry");
        RemoveLogDevice(device.Get());
    }
    SetConsoleLogMode(ConsoleLogMode::Auto);
    CHECK(Read(path).View() == "[FileDevice] INFO: through the registry\n");
}
