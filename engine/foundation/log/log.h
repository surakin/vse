#pragma once

#include "log/assert.h"
#include "string/string.h"
#include "types.h"

//! Channel-based logging with per-channel verbosity, overridable from a config file.
//!
//! Needs no allocator; log lines and config parsing use fixed-size stack buffers.
namespace engine_placeholder {

/// Severity of a log message and the threshold a channel filters by.
///
/// Lower values are more severe; a channel logs a message when its level is
/// less than or equal to the channel's verbosity.
enum class LogVerbosity : u8 {
    Error = 0, ///< Failures the program cannot ignore.
    Warn = 1,  ///< Suspicious conditions; the default channel threshold.
    Info = 2,  ///< Normal progress messages.
    Debug = 3, ///< Developer diagnostics.
    Trace = 4  ///< Very fine-grained, high-volume tracing.
};

/// A named source of log messages with its own verbosity threshold and enable flag.
///
/// Every constructed channel links itself into a global registry (used by
/// [[LogChannel::Find]]), so channels are meant to be long-lived globals and
/// must never be destroyed while the program can still look them up.
///
/// Thread-safety: not-thread-safe. Construct channels during static
///   initialization; the verbosity and enable setters are unsynchronized.
/// Ownership: does not own `name`; the string must outlive the channel.
class LogChannel {
public:
    /// Creates a channel, enabled, and adds it to the global registry.
    ///
    /// Param name: channel name used in output and config files; must be a
    ///   string that outlives the channel (typically a literal).
    /// Param default_verbosity: initial threshold.
    explicit LogChannel(const char* name, LogVerbosity default_verbosity = LogVerbosity::Warn);

    /// Returns the channel name as passed to the constructor.
    const char* Name() const { return name_; }
    /// Returns the current verbosity threshold.
    LogVerbosity Verbosity() const { return verbosity_; }
    /// Returns whether the channel is enabled.
    bool Enabled() const { return enabled_; }

    /// Sets the verbosity threshold.
    void SetVerbosity(LogVerbosity v) { verbosity_ = v; }
    /// Enables or disables the channel; a disabled channel logs nothing.
    void SetEnabled(bool e) { enabled_ = e; }

    /// Reports whether a message of severity `level` would be emitted.
    ///
    /// Param level: severity of the candidate message.
    /// Returns: true if the channel is enabled and `level` is at most the
    ///   channel's verbosity.
    bool ShouldLog(LogVerbosity level) const { return enabled_ && level <= verbosity_; }

    /// Looks up a registered channel by name, for config-file overrides.
    ///
    /// Param name: exact, case-sensitive channel name.
    /// Returns: the channel, or nullptr if none has that name.
    /// Fails: reports an unknown name by returning nullptr.
    /// Ownership: the channel is owned by whoever constructed it.
    static LogChannel* Find(StringView name);

    /// Returns the first channel of the global registry (the most recently constructed), or null; `Next` walks the rest.
    ///
    /// Fails: never.
    /// Thread-safety: not-thread-safe against channel construction.
    static LogChannel* First() { return head_; }
    /// Returns the channel constructed before this one, or null at the end of the registry.
    LogChannel* Next() const { return next_; }

private:
    const char* name_;
    LogVerbosity verbosity_;
    bool enabled_;
    LogChannel* next_ = nullptr;

    // Intrusive registry: every constructed LogChannel links itself in.
    static inline LogChannel* head_ = nullptr;
};

/// Slots in the ring of recent log lines.
constexpr usize kLogRingSlots = 256;
/// Bytes kept of each line in the ring, newline included; a longer line is cut.
constexpr usize kLogRingLineBytes = 256;

/// Receives one recent log line from [[ReadRecentLogLines]]: the text (null-terminated, with its trailing newline) and its length.
using RecentLogLineFn = void (*)(const char* line, usize length, void* context);

/// Calls `callback` for the most recent log lines, oldest first, at most [[kLogRingSlots]] of them.
///
/// Every line [[Log]] emits is also kept in a ring in memory, so a crash report can show what the program said last. A slot
/// that is being overwritten while it is read is skipped, so every line delivered is whole. Reads no file and allocates
/// nothing.
///
/// Param callback: called once per line.
/// Param context: passed through to `callback`; borrowed for the call.
/// Param max_lines: deliver at most this many of the newest lines (0 means no limit beyond the ring size).
/// Thread-safety: thread-safe and async-signal-safe: lock-free, concurrent with any number of [[Log]] calls.
void ReadRecentLogLines(RecentLogLineFn callback, void* context, usize max_lines = 0);

/// Empties the ring of recent log lines. For tests.
/// Thread-safety: not-thread-safe. Call only while no thread logs.
void ResetLogRingForTesting();

/// Makes every `Error` line carry a short stack trace under it, to find where an error came from. Off by default;
/// [[debug::StartProgramDebugging]] turns it on in Debug builds. The output of lines at other levels does not change.
/// Param enabled: true to add traces.
/// Thread-safety: thread-safe.
void SetLogErrorTraces(bool enabled);

/// Reports whether `Error` lines carry a trace.
/// Returns: the setting made by [[SetLogErrorTraces]].
/// Thread-safety: thread-safe.
bool LogErrorTraces();

/// Channel for core engine and foundation messages.
extern LogChannel LogCore;
/// Channel for rendering messages.
extern LogChannel LogRender;
/// Channel for platform and windowing messages.
extern LogChannel LogPlatform;

/// Writes `[Channel] LEVEL: message` plus a newline to the write sink (standard
/// output by default) if the channel allows `level`.
///
/// Param channel: channel to log on.
/// Param level: severity of this message.
/// Param message: text to print. The whole line is formatted into a 512-byte
///   buffer, so overlong messages are cut; the line always ends with its newline.
/// Thread-safety: thread-compatible. Formatting uses a stack buffer, but the
///   channel state it reads is unsynchronized.
void Log(LogChannel& channel, LogVerbosity level, StringView message);

/// Applies per-channel overrides from a `key=value` config file.
///
/// Lines have the form `ChannelName.verbosity=Level` (`Error`, `Warn`, `Info`,
/// `Debug` or `Trace`) or `ChannelName.enabled=true|false` (`1` also means
/// true). Unknown channels, attributes and malformed lines are skipped
/// silently. An unrecognized verbosity name is not skipped: it sets the
/// channel to `Warn`. Only the first 4096 bytes of the file are read.
///
/// Param path: path of the config file, NUL-terminated.
/// Returns: true if the file was opened and had content; false otherwise.
/// Fails: returns false if the file cannot be opened or is empty. A missing
///   config file is not an error for the caller.
/// Thread-safety: not-thread-safe. Modifies channel state without
///   synchronization.
bool LoadConfig(const char* path);

} // namespace engine_placeholder
