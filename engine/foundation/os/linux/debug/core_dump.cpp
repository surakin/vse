// The Linux half of post-mortem dumps (debug/post_mortem.h): raise the core size limit, make the process dumpable, say where the
// core goes.
#include "debug/core_dump_linux.h"
#include "debug/post_mortem.h"
#include "io/file.h"
#include "os/os.h"

#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/resource.h>

namespace engine_placeholder::debug {

namespace {

void CopyInto(char* out, usize capacity, const char* text) {
    if (capacity == 0) {
        return;
    }
    const usize length = std::strlen(text);
    const usize n = length < capacity - 1 ? length : capacity - 1;
    std::memcpy(out, text, n);
    out[n] = '\0';
}

u64 ToBytes(rlim_t value) {
    return value == RLIM_INFINITY ? ~u64{0} : static_cast<u64>(value);
}

} // namespace

bool CorePatternIsPipe(const char* pattern) {
    return pattern != nullptr && pattern[0] == '|';
}

void DescribeCorePattern(const char* pattern, char* out, usize capacity) {
    if (pattern == nullptr || pattern[0] == '\0') {
        CopyInto(out, capacity, "unknown (core_pattern could not be read)");
        return;
    }
    if (CorePatternIsPipe(pattern)) {
        if (std::strstr(pattern, "systemd-coredump") != nullptr) {
            CopyInto(out, capacity, "stored by systemd-coredump; list the cores with `coredumpctl list`, open one with `coredumpctl gdb`");
        } else {
            char text[kPostMortemTextBytes];
            std::snprintf(text, sizeof(text), "piped to a program: %s", pattern + 1);
            CopyInto(out, capacity, text);
        }
        return;
    }
    char text[kPostMortemTextBytes];
    std::snprintf(text, sizeof(text), "written to a file named by the pattern '%s' (a relative name is relative to the working directory of the crash)", pattern);
    CopyInto(out, capacity, text);
}

PostMortemReport EnablePostMortemDumps(const PostMortemOptions& options) {
    PostMortemReport report;
    struct rlimit limit;
    if (::getrlimit(RLIMIT_CORE, &limit) != 0) {
        report.result = PostMortemResult::Limited;
        CopyInto(report.note, sizeof(report.note), "the core size limit could not be read");
        CopyInto(report.destination, sizeof(report.destination), "unknown");
        return report;
    }
    report.previous_limit_bytes = ToBytes(limit.rlim_cur);
    report.current_limit_bytes = report.previous_limit_bytes;

    rlim_t wanted = limit.rlim_max;
    if (options.size_limit_bytes != 0 && (limit.rlim_max == RLIM_INFINITY || options.size_limit_bytes < limit.rlim_max)) {
        wanted = static_cast<rlim_t>(options.size_limit_bytes);
    }
    if (limit.rlim_max == 0) {
        report.result = PostMortemResult::Limited;
        CopyInto(report.note, sizeof(report.note),
                 "the hard core size limit is 0, which the process cannot raise; start it from a shell after `ulimit -c unlimited` "
                 "(the hard limit comes from /etc/security/limits.conf or the service manager)");
    } else {
        limit.rlim_cur = wanted;
        if (::setrlimit(RLIMIT_CORE, &limit) == 0) {
            report.current_limit_bytes = ToBytes(wanted);
            report.result = PostMortemResult::Enabled;
            // The caller asked for a core, so the process must be dumpable: a setuid or capability-dropping program may have been
            // made undumpable. (This also lets same-user processes attach to it, which is why it is opt-in.)
            ::prctl(PR_SET_DUMPABLE, 1, 0, 0, 0);
        } else {
            report.result = PostMortemResult::Limited;
            CopyInto(report.note, sizeof(report.note), "the core size limit could not be raised");
        }
    }

    char pattern[kPostMortemTextBytes] = {};
    io::FilePtr pattern_file;
    if (io::File::Open(io::Path("/proc/sys/kernel/core_pattern", io::PathStyle::Posix), io::OpenMode::Read, io::kOpenNone, &pattern_file).Ok()) {
        usize n = 0;
        if (pattern_file->Read(pattern, sizeof(pattern) - 1, 0, &n).Ok() && n > 0) {
            pattern[n] = '\0';
            if (char* newline = std::strchr(pattern, '\n')) {
                *newline = '\0';
            }
        }
    }
    DescribeCorePattern(pattern, report.destination, sizeof(report.destination));
    return report;
}

} // namespace engine_placeholder::debug
