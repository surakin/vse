#pragma once

#include "debug/compiler.h"
#include "types.h"

//! Program output through the log system (Maintenance: log everything).
//!
//! A program's own output (results, statistics, help text, a tool's report) is written with these functions, not with
//! the C library: it reaches the console device and every added device exactly as written, with no `[Channel] LEVEL:`
//! prefix, no colour and no escaping, so the bytes a program prints do not change. Diagnostics are [[Log]] lines
//! instead.
//!
//! Output is not buffered by the C library: one call is one write, in order with the log lines. It is not kept in the
//! crash ring, does not reach the observer, and is not filtered by a channel. A device added with a minimum level
//! receives it as an `Info` line. `ConsoleLogMode::Off` silences it on the console.
namespace vse::out
{

/// Longest formatted result of [[Print]] and [[PrintError]]; a longer one is cut and ends with `...[cut]` and a
/// newline.
constexpr usize kMaxFormattedBytes = 16 * 1024;

/// Formats and writes to standard output (the console device of the log system).
///
/// - Returns: the length the formatted text has (as `printf`), or negative on a format error.
/// - Thread-safety: thread-safe. One call's text is written as one record, not interleaved with another's.
int Print(const char* format, ...) ENGINE_PRINTF_FORMAT(1, 2);

/// Formats and writes to standard error.
///
/// - Returns: as [[Print]].
/// - Thread-safety: thread-safe.
int PrintError(const char* format, ...) ENGINE_PRINTF_FORMAT(1, 2);

/// Writes already formatted text to standard output, without a newline. A text longer than [[kMaxFormattedBytes]] is
/// written in several records, cut at a newline where there is one.
/// - Thread-safety: thread-safe.
void PrintText(const char* text);

/// Writes already formatted text to standard error, without a newline.
/// - Thread-safety: thread-safe.
void PrintErrorText(const char* text);

/// Writes `text` and a newline to standard output.
/// - Thread-safety: thread-safe.
void PrintLine(const char* text);

/// Writes `size` bytes to standard output, a NUL included.
/// - Returns: the number of bytes written (`size`).
/// - Thread-safety: thread-safe.
usize PrintBytes(const void* data, usize size);

/// Writes `size` bytes to standard error.
/// - Returns: the number of bytes written (`size`).
/// - Thread-safety: thread-safe.
usize PrintErrorBytes(const void* data, usize size);

/// Flushes the devices that buffer (a log file), so what was printed is on disk. Nothing is buffered for the console.
/// - Thread-safety: thread-safe.
void Flush();

/// As [[out::Flush]], for what was printed to standard error.
/// - Thread-safety: thread-safe.
void FlushError();

}  // namespace vse::out
