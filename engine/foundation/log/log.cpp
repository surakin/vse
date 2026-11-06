#include "log/log.h"

#include "debug/crash_context.h"
#include "debug/crash_report.h"
#include "debug/debugger.h"
#include "debug/stack_trace.h"
#include "os/os.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>

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

// ---- the ring of recent log lines ------------------------------------------------------------------------------------------
//
// Each slot is a seqlock: `sequence` is odd while a writer fills the slot and 2 * (line number + 1) once it holds that line.
// The text is stored as relaxed atomic words, so a reader that races a writer is well-defined (and race-free for TSan); it
// notices the race from the sequence and drops the slot. Writers never wait for each other or for readers. Two writers on
// one slot would need the ring to wrap (256 lines) while a writer is stalled mid-copy; the reader then sees a changed
// sequence and skips.
constexpr usize kRingWords = kLogRingLineBytes / sizeof(u64);

struct RingSlot {
    std::atomic<u64> sequence{0};
    std::atomic<u32> length{0};
    std::atomic<u64> words[kRingWords];
};

RingSlot g_log_ring[kLogRingSlots];
std::atomic<u64> g_log_ring_head{0}; // number of lines ever written

void RingAppend(const char* text, usize length) {
    if (length > kLogRingLineBytes) {
        length = kLogRingLineBytes;
    }
    const u64 line = g_log_ring_head.fetch_add(1, std::memory_order_relaxed);
    RingSlot& slot = g_log_ring[line % kLogRingSlots];
    // Odd: being written. The data stores below are release stores so the odd marker above is visible before any of the new
    // data on weakly ordered CPUs (an acquire exchange alone does not order later stores; a fence would, but TSan does not
    // model fences). The exchange is an acquire RMW so the reader's final check sees every earlier writer.
    slot.sequence.exchange(2 * line + 1, std::memory_order_acquire);
    slot.length.store(static_cast<u32>(length), std::memory_order_release);
    for (usize w = 0; w < kRingWords; ++w) {
        u64 word = 0;
        const usize offset = w * sizeof(u64);
        if (offset < length) {
            const usize n = length - offset < sizeof(u64) ? length - offset : sizeof(u64);
            std::memcpy(&word, text + offset, n);
        }
        slot.words[w].store(word, std::memory_order_release);
    }
    slot.sequence.store(2 * line + 2, std::memory_order_release); // even: holds line `line`
}

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
    RingAppend(buf, offset);
    g_write_sink(1, buf, offset);
}

void ReadRecentLogLines(RecentLogLineFn callback, void* context, usize max_lines) {
    // The loads of the text below are relaxed; the release read-modify-write after them keeps them from moving past the
    // second look at the sequence.
    const u64 head = g_log_ring_head.load(std::memory_order_acquire);
    u64 first = head > kLogRingSlots ? head - kLogRingSlots : 0;
    if (max_lines != 0 && head - first > max_lines) {
        first = head - max_lines;
    }
    for (u64 line = first; line < head; ++line) {
        RingSlot& slot = g_log_ring[line % kLogRingSlots];
        const u64 before = slot.sequence.load(std::memory_order_acquire);
        if (before != 2 * line + 2) {
            continue; // being written, or already overwritten by a newer line
        }
        char text[kLogRingLineBytes + 1];
        u64 words[kRingWords];
        const u32 length = slot.length.load(std::memory_order_relaxed);
        for (usize w = 0; w < kRingWords; ++w) {
            words[w] = slot.words[w].load(std::memory_order_relaxed);
        }
        if (slot.sequence.fetch_add(0, std::memory_order_release) != before || length > kLogRingLineBytes) {
            continue; // changed while we copied: torn, drop it
        }
        std::memcpy(text, words, kLogRingLineBytes);
        text[length] = '\0';
        callback(text, length, context);
    }
}

void ResetLogRingForTesting() {
    g_log_ring_head.store(0, std::memory_order_relaxed);
    for (RingSlot& slot : g_log_ring) {
        slot.sequence.store(0, std::memory_order_relaxed);
        slot.length.store(0, std::memory_order_relaxed);
    }
}

namespace {

const char* AssertKindLabel(AssertKind kind) {
    switch (kind) {
    case AssertKind::Assert: return "ASSERT FAILED: ";
    case AssertKind::Ensure: return "ENSURE FAILED: ";
    case AssertKind::Verify: return "VERIFY FAILED: ";
    case AssertKind::Unreachable: return "UNREACHABLE REACHED: ";
    case AssertKind::NotImplemented: return "NOT IMPLEMENTED: ";
    }
    return "ASSERT FAILED: ";
}

std::atomic<AssertHandler> g_assert_handler{&DefaultAssertHandler};

// Head of the list of ENGINE_ENSURE call sites that have failed.
std::atomic<detail::EnsureSite*> g_ensure_head{nullptr};

} // namespace

// Sends a piece of report text to the shared write sink.
static void AssertReportWrite(void*, const char* data, usize length) {
    g_write_sink(1, data, length);
}

AssertAction DefaultAssertHandler(const AssertInfo& info) {
    // A null string must not crash the code that reports a failure.
    auto text = [](const char* s) { return s != nullptr ? s : ""; };
    char buf[512];
    usize offset = 0;
    offset = AppendStrToBuffer(buf, sizeof(buf), offset, AssertKindLabel(info.kind));
    offset = AppendStrToBuffer(buf, sizeof(buf), offset, text(info.file));
    offset = AppendStrToBuffer(buf, sizeof(buf), offset, ":");
    offset = AppendUIntToBuffer(buf, sizeof(buf), offset, static_cast<u64>(info.line));
    if (info.function != nullptr && info.function[0] != '\0') {
        offset = AppendStrToBuffer(buf, sizeof(buf), offset, " ");
        offset = AppendStrToBuffer(buf, sizeof(buf), offset, text(info.function));
        offset = AppendStrToBuffer(buf, sizeof(buf), offset, ":");
    }
    if (info.condition != nullptr && info.condition[0] != '\0') {
        offset = AppendStrToBuffer(buf, sizeof(buf), offset, " ");
        offset = AppendStrToBuffer(buf, sizeof(buf), offset, text(info.condition));
        offset = AppendStrToBuffer(buf, sizeof(buf), offset, ":");
    }
    offset = AppendStrToBuffer(buf, sizeof(buf), offset, " ");
    offset = AppendStrToBuffer(buf, sizeof(buf), offset, text(info.message));
    if (offset >= sizeof(buf)) {
        offset = sizeof(buf) - 1; // truncated: keep room for the newline
    }
    buf[offset++] = '\n';

    // The message line and, under it, the stack trace with the first frame at the line that failed, in one buffer and one
    // write so concurrent failures do not interleave. The frames above are this handler, AssertFailed and, for an ensure,
    // EnsureFailed and the macro's lambda (all kept out of line so the count holds); reached through a wrapper, the trace
    // simply starts with the wrapper's frames. Names stay mangled: demangling allocates, and an assertion can fail inside
    // the allocator.
    char report[sizeof(buf) + 8192];
    std::memcpy(report, buf, offset);
    usize report_length = offset;
    debug::StackTrace trace;
    if (debug::CaptureStackTrace(trace, info.kind == AssertKind::Ensure ? 4 : 2)) {
        report_length += debug::FormatStackTrace(trace, report + offset, sizeof(report) - offset);
    }
    g_write_sink(1, report, report_length);

    // A failed check that ends the program also says what program and machine it was, what the subsystems report and what was
    // logged last, exactly like a crash report does. An ensure carries on, so it gets only the message and the trace.
    if (info.kind != AssertKind::Ensure) {
        if (!debug::CrashContextInitialized()) {
            debug::InitCrashContext(0, nullptr); // an assert is not a signal handler, so reading /proc is fine here
        }
        debug::CrashReportSink sink;
        sink.write = &AssertReportWrite;
        debug::WriteCrashContext(sink);
    }

    if (debug::IsDebuggerAttached()) {
        return AssertAction::Break;
    }
    return info.kind == AssertKind::Ensure ? AssertAction::Continue : AssertAction::Abort;
}

AssertHandler SetAssertHandler(AssertHandler handler) {
    return g_assert_handler.exchange(handler != nullptr ? handler : &DefaultAssertHandler, std::memory_order_acq_rel);
}

[[noreturn]] void AssertFail(const char* file, int line, const char* message) {
    detail::AssertFailed(AssertKind::Assert, file, line, "", "", message);
    std::abort(); // the handler chose to continue (or break and was resumed); this function does not return
}

namespace detail {

AssertAction AssertFailed(AssertKind kind, const char* file, int line, const char* function, const char* condition,
                          const char* message) {
    const AssertInfo info{kind, file, line, function, condition, message};
    AssertAction action = g_assert_handler.load(std::memory_order_acquire)(info);
    // Control cannot continue past a place declared unreachable.
    if (action == AssertAction::Continue && kind == AssertKind::Unreachable) {
        action = AssertAction::Abort;
    }
    if (action == AssertAction::Abort) {
        std::abort();
    }
    return action;
}

void UnreachableAbort() {
    std::abort();
}

AssertAction EnsureFailed(EnsureSite& site, const char* function) {
    site.count.fetch_add(1, std::memory_order_relaxed);
    // `reported`, not the count, decides who is first: the count is only a statistic and may wrap.
    if (site.reported.exchange(true, std::memory_order_acq_rel)) {
        return AssertAction::Continue; // already reported; only counted
    }
    // The first failure at this site: publish it in the list, then report. Only one thread gets here per site.
    site.function = function;
    EnsureSite* head = g_ensure_head.load(std::memory_order_relaxed);
    do {
        site.next = head;
    } while (!g_ensure_head.compare_exchange_weak(head, &site, std::memory_order_release, std::memory_order_relaxed));
    const AssertAction action = AssertFailed(AssertKind::Ensure, site.file, site.line, function, site.condition, site.message);
    // Not a tail call: the stack trace the default handler prints counts this frame, and an optimizing compiler would drop it.
    ENGINE_COMPILER_BARRIER();
    return action;
}

} // namespace detail

void ForEachFailedEnsure(EnsureVisitor visitor, void* context) {
    for (const detail::EnsureSite* s = g_ensure_head.load(std::memory_order_acquire); s != nullptr; s = s->next) {
        const EnsureSiteInfo info{s->file, s->line, s->function != nullptr ? s->function : "", s->condition, s->message,
                                  s->count.load(std::memory_order_relaxed)};
        visitor(info, context);
    }
}

u32 EnsureFailedSiteCount() {
    u32 n = 0;
    for (const detail::EnsureSite* s = g_ensure_head.load(std::memory_order_acquire); s != nullptr; s = s->next) {
        ++n;
    }
    return n;
}

void ResetEnsureStateForTesting() {
    detail::EnsureSite* s = g_ensure_head.exchange(nullptr, std::memory_order_acq_rel);
    while (s != nullptr) {
        detail::EnsureSite* next = s->next;
        s->count.store(0, std::memory_order_relaxed);
        s->reported.store(false, std::memory_order_relaxed);
        s->next = nullptr;
        s = next;
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
