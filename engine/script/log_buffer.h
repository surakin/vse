// The script log buffer (Phase 27 Day 453): what `log.info` and its siblings write into. A partition's job appends to
// its own buffer (no lock, no shared state); at the sync point the buffers are flushed in partition order, so the log
// reads the same for any thread count. A per-script rate limit keeps one script from flooding the log.
#pragma once

#include "containers/span.h"
#include "containers/vector.h"
#include "log/log.h"
#include "mem/heap_allocator.h"
#include "string/string.h"
#include "string_view.h"
#include "types.h"

namespace engine_placeholder::script
{

/// The log channel of everything scripts write (`print` and `log.*`); `LogScript`, default verbosity Info.
extern LogChannel LogScript;

/// How serious a script log message is (`log.debug`, `log.info`, `log.warn`, `log.error`).
///
/// - Thread-safety: thread-compatible plain data.
enum class ScriptLogLevel : u8
{
    /// Developer diagnostics.
    Debug,
    /// Normal progress.
    Info,
    /// Suspicious conditions.
    Warn,
    /// Failures.
    Error,
};

/// The log level of a script level.
///
/// - Thread-safety: thread-safe.
/// - Fails: never.
LogVerbosity LogVerbosityOf(ScriptLogLevel level);

/// The most bytes of one script log message; longer text is cut.
inline constexpr usize kMaxScriptLogText = 400;

/// The most distinct script names one buffer counts for the rate limit; further names share one counter.
inline constexpr usize kMaxLogScripts = 256;

/// One partition's pending log messages and the per-script rate limit.
///
/// A script (named by the chunk it runs in) may add `limit` messages between two flushes; the rest are counted and
/// reported as one warning at the flush. The limit counts per tick, because the buffer is flushed once per tick.
///
/// - Thread-safety: thread-compatible; one job owns a buffer at a time, like the partition it belongs to.
/// - Ownership: owns its entries.
class LogBuffer
{
public:
    /// Creates a buffer.
    ///
    /// - Param limit_per_script: messages per script between flushes; 0 is no limit.
    explicit LogBuffer(u32 limit_per_script = 64);

    /// Sets the rate limit (see the constructor).
    ///
    /// - Fails: never.
    void SetLimit(u32 limit_per_script) { limit_ = limit_per_script; }

    /// Adds a message unless `script` has used up its limit; text beyond [[script::kMaxScriptLogText]] is cut.
    ///
    /// - Param script: the script's name (its chunk name).
    /// - Param line: the line, 0 when unknown.
    /// - Returns: whether the message was kept.
    /// - Ownership: the strings are copied.
    /// - Fails: false when the limit is used up (the message is counted, not kept).
    bool Add(ScriptLogLevel level, StringView script, u32 line, StringView text);

    /// How many messages wait.
    ///
    /// - Fails: never.
    usize Pending() const { return entries_.Size(); }

    /// How many messages were dropped for the limit since the last flush.
    ///
    /// - Fails: never.
    u64 DroppedSinceFlush() const;

    /// Writes the waiting messages to [[script::LogScript]] in the order they were added, then one warning per script
    /// that dropped messages, and empties the buffer (the limits start again).
    ///
    /// - Returns: how many log lines were written.
    /// - Fails: never.
    usize Flush();

private:
    struct Entry
    {
        ScriptLogLevel level = ScriptLogLevel::Info;
        u32 line = 0;
        String<HeapAllocator> script{HeapAllocator::Default()};
        String<HeapAllocator> text{HeapAllocator::Default()};
    };
    struct Count
    {
        String<HeapAllocator> script{HeapAllocator::Default()};
        u32 used = 0;
        u32 dropped = 0;
    };

    Count* CountFor(StringView script);

    u32 limit_;
    Vector<Entry> entries_;
    Vector<Count> counts_;
};

/// Flushes buffers in the order given (the partition order, which is the merge order), so the log is the same for any
/// thread count.
///
/// - Param buffers: the buffers; null entries are skipped.
/// - Returns: the log lines written.
/// - Ownership: borrowed for the call.
/// - Thread-safety: thread-compatible; call it at the sync point with no job running.
/// - Fails: never.
usize FlushLogBuffers(Span<LogBuffer* const> buffers);

/// Writes one script log message straight to [[script::LogScript]], formatted as `[script:line] text` (the form
/// [[script::LogBuffer::Flush]] uses).
///
/// - Thread-safety: thread-safe (the log is).
/// - Fails: never.
void WriteScriptLog(ScriptLogLevel level, StringView script, u32 line, StringView text);

}  // namespace engine_placeholder::script
