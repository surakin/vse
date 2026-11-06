#include "debug/crash_context.h"

#include "debug/crash_context_platform.h"

#include <atomic>
#include <mutex>

namespace engine_placeholder::debug {

namespace {

CrashContext g_crash_context;
std::atomic<bool> g_crash_context_ready{false};
std::mutex g_crash_context_mutex;

void CopyBounded(char* dest, usize capacity, const char* source) {
    usize n = 0;
    for (; source != nullptr && n + 1 < capacity && source[n] != '\0'; ++n) {
        dest[n] = source[n];
    }
    dest[n] = '\0';
}

} // namespace

void InitCrashContext(int argc, const char* const* argv) {
    std::lock_guard<std::mutex> lock(g_crash_context_mutex);
    const bool first = !g_crash_context_ready.load(std::memory_order_acquire);
    if (!first && (g_crash_context.command_line[0] != '\0' || argc <= 0)) {
        return;
    }
    if (first) {
        CopyBounded(g_crash_context.engine_version, sizeof(g_crash_context.engine_version), "dev");
        detail::FillPlatformCrashContext(g_crash_context);
    }
    // The command line, arguments joined by single spaces. A later call may supply it if the first had none (the crash
    // handler initializes the context without arguments when the program has not).
    usize used = 0;
    for (int i = 0; argv != nullptr && i < argc && argv[i] != nullptr; ++i) {
        if (i > 0 && used + 1 < sizeof(g_crash_context.command_line)) {
            g_crash_context.command_line[used++] = ' ';
        }
        for (usize k = 0; argv[i][k] != '\0' && used + 1 < sizeof(g_crash_context.command_line); ++k) {
            g_crash_context.command_line[used++] = argv[i][k];
        }
    }
    g_crash_context.command_line[used] = '\0';
    g_crash_context_ready.store(true, std::memory_order_release);
}

bool CrashContextInitialized() {
    return g_crash_context_ready.load(std::memory_order_acquire);
}

const CrashContext& GetCrashContext() {
    return g_crash_context;
}

} // namespace engine_placeholder::debug
