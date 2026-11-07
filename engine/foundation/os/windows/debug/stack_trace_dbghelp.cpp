// The stack-trace facade on Windows (Phase 13 Day 154): capture with RtlCaptureStackBackTrace, resolution with DbgHelp from the PDBs. The
// counterpart of debug/stack_trace_backtrace.cpp (libbacktrace, Linux); FormatStackTrace is the neutral debug/stack_trace_format.cpp.
//
// DbgHelp is single-threaded: every call is made under one lock. DbgHelp is never used while a crash is being reported: the resolver then
// gives raw addresses and module offsets from the module table (read at initialization, no lock), and names are found afterwards (in the
// test parent, the crash script or a debugger; os/windows/debug/BACKEND.md). Capture needs no DbgHelp and no lock.
#include "debug/crash_event_platform.h"
#include "debug/failure_hooks.h"
#include "debug/module_list.h"
#include "debug/stack_trace.h"
#include "os/windows/utf16.h"
#include "os/windows/win32.h"

#include <atomic>
#include <cstring>
#include <mutex>

#include "unity/windows_enable.h"
#include <dbghelp.h>
#include "unity/windows_disable.h"

namespace engine_placeholder::debug {

namespace {

std::mutex g_dbghelp_mutex;           // every DbgHelp call
std::atomic<bool> g_initialized{false};
bool g_symbols_ready = false;         // SymInitializeW succeeded (under the mutex)

void CopyText(char* dest, usize capacity, const char* source) {
    if (capacity == 0) return;
    usize n = 0;
    if (source != nullptr) {
        while (n + 1 < capacity && source[n] != '\0') {
            dest[n] = source[n];
            ++n;
        }
    }
    dest[n] = '\0';
}

// Converts a UTF-16 name to UTF-8 into a fixed buffer, cut (on a whole code point) to fit.
void CopyWide(char* dest, usize capacity, const wchar_t* source) {
    if (capacity == 0) return;
    usize length = 0;
    while (source[length] != 0) ++length;
    const usize needed = os::windows::Utf16ToUtf8(source, length, dest, capacity);
    if (needed >= capacity) dest[capacity - 1] = '\0';
}

// The symbol search path: the executable's directory, and nothing from the environment. The default path includes _NT_SYMBOL_PATH, which
// can name a symbol server (`srv*...*https://...`): symbol lookup would then be a network path, which the debugging facilities never have
// (CLAUDE.md; found by the Day 154 review, with a test). A PDB is also found through the path recorded in its module (DbgHelp tries that
// first), which is local.
bool SearchPath(wchar_t* out, usize capacity) {
    const DWORD n = GetModuleFileNameW(nullptr, out, static_cast<DWORD>(capacity));
    if (n == 0 || n >= capacity) return false;
    usize cut = n;
    while (cut > 0 && out[cut - 1] != L'\\' && out[cut - 1] != L'/') --cut;
    if (cut == 0) return false;
    out[cut - 1] = 0;
    return true;
}

// Starts DbgHelp for this process, or sets its search path again (call with the mutex held). Symbols load lazily, with line numbers.
void StartSymbols() {
    wchar_t search[MAX_PATH * 4];
    const bool have_path = SearchPath(search, sizeof(search) / sizeof(search[0]));
    if (g_symbols_ready) {
        SymSetSearchPathW(GetCurrentProcess(), have_path ? search : L"");
        return;
    }
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS);
    // An empty path rather than null when the executable's directory is unknown: null means "the default", environment included.
    g_symbols_ready = SymInitializeW(GetCurrentProcess(), have_path ? search : L"", TRUE) != FALSE;
}

void ApplyModule(FrameInfo& info) {
    LoadedModule module;
    if (FindLoadedModule(info.address, &module)) {
        info.has_module = true;
        info.module_offset = info.address - module.load_bias;
        CopyText(info.module, sizeof(info.module), module.path);
    }
}

// Fills `info` with the function and line DbgHelp knows for `address` in inline context `context` (0: the function itself, no inline frame).
void ResolveIn(u64 address, DWORD context, bool use_context, FrameInfo& info) {
    const HANDLE process = GetCurrentProcess();
    // The symbol: a SYMBOL_INFOW followed by room for the longest name the facade keeps.
    alignas(SYMBOL_INFOW) byte storage[sizeof(SYMBOL_INFOW) + kMaxSymbolBytes * sizeof(wchar_t)];
    SYMBOL_INFOW* symbol = reinterpret_cast<SYMBOL_INFOW*>(storage);
    std::memset(symbol, 0, sizeof(SYMBOL_INFOW));
    symbol->SizeOfStruct = sizeof(SYMBOL_INFOW);
    symbol->MaxNameLen = kMaxSymbolBytes - 1; // in characters; longer names are cut by DbgHelp
    DWORD64 displacement = 0;
    const BOOL named = use_context ? SymFromInlineContextW(process, address, context, &displacement, symbol)
                                   : SymFromAddrW(process, address, &displacement, symbol);
    if (named) {
        const usize length = symbol->NameLen < symbol->MaxNameLen ? symbol->NameLen : symbol->MaxNameLen;
        symbol->Name[length] = 0;
        CopyWide(info.function, sizeof(info.function), symbol->Name);
    }
    IMAGEHLP_LINEW64 line{};
    line.SizeOfStruct = sizeof(line);
    DWORD line_displacement = 0;
    const BOOL lined = use_context ? SymGetLineFromInlineContextW(process, address, context, 0, &line_displacement, &line)
                                   : SymGetLineFromAddrW64(process, address, &line_displacement, &line);
    if (lined && line.FileName != nullptr) {
        CopyWide(info.file, sizeof(info.file), line.FileName);
        info.line = static_cast<int>(line.LineNumber);
    }
}

// Appends the entries for one address (call with the mutex held), as the Linux resolver does: one per function inlined at it, innermost
// first and marked inlined, then the function that contains them. Entries that do not fit are dropped (the last kept one is then an
// inlined one, which says so).
void ResolveOne(u64 address, FrameInfo* out, usize capacity, usize* count) {
    const usize first = *count;
    // Every captured frame is a return address: the instruction after the call. Looking up the byte before it names the call itself, as
    // libbacktrace does on Linux; after a call to a [[noreturn]] function (an assert's path) the return address can even lie past the
    // caller's last byte, in the next function (found by the Day 154 review). The entries report the address as captured.
    const u64 lookup = address > 0 ? address - 1 : address;
    if (g_symbols_ready && *count < capacity) {
        const HANDLE process = GetCurrentProcess();
        const DWORD inlined = SymAddrIncludeInlineTrace(process, lookup);
        DWORD context = 0;
        DWORD frame_index = 0;
        const bool have_inline = inlined > 0 && SymQueryInlineTrace(process, lookup, 0, lookup, lookup, &context, &frame_index);
        if (have_inline) {
            for (DWORD i = 0; i < inlined && *count < capacity; ++i) {
                FrameInfo& info = out[(*count)++];
                info = FrameInfo();
                info.address = address;
                info.inlined = true;
                ResolveIn(lookup, context + i, true, info);
            }
        }
        if (*count < capacity) {
            FrameInfo& info = out[(*count)++];
            info = FrameInfo();
            info.address = address;
            // The function the inlined code sits in: the outermost inline context, or the plain lookup when nothing is inlined here.
            ResolveIn(lookup, have_inline ? context + inlined : 0, have_inline, info);
        }
    } else if (*count < capacity) {
        out[*count] = FrameInfo();
        out[*count].address = address;
        ++*count;
    }
    for (usize i = first; i < *count; ++i) ApplyModule(out[i]);
}

} // namespace

void InitDebugFacilities() {
    InstallDiagnosticsHooks();
    if (g_initialized.load(std::memory_order_acquire)) return;
    std::lock_guard<std::mutex> lock(g_dbghelp_mutex);
    if (g_initialized.load(std::memory_order_relaxed)) return;
    RefreshLoadedModules();
    StartSymbols();
    g_initialized.store(true, std::memory_order_release);
}

void ReloadDebugModules() {
    std::lock_guard<std::mutex> lock(g_dbghelp_mutex);
    RefreshLoadedModules();
    StartSymbols(); // or, when started, only the search path again (the environment is never consulted)
    if (g_symbols_ready) SymRefreshModuleList(GetCurrentProcess()); // modules loaded or unloaded since SymInitializeW
    g_initialized.store(true, std::memory_order_release);
}

bool CaptureStackTrace(StackTrace& trace, u32 skip_frames) {
    trace = StackTrace();
    // One more frame to skip: this function itself. Ask for one frame more than fits, to know whether the stack was cut.
    void* frames[kMaxStackFrames + 1];
    const USHORT n = RtlCaptureStackBackTrace(static_cast<DWORD>(skip_frames) + 1, static_cast<DWORD>(kMaxStackFrames + 1), frames, nullptr);
    const u32 kept = n > kMaxStackFrames ? static_cast<u32>(kMaxStackFrames) : static_cast<u32>(n);
    for (u32 i = 0; i < kept; ++i) trace.frames[i] = reinterpret_cast<u64>(frames[i]);
    trace.count = kept;
    trace.truncated = n > kMaxStackFrames;
    return trace.count > 0;
}

usize ResolveStackTrace(const StackTrace& trace, FrameInfo* out, usize capacity) {
    if (detail::CrashReportInProgress()) {
        // A crash is being reported (on the library's handler thread, while the faulting thread waits, perhaps holding a lock or the heap's):
        // no DbgHelp, which locks and allocates. The module and offset only, from the table read at initialization; names come afterwards.
        usize count = 0;
        for (u32 i = 0; i < trace.count && count < capacity; ++i) {
            out[count] = FrameInfo();
            out[count].address = trace.frames[i];
            ApplyModule(out[count]);
            ++count;
        }
        return count;
    }
    if (!g_initialized.load(std::memory_order_acquire)) InitDebugFacilities();
    std::lock_guard<std::mutex> lock(g_dbghelp_mutex);
    usize count = 0;
    for (u32 i = 0; i < trace.count && count < capacity; ++i) ResolveOne(trace.frames[i], out, capacity, &count);
    return count;
}

bool DemangleSymbol(const char* mangled, char* out, usize capacity) {
    if (capacity > 0) out[0] = '\0';
    if (mangled == nullptr || capacity == 0) return false;
    // MSVC and clang-cl both decorate the Microsoft way (`?Name@...`); a name without the leading `?` is not decorated.
    if (mangled[0] != '?') return false;
    char readable[kMaxSymbolBytes];
    DWORD n = 0;
    {
        std::lock_guard<std::mutex> lock(g_dbghelp_mutex); // UnDecorateSymbolName is part of DbgHelp
        n = UnDecorateSymbolName(mangled, readable, static_cast<DWORD>(sizeof(readable)), UNDNAME_COMPLETE);
    }
    if (n == 0 || n >= sizeof(readable) || n >= capacity) return false;
    std::memcpy(out, readable, n + 1);
    return true;
}

} // namespace engine_placeholder::debug
