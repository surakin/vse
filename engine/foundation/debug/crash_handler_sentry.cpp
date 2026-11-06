// The crash handler facade over sentry-native (Breakpad backend, no transport). This is the only file that includes
// sentry.h (enforced by tools/platform_neutral_check); the rest of the engine sees debug/crash_handler.h.
#include "debug/crash_handler.h"

#include "debug/crash_context.h"
#include "log/log.h"
#include "os/os.h"

#include <sentry.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace engine_placeholder::debug {

namespace {

std::mutex g_crash_handler_mutex;
std::atomic<bool> g_crash_handler_installed{false};

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

} // namespace

CrashHandlerResult InstallCrashHandler(const CrashHandlerOptions& options) {
    if (!options.enabled) {
        return CrashHandlerResult::Disabled;
    }
    std::lock_guard<std::mutex> lock(g_crash_handler_mutex);
    if (g_crash_handler_installed.load(std::memory_order_acquire)) {
        return CrashHandlerResult::AlreadyInstalled;
    }

    char default_directory[os::kMaxPathBytes];
    const char* directory = options.dump_directory;
    if (directory == nullptr || directory[0] == '\0') {
        if (!os::GetCrashDumpDirectory(default_directory, sizeof(default_directory))) {
            Log(LogCrash, LogVerbosity::Warn, "no directory for crash dumps could be found or created; crash handler not installed");
            return CrashHandlerResult::DirectoryFailed;
        }
        directory = default_directory;
    } else if (!os::EnsurePrivateDirectory(StringView(directory))) {
        Log(LogCrash, LogVerbosity::Warn, "the crash dump directory is missing, not private to this user, or not writable; crash handler not installed");
        return CrashHandlerResult::DirectoryFailed;
    }
    const int saved_length = std::snprintf(g_saved_directory, sizeof(g_saved_directory), "%s/saved", directory);
    if (saved_length < 0 || static_cast<usize>(saved_length) >= sizeof(g_saved_directory) ||
        !os::EnsurePrivateDirectory(StringView(g_saved_directory))) {
        Log(LogCrash, LogVerbosity::Warn, "the directory for saved crash dumps could not be created; crash handler not installed");
        return CrashHandlerResult::DirectoryFailed;
    }

    sentry_options_t* sentry = sentry_options_new();
    if (sentry == nullptr) {
        return CrashHandlerResult::InitFailed;
    }
    sentry_options_set_database_path(sentry, directory);
    // Every dump records which build made it: the caller's version text, or else the executable's build id.
    if (!CrashContextInitialized()) {
        InitCrashContext(0, nullptr);
    }
    if (options.release != nullptr && options.release[0] != '\0') {
        sentry_options_set_release(sentry, options.release);
    } else if (GetCrashContext().build_id[0] != '\0') {
        sentry_options_set_release(sentry, GetCrashContext().build_id);
    }
    if (options.handler_path != nullptr && options.handler_path[0] != '\0') {
        sentry_options_set_handler_path(sentry, options.handler_path);
    }
    if (options.report_file != nullptr && options.report_file[0] != '\0') {
        sentry_options_add_attachment(sentry, options.report_file);
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

    if (sentry_init(sentry) != 0) { // takes ownership of `sentry`
        Log(LogCrash, LogVerbosity::Warn, "the crash handler library failed to start; crash handler not installed");
        return CrashHandlerResult::InitFailed;
    }
    g_crash_handler_installed.store(true, std::memory_order_release);
    return CrashHandlerResult::Installed;
}

void UninstallCrashHandler() {
    std::lock_guard<std::mutex> lock(g_crash_handler_mutex);
    if (!g_crash_handler_installed.load(std::memory_order_acquire)) {
        return;
    }
    sentry_close();
    g_crash_handler_installed.store(false, std::memory_order_release);
}

bool CrashHandlerInstalled() {
    return g_crash_handler_installed.load(std::memory_order_acquire);
}

} // namespace engine_placeholder::debug
