// The macOS debugging backend's first third (Phase 26 Day 306): stack traces through libbacktrace on Mach-O with dSYMs, the module list from dyld,
// the thread id, and the crash context against what `sysctl -n` prints.
#include <catch2/catch_test_macros.hpp>

#include <crt_externs.h>
#include <fcntl.h>
#include <pthread.h>
#include <spawn.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#include "cstr/cstr.h"
#include "build_defs.h"
#include "debug/compiler.h"
#include "debug/crash_context.h"
#include "debug/module_list.h"
#include "debug/stack_trace.h"
#include "debug/thread_info.h"
#include "os/last_error.h"
#include "os/os.h"
#include "string_view.h"
#include "types.h"

using namespace engine_placeholder;
using namespace engine_placeholder::debug;

namespace {

StackTrace g_trace;

void Capture() { CaptureStackTrace(g_trace); }

// Runs /usr/sbin/sysctl -n <names...> and returns its output (one value per line), through a pipe.
StringView SysctlOutput(const char* const* names, char (&buffer)[2048]) {
    int fds[2];
    if (::pipe(fds) != 0) return StringView();
    posix_spawn_file_actions_t actions;
    ::posix_spawn_file_actions_init(&actions);
    ::posix_spawn_file_actions_adddup2(&actions, fds[1], 1);
    ::posix_spawn_file_actions_addclose(&actions, fds[0]);
    ::posix_spawn_file_actions_addclose(&actions, fds[1]);
    const char* argv[8] = {"/usr/sbin/sysctl", "-n"};
    usize argc = 2;
    for (const char* const* n = names; *n != nullptr && argc < 7; ++n) argv[argc++] = *n;
    argv[argc] = nullptr;
    pid_t pid = 0;
    const int started = ::posix_spawn(&pid, argv[0], &actions, nullptr, const_cast<char* const*>(argv), *::_NSGetEnviron());
    ::posix_spawn_file_actions_destroy(&actions);
    os::Close(fds[1]);
    usize used = 0;
    if (started == 0) {
        for (;;) {
            const long got = os::Read(fds[0], buffer + used, sizeof buffer - 1 - used);
            if (got < 0 && os::LastError() == EINTR) continue;
            if (got <= 0) break;
            used += static_cast<usize>(got);
        }
        int status = 0;
        while (::waitpid(pid, &status, 0) < 0 && os::LastError() == EINTR) {
        }
    }
    os::Close(fds[0]);
    buffer[used] = '\0';
    return StringView(buffer, used);
}

// Line `index` (0-based) of `text`, without its newline.
StringView Line(StringView text, usize index) {
    for (usize i = 0; i < index; ++i) {
        const usize end = text.IndexOf('\n');
        if (end == text.Length()) return StringView();
        text = text.Substring(end + 1);
    }
    return text.Substring(0, text.IndexOf('\n'));
}

u64 ParseU64(StringView text) {
    u64 v = 0;
    for (usize i = 0; i < text.Length() && text[i] >= '0' && text[i] <= '9'; ++i) v = v * 10 + static_cast<u64>(text[i] - '0');
    return v;
}

} // namespace

TEST_CASE("macOS: a frame in a dylib of ours has its function, file and line from the dylib's dSYM", "[stacktrace][debug][macos]") {
    InitDebugFacilities();
    os::DynamicLibraryHandle library = os::LoadLibrary(DEBUG_TEST_PLUGIN_PATH);
    REQUIRE(library != nullptr);
    using PluginCall = void (*)(void (*)());
    auto call = reinterpret_cast<PluginCall>(os::GetSymbol(library, "DebugTestPluginCall"));
    REQUIRE(call != nullptr);
    ReloadDebugModules();
    call(&Capture);
    FrameInfo entries[kMaxStackFrames * 2];
    const usize n = ResolveStackTrace(g_trace, entries, sizeof(entries) / sizeof(entries[0]));
    // The plugin's frame, found by address (its name needs the dSYM: without one libbacktrace cannot name the last symbol of an image, and this
    // image has only one; KNOWN-ISSUES.md).
    int plugin = -1;
    for (usize i = 0; i < n; ++i) {
        if (entries[i].address >= reinterpret_cast<u64>(call) && entries[i].address < reinterpret_cast<u64>(call) + 0x100) plugin = static_cast<int>(i);
    }
    REQUIRE(plugin >= 0);
    CHECK(entries[plugin].has_module);
    CHECK(CStrFind(entries[plugin].module, "libdebug_test_plugin") != nullptr);
    if (entries[plugin].file[0] == '\0') {
        SKIP("the plugin has no dSYM (ENGINE_DSYM off, or no dsymutil): its frame has no file, line or (being the image's only symbol) name");
    }
    CHECK(CStrFind(entries[plugin].function, "DebugTestPluginCall") != nullptr);
    CHECK(CStrFind(entries[plugin].file, "plugin.cpp") != nullptr);
    CHECK(entries[plugin].line > 0);
    // The frame's offset is the address minus the image's slide (ASLR): inside the dylib's own address range from zero.
    LoadedModule m;
    REQUIRE(FindLoadedModule(entries[plugin].address, &m));
    CHECK(entries[plugin].module_offset == entries[plugin].address - m.load_bias);
    os::UnloadLibrary(library);
}

TEST_CASE("macOS: a frame in a system library is named by its module", "[stacktrace][debug][macos]") {
    // The callback runs inside pthread_once in libsystem_pthread, which lives in the dyld shared cache (no file on disk for libbacktrace to
    // read): the module list still names the image.
    InitDebugFacilities();
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    REQUIRE(::pthread_once(&once, &Capture) == 0);
    FrameInfo entries[kMaxStackFrames * 2];
    const usize n = ResolveStackTrace(g_trace, entries, sizeof(entries) / sizeof(entries[0]));
    bool system_frame = false;
    for (usize i = 0; i < n; ++i) {
        UNSCOPED_INFO(i << ": " << entries[i].function << " | " << entries[i].module << " | has_module " << entries[i].has_module);
        // Under TSan its runtime intercepts pthread_once (the call never reaches libsystem_pthread): the frame is in that runtime's dylib.
#if ENGINE_SANITIZER_THREAD
        const char* const expected = "libclang_rt.tsan";
#else
        const char* const expected = "/usr/lib/system/libsystem_pthread.dylib";
#endif
        if (entries[i].has_module && CStrFind(entries[i].module, expected) != nullptr) system_frame = true;
        // No image claims another's frame: libobjc is not on this path (Day 306 found shared-cache images' ranges overlapping).
        CHECK(CStrFind(entries[i].module, "libobjc") == nullptr);
    }
    CHECK(system_frame);
}

TEST_CASE("macOS: thread ids are pthread_threadid_np's, as the job system prints them", "[crash][debug][macos]") {
    u64 raw = 0;
    REQUIRE(::pthread_threadid_np(nullptr, &raw) == 0);
    CHECK(CurrentThreadId() == (raw & 0x7FFFFFFFu));
    CHECK(CurrentThreadId() != 0);
}

TEST_CASE("macOS: the crash context matches what sysctl reports", "[crash][debug][macos]") {
    const char* const argv[] = {"debug_tests", nullptr};
    InitCrashContext(1, argv);
    const CrashContext& c = GetCrashContext();
    const char* const names[] = {"kern.osproductversion", "hw.logicalcpu", "hw.memsize", "machdep.cpu.brand_string", nullptr};
    char buffer[2048];
    const StringView out = SysctlOutput(names, buffer);
    REQUIRE(!out.Empty());
    CHECK(StringView(c.os_name) == StringView("macOS"));
    CHECK(StringView(c.os_version) == Line(out, 0));
    CHECK(c.cpu_count == static_cast<u32>(ParseU64(Line(out, 1))));
    CHECK(c.total_memory_bytes == ParseU64(Line(out, 2)));
    CHECK(StringView(c.cpu_brand) == Line(out, 3)); // "Intel(R) Core(TM) ..." here, "Apple M1" and so on on Apple silicon
    struct utsname name;
    REQUIRE(::uname(&name) == 0);
    CHECK(StringView(c.architecture) == StringView(name.machine)); // x86_64 here, arm64 on Apple silicon
    CHECK(CStrLength(c.build_id) == 32); // LC_UUID, 16 bytes in hex
    CHECK(CStrFind(c.executable, "debug_tests") != nullptr);
}
