// The stack-trace facade over libbacktrace. This is the only file that includes backtrace.h (enforced by
// tools/platform_neutral_check); the rest of the engine sees debug/stack_trace.h.
#include "debug/stack_trace.h"

#include "debug/module_list.h"

#include <backtrace.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <cxxabi.h>
#include <mutex>

namespace engine_placeholder::debug {

namespace {

std::atomic<backtrace_state*> g_trace_state{nullptr};
std::mutex g_trace_init_mutex; // serializes Init and Reload (never taken by the capture and resolve paths)

// libbacktrace reports problems (a file it cannot read, bad debug information) through this callback. There is nothing
// useful to do with them: the facade carries on with what the library has, and an address it cannot explain is shown as
// a module and offset or as ??.
void TraceError(void*, const char*, int) {}

void CopyText(char* dest, usize capacity, const char* source) {
    if (capacity == 0) {
        return;
    }
    usize n = 0;
    if (source != nullptr) {
        while (n + 1 < capacity && source[n] != '\0') {
            dest[n] = source[n];
            ++n;
        }
    }
    dest[n] = '\0';
}

backtrace_state* CreateState() {
    // No file name: libbacktrace finds the running executable itself. threaded = 1: its tables are guarded by a lock.
    return backtrace_create_state(nullptr, 1, &TraceError, nullptr);
}

// Makes the first lookup now, so libbacktrace reads the executable's debug information at startup and not in a signal
// handler.
void WarmUp(backtrace_state* state) {
    backtrace_pcinfo(
        state, reinterpret_cast<uintptr_t>(&InitDebugFacilities),
        [](void*, uintptr_t, const char*, int, const char*) -> int { return 1; }, &TraceError, nullptr);
}

backtrace_state* State() {
    backtrace_state* state = g_trace_state.load(std::memory_order_acquire);
    if (state == nullptr) {
        InitDebugFacilities();
        state = g_trace_state.load(std::memory_order_acquire);
    }
    return state;
}

struct CaptureContext {
    StackTrace* trace;
};

int CaptureFrame(void* data, uintptr_t pc) {
    StackTrace& trace = *static_cast<CaptureContext*>(data)->trace;
    // The unwinder ends the walk with a bogus frame at the top of the address space.
    if (pc == 0 || pc == ~uintptr_t{0}) {
        return 0;
    }
    if (trace.count >= kMaxStackFrames) {
        trace.truncated = true;
        return 1; // one frame more than fits: stop
    }
    trace.frames[trace.count++] = static_cast<u64>(pc);
    return 0;
}

struct ResolveContext {
    FrameInfo* out;
    usize capacity;
    usize count;
    usize first_for_frame; // index of the first entry written for the current address
    bool have_function;    // an entry for the current address already has a function name
    bool chain_cut;        // entries for the current address were dropped for lack of room
};

void ApplyModule(FrameInfo& info) {
    LoadedModule module;
    if (FindLoadedModule(info.address, &module)) {
        info.has_module = true;
        info.module_offset = info.address - module.load_bias;
        CopyText(info.module, sizeof(info.module), module.path);
    }
}

int ResolveLine(void* data, uintptr_t pc, const char* file, int line, const char* function) {
    ResolveContext& ctx = *static_cast<ResolveContext*>(data);
    if (ctx.count >= ctx.capacity) {
        ctx.chain_cut = true;
        return 1;
    }
    FrameInfo& info = ctx.out[ctx.count++];
    info = FrameInfo();
    info.address = static_cast<u64>(pc);
    info.line = line;
    CopyText(info.file, sizeof(info.file), file);
    CopyText(info.function, sizeof(info.function), function);
    if (function != nullptr && function[0] != '\0') {
        ctx.have_function = true;
    }
    return 0;
}

void ResolveSymbol(void* data, uintptr_t pc, const char* symbol, uintptr_t, uintptr_t) {
    ResolveContext& ctx = *static_cast<ResolveContext*>(data);
    if (symbol == nullptr) {
        return;
    }
    if (ctx.count == ctx.first_for_frame) {
        // Nothing from the debug information: this address has only a symbol.
        if (ctx.count >= ctx.capacity) {
            return;
        }
        FrameInfo& info = ctx.out[ctx.count++];
        info = FrameInfo();
        info.address = static_cast<u64>(pc);
        CopyText(info.function, sizeof(info.function), symbol);
    } else {
        // Debug information gave a file and line but no name: name the outermost entry.
        CopyText(ctx.out[ctx.count - 1].function, sizeof(ctx.out[ctx.count - 1].function), symbol);
    }
    ctx.have_function = true;
}

// Resolves one address into one or more entries appended to `ctx`; returns how many were added.
usize ResolveOne(backtrace_state* state, u64 address, ResolveContext& ctx) {
    ctx.first_for_frame = ctx.count;
    ctx.have_function = false;
    ctx.chain_cut = false;
    backtrace_pcinfo(state, static_cast<uintptr_t>(address), &ResolveLine, &TraceError, &ctx);
    if (!ctx.have_function) {
        backtrace_syminfo(state, static_cast<uintptr_t>(address), &ResolveSymbol, &TraceError, &ctx);
    }
    if (ctx.count == ctx.first_for_frame && ctx.count < ctx.capacity) {
        FrameInfo& info = ctx.out[ctx.count++];
        info = FrameInfo();
        info.address = address; // nothing is known beyond the address
    }
    const usize added = ctx.count - ctx.first_for_frame;
    for (usize i = ctx.first_for_frame; i < ctx.count; ++i) {
        ctx.out[i].address = address;
        // Every entry but the last was inlined into the next; if the chain was cut, the last kept one was inlined too.
        ctx.out[i].inlined = (i + 1 < ctx.count) || ctx.chain_cut;
        ApplyModule(ctx.out[i]);
    }
    return added;
}

// Small appender for FormatStackTrace: never writes past the buffer, always leaves room for the null byte.
class TextWriter {
public:
    TextWriter(char* out, usize capacity) : out_(out), capacity_(capacity) {
        if (capacity_ > 0) {
            out_[0] = '\0';
        }
    }
    void Add(const char* text) {
        if (text == nullptr) {
            return;
        }
        for (; *text != '\0'; ++text) {
            AddChar(*text);
        }
    }
    void AddChar(char c) {
        if (capacity_ > 0 && length_ + 1 < capacity_) {
            out_[length_++] = c;
            out_[length_] = '\0';
        }
    }
    void AddDecimal(u64 value) {
        char digits[20];
        int n = 0;
        do {
            digits[n++] = static_cast<char>('0' + (value % 10));
            value /= 10;
        } while (value > 0);
        while (n > 0) {
            AddChar(digits[--n]);
        }
    }
    void AddHex(u64 value, int digits) {
        for (int shift = (digits - 1) * 4; shift >= 0; shift -= 4) {
            AddChar("0123456789abcdef"[(value >> shift) & 0xf]);
        }
    }
    usize Length() const { return length_; }

private:
    char* out_;
    usize capacity_;
    usize length_ = 0;
};

usize ResolveWithState(backtrace_state* state, const StackTrace& trace, FrameInfo* out, usize capacity) {
    ResolveContext ctx{out, capacity, 0, 0, false, false};
    for (u32 i = 0; i < trace.count && ctx.count < ctx.capacity; ++i) {
        ResolveOne(state, trace.frames[i], ctx);
    }
    return ctx.count;
}

constexpr usize kEntriesPerFrame = 8; // inlined functions shown per address; more are dropped (and the last shown is marked inlined)

} // namespace

void InitDebugFacilities() {
    if (g_trace_state.load(std::memory_order_acquire) != nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_trace_init_mutex);
    if (g_trace_state.load(std::memory_order_acquire) != nullptr) {
        return; // another thread finished first
    }
    RefreshLoadedModules();
    backtrace_state* created = CreateState();
    WarmUp(created);
    // Take and resolve a trace once: this primes the stack unwinder and the debug information on the path, so the same
    // work does not happen later in a signal handler. Done on the state before it is published, so no other thread uses
    // the library while its first-time setup is still running.
    StackTrace trace;
    CaptureContext ctx{&trace};
    backtrace_simple(created, 0, &CaptureFrame, &TraceError, &ctx);
    FrameInfo entries[kEntriesPerFrame * 4];
    ResolveWithState(created, trace, entries, sizeof(entries) / sizeof(entries[0]));
    g_trace_state.store(created, std::memory_order_release);
}

void ReloadDebugModules() {
    std::lock_guard<std::mutex> lock(g_trace_init_mutex);
    RefreshLoadedModules();
    backtrace_state* created = CreateState();
    WarmUp(created); // before it is published, like in InitDebugFacilities
    g_trace_state.store(created, std::memory_order_release); // the old state stays allocated: a reader may be using it
}

bool CaptureStackTrace(StackTrace& trace, u32 skip_frames) {
    trace = StackTrace();
    backtrace_state* state = State();
    if (state == nullptr) {
        return false;
    }
    CaptureContext ctx{&trace};
    // One more frame to skip: this function itself.
    backtrace_simple(state, static_cast<int>(skip_frames) + 1, &CaptureFrame, &TraceError, &ctx);
    return trace.count > 0;
}

usize ResolveStackTrace(const StackTrace& trace, FrameInfo* out, usize capacity) {
    backtrace_state* state = State();
    if (state == nullptr) {
        return 0;
    }
    return ResolveWithState(state, trace, out, capacity);
}

usize FormatStackTrace(const StackTrace& trace, char* out, usize capacity, const FormatOptions& options) {
    TextWriter writer(out, capacity);
    backtrace_state* state = State();
    FrameInfo entries[kEntriesPerFrame];
    for (u32 frame = 0; frame < trace.count; ++frame) {
        ResolveContext ctx{entries, kEntriesPerFrame, 0, 0, false, false};
        if (state != nullptr) {
            ResolveOne(state, trace.frames[frame], ctx);
        }
        if (ctx.count == 0) { // no state: only the address is known
            entries[0] = FrameInfo();
            entries[0].address = trace.frames[frame];
            ctx.count = 1;
        }
        for (usize e = 0; e < ctx.count; ++e) {
            const FrameInfo& info = entries[e];
            writer.AddChar('#');
            writer.AddDecimal(frame);
            writer.Add("  0x");
            writer.AddHex(info.address, 16);
            writer.Add("  ");
            if (info.function[0] != '\0') {
                char readable[kMaxSymbolBytes];
                if (options.demangle && DemangleSymbol(info.function, readable, sizeof(readable))) {
                    writer.Add(readable);
                } else {
                    writer.Add(info.function);
                }
            } else if (info.has_module) {
                writer.Add(info.module);
                writer.Add("+0x");
                writer.AddHex(info.module_offset, 1);
            } else {
                writer.Add("??");
            }
            if (info.file[0] != '\0') {
                writer.Add("  ");
                writer.Add(info.file);
                writer.AddChar(':');
                writer.AddDecimal(static_cast<u64>(info.line));
            }
            if (info.inlined) {
                writer.Add(" [inlined]");
            }
            writer.AddChar('\n');
        }
    }
    if (trace.truncated) {
        writer.Add("  ... more frames\n");
    }
    return writer.Length();
}

bool DemangleSymbol(const char* mangled, char* out, usize capacity) {
    if (capacity > 0) {
        out[0] = '\0';
    }
    if (mangled == nullptr || capacity == 0) {
        return false;
    }
    int status = 0;
    char* readable = abi::__cxa_demangle(mangled, nullptr, nullptr, &status);
    if (readable == nullptr || status != 0) {
        std::free(readable);
        return false;
    }
    const usize length = std::strlen(readable);
    const bool fits = length < capacity;
    if (fits) {
        std::memcpy(out, readable, length + 1);
    }
    std::free(readable);
    return fits;
}

} // namespace engine_placeholder::debug
