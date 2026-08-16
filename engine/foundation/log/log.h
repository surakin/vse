#pragma once

#include "log/assert.h"
#include "string/string.h"
#include "types.h"

// Channel-based logging, designed against Unreal's log-spam problem
// from the start (PLAN.md assumption #10): named channels, independent
// per-channel verbosity/enable state, conservative defaults, and
// per-user config overridable without recompiling (LoadConfig).
//
// No allocator dependency anywhere in this file — log lines and config
// parsing both work off fixed-size stack buffers, deliberately, so
// logging/assert never need a heap/arena to be set up first.
namespace engine_placeholder {

enum class LogVerbosity : u8 { Error = 0, Warn = 1, Info = 2, Debug = 3, Trace = 4 };

class LogChannel {
public:
    explicit LogChannel(const char* name, LogVerbosity default_verbosity = LogVerbosity::Warn);

    const char* Name() const { return name_; }
    LogVerbosity Verbosity() const { return verbosity_; }
    bool Enabled() const { return enabled_; }

    void SetVerbosity(LogVerbosity v) { verbosity_ = v; }
    void SetEnabled(bool e) { enabled_ = e; }

    bool ShouldLog(LogVerbosity level) const { return enabled_ && level <= verbosity_; }

    // Registry lookup by name, for config-file overrides.
    static LogChannel* Find(StringView name);

private:
    const char* name_;
    LogVerbosity verbosity_;
    bool enabled_;
    LogChannel* next_ = nullptr;

    // Intrusive registry: every constructed LogChannel links itself in.
    static inline LogChannel* head_ = nullptr;
};

extern LogChannel LogCore;
extern LogChannel LogRender;
extern LogChannel LogPlatform;

void Log(LogChannel& channel, LogVerbosity level, StringView message);

// Minimal hand-rolled `key=value` config reader: lines of the form
// `ChannelName.verbosity=Level` or `ChannelName.enabled=true|false`.
// Unknown channels/attributes/values are silently skipped — kept
// intentionally dumb (see this day's plan notes). Returns false if the
// file couldn't be opened; a missing config file is not an error.
bool LoadConfig(const char* path);

} // namespace engine_placeholder
