#include "log/log.h"

#include "log/diagnostics_hooks.h"
#include "os/os.h"

#include "sync/atomic.h"
#include "sync/thread.h"
#include "os/console.h"
#include "cstr/mem_ops.h"

namespace engine_placeholder
{

LogChannel* LogChannel::head_ = nullptr;

LogChannel LogCore("LogCore");
LogChannel LogRender("LogRender");
LogChannel LogPlatform("LogPlatform");

LogChannel::LogChannel(const char* name, LogVerbosity default_verbosity)
    : name_(name), verbosity_(default_verbosity), enabled_(true)
{
    next_ = head_;
    head_ = this;
}

LogChannel* LogChannel::Find(StringView name)
{
    for (LogChannel* c = head_; c != nullptr; c = c->next_)
    {
        if (StringView(c->name_) == name)
        {
            return c;
        }
    }
    return nullptr;
}

namespace
{

WriteSinkFn g_write_sink = &os::Write;

// ---- the ring of recent log lines
// ------------------------------------------------------------------------------------------
//
// Each slot is a seqlock: `sequence` is odd while a writer fills the slot and 2 * (line number + 1) once it holds that
// line. The text is stored as relaxed atomic words, so a reader that races a writer is well-defined (and race-free for
// TSan); it notices the race from the sequence and drops the slot. Writers never wait for each other or for readers.
// Two writers on one slot would need the ring to wrap (256 lines) while a writer is stalled mid-copy; the reader then
// sees a changed sequence and skips.
constexpr usize kRingWords = kLogRingLineBytes / sizeof(u64);

struct RingSlot
{
    Atomic<u64> sequence{0};
    Atomic<u32> length{0};
    Atomic<u64> words[kRingWords];
};

RingSlot g_log_ring[kLogRingSlots];
Atomic<u64> g_log_ring_head{0};  // number of lines ever written

// SIGNAL-SAFE-BEGIN
void RingAppend(const char* text, usize length)
{
    if (length > kLogRingLineBytes)
    {
        length = kLogRingLineBytes;
    }
    const u64 line = g_log_ring_head.FetchAdd(1, MemoryOrder::Relaxed);
    RingSlot& slot = g_log_ring[line % kLogRingSlots];
    // Odd means being written. The data stores below are release stores, so the odd mark is visible before any of the
    // new data on weakly ordered CPUs (an acquire RMW alone does not order later stores; a fence would, but TSan does
    // not model fences). Only one writer at a time may own a slot, or a writer that was descheduled and lapped by 256
    // newer lines would later overwrite the text of the newer line while the slot still says it holds that newer line.
    // So claim the slot only if it is idle (even) and does not already hold a newer line; otherwise leave this line out
    // of the ring (it is still logged).
    u64 current = slot.sequence.Load(MemoryOrder::Relaxed);
    if ((current & 1) != 0 || current > 2 * line + 1 ||
        !slot.sequence.CompareExchangeStrong(current, 2 * line + 1, MemoryOrder::Acquire, MemoryOrder::Relaxed))
    {
        return;
    }
    slot.length.Store(static_cast<u32>(length), MemoryOrder::Release);
    for (usize w = 0; w < kRingWords; ++w)
    {
        u64 word = 0;
        const usize offset = w * sizeof(u64);
        if (offset < length)
        {
            const usize n = length - offset < sizeof(u64) ? length - offset : sizeof(u64);
            MemCopy(&word, text + offset, n);
        }
        slot.words[w].Store(word, MemoryOrder::Release);
    }
    slot.sequence.Store(2 * line + 2, MemoryOrder::Release);  // even: holds line `line`
}

// SIGNAL-SAFE-END
const char* VerbosityName(LogVerbosity level)
{
    switch (level)
    {
    case LogVerbosity::Error: return "ERROR";
    case LogVerbosity::Warn: return "WARN";
    case LogVerbosity::Info: return "INFO";
    case LogVerbosity::Debug: return "DEBUG";
    case LogVerbosity::Trace: return "TRACE";
    }
    return "?";
}

usize AppendStrToBuffer(char* buf, usize capacity, usize offset, StringView sv)
{
    usize n = sv.Length();
    usize remaining = offset < capacity ? capacity - offset : 0;
    if (n > remaining)
    {
        n = remaining;
    }
    MemCopy(buf + offset, sv.Data(), n);
    return offset + n;
}

usize AppendUIntToBuffer(char* buf, usize capacity, usize offset, u64 value)
{
    char digits[20];
    int n = 0;
    if (value == 0)
    {
        digits[n++] = '0';
    }
    else
    {
        while (value > 0)
        {
            digits[n++] = static_cast<char>('0' + (value % 10));
            value /= 10;
        }
    }
    while (n > 0)
    {
        if (offset < capacity)
        {
            buf[offset++] = digits[n - 1];
        }
        --n;
    }
    return offset;
}

}  // namespace

void SetWriteSinkForTesting(WriteSinkFn sink)
{
    g_write_sink = sink;
}

namespace
{
Atomic<bool> g_log_error_traces{false};
}

void SetLogErrorTraces(bool enabled)
{
    g_log_error_traces.Store(enabled, MemoryOrder::Relaxed);
}

bool LogErrorTraces()
{
    return g_log_error_traces.Load(MemoryOrder::Relaxed);
}

namespace
{
// The observer is a pair read by every Log call; `g_observer_busy` counts the calls using it so that SetLogObserver can
// wait them out. Plain statics in this one translation unit (one copy in the engine library).
Atomic<LogObserverFn> g_observer_fn{nullptr};
Atomic<void*> g_observer_user{nullptr};
Atomic<int> g_observer_busy{0};
}  // namespace

void SetLogObserver(LogObserverFn fn, void* user)
{
    g_observer_fn.Store(nullptr, MemoryOrder::SeqCst);  // new calls stop using the old pair
    while (g_observer_busy.Load(MemoryOrder::SeqCst) != 0)
    {
        ThisThread::Yield();
    }
    g_observer_user.Store(user, MemoryOrder::SeqCst);
    g_observer_fn.Store(fn, MemoryOrder::SeqCst);
}

void Log(LogChannel& channel, LogVerbosity level, StringView message)
{
    if (!channel.ShouldLog(level))
    {
        return;
    }
    bool handled = false;
    g_observer_busy.FetchAdd(1, MemoryOrder::SeqCst);
    if (LogObserverFn observer = g_observer_fn.Load(MemoryOrder::SeqCst))
    {
        handled = observer(g_observer_user.Load(MemoryOrder::SeqCst), channel.Name(), level, message);
    }
    g_observer_busy.FetchSub(1, MemoryOrder::SeqCst);
    char buf[512];
    // Everything but the newline goes into the first 511 bytes, so a long message is cut but the line still ends.
    constexpr usize kText = sizeof(buf) - 1;
    usize offset = 0;
    offset = AppendStrToBuffer(buf, kText, offset, "[");
    offset = AppendStrToBuffer(buf, kText, offset, channel.Name());
    offset = AppendStrToBuffer(buf, kText, offset, "] ");
    offset = AppendStrToBuffer(buf, kText, offset, VerbosityName(level));
    offset = AppendStrToBuffer(buf, kText, offset, ": ");
    offset = AppendStrToBuffer(buf, kText, offset, message);
    buf[offset++] = '\n';
    RingAppend(buf, offset);
    if (handled)
    {
        return;
    }
    if (level == LogVerbosity::Error && g_log_error_traces.Load(MemoryOrder::Relaxed))
    {
        // Where the error came from, under the line, in the same write so another thread's output cannot come between
        // them (names stay mangled: demangling allocates).
        char text[sizeof(buf) + 4096];
        MemCopy(text, buf, offset);
        usize length = offset;
        const FailureReportHooks* hooks = GetFailureReportHooks();
        if (hooks != nullptr && hooks->append_stack_trace != nullptr)
        {
            length += hooks->append_stack_trace(text + offset, sizeof(text) - offset, 1);
        }
        g_write_sink(1, text, length);
        return;
    }
    g_write_sink(1, buf, offset);
}

// SIGNAL-SAFE-BEGIN
namespace
{

// Delivers lines [first, head) that are still whole in the ring, oldest first. Returns the number of the first line it
// stopped at because a writer had claimed it and was still copying it (only that line, or one of the newest few whose
// writer has not claimed its slot yet, can be in that state without being lost), or `head` when it read to the end; a
// caller that resumes from the returned number then loses no line to a race with a writer. Without `stop_at_pending` it
// passes such a line by (the crash report wants the rest).
constexpr u64
    kPendingWindow = 8;  // how many of the newest lines a reader waits for when their slots are not yet written

u64 ReadRingRange(u64 first, u64 head, RecentLogLineFn callback, void* context, bool stop_at_pending)
{
    // The loads of the text below are acquire: if one of them sees a word a newer writer stored (a release store, made
    // after that writer marked the slot odd), the second look at the sequence is then guaranteed to see the mark and
    // drop the line. An acquire load keeps everything after it, including the second look, from happening before it, so
    // the loads cannot drift past that look.
    for (u64 line = first; line < head; ++line)
    {
        RingSlot& slot = g_log_ring[line % kLogRingSlots];
        const u64 before = slot.sequence.Load(MemoryOrder::Acquire);
        if (before != 2 * line + 2)
        {
            // A writer that took its number and was descheduled before it claimed the slot leaves the slot holding an
            // older line while newer lines complete after it, so "pending" is not only the newest line: any recent line
            // whose slot still holds an older one may be on its way. A line older than the window is taken as left out.
            if (stop_at_pending && (before == 2 * line + 1 || (before < 2 * line + 2 && line + kPendingWindow >= head)))
            {
                return line;  // a writer is copying it, or has just taken its number: it is whole a moment later
            }
            continue;  // already overwritten by a newer line, or left out of the ring (the slot was busy)
        }
        char text[kLogRingLineBytes + 1];
        u64 words[kRingWords];
        const u32 length = slot.length.Load(MemoryOrder::Acquire);
        for (usize w = 0; w < kRingWords; ++w)
        {
            words[w] = slot.words[w].Load(MemoryOrder::Acquire);
        }
        if (slot.sequence.FetchAdd(0, MemoryOrder::Release) != before || length > kLogRingLineBytes)
        {
            continue;  // changed while we copied: torn, drop it
        }
        MemCopy(text, words, kLogRingLineBytes);
        text[length] = '\0';
        callback(text, length, context);
    }
    return head;
}

}  // namespace

void ReadRecentLogLines(RecentLogLineFn callback, void* context, usize max_lines)
{
    const u64 head = g_log_ring_head.Load(MemoryOrder::Acquire);
    u64 first = head > kLogRingSlots ? head - kLogRingSlots : 0;
    if (max_lines != 0 && head - first > max_lines)
    {
        first = head - max_lines;
    }
    ReadRingRange(first, head, callback, context, false);
}

u64 LogLinesWritten()
{
    return g_log_ring_head.Load(MemoryOrder::Acquire);
}

u64 ReadLogLinesSince(u64 next, RecentLogLineFn callback, void* context)
{
    const u64 head = g_log_ring_head.Load(MemoryOrder::Acquire);
    u64 first = head > kLogRingSlots ? head - kLogRingSlots : 0;
    if (next > first)
    {
        first = next < head ? next : head;
    }
    return ReadRingRange(first, head, callback, context, true);
}

// SIGNAL-SAFE-END
void ResetLogRingForTesting()
{
    g_log_ring_head.Store(0, MemoryOrder::Relaxed);
    for (RingSlot& slot : g_log_ring)
    {
        slot.sequence.Store(0, MemoryOrder::Relaxed);
        slot.length.Store(0, MemoryOrder::Relaxed);
    }
}

namespace
{

const char* AssertKindLabel(AssertKind kind)
{
    switch (kind)
    {
    case AssertKind::Assert: return "ASSERT FAILED: ";
    case AssertKind::Ensure: return "ENSURE FAILED: ";
    case AssertKind::Verify: return "VERIFY FAILED: ";
    case AssertKind::Unreachable: return "UNREACHABLE REACHED: ";
    case AssertKind::NotImplemented: return "NOT IMPLEMENTED: ";
    }
    return "ASSERT FAILED: ";
}

Atomic<AssertHandler> g_assert_handler{&DefaultAssertHandler};

// Head of the list of ENGINE_ENSURE call sites that have failed.
Atomic<detail::EnsureSite*> g_ensure_head{nullptr};

}  // namespace

// Sends a piece of report text to the shared write sink.
static void AssertReportWrite(void*, const char* data, usize length)
{
    g_write_sink(1, data, length);
}

AssertAction DefaultAssertHandler(const AssertInfo& info)
{
    // A null string must not crash the code that reports a failure.
    auto text = [](const char* s) { return s != nullptr ? s : ""; };
    char buf[512];
    usize offset = 0;
    offset = AppendStrToBuffer(buf, sizeof(buf), offset, AssertKindLabel(info.kind));
    offset = AppendStrToBuffer(buf, sizeof(buf), offset, text(info.file));
    offset = AppendStrToBuffer(buf, sizeof(buf), offset, ":");
    offset = AppendUIntToBuffer(buf, sizeof(buf), offset, static_cast<u64>(info.line));
    if (info.function != nullptr && info.function[0] != '\0')
    {
        offset = AppendStrToBuffer(buf, sizeof(buf), offset, " ");
        offset = AppendStrToBuffer(buf, sizeof(buf), offset, text(info.function));
        offset = AppendStrToBuffer(buf, sizeof(buf), offset, ":");
    }
    if (info.condition != nullptr && info.condition[0] != '\0')
    {
        offset = AppendStrToBuffer(buf, sizeof(buf), offset, " ");
        offset = AppendStrToBuffer(buf, sizeof(buf), offset, text(info.condition));
        offset = AppendStrToBuffer(buf, sizeof(buf), offset, ":");
    }
    offset = AppendStrToBuffer(buf, sizeof(buf), offset, " ");
    offset = AppendStrToBuffer(buf, sizeof(buf), offset, text(info.message));
    if (offset >= sizeof(buf))
    {
        offset = sizeof(buf) - 1;  // truncated: keep room for the newline
    }
    buf[offset++] = '\n';

    // The message line and, under it, the stack trace with the first frame at the line that failed, in one buffer and
    // one write so concurrent failures do not interleave. The frames above are this handler, AssertFailed and, for an
    // ensure, EnsureFailed and the macro's lambda (all kept out of line so the count holds); reached through a wrapper,
    // the trace simply starts with the wrapper's frames. Names stay mangled: demangling allocates, and an assertion can
    // fail inside the allocator.
    char report[sizeof(buf) + 8192];
    MemCopy(report, buf, offset);
    usize report_length = offset;
    // Without the hooks (nothing installed them yet) the report is the message line alone.
    const FailureReportHooks* hooks = GetFailureReportHooks();
    if (hooks != nullptr && hooks->append_stack_trace != nullptr)
    {
        report_length += hooks->append_stack_trace(report + offset, sizeof(report) - offset,
                                                   info.kind == AssertKind::Ensure ? 4 : 2);
    }
    g_write_sink(1, report, report_length);

    // A failed check that ends the program also says what program and machine it was, what the subsystems report and
    // what was logged last, exactly like a crash report does. An ensure carries on, so it gets only the message and the
    // trace.
    if (info.kind != AssertKind::Ensure && hooks != nullptr && hooks->write_crash_context != nullptr)
    {
        hooks->write_crash_context(&AssertReportWrite, nullptr);
    }

    if (hooks != nullptr && hooks->debugger_attached != nullptr && hooks->debugger_attached())
    {
        return AssertAction::Break;
    }
    return info.kind == AssertKind::Ensure ? AssertAction::Continue : AssertAction::Abort;
}

namespace
{
Atomic<const FailureReportHooks*> g_failure_hooks{nullptr};
Atomic<ThreadStartHook> g_thread_start_hook{nullptr};
Atomic<ThreadEndHook> g_thread_end_hooks[kMaxThreadEndHooks];
}  // namespace

const FailureReportHooks* SetFailureReportHooks(const FailureReportHooks* hooks)
{
    return g_failure_hooks.Exchange(hooks, MemoryOrder::AcqRel);
}

const FailureReportHooks* GetFailureReportHooks()
{
    return g_failure_hooks.Load(MemoryOrder::Acquire);
}

ThreadStartHook SetThreadStartHook(ThreadStartHook hook)
{
    return g_thread_start_hook.Exchange(hook, MemoryOrder::AcqRel);
}

void NotifyThreadStart(const char* name)
{
    ThreadStartHook hook = g_thread_start_hook.Load(MemoryOrder::Acquire);
    if (hook != nullptr)
    {
        hook(name);
    }
}

bool AddThreadEndHook(ThreadEndHook hook)
{
    for (usize i = 0; i < kMaxThreadEndHooks; ++i)
    {
        ThreadEndHook expected = nullptr;
        if (g_thread_end_hooks[i].CompareExchangeStrong(expected, hook, MemoryOrder::AcqRel) || expected == hook)
        {
            return true;
        }
    }
    return false;
}

void NotifyThreadEnd()
{
    for (usize i = 0; i < kMaxThreadEndHooks; ++i)
    {
        ThreadEndHook hook = g_thread_end_hooks[i].Load(MemoryOrder::Acquire);
        if (hook != nullptr)
        {
            hook();
        }
    }
}

AssertHandler SetAssertHandler(AssertHandler handler)
{
    return g_assert_handler.Exchange(handler != nullptr ? handler : &DefaultAssertHandler, MemoryOrder::AcqRel);
}

[[noreturn]] void AssertFail(const char* file, int line, const char* message)
{
    detail::AssertFailed(AssertKind::Assert, file, line, "", "", message);
    os::Abort();  // the handler chose to continue (or break and was resumed); this function does not return
}

namespace detail
{

AssertAction AssertFailed(AssertKind kind, const char* file, int line, const char* function, const char* condition,
                          const char* message)
{
    const AssertInfo info{kind, file, line, function, condition, message};
    AssertAction action = g_assert_handler.Load(MemoryOrder::Acquire)(info);
    // Control cannot continue past a place declared unreachable.
    if (action == AssertAction::Continue && kind == AssertKind::Unreachable)
    {
        action = AssertAction::Abort;
    }
    if (action == AssertAction::Abort)
    {
        os::Abort();
    }
    return action;
}

void UnreachableAbort()
{
    os::Abort();
}

AssertAction EnsureFailed(EnsureSite& site, const char* function)
{
    site.count.FetchAdd(1, MemoryOrder::Relaxed);
    // `reported`, not the count, decides who is first: the count is only a statistic and may wrap.
    if (site.reported.Exchange(true, MemoryOrder::AcqRel))
    {
        return AssertAction::Continue;  // already reported; only counted
    }
    // The first failure at this site: publish it in the list, then report. Only one thread gets here per site.
    site.function = function;
    EnsureSite* head = g_ensure_head.Load(MemoryOrder::Relaxed);
    do
    {
        site.next = head;
    } while (!g_ensure_head.CompareExchangeWeak(head, &site, MemoryOrder::Release, MemoryOrder::Relaxed));
    const AssertAction action = AssertFailed(AssertKind::Ensure, site.file, site.line, function, site.condition,
                                             site.message);
    // Not a tail call: the stack trace the default handler prints counts this frame, and an optimizing compiler would
    // drop it.
    ENGINE_COMPILER_BARRIER();
    return action;
}

}  // namespace detail

void ForEachFailedEnsure(EnsureVisitor visitor, void* context)
{
    for (const detail::EnsureSite* s = g_ensure_head.Load(MemoryOrder::Acquire); s != nullptr; s = s->next)
    {
        const EnsureSiteInfo info{s->file,      s->line,    s->function != nullptr ? s->function : "",
                                  s->condition, s->message, s->count.Load(MemoryOrder::Relaxed)};
        visitor(info, context);
    }
}

u32 EnsureFailedSiteCount()
{
    u32 n = 0;
    for (const detail::EnsureSite* s = g_ensure_head.Load(MemoryOrder::Acquire); s != nullptr; s = s->next)
    {
        ++n;
    }
    return n;
}

void ResetEnsureStateForTesting()
{
    detail::EnsureSite* s = g_ensure_head.Exchange(nullptr, MemoryOrder::AcqRel);
    while (s != nullptr)
    {
        detail::EnsureSite* next = s->next;
        s->count.Store(0, MemoryOrder::Relaxed);
        s->reported.Store(false, MemoryOrder::Relaxed);
        s->next = nullptr;
        s = next;
    }
}

}  // namespace engine_placeholder
