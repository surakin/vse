// The crash handler facade over sentry-native (Breakpad backend, no transport). This is the only file that includes
// sentry.h (enforced by tools/platform_neutral_check); the rest of the engine sees debug/crash_handler.h.
#include "debug/crash_handler.h"

#include "debug/crash_context.h"
#include "debug/crash_event_platform.h"
#include "debug/crash_report.h"
#include "debug/post_mortem.h"
#include "debug/stack_trace.h"
#include "log/log.h"
#include "io/file.h"
#include "io/file_system.h"
#include "os/os.h"

// On Windows sentry.h includes <windows.h> itself: the enable/disable pair keeps its macros (CreateDirectory, LoadLibrary, ...) from leaking
// into the files a unity build merges after this one (CLAUDE.md, "Unity builds"; Phase 13 Day 154). The pair is harmless elsewhere.
#include "unity/windows_enable.h"
#include <sentry.h>
#include "unity/windows_disable.h"

#include "sync/atomic.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include "sync/mutex.h"
#include "build_defs.h"

namespace engine_placeholder::debug {

namespace {

Mutex g_crash_handler_mutex;
Atomic<bool> g_crash_handler_installed{false};

LogChannel LogCrash("LogCrash", LogVerbosity::Warn);

// Where completed crash envelopes are kept for good: <dump directory>/saved. Set before sentry_init.
char g_saved_directory[os::kMaxPathBytes];

// The library's "transport": instead of sending an envelope anywhere, copy it into the saved directory.
//
// The library moves the envelope of a crash into its transport on the next start of the program, and then deletes the run
// directory the crash left it in. A transport that discards envelopes (the null transport) would make the next launch
// delete the dump of the crash, so this one writes each envelope to <saved>/<uuid>.envelope. It never opens a socket.
void SaveEnvelope(sentry_envelope_t* envelope, void*) {
    sentry_uuid_t id = sentry_uuid_new_v4();
    char name[37];
    sentry_uuid_as_string(&id, name);
    char path[os::kMaxPathBytes];
    const int length = std::snprintf(path, sizeof(path), "%s/%s.envelope", g_saved_directory, name);
    if (length > 0 && static_cast<usize>(length) < sizeof(path)) {
        if (sentry_envelope_write_to_file(envelope, path) != 0) {
            Log(LogCrash, LogVerbosity::Warn, "a crash dump from an earlier run could not be saved");
        }
    }
    sentry_envelope_free(envelope);
}

// sentry-native prints its own messages (to stderr by default). They go to the engine log instead, at a matching level.
void RouteSentryLog(sentry_level_t level, const char* message, va_list args, void*) {
    char text[512];
    std::vsnprintf(text, sizeof(text), message, args);
    // With no server address, which is the point, the library complains about it on every start. Not a problem here.
    if (StringView(text).Contains(StringView("DSN"))) {
        return;
    }
    LogVerbosity verbosity = LogVerbosity::Debug;
    switch (level) {
    case SENTRY_LEVEL_FATAL:
    case SENTRY_LEVEL_ERROR: verbosity = LogVerbosity::Error; break;
    case SENTRY_LEVEL_WARNING: verbosity = LogVerbosity::Warn; break;
    case SENTRY_LEVEL_INFO: verbosity = LogVerbosity::Info; break;
    default: verbosity = LogVerbosity::Debug; break;
    }
    Log(LogCrash, verbosity, StringView(text));
}

// ---- the report -------------------------------------------------------------------------------------------------------------

// Where the crash report text is also written: <dump directory>/crash-report.txt, or the caller's report_file. The library
// attaches that file to the envelope of the crash it records. Set before the handler is installed, read in the crash.
char g_report_path[os::kMaxPathBytes];
// A crash report that takes longer than this to write is cut off between pieces, so a stuck provider cannot hold the process.
constexpr u64 kReportBudgetNanos = 10ull * 1000 * 1000 * 1000;

// SIGNAL-SAFE-BEGIN
struct ReportDestination {
    int file_descriptor = -1;
    u64 deadline = 0;
    bool cut_off = false;
};

// Writes all of the bytes, retrying a write that took only part of them, but waiting at most 2 seconds in total for a pipe whose
// reader has stopped: the report must not hold a crashing process forever.
void WriteAll(int fd, const char* data, usize length) {
    os::WriteAllWithTimeout(fd, data, length, 2ull * 1000 * 1000 * 1000);
}

void WriteReportPiece(void* context, const char* data, usize length) {
    ReportDestination& destination = *static_cast<ReportDestination*>(context);
    if (os::MonotonicTimeNanos() > destination.deadline) {
        // Out of time: what is written stays, the rest is dropped, and the report says so once.
        if (!destination.cut_off) {
            destination.cut_off = true;
            static const char kText[] = "\n[report cut off: out of time]\n";
            WriteAll(2, kText, sizeof(kText) - 1);
            if (destination.file_descriptor >= 0) {
                WriteAll(destination.file_descriptor, kText, sizeof(kText) - 1);
            }
        }
        return;
    }
    WriteAll(2, data, length);
    if (destination.file_descriptor >= 0) {
        WriteAll(destination.file_descriptor, data, length);
    }
}

// Runs on the thread that faulted, inside the platform's recorder, before the library writes the dump.
void OnCrashEvent(const detail::CrashEvent& event) {
    ReportDestination destination;
    destination.deadline = os::MonotonicTimeNanos() + kReportBudgetNanos;
    if (g_report_path[0] != '\0') {
        destination.file_descriptor = os::CreateFileForWriting(StringView(g_report_path));
    }
    CrashReportInfo info;
    info.reason = event.reason;
    info.has_fault_address = event.has_fault_address;
    info.fault_address = event.fault_address;
    info.trace = &event.trace;
    info.thread_id = event.thread_id;
    info.thread_name = event.thread_name;
    info.thread_known = event.thread_known;
    CrashReportSink sink;
    sink.write = &WriteReportPiece;
    sink.context = &destination;
    WriteCrashReport(sink, info);
    if (destination.file_descriptor >= 0) {
        os::Close(destination.file_descriptor);
    }
}

#if ENGINE_PLATFORM_WINDOWS
// Windows: the library calls this on its handler thread, after it wrote the dump, with the fault's EXCEPTION_POINTERS; the platform turns them
// into the CrashEvent and calls OnCrashEvent (os/windows/debug/crash_hook.cpp). Keeping the event keeps the envelope with the dump and report.
sentry_value_t OnLibraryCrash(const sentry_ucontext_t* context, sentry_value_t event, sentry_hint_t*, void*) {
    if (context != nullptr) detail::ReportCrashFromExceptionPointers(&context->exception_ptrs);
    return event;
}
#endif

// SIGNAL-SAFE-END
} // namespace

CrashHandlerResult InstallCrashHandler(const CrashHandlerOptions& options) {
    if (!options.enabled) {
        return CrashHandlerResult::Disabled;
    }
    LockGuard<Mutex> lock(g_crash_handler_mutex);
    if (g_crash_handler_installed.Load(MemoryOrder::Acquire)) {
        return CrashHandlerResult::AlreadyInstalled;
    }

    io::Path directory_path; // the native style: a Windows dump directory is a Windows path (Phase 13 Day 155)
    if (options.dump_directory == nullptr || options.dump_directory[0] == '\0') {
        if (io::CrashDumpDirectory(&directory_path).Failed()) {
            Log(LogCrash, LogVerbosity::Warn, "no directory for crash dumps could be found or created; crash handler not installed");
            return CrashHandlerResult::DirectoryFailed;
        }
    } else if (!io::Path::Parse(StringView(options.dump_directory), io::NativePathStyle(), &directory_path) ||
               io::CreatePrivateDirectory(directory_path).Failed()) {
        Log(LogCrash, LogVerbosity::Warn, "the crash dump directory is missing, not private to this user, or not writable; crash handler not installed");
        return CrashHandlerResult::DirectoryFailed;
    }
    char directory_text[io::kMaxPathBytes + 1];
    if (!directory_path.ToNative(directory_text, sizeof(directory_text))) {
        Log(LogCrash, LogVerbosity::Warn, "the crash dump directory path is too long; crash handler not installed");
        return CrashHandlerResult::DirectoryFailed;
    }
    const char* directory = directory_text;
    const io::Path saved_path = directory_path.Join("saved");
    if (!saved_path.ToNative(g_saved_directory, sizeof(g_saved_directory)) || io::CreatePrivateDirectory(saved_path).Failed()) {
        Log(LogCrash, LogVerbosity::Warn, "the directory for saved crash dumps could not be created; crash handler not installed");
        return CrashHandlerResult::DirectoryFailed;
    }

    PruneSavedDumps(saved_path, options.keep_saved_dumps);

    // The crash report calls the stack-trace functions inside the fault; they are safe there only once this has run.
    InitDebugFacilities();
    if (!CrashContextInitialized()) {
        InitCrashContext(0, nullptr);
    }

    sentry_options_t* sentry = sentry_options_new();
    if (sentry == nullptr) {
        return CrashHandlerResult::InitFailed;
    }
    sentry_options_set_database_path(sentry, directory);
    // Every dump records which build made it: the caller's version text, or else the executable's build id.
    if (options.release != nullptr && options.release[0] != '\0') {
        sentry_options_set_release(sentry, options.release);
    } else if (GetCrashContext().build_id[0] != '\0') {
        sentry_options_set_release(sentry, GetCrashContext().build_id);
    }
    if (options.handler_path != nullptr && options.handler_path[0] != '\0') {
        sentry_options_set_handler_path(sentry, options.handler_path);
    }
    // The report text goes next to the dump, and into the envelope with it.
    if (options.report_file != nullptr && options.report_file[0] != '\0') {
        const int n = std::snprintf(g_report_path, sizeof(g_report_path), "%s", options.report_file);
        if (n < 0 || static_cast<usize>(n) >= sizeof(g_report_path)) {
            g_report_path[0] = '\0';
        }
    } else {
        const int n = std::snprintf(g_report_path, sizeof(g_report_path), "%s/crash-report.txt", directory);
        if (n < 0 || static_cast<usize>(n) >= sizeof(g_report_path)) {
            g_report_path[0] = '\0';
        }
    }
    if (g_report_path[0] != '\0') {
        // Empty any report an earlier run left, so a crash that writes none does not attach a stale one.
        const int stale = os::CreateFileForWriting(StringView(g_report_path)); // mode 0600, as the fault handler creates it
        if (stale >= 0) {
            os::Close(stale);
        }
        sentry_options_add_attachment(sentry, g_report_path);
    } else {
        Log(LogCrash, LogVerbosity::Warn, "the crash report file path is too long; the report will not be written to a file");
    }
    // Only local files: no account, no address to send to, no session tracking, a transport that saves envelopes to disk, and
    // the library's own warnings and errors in our log. (The library calls the logger only when "debug" is on; the level
    // keeps everything below a warning out.)
    sentry_options_set_auto_session_tracking(sentry, 0);
    sentry_transport_t* transport = sentry_transport_new(&SaveEnvelope);
    if (transport == nullptr) {
        sentry_options_free(sentry);
        return CrashHandlerResult::InitFailed;
    }
    sentry_options_set_transport(sentry, transport);
    sentry_options_set_debug(sentry, 1);
    sentry_options_set_logger_level(sentry, SENTRY_LEVEL_WARNING);
    sentry_options_set_logger(sentry, &RouteSentryLog, nullptr);
    sentry_options_set_logger_enabled_when_crashed(sentry, 0); // our log is not safe to call from a crashing process
#if ENGINE_PLATFORM_WINDOWS
    sentry_options_set_on_crash(sentry, &OnLibraryCrash, nullptr); // the report on Windows (above)
    detail::PrepareCrashEventHook(); // the crash gate goes under the library's filter (crash_hook.cpp)
#endif

    if (sentry_init(sentry) != 0) { // takes ownership of `sentry`
        Log(LogCrash, LogVerbosity::Warn, "the crash handler library failed to start; crash handler not installed");
        return CrashHandlerResult::InitFailed;
    }
    // The recorder goes in front of the library's handlers (installed by sentry_init), so the report is written first and the
    // fault's details are known; it then calls the library. The calling thread also gets a stack big enough for both.
    if (!detail::InstallCrashEventHook(&OnCrashEvent)) {
        Log(LogCrash, LogVerbosity::Warn, "the crash report hook could not be installed; crashes leave a dump but no report");
    }
    g_crash_handler_installed.Store(true, MemoryOrder::Release); // before the registration below, which needs it
    RegisterThreadForCrashHandling();
    // Last, so a failed install leaves the process's core settings alone. The report says where a core goes, or why none will.
    SetCoreDumpDescription("");
    if (options.post_mortem_dumps) {
        const PostMortemReport post_mortem = EnablePostMortemDumps(PostMortemOptions());
        if (post_mortem.result == PostMortemResult::Enabled) {
            SetCoreDumpDescription(post_mortem.destination);
        } else {
            char text[kPostMortemTextBytes];
            std::snprintf(text, sizeof(text), "none will be written: %.200s", post_mortem.note[0] != '\0' ? post_mortem.note : "not supported here");
            SetCoreDumpDescription(text);
            Log(LogCrash, LogVerbosity::Warn, StringView(post_mortem.note));
        }
    }
    return CrashHandlerResult::Installed;
}

void UninstallCrashHandler() {
    LockGuard<Mutex> lock(g_crash_handler_mutex);
    if (!g_crash_handler_installed.Load(MemoryOrder::Acquire)) {
        return;
    }
    detail::RemoveCrashEventHook();
    // A crash being reported on another thread must finish before the library goes away (bounded: it normally ends the process).
    for (int waited = 0; waited < 1000 && detail::CrashReportInProgress(); ++waited) {
        os::SleepNanos(1000 * 1000);
    }
    sentry_close();
    g_crash_handler_installed.Store(false, MemoryOrder::Release);
}

bool CrashHandlerInstalled() {
    return g_crash_handler_installed.Load(MemoryOrder::Acquire);
}

} // namespace engine_placeholder::debug
