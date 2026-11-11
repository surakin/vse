// The macOS half of post-mortem dumps (debug/post_mortem.h; Phase 26 Day 307, brought forward from Day 308): raise the
// core size limit as on Linux, and say where the core goes. macOS writes cores to kern.corefile (normally
// /cores/core.<pid>) only when kern.coredump is 1 and the directory is writable by the crashing user, which /cores is
// not by default (root, 755): then the result is Limited, with how to lift it. The system's crash reporter writes its
// own report to ~/Library/Logs/DiagnosticReports either way.
#include "debug/post_mortem.h"

#include "cstr/cstr.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"
#include "io/file_system.h"

#include <sys/resource.h>
#include <sys/sysctl.h>

namespace engine_placeholder::debug
{

namespace
{

u64 ToBytes(rlim_t value)
{
    return value == RLIM_INFINITY ? ~u64{0} : static_cast<u64>(value);
}

}  // namespace

PostMortemReport EnablePostMortemDumps(const PostMortemOptions& options)
{
    PostMortemReport report;
    char corefile[256] = "/cores/core.%P";
    size_t corefile_size = sizeof(corefile);
    if (::sysctlbyname("kern.corefile", corefile, &corefile_size, nullptr, 0) != 0)
    {
        CStrCopyTo(corefile, sizeof(corefile), "/cores/core.%P");
    }
    corefile[sizeof(corefile) - 1] = '\0';
    char destination[kPostMortemTextBytes];
    FormatTo(
        destination, sizeof(destination),
        "a core file named by kern.corefile '%s'; the system's crash report goes to ~/Library/Logs/DiagnosticReports",
        corefile);
    CStrCopyTo(report.destination, sizeof(report.destination), destination);

    struct rlimit limit;
    if (::getrlimit(RLIMIT_CORE, &limit) != 0)
    {
        report.result = PostMortemResult::Limited;
        CStrCopyTo(report.note, sizeof(report.note), "the core size limit could not be read");
        return report;
    }
    report.previous_limit_bytes = ToBytes(limit.rlim_cur);
    report.current_limit_bytes = report.previous_limit_bytes;
    rlim_t wanted = limit.rlim_max;
    if (options.size_limit_bytes != 0 && (limit.rlim_max == RLIM_INFINITY || options.size_limit_bytes < limit.rlim_max))
    {
        wanted = static_cast<rlim_t>(options.size_limit_bytes);
    }
    if (limit.rlim_max == 0)
    {
        report.result = PostMortemResult::Limited;
        CStrCopyTo(
            report.note, sizeof(report.note),
            "the hard core size limit is 0, which the process cannot raise; start it after `ulimit -c unlimited`");
        return report;
    }
    limit.rlim_cur = wanted;
    if (::setrlimit(RLIMIT_CORE, &limit) != 0)
    {
        report.result = PostMortemResult::Limited;
        CStrCopyTo(report.note, sizeof(report.note), "the core size limit could not be raised");
        return report;
    }
    report.current_limit_bytes = ToBytes(wanted);
    report.result = PostMortemResult::Enabled;

    // The system's own switches: kern.coredump, and a corefile directory the user can write (it is the directory of the
    // name up to the last '/').
    int enabled = 1;
    size_t enabled_size = sizeof(enabled);
    if (::sysctlbyname("kern.coredump", &enabled, &enabled_size, nullptr, 0) == 0 && enabled == 0)
    {
        report.result = PostMortemResult::Limited;
        CStrCopyTo(
            report.note, sizeof(report.note),
            "core dumps are off system-wide (kern.coredump is 0); turn them on with `sudo sysctl kern.coredump=1`");
        return report;
    }
    char directory[256];
    CStrCopyTo(directory, sizeof(directory), corefile);
    char* slash = nullptr;
    for (char* p = directory; *p != '\0'; ++p)
    {
        if (*p == '/')
        {
            slash = p;
        }
    }
    if (slash != nullptr)
    {
        *(slash == directory ? slash + 1 : slash) = '\0';
        io::FileInfo info;
        if (io::Stat(io::Path(StringView(directory), io::PathStyle::Posix), &info).Failed() || info.read_only)
        {
            report.result = PostMortemResult::Limited;
            char note[kPostMortemTextBytes];
            FormatTo(note, sizeof(note),
                     "%s is not writable by this user, so no core is written; allow it with `sudo chmod 1777 %s`",
                     directory, directory);
            CStrCopyTo(report.note, sizeof(report.note), note);
        }
    }
    return report;
}

}  // namespace engine_placeholder::debug
