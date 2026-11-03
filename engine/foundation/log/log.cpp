#include "log/log.h"

#include <fcntl.h>

#include <cstring>
#include "os/os.h"

namespace engine_placeholder {

LogChannel LogCore("LogCore");
LogChannel LogRender("LogRender");
LogChannel LogPlatform("LogPlatform");

LogChannel::LogChannel(const char* name, LogVerbosity default_verbosity)
    : name_(name), verbosity_(default_verbosity), enabled_(true) {
    next_ = head_;
    head_ = this;
}

LogChannel* LogChannel::Find(StringView name) {
    for (LogChannel* c = head_; c != nullptr; c = c->next_) {
        if (StringView(c->name_) == name) {
            return c;
        }
    }
    return nullptr;
}

namespace {

WriteSinkFn g_write_sink = &os::Write;
ExitSinkFn g_exit_sink = &os::Exit;

const char* VerbosityName(LogVerbosity level) {
    switch (level) {
        case LogVerbosity::Error:
            return "ERROR";
        case LogVerbosity::Warn:
            return "WARN";
        case LogVerbosity::Info:
            return "INFO";
        case LogVerbosity::Debug:
            return "DEBUG";
        case LogVerbosity::Trace:
            return "TRACE";
    }
    return "?";
}

usize AppendStrToBuffer(char* buf, usize capacity, usize offset, StringView sv) {
    usize n = sv.Length();
    usize remaining = offset < capacity ? capacity - offset : 0;
    if (n > remaining) {
        n = remaining;
    }
    memcpy(buf + offset, sv.Data(), n);
    return offset + n;
}

usize AppendUIntToBuffer(char* buf, usize capacity, usize offset, u64 value) {
    char digits[20];
    int n = 0;
    if (value == 0) {
        digits[n++] = '0';
    } else {
        while (value > 0) {
            digits[n++] = static_cast<char>('0' + (value % 10));
            value /= 10;
        }
    }
    while (n > 0) {
        if (offset < capacity) {
            buf[offset++] = digits[n - 1];
        }
        --n;
    }
    return offset;
}

LogVerbosity ParseVerbosityName(StringView v) {
    if (v == StringView("Error")) {
        return LogVerbosity::Error;
    }
    if (v == StringView("Warn")) {
        return LogVerbosity::Warn;
    }
    if (v == StringView("Info")) {
        return LogVerbosity::Info;
    }
    if (v == StringView("Debug")) {
        return LogVerbosity::Debug;
    }
    if (v == StringView("Trace")) {
        return LogVerbosity::Trace;
    }
    return LogVerbosity::Warn;
}

void ParseConfigLine(StringView line) {
    usize eq = line.IndexOf('=');
    if (eq == line.Length()) {
        return; // not a key=value line
    }
    StringView key = line.Substring(0, eq);
    StringView value = line.Substring(eq + 1, line.Length() - eq - 1);

    usize dot = key.IndexOf('.');
    if (dot == key.Length()) {
        return; // not "channel.attribute"
    }
    StringView channel_name = key.Substring(0, dot);
    StringView attribute = key.Substring(dot + 1, key.Length() - dot - 1);

    LogChannel* channel = LogChannel::Find(channel_name);
    if (channel == nullptr) {
        return;
    }
    if (attribute == StringView("verbosity")) {
        channel->SetVerbosity(ParseVerbosityName(value));
    } else if (attribute == StringView("enabled")) {
        channel->SetEnabled(value == StringView("true") || value == StringView("1"));
    }
}

} // namespace

void SetWriteSinkForTesting(WriteSinkFn sink) {
    g_write_sink = sink;
}

void SetExitSinkForTesting(ExitSinkFn sink) {
    g_exit_sink = sink;
}

void Log(LogChannel& channel, LogVerbosity level, StringView message) {
    if (!channel.ShouldLog(level)) {
        return;
    }
    char buf[512];
    usize offset = 0;
    offset = AppendStrToBuffer(buf, sizeof(buf), offset, "[");
    offset = AppendStrToBuffer(buf, sizeof(buf), offset, channel.Name());
    offset = AppendStrToBuffer(buf, sizeof(buf), offset, "] ");
    offset = AppendStrToBuffer(buf, sizeof(buf), offset, VerbosityName(level));
    offset = AppendStrToBuffer(buf, sizeof(buf), offset, ": ");
    offset = AppendStrToBuffer(buf, sizeof(buf), offset, message);
    offset = AppendStrToBuffer(buf, sizeof(buf), offset, "\n");
    g_write_sink(1, buf, offset);
}

[[noreturn]] void AssertFail(const char* file, int line, const char* message) {
    char buf[512];
    usize offset = 0;
    offset = AppendStrToBuffer(buf, sizeof(buf), offset, "ASSERT FAILED: ");
    offset = AppendStrToBuffer(buf, sizeof(buf), offset, file);
    offset = AppendStrToBuffer(buf, sizeof(buf), offset, ":");
    offset = AppendUIntToBuffer(buf, sizeof(buf), offset, static_cast<u64>(line));
    offset = AppendStrToBuffer(buf, sizeof(buf), offset, " ");
    offset = AppendStrToBuffer(buf, sizeof(buf), offset, message);
    offset = AppendStrToBuffer(buf, sizeof(buf), offset, "\n");
    g_write_sink(1, buf, offset);
    g_exit_sink(1);
    for (;;) {
        // Unreachable: both the real os::Exit and any test-installed
        // exit sink never return (the latter via longjmp).
    }
}

bool LoadConfig(const char* path) {
    int fd = os::Open(path, O_RDONLY);
    if (fd < 0) {
        return false;
    }
    char buffer[4096];
    long n = os::Read(fd, buffer, sizeof(buffer));
    os::Close(fd);
    if (n <= 0) {
        return false;
    }
    usize len = static_cast<usize>(n);

    usize line_start = 0;
    for (usize i = 0; i <= len; ++i) {
        if (i == len || buffer[i] == '\n') {
            usize line_len = i - line_start;
            if (line_len > 0) {
                ParseConfigLine(StringView(buffer + line_start, line_len));
            }
            line_start = i + 1;
        }
    }
    return true;
}

} // namespace engine_placeholder
