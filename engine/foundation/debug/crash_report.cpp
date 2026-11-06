#include "debug/crash_report.h"

#include "debug/crash_context.h"
#include "debug/thread_info.h"
#include "log/log.h"

#include <atomic>

namespace engine_placeholder::debug {

// SIGNAL-SAFE-BEGIN
namespace {

struct ProviderEntry {
    const char* name;
    CrashStateFn fn;
    void* context;
    std::atomic<bool> ready; // set (release) once the three fields above are written; a report skips an entry not ready
};

ProviderEntry g_providers[kMaxCrashStateProviders];
std::atomic<usize> g_provider_count{0};

// Builds one line (or a few) in a small buffer on the stack and hands it to the sink in one call, so each piece that has
// been written is whole.
class LineWriter {
public:
    explicit LineWriter(const CrashReportSink& sink) : sink_(sink) {}
    ~LineWriter() { Flush(); }
    LineWriter(const LineWriter&) = delete;
    LineWriter& operator=(const LineWriter&) = delete;

    LineWriter& Text(const char* text) {
        for (; text != nullptr && *text != '\0'; ++text) {
            Char(*text);
        }
        return *this;
    }
    LineWriter& Char(char c) {
        if (length_ == sizeof(buffer_)) {
            Flush();
        }
        buffer_[length_++] = c;
        return *this;
    }
    LineWriter& Decimal(u64 value) {
        char digits[20];
        int n = 0;
        do {
            digits[n++] = static_cast<char>('0' + (value % 10));
            value /= 10;
        } while (value > 0);
        while (n > 0) {
            Char(digits[--n]);
        }
        return *this;
    }
    LineWriter& Hex(u64 value, int digits) {
        for (int shift = (digits - 1) * 4; shift >= 0; shift -= 4) {
            Char("0123456789abcdef"[(value >> shift) & 0xf]);
        }
        return *this;
    }
    void Flush() {
        if (length_ > 0 && sink_.write != nullptr) {
            sink_.write(sink_.context, buffer_, length_);
        }
        length_ = 0;
    }
    // Ends the line and sends it.
    void EndLine() {
        Char('\n');
        Flush();
    }

private:
    CrashReportSink sink_;
    char buffer_[512];
    usize length_ = 0;
};

void WriteLogLine(const char* line, usize length, void* context) {
    const CrashReportSink& sink = *static_cast<const CrashReportSink*>(context);
    if (sink.write != nullptr && length > 0) {
        sink.write(sink.context, line, length);
        if (line[length - 1] != '\n') {
            sink.write(sink.context, "\n", 1);
        }
    }
}

} // namespace

// SIGNAL-SAFE-END
bool RegisterCrashStateProvider(const char* name, CrashStateFn fn, void* context) {
    if (name == nullptr || fn == nullptr) {
        return false;
    }
    // Claim a slot, fill it, then publish it by the count; a report only reads entries below the count.
    usize slot = g_provider_count.load(std::memory_order_relaxed);
    do {
        if (slot >= kMaxCrashStateProviders) {
            return false;
        }
    } while (!g_provider_count.compare_exchange_weak(slot, slot + 1, std::memory_order_relaxed, std::memory_order_relaxed));
    ProviderEntry& entry = g_providers[slot];
    entry.name = name;
    entry.fn = fn;
    entry.context = context;
    entry.ready.store(true, std::memory_order_release);
    return true;
}

// SIGNAL-SAFE-BEGIN
void ResetCrashStateProvidersForTesting() {
    g_provider_count.store(0, std::memory_order_relaxed);
    for (ProviderEntry& entry : g_providers) {
        entry.ready.store(false, std::memory_order_relaxed);
    }
}

void WriteCrashContext(const CrashReportSink& sink) {
    const CrashContext& c = GetCrashContext();
    {
        LineWriter w(sink);
        w.Text("--- build ---").EndLine();
        w.Text("build id: ").Text(c.build_id[0] != '\0' ? c.build_id : "(none)").EndLine();
        w.Text("engine: ").Text(c.engine_version).EndLine();
        w.Text("executable: ").Text(c.executable).EndLine();
        if (c.command_line[0] != '\0') {
            w.Text("command line: ").Text(c.command_line).EndLine();
        }
        w.Text("started (unix time): ").Decimal(c.start_time_unix).EndLine();
    }
    {
        LineWriter w(sink);
        w.Text("--- platform ---").EndLine();
        w.Text("os: ").Text(c.os_name).Char(' ').Text(c.os_version).EndLine();
        w.Text("architecture: ").Text(c.architecture).EndLine();
        if (c.device_model[0] != '\0') {
            w.Text("device: ").Text(c.device_model).EndLine();
        }
        w.Text("cpu: ").Text(c.cpu_brand[0] != '\0' ? c.cpu_brand : "(unknown)").Text(", ").Decimal(c.cpu_count).Text(" logical").EndLine();
        if (c.core_dumps[0] != '\0') {
            w.Text("core dumps: ").Text(c.core_dumps).EndLine();
        }
        w.Text("memory: ").Decimal(c.total_memory_bytes / (1024ull * 1024ull)).Text(" MiB").EndLine();
    }
    {
        LineWriter w(sink);
        w.Text("--- state ---").EndLine();
    }
    // A provider that fails a check while running would come back here through the assert handler; the inner report skips them.
    static thread_local bool in_providers = false;
    const usize providers = in_providers ? 0 : g_provider_count.load(std::memory_order_relaxed);
    in_providers = true;
    for (usize i = 0; i < providers && i < kMaxCrashStateProviders; ++i) {
        if (!g_providers[i].ready.load(std::memory_order_acquire)) {
            continue; // being registered right now
        }
        char text[kCrashStateTextBytes];
        text[0] = '\0';
        text[kCrashStateTextBytes - 1] = '\0'; // a provider that fills the buffer is still cut here
        g_providers[i].fn(g_providers[i].context, text, kCrashStateTextBytes);
        text[kCrashStateTextBytes - 1] = '\0';
        LineWriter w(sink);
        w.Text(g_providers[i].name).Text(": ").Text(text).EndLine();
    }
    in_providers = false;
    {
        LineWriter w(sink);
        w.Text("--- recent log (oldest first, last ").Decimal(kCrashReportLogLines).Text(") ---").EndLine();
    }
    CrashReportSink log_sink = sink;
    ReadRecentLogLines(&WriteLogLine, &log_sink, kCrashReportLogLines);
}

void WriteCrashReport(const CrashReportSink& sink, const CrashReportInfo& info) {
    {
        LineWriter w(sink);
        w.Text("=== CRASH REPORT ===").EndLine();
        w.Text("reason: ").Text(CrashReasonName(info.reason));
        if (info.has_fault_address) {
            w.Text(" at 0x").Hex(info.fault_address, 16);
        }
        if (info.message != nullptr && info.message[0] != '\0') {
            w.Text(": ").Text(info.message);
        }
        w.EndLine();
        w.Text("thread: ").Decimal(info.thread_id != 0 ? info.thread_id : CurrentThreadId());
        if (CrashThreadName()[0] != '\0') {
            w.Text(" \"").Text(CrashThreadName()).Char('"');
        }
        w.EndLine();
    }
    {
        LineWriter w(sink);
        w.Text("--- stack trace ---").EndLine();
    }
    if (info.trace != nullptr && sink.write != nullptr) {
        // One frame at a time, written as soon as it is resolved, so a fault while resolving frame N leaves frames 0 to N-1 written.
        for (u32 i = 0; i < info.trace->count; ++i) {
            StackTrace one;
            one.frames[0] = info.trace->frames[i];
            one.count = 1;
            char text[2048];
            const usize length = FormatStackTrace(one, text, sizeof(text));
            if (length <= 2) {
                continue;
            }
            // The formatted line starts "#0"; this frame is number i.
            {
                LineWriter w(sink);
                w.Text("#").Decimal(i);
            } // (flushed here, before the rest of the line)
            sink.write(sink.context, text + 2, length - 2);
        }
    }
    WriteCrashContext(sink);
    LineWriter w(sink);
    w.Text("=== END OF REPORT ===").EndLine();
}

// SIGNAL-SAFE-END
} // namespace engine_placeholder::debug
