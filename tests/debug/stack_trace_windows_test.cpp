// Stack traces on Windows (Phase 13 Day 154): MSVC-decorated names, no Itanium names from either compiler, line numbers
// from the PDB, the module table, and the crash context's build id from the CodeView record. Windows only.
#include <catch2/catch_test_macros.hpp>

#include "cstr/cstr.h"
#include "sync/thread.h"

#include "debug/compiler.h"
#include "debug/crash_context.h"
#include "debug/debugger.h"
#include "debug/module_list.h"
#include "debug/stack_trace.h"
#include "debug/thread_info.h"
#include "os/windows/win32.h"
#include "symbol_search_path.h"  // tests/windows/support

using namespace engine_placeholder;
using namespace engine_placeholder::debug;

namespace stack_trace_windows_test
{

int g_capture_line = 0;

ENGINE_NOINLINE void CaptureHere(StackTrace* trace)
{
    g_capture_line = __LINE__ + 1;
    CaptureStackTrace(*trace, 0);
    ENGINE_COMPILER_BARRIER();
}

}  // namespace stack_trace_windows_test

using namespace stack_trace_windows_test;

TEST_CASE("Windows: DemangleSymbol reads MSVC-decorated names and refuses the rest", "[stacktrace][debug][windows]")
{
    char out[256];
    REQUIRE(DemangleSymbol("?Foo@@YAXH@Z", out, sizeof(out)));
    CHECK(CStrCompare(out, "void __cdecl Foo(int)") == 0);
    REQUIRE(DemangleSymbol("?Bar@Baz@@QEAAHXZ", out, sizeof(out)));
    CHECK(CStrFind(out, "Baz::Bar") != nullptr);
    CHECK_FALSE(DemangleSymbol("main", out, sizeof(out)));
    CHECK(out[0] == '\0');
    CHECK_FALSE(DemangleSymbol("_ZN3foo3barEv", out, sizeof(out)));  // Itanium names do not occur on this target
    CHECK_FALSE(DemangleSymbol(nullptr, out, sizeof(out)));
    char tiny[4];
    CHECK_FALSE(DemangleSymbol("?Foo@@YAXH@Z", tiny, sizeof(tiny)));  // does not fit
}

TEST_CASE("Windows: resolved names are plain C++ names, from MSVC and clang-cl alike, with the PDB's line",
          "[stacktrace][debug][windows]")
{
    InitDebugFacilities();
    StackTrace trace;
    CaptureHere(&trace);
    REQUIRE(trace.count >= 2);
    FrameInfo entries[kMaxStackFrames * 2];
    const usize n = ResolveStackTrace(trace, entries, sizeof(entries) / sizeof(entries[0]));
    REQUIRE(n >= 1);
    // The innermost frame is CaptureHere (skip 0 starts at the caller of CaptureStackTrace).
    CHECK(CStrFind(entries[0].function, "CaptureHere") != nullptr);
    CHECK(CStrCompareN(entries[0].function, "_Z", 2) != 0);  // clang-cl decorates the Microsoft way too
    CHECK(entries[0].function[0] != '?');                    // and DbgHelp gives the undecorated name
    REQUIRE(entries[0].file[0] != '\0');                     // the test is built with /Zi: a PDB is beside it
    CHECK(CStrFind(entries[0].file, "stack_trace_windows_test.cpp") != nullptr);
    CHECK(entries[0].line == g_capture_line);
    // The module is this executable, with an offset inside its image.
    REQUIRE(entries[0].has_module);
    CHECK(CStrFind(entries[0].module, "debug_tests") != nullptr);
    LoadedModule self;
    REQUIRE(FindLoadedModule(trace.frames[0], &self));
    CHECK(entries[0].module_offset == trace.frames[0] - self.begin);
    CHECK(entries[0].module_offset < self.end - self.begin);
}

TEST_CASE("Windows: the module table lists the executable and the system DLLs", "[stacktrace][debug][windows]")
{
    InitDebugFacilities();
    LoadedModule m;
    REQUIRE(FindLoadedModule(reinterpret_cast<u64>(&CaptureHere), &m));
    CHECK(CStrFind(m.path, ".exe") != nullptr);
    const HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    REQUIRE(kernel32 != nullptr);
    REQUIRE(FindLoadedModule(reinterpret_cast<u64>(kernel32) + 0x1000, &m));
    CHECK((CStrFind(m.path, "KERNEL32") != nullptr || CStrFind(m.path, "kernel32") != nullptr));
    CHECK_FALSE(FindLoadedModule(0x10, &m));  // the first page is never a module
}

TEST_CASE("Windows: the crash context carries the PE build id, the OS, the CPU and memory", "[debug][windows]")
{
    InitCrashContext(0, nullptr);
    const CrashContext& c = GetCrashContext();
    // GUID (32 hex digits) and age (at least one), lowercase.
    const usize id_length = CStrLength(c.build_id);
    CHECK(id_length >= 33);
    for (usize i = 0; i < id_length; ++i)
    {
        CHECK(((c.build_id[i] >= '0' && c.build_id[i] <= '9') || (c.build_id[i] >= 'a' && c.build_id[i] <= 'f')));
    }
    CHECK(CStrCompare(c.os_name, "Windows") == 0);
    CHECK(CStrCompareN(c.os_version, "10.0.", 5) == 0);  // Windows 10 and 11 both report 10.0.<build>
    CHECK(CStrCompare(c.architecture, "x86_64") == 0);
    CHECK(c.cpu_brand[0] != '\0');
    CHECK(c.cpu_count == GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
    CHECK(c.total_memory_bytes > (u64{1} << 30));
    CHECK(CStrFind(c.executable, "debug_tests") != nullptr);
}

TEST_CASE("Windows: thread ids, the debugger check and thread names", "[debug][windows]")
{
    CHECK(CurrentThreadId() == GetCurrentThreadId());
    CHECK(IsDebuggerAttached() == (IsDebuggerPresent() != FALSE));
    u64 other = 0;
    Thread t([&] { other = CurrentThreadId(); });
    t.Join();
    CHECK(other != CurrentThreadId());
    CHECK(other != 0);
}

TEST_CASE("Windows: DbgHelp searches only the executable's directory, never a symbol server",
          "[stacktrace][debug][windows]")
{
    // _NT_SYMBOL_PATH may name a symbol server (srv*...*https://...), which would make symbol lookup a network path;
    // the facade sets its own search path instead (CLAUDE.md: the debugging facilities have no network path).
    SetEnvironmentVariableW(L"_NT_SYMBOL_PATH", L"srv*C:\\nonexistent-cache*https://symbols.invalid/download/symbols");
    ReloadDebugModules();
    SetEnvironmentVariableW(L"_NT_SYMBOL_PATH", nullptr);
    wchar_t search[4096] = {};
    REQUIRE(test_support::SymbolSearchPath(search, static_cast<unsigned long>(sizeof(search) / sizeof(search[0]))));
    CHECK(WStrFind(search, L"srv*") == nullptr);
    CHECK(WStrFind(search, L"http") == nullptr);
    wchar_t exe[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(sizeof(exe) / sizeof(exe[0])));
    REQUIRE(n > 0);
    wchar_t* slash = WStrFindLastChar(exe, L'\\');
    REQUIRE(slash != nullptr);
    *slash = 0;
    CHECK(_wcsicmp(search, exe) == 0);  // exactly the executable's directory
}
