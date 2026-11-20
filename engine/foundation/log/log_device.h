#pragma once

#include "log/log.h"
#include "string_view.h"
#include "types.h"

//! Log devices: where the lines [[Log]] emits go (Maintenance: log devices).
//!
//! A device is an object that receives every line a channel lets through: standard output (the default), a coloured
//! terminal, a file, a window, the network. [[Log]] writes each line to the **console device** (chosen by
//! [[SetConsoleLogMode]]) and to every device added with [[AddLogDevice]], at most [[kMaxLogDevices]] of them.
//!
//! The crash and assert reports do not go through devices: they write straight to the log core's write sink, so they
//! stay async-signal-safe and plain. Needs no allocator.
namespace vse
{

/// Bytes of stack trace a record can carry.
constexpr usize kLogTraceBytes = 4096;

/// Number of devices [[AddLogDevice]] accepts at once (the console device is not counted).
constexpr usize kMaxLogDevices = 8;

/// What a [[LogRecord]] is.
enum class LogRecordKind : u8
{
    Log,    ///< A line of [[Log]]: `[Channel] LEVEL: message` and a newline.
    Output  ///< A piece of program output ([[out::Print]]): the bytes as written, no prefix, no newline added.
};

/// One log line as a device receives it. Every pointer is valid for the call only.
struct LogRecord
{
    LogRecordKind kind = LogRecordKind::Log;  ///< A log line, or program output (then `channel` is empty, `message` and
                                              ///< `line` are the text).
    int stream = 1;            ///< The descriptor the line is for: 1, or 2 for output written to standard error.
    const char* channel = "";  ///< Name of the channel that logged.
    LogVerbosity level = LogVerbosity::Info;  ///< Severity of the line.
    StringView message;                       ///< The message text alone: no brackets, no newline.
    StringView line;   ///< The whole line, `[Channel] LEVEL: message` and its newline (cut at 512 bytes, newline kept).
    StringView trace;  ///< The stack trace of an `Error` line when [[SetLogErrorTraces]] is on, else empty.
    u64 timestamp_ns = 0;  ///< [[os::MonotonicTimeNanos]] when the line was logged.
};

/// Receives log lines. Implement `Write`, and `Flush` if the device buffers.
///
/// `Write` may run on several threads at once, so a device synchronises itself. It must not call [[Log]],
/// [[AddLogDevice]] or [[RemoveLogDevice]] (it would recurse or wait for itself).
///
/// - Thread-safety: thread-safe when the implementation is; `Write` is called concurrently.
/// - Ownership: the registry borrows the device; the caller owns it and removes it before destroying it.
class LogDevice
{
public:
    virtual ~LogDevice() = default;

    /// Delivers one line.
    /// - Param record: the line; borrowed for the call.
    virtual void Write(const LogRecord& record) = 0;

    /// Makes everything written so far reach its destination. The default does nothing.
    virtual void Flush() {}
};

/// Adds `device` to the registry; it then receives every line at `max_verbosity` or more severe.
///
/// - Param device: the device; borrowed until [[RemoveLogDevice]] returns.
/// - Param max_verbosity: the least severe level the device receives (`Trace` receives everything).
/// - Returns: true when added.
/// - Fails: returns false when `device` is null or already added, or when [[kMaxLogDevices]] devices are in use.
/// - Thread-safety: thread-safe, also against [[Log]] on other threads.
bool AddLogDevice(LogDevice* device, LogVerbosity max_verbosity = LogVerbosity::Trace);

/// Removes `device`, waiting for the calls to its `Write` that are still running, so it may be destroyed after.
/// Removing a device that is not added does nothing.
///
/// - Thread-safety: thread-safe, also against [[Log]] on other threads; not callable from a device's own `Write`.
void RemoveLogDevice(LogDevice* device);

/// Calls `Flush` on every added device and on the console device.
/// - Thread-safety: thread-safe.
void FlushLogDevices();

/// Which device is the console device.
enum class ConsoleLogMode : u8
{
    Auto,   ///< The coloured [[TtyLogDevice]] when standard output is a colour-capable terminal, else the plain one.
    Plain,  ///< [[StdoutLogDevice]]: the line as formatted.
    Color,  ///< [[TtyLogDevice]], whatever standard output is.
    Off     ///< No console device.
};

/// Chooses the console device. The default is `Auto`.
/// - Thread-safety: thread-safe.
void SetConsoleLogMode(ConsoleLogMode mode);

/// Returns the mode set by [[SetConsoleLogMode]].
/// - Thread-safety: thread-safe.
ConsoleLogMode GetConsoleLogMode();

/// Writes bytes to the log core's write sink (descriptor `fd`: 1, or 2 for standard error), the one place the console
/// devices write through.
///
/// The sink is standard output unless a test replaced it. Returns the number of bytes written, or a negative value on
/// error.
/// - Thread-safety: thread-safe when the sink is.
long WriteLogConsoleBytes(const char* bytes, usize length, int fd = 1);

/// The plain console device: writes the line as formatted, followed by the trace when there is one, in one write.
class StdoutLogDevice final : public LogDevice
{
public:
    void Write(const LogRecord& record) override;
};

namespace detail
{
/// Delivers program output to the console device and the added devices (what [[out::Print]] calls). Not for other use.
void EmitOutput(StringView text, int stream);
}  // namespace detail

}  // namespace vse
