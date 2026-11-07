#include <catch2/catch_test_macros.hpp>

#include "debug/compiler.h"
#include "debug/death_test.h"
#include "debug/module_list.h"
#include "debug/stack_trace.h"
#include "jobs/scheduler.h"
#include "os/os.h"
#include "support/death_test_skip.h"
#include "test_text.h"

#include <atomic>
#include <cstring>
#include <thread>

using namespace engine_placeholder;
using namespace engine_placeholder::debug;

namespace stack_trace_test {

// A chain of out-of-line functions of known depth and, in a build with debug information, known lines. Each records the
// line of its call to the next one.
int g_line_a = 0;
int g_line_b = 0;
int g_line_c = 0;

ENGINE_NOINLINE void ChainC(StackTrace* trace, u32 skip) {
    g_line_c = __LINE__ + 1;
    CaptureStackTrace(*trace, skip);
    ENGINE_COMPILER_BARRIER();
}
ENGINE_NOINLINE void ChainB(StackTrace* trace, u32 skip) {
    g_line_b = __LINE__ + 1;
    ChainC(trace, skip);
    ENGINE_COMPILER_BARRIER();
}
ENGINE_NOINLINE void ChainA(StackTrace* trace, u32 skip) {
    g_line_a = __LINE__ + 1;
    ChainB(trace, skip);
    ENGINE_COMPILER_BARRIER();
}

ENGINE_NOINLINE u32 Recurse(int depth, StackTrace* trace) {
    if (depth == 0) {
        CaptureStackTrace(*trace);
        return trace->count;
    }
    const u32 r = Recurse(depth - 1, trace);
    ENGINE_COMPILER_BARRIER();
    return r;
}

// A leaf that is always inlined into its caller, so one address belongs to two functions. MSVC's Debug build inlines nothing, even
// __forceinline: the test below skips when the leaf is a frame of its own.
#if defined(_MSC_VER) && !defined(__clang__)
#define STACK_TRACE_TEST_ALWAYS_INLINE __forceinline
#else
#define STACK_TRACE_TEST_ALWAYS_INLINE __attribute__((always_inline)) inline
#endif
STACK_TRACE_TEST_ALWAYS_INLINE void InlinedLeaf(StackTrace* trace) {
    CaptureStackTrace(*trace, 0);
}
ENGINE_NOINLINE void InlinedHost(StackTrace* trace) {
    InlinedLeaf(trace);
    ENGINE_COMPILER_BARRIER();
}

// Finds the first entry whose function name contains `needle`; -1 if none.
int FindEntry(const FrameInfo* entries, usize count, const char* needle) {
    for (usize i = 0; i < count; ++i) {
        if (std::strstr(entries[i].function, needle) != nullptr) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace stack_trace_test

using namespace stack_trace_test;

TEST_CASE("A call chain of known depth resolves to the right functions in order", "[stacktrace][debug]") {
    InitDebugFacilities();
    StackTrace trace;
    ChainA(&trace, 0);
    REQUIRE(trace.count >= 4);
    CHECK_FALSE(trace.truncated);
    // The whole stack was walked, down to the thread's start, and the unwinder's bogus last frame (the top of the address
    // space) was dropped.
    for (u32 i = 0; i < trace.count; ++i) {
        CHECK(trace.frames[i] != 0);
        CHECK(trace.frames[i] != ~u64{0});
    }
    FrameInfo entries[kMaxStackFrames * 2];
    const usize n = ResolveStackTrace(trace, entries, sizeof(entries) / sizeof(entries[0]));
    REQUIRE(n >= trace.count);
    const int c = FindEntry(entries, n, "ChainC");
    const int b = FindEntry(entries, n, "ChainB");
    const int a = FindEntry(entries, n, "ChainA");
    REQUIRE(c >= 0);
    REQUIRE(b >= 0);
    REQUIRE(a >= 0);
    CHECK(c < b); // innermost first
    CHECK(b < a);
    CHECK(c == 0); // skip 0 starts with the function that called CaptureStackTrace
    // The frames carry the module they live in and a plausible offset.
    CHECK(entries[c].has_module);
    CHECK(entries[c].module[0] != '\0');
}

TEST_CASE("With debug information the chain's frames carry file and the line of the call", "[stacktrace][debug]") {
    InitDebugFacilities();
    StackTrace trace;
    ChainA(&trace, 0);
    FrameInfo entries[kMaxStackFrames * 2];
    const usize n = ResolveStackTrace(trace, entries, sizeof(entries) / sizeof(entries[0]));
    const int c = FindEntry(entries, n, "ChainC");
    const int b = FindEntry(entries, n, "ChainB");
    const int a = FindEntry(entries, n, "ChainA");
    REQUIRE((c >= 0 && b >= 0 && a >= 0));
    if (entries[c].file[0] == '\0') {
        SKIP("this binary has no debug information (built without -g)");
    }
    CHECK(std::strstr(entries[c].file, "stack_trace_test.cpp") != nullptr);
    CHECK(entries[c].line == g_line_c);
    CHECK(entries[b].line == g_line_b);
    CHECK(entries[a].line == g_line_a);
}

TEST_CASE("A function inlined into another gets its own entry marked inlined, at the same address", "[stacktrace][debug]") {
    InitDebugFacilities();
    StackTrace trace;
    InlinedHost(&trace);
    REQUIRE(trace.count >= 2);
    FrameInfo entries[kMaxStackFrames * 2];
    const usize n = ResolveStackTrace(trace, entries, sizeof(entries) / sizeof(entries[0]));
    const int leaf = FindEntry(entries, n, "InlinedLeaf");
    const int host = FindEntry(entries, n, "InlinedHost");
    if (leaf < 0) {
        SKIP("this binary has no inlining information (built without -g)");
    }
    REQUIRE(host >= 0);
    if (entries[leaf].address != entries[host].address) {
        SKIP("the compiler did not inline the leaf in this build (MSVC Debug inlines nothing)");
    }
    CHECK(host == leaf + 1);                           // the inlined function first, the function it was inlined into after it
    CHECK(entries[leaf].address == entries[host].address);
    CHECK(entries[leaf].inlined);
    CHECK_FALSE(entries[host].inlined);
    char text[8192];
    FormatStackTrace(trace, text, sizeof(text));
    CHECK(std::strstr(text, "[inlined]") != nullptr);
}

TEST_CASE("skip_frames leaves out the innermost frames", "[stacktrace][debug]") {
    InitDebugFacilities();
    StackTrace all;
    StackTrace skipped;
    ChainA(&all, 0);
    ChainA(&skipped, 2);
    REQUIRE(all.count > 2);
    CHECK(skipped.count + 2 == all.count);
    // After skipping ChainC and ChainB, the first frame is ChainA.
    FrameInfo entries[kMaxStackFrames * 2];
    const usize n = ResolveStackTrace(skipped, entries, sizeof(entries) / sizeof(entries[0]));
    REQUIRE(n > 0);
    CHECK(std::strstr(entries[0].function, "ChainA") != nullptr);
    // Skipping more than there is gives nothing.
    StackTrace none;
    CHECK_FALSE(CaptureStackTrace(none, 100000));
    CHECK(none.count == 0);
}

TEST_CASE("A trace taken on another thread contains that thread's entry function", "[stacktrace][debug]") {
    InitDebugFacilities();
    StackTrace trace;
    std::thread t([&] { ChainA(&trace, 0); });
    t.join();
    REQUIRE(trace.count >= 4);
    FrameInfo entries[kMaxStackFrames * 2];
    const usize n = ResolveStackTrace(trace, entries, sizeof(entries) / sizeof(entries[0]));
    CHECK(FindEntry(entries, n, "ChainC") >= 0);
    CHECK(FindEntry(entries, n, "ChainA") >= 0);
}

namespace {

jobs::Job StackTraceCaptureJob(StackTrace* trace, std::atomic<int>* done) {
    stack_trace_test::ChainA(trace, 0);
    done->store(1, std::memory_order_release);
    co_return;
}

} // namespace

TEST_CASE("A trace taken inside a job on a worker thread reaches the job's function", "[stacktrace][debug]") {
    InitDebugFacilities();
    jobs::Scheduler::Config config;
    config.participants = 2;
    jobs::Scheduler scheduler(config);
    StackTrace trace;
    std::atomic<int> done{0};
    scheduler.Submit(StackTraceCaptureJob(&trace, &done));
    scheduler.HelpUntil(done, 1);
    scheduler.Shutdown();
    REQUIRE(trace.count >= 4);
    FrameInfo entries[kMaxStackFrames * 2];
    const usize n = ResolveStackTrace(trace, entries, sizeof(entries) / sizeof(entries[0]));
    CHECK(FindEntry(entries, n, "ChainA") >= 0);
    // The coroutine's own frame sits above the chain.
    CHECK(FindEntry(entries, n, "StackTraceCaptureJob") >= 0);
}

TEST_CASE("A stack deeper than the capacity is cut and flagged", "[stacktrace][debug]") {
    InitDebugFacilities();
    StackTrace trace;
    stack_trace_test::Recurse(10000, &trace);
    CHECK(trace.count == kMaxStackFrames);
    CHECK(trace.truncated);
    char text[16384];
    FormatStackTrace(trace, text, sizeof(text));
    CHECK(std::strstr(text, "  ... more frames\n") != nullptr);
}

TEST_CASE("Formatting never writes past the buffer and always terminates it", "[stacktrace][debug]") {
    InitDebugFacilities();
    StackTrace trace;
    ChainA(&trace, 0);
    for (usize capacity : {usize{0}, usize{1}, usize{2}, usize{80}, usize{4096}}) {
        // A canary after the buffer shows a write past the end (and ASan flags it).
        char storage[4096 + 8];
        std::memset(storage, 'Z', sizeof(storage));
        const usize written = FormatStackTrace(trace, storage, capacity);
        INFO("capacity " << capacity);
        CHECK(written <= (capacity == 0 ? 0 : capacity - 1));
        for (usize i = capacity; i < sizeof(storage); ++i) {
            REQUIRE(storage[i] == 'Z');
        }
        if (capacity > 0) {
            CHECK(storage[written] == '\0');
        }
    }
}

TEST_CASE("The trace text has a fixed layout and names the chain", "[stacktrace][debug]") {
    InitDebugFacilities();
    StackTrace trace;
    ChainA(&trace, 0);
    char text[8192];
    const usize written = FormatStackTrace(trace, text, sizeof(text));
    REQUIRE(written > 0);
    CHECK(std::strncmp(text, "#0  0x", 6) == 0);
    // Sixteen hex digits, two spaces, then the function.
    for (int i = 0; i < 16; ++i) {
        const char c = text[6 + i];
        CHECK(((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')));
    }
    CHECK(text[22] == ' ');
    CHECK(text[23] == ' ');
    CHECK(std::strstr(text, "ChainC") != nullptr);
    CHECK(std::strstr(text, "ChainA") != nullptr);
    CHECK(text[written - 1] == '\n');
    // Without demangling the name is the mangled one; with it the namespace is readable.
    FormatOptions options;
    options.demangle = true;
    char readable[8192];
    FormatStackTrace(trace, readable, sizeof(readable), options);
#if defined(_WIN32)
    // The PDB stores the name readable already (no parameter list): demangling changes nothing (os/windows/debug/BACKEND.md).
    CHECK(std::strstr(text, "stack_trace_test::ChainC") != nullptr);
    CHECK(std::strstr(readable, "stack_trace_test::ChainC") != nullptr);
#else
    CHECK(std::strstr(text, "_ZN16stack_trace_test6ChainC") != nullptr);
    CHECK(std::strstr(readable, "stack_trace_test::ChainC(") != nullptr);
#endif
}

#if !defined(_WIN32) // Itanium names; Windows decorates the Microsoft way (stack_trace_windows_test.cpp)
TEST_CASE("DemangleSymbol turns mangled names readable and refuses the rest", "[stacktrace][debug]") {
    char out[128];
    CHECK(DemangleSymbol("_ZN3foo3barEv", out, sizeof(out)));
    CHECK(std::strcmp(out, "foo::bar()") == 0);
    CHECK_FALSE(DemangleSymbol("main", out, sizeof(out)));
    CHECK(out[0] == '\0');
    CHECK_FALSE(DemangleSymbol(nullptr, out, sizeof(out)));
    char tiny[4];
    CHECK_FALSE(DemangleSymbol("_ZN3foo3barEv", tiny, sizeof(tiny))); // does not fit
    CHECK_FALSE(DemangleSymbol("_ZN3foo3barEv", tiny, 0));
}
#endif

TEST_CASE("An address with no symbol is named by module and offset, and one in no module by ??", "[stacktrace][debug]") {
    InitDebugFacilities();
    LoadedModule main_module;
    REQUIRE(FindLoadedModule(reinterpret_cast<u64>(&ChainA), &main_module));
    StackTrace trace;
    // An address inside the program's own image that no function or symbol covers. The first page holds the ELF headers,
    // which no function covers; the exact symbols near it depend on the toolchain, so look for a free spot.
    u64 gap = 0;
    for (u64 address = main_module.begin; address < main_module.begin + 4096 && gap == 0; address += 8) {
        StackTrace probe;
        probe.frames[0] = address;
        probe.count = 1;
        FrameInfo entry[4];
        if (ResolveStackTrace(probe, entry, 4) == 1 && entry[0].has_module && entry[0].function[0] == '\0') {
            gap = address;
        }
    }
    if (gap == 0) {
        SKIP("the first page of this binary is covered by symbols");
    }
    trace.frames[0] = gap;
    trace.frames[1] = 0x1000;                   // in nothing
    trace.count = 2;
    char text[1024];
    FormatStackTrace(trace, text, sizeof(text));
    // "#0  0x...  <module path>+0x<offset>" then "#1  0x0000000000001000  ??"
    CHECK(std::strstr(text, "+0x") != nullptr);
    CHECK(std::strstr(text, "#1  0x0000000000001000  ??\n") != nullptr);
    FrameInfo entries[4];
    CHECK(ResolveStackTrace(trace, entries, 4) == 2);
    CHECK(entries[0].has_module);
    CHECK(entries[0].function[0] == '\0');
    CHECK_FALSE(entries[1].has_module);
}

#ifdef DEBUG_TEST_PLUGIN_PATH
namespace {

StackTrace g_plugin_trace;
void PluginCaptureCallback() {
    CaptureStackTrace(g_plugin_trace, 0);
}

} // namespace

TEST_CASE("A library loaded after startup is symbolized once the modules are reloaded", "[stacktrace][debug]") {
    InitDebugFacilities();
    os::DynamicLibraryHandle library = os::LoadLibrary(DEBUG_TEST_PLUGIN_PATH);
    REQUIRE(library != nullptr);
    using PluginCall = void (*)(void (*)());
    PluginCall call = reinterpret_cast<PluginCall>(os::GetSymbol(library, "DebugTestPluginCall"));
    REQUIRE(call != nullptr);
    call(&PluginCaptureCallback);
    REQUIRE(g_plugin_trace.count >= 3);
    FrameInfo entries[kMaxStackFrames * 2];

    // The program started without the library, so its list of modules does not know it yet.
    usize n = ResolveStackTrace(g_plugin_trace, entries, sizeof(entries) / sizeof(entries[0]));
    int plugin_entry = -1;
    for (usize i = 0; i < n; ++i) {
        if (entries[i].address >= reinterpret_cast<u64>(call) && entries[i].address < reinterpret_cast<u64>(call) + 0x100) {
            plugin_entry = static_cast<int>(i);
        }
    }
    REQUIRE(plugin_entry >= 0);
    CHECK_FALSE(entries[plugin_entry].has_module);

    ReloadDebugModules();
    n = ResolveStackTrace(g_plugin_trace, entries, sizeof(entries) / sizeof(entries[0]));
    CHECK(FindEntry(entries, n, "DebugTestPluginCall") >= 0);
    plugin_entry = FindEntry(entries, n, "DebugTestPluginCall");
    REQUIRE(plugin_entry >= 0);
    CHECK(entries[plugin_entry].has_module);
    CHECK(std::strstr(entries[plugin_entry].module, "debug_test_plugin") != nullptr); // libdebug_test_plugin.so, debug_test_plugin.dll
    os::UnloadLibrary(library);
}
#endif

// ---- initialization from many threads, in a fresh process -----------------------------------------------------------------

ENGINE_DEATH_TEST_FN(dt_stack_concurrent_init) {
    std::atomic<int> ready{0};
    std::atomic<bool> go{false};
    std::atomic<int> failures{0};
    std::thread threads[8];
    for (std::thread& t : threads) {
        t = std::thread([&] {
            ready.fetch_add(1);
            while (!go.load()) {
                std::this_thread::yield(); // other tests run at the same time: do not hog the CPU while waiting
            }
            InitDebugFacilities(); // all at once, the first call of the process
            StackTrace trace;
            if (!CaptureStackTrace(trace, 0) || trace.count == 0) {
                failures.fetch_add(1);
            }
            FrameInfo entries[16];
            if (ResolveStackTrace(trace, entries, 16) == 0) {
                failures.fetch_add(1);
            }
        });
    }
    while (ready.load() < 8) {
        std::this_thread::yield();
    }
    go = true;
    for (std::thread& t : threads) {
        t.join();
    }
    os::Write(1, failures.load() == 0 ? "INIT-OK\n" : "INIT-FAILED\n", failures.load() == 0 ? 8 : 12);
}

TEST_CASE("Eight threads starting the facilities at once all get a working trace", "[stacktrace][death][debug]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    for (int round = 0; round < 5; ++round) {
        const ChildResult r = RunInChild("dt_stack_concurrent_init");
        INFO("timed out " << r.timed_out << ", terminated " << r.terminated << " by " << r.native_code << ", exit code " << r.exit_code
                          << "\nchild stderr: "
                          << engine_test::MakeText(StringView(r.stderr_text, r.stderr_length < 600 ? r.stderr_length : 600)).CStr());
        REQUIRE(r.exited);
        CHECK(r.exit_code == 0);
        CHECK(r.StdoutContains("INIT-OK"));
    }
}

// ---- a binary without symbols ---------------------------------------------------------------------------------------------

TEST_CASE("In a stripped binary the frames fall back to module and offset", "[stacktrace][stripped][debug]") {
    // Run by the ctest entry that executes the stripped copy of this executable with ENGINE_EXPECT_STRIPPED_BINARY=1.
    const char* expected = std::getenv("ENGINE_EXPECT_STRIPPED_BINARY");
    if (expected == nullptr || expected[0] != '1') {
        SKIP("only meaningful in the stripped copy of this executable");
    }
    InitDebugFacilities();
    StackTrace trace;
    ChainA(&trace, 0);
    FrameInfo entries[kMaxStackFrames * 2];
    const usize n = ResolveStackTrace(trace, entries, sizeof(entries) / sizeof(entries[0]));
    REQUIRE(n >= 4);
    // No symbol table, no debug information: no function names for the chain, but the module and offset are known.
    CHECK(FindEntry(entries, n, "Chain") < 0);
    CHECK(entries[0].function[0] == '\0');
    CHECK(entries[0].file[0] == '\0');
    CHECK(entries[0].has_module);
    CHECK(entries[0].module_offset > 0);
    char text[8192];
    FormatStackTrace(trace, text, sizeof(text));
    CHECK(std::strstr(text, "+0x") != nullptr);
    CHECK(std::strstr(text, "ChainC") == nullptr);
}
