#pragma once

// Helpers shared by the Linux tests of tests/debug (Phase 16 Day 217): text concatenation, a shell command run with its output
// captured (what `popen` did), removing a directory tree (what `remove` inside `nftw` did), finding files under a directory and
// reading a whole file. They exist so the tests need no `std::string`, no `<cstdio>` and no `<cstdlib>`; the POSIX calls they make
// (`posix_spawn`, `waitpid`, `nftw`, `opendir`) are not part of the C++ standard library.

#include <dirent.h>
#include <fcntl.h>
#include <ftw.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "containers/vector.h"
#include "cstr/cstr.h"
#include "debug/death_test.h"
#include "io/file.h"
#include "io/path.h"
#include "os/last_error.h"
#include "os/os.h"
#include "string_view.h"
#include "test_text.h"
#include "types.h"

namespace debug_test {

using engine_placeholder::StringView;
using engine_placeholder::u8;
using engine_placeholder::usize;
using engine_placeholder::Vector;
using engine_test::Text;

/// The pieces joined into one owned text. Each piece is anything a StringView is made from (a literal, a `const char*`, a view, a Text).
template <typename... Parts>
Text Cat(const Parts&... parts) {
    Text out = engine_test::MakeText();
    (out.Append(StringView(parts)), ...);
    return out;
}

/// What the child captured on standard output / standard error (valid while `r` is).
inline StringView StdoutView(const engine_placeholder::debug::ChildResult& r) { return StringView(r.stdout_text, r.stdout_length); }
inline StringView StderrView(const engine_placeholder::debug::ChildResult& r) { return StringView(r.stderr_text, r.stderr_length); }

/// The text after the first `key` up to the next newline (to the end if none), or an empty view when `key` does not occur.
inline StringView ValueAfter(StringView text, StringView key) {
    const usize at = text.Find(key);
    if (at == text.Length()) {
        return StringView();
    }
    const StringView rest = text.Substring(at + key.Length());
    return rest.Substring(0, rest.IndexOf('\n'));
}

/// The index of the last occurrence of `needle` in `text`, or `text.Length()` if it does not occur.
inline usize FindLast(StringView text, StringView needle) {
    usize last = text.Length();
    for (usize from = 0; from < text.Length();) {
        const usize at = text.Substring(from).Find(needle);
        if (at == text.Length() - from) {
            break;
        }
        last = from + at;
        from = last + 1;
    }
    return last;
}

/// How many times `needle` occurs in `text`, overlapping occurrences counted.
inline usize CountOf(StringView text, StringView needle) {
    usize count = 0;
    for (usize from = 0; from < text.Length();) {
        const usize at = text.Substring(from).Find(needle);
        if (at == text.Length() - from) {
            break;
        }
        ++count;
        from += at + 1;
    }
    return count;
}

/// The outcome of a shell command: everything it wrote to standard output, and its wait status.
struct ShellRun {
    Text output = engine_test::MakeText();
    int status = -1; ///< The wait status (`waitpid`), or -1 if the command could not be started.
    /// The exit code, or -1 when the command did not start or was ended by a signal.
    int ExitCode() const { return WIFEXITED(status) ? WEXITSTATUS(status) : -1; }
};

/// Runs `command` with `/bin/sh -c` and collects its standard output (add `2>&1` to the command for standard error as well).
inline ShellRun RunShell(StringView command) {
    ShellRun run;
    const Text owned = engine_test::MakeText(command); // NUL-terminated
    int fds[2];
    if (::pipe2(fds, O_CLOEXEC) != 0) {
        return run;
    }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fds[1], 1);
    char program[] = "sh";
    char flag[] = "-c";
    char* argv[] = {program, flag, const_cast<char*>(owned.CStr()), nullptr};
    pid_t pid = 0;
    const int started = posix_spawn(&pid, "/bin/sh", &actions, nullptr, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    engine_placeholder::os::Close(fds[1]);
    if (started != 0) {
        engine_placeholder::os::Close(fds[0]);
        return run;
    }
    char buffer[4096];
    for (;;) {
        const long n = engine_placeholder::os::Read(fds[0], buffer, sizeof(buffer));
        if (n < 0 && engine_placeholder::os::LastError() == EINTR) {
            continue;
        }
        if (n <= 0) {
            break;
        }
        run.output.AppendRawBytes(buffer, static_cast<usize>(n));
    }
    engine_placeholder::os::Close(fds[0]);
    while (::waitpid(pid, &run.status, 0) < 0 && engine_placeholder::os::LastError() == EINTR) {
    }
    return run;
}

/// One `nftw` step that removes an entry (directories after their contents, links not followed).
inline int RemoveTreeEntry(const char* entry, const struct stat*, int type, struct FTW*) {
    return (type == FTW_DP || type == FTW_D || type == FTW_DNR) ? ::rmdir(entry) : ::unlink(entry);
}

/// Removes `path` and everything under it; an empty path or the root is refused (a test that builds the path from an unset variable must not wipe a disk).
inline void RemoveTree(const char* path) {
    if (path == nullptr || path[0] == '\0' || (path[0] == '/' && path[1] == '\0')) {
        return;
    }
    nftw(path, &RemoveTreeEntry, 16, FTW_DEPTH | FTW_PHYS);
}

/// Appends the path of every file under `directory` (recursively) whose name ends with `suffix`.
inline void FindFiles(const char* directory, StringView suffix, engine_test::TextList* found) {
    DIR* dir = opendir(directory);
    if (dir == nullptr) {
        return;
    }
    while (const dirent* entry = readdir(dir)) {
        if (engine_placeholder::CStrCompare(entry->d_name, ".") == 0 || engine_placeholder::CStrCompare(entry->d_name, "..") == 0) {
            continue;
        }
        const Text path = Cat(directory, "/", entry->d_name);
        struct stat info;
        if (::lstat(path.CStr(), &info) != 0) { // links are not followed, so a link cycle cannot recurse forever
            continue;
        }
        if (S_ISDIR(info.st_mode)) {
            FindFiles(path.CStr(), suffix, found);
        } else if (path.View().EndsWith(suffix)) {
            found->Push(path.View());
        }
    }
    closedir(dir);
}

/// All the bytes of the file at `path`, or an empty text if it cannot be read.
inline Text ReadWholeFile(StringView path) {
    Vector<u8> bytes;
    Text text = engine_test::MakeText();
    if (engine_placeholder::io::ReadFile(engine_placeholder::io::Path(path), &bytes).Ok()) {
        text.AppendRawBytes(reinterpret_cast<const char*>(bytes.Data()), bytes.Size());
    }
    return text;
}

/// Creates (or empties) the file at `path` and writes `size` bytes of `data` into it; true on success.
inline bool WriteWholeFile(StringView path, const void* data, usize size) {
    return engine_placeholder::io::WriteFile(engine_placeholder::io::Path(path), data, size).Ok();
}

} // namespace debug_test
