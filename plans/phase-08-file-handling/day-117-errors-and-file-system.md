# Phase 8 / Day 117 — Errors, `FileInfo` and File-System Operations

**Phase:** [Phase 8 — File Handling](../phase-08-file-handling.md)
**Depends on:** Day 116.

What `File` and `Directory` both need: an error type, a description of what is on disk, and the operations that work on a path rather than an open file. New files
`io/error.h`, `io/file_system.h`, `io/linux/file_system.cpp`.

## Tasks

- [ ] `io::Error` (`None`, `NotFound`, `PermissionDenied`, `AlreadyExists`, `NotADirectory`, `IsADirectory`, `NotEmpty`, `NoSpace`, `TooLong`, `Interrupted`, `Unsupported`, `Io`, `Invalid`, ...), `ErrorName`, and
      `io::Status` (the error plus the OS code, for logs only); the Linux mapping from `errno` is one function with a test table; document which POSIX codes collapse into `Io`.
- [ ] `FileType` (`Regular`, `Directory`, `Symlink`, `Other`, `None`) and `FileInfo` (type, size, modification time in nanoseconds since the epoch, permissions as a portable subset, whether it is read-only for this process).
- [ ] Operations on paths, each returning `Status`: `Stat` (following links) and `Lstat`, `Exists`, `IsFile`, `IsDirectory`, `Remove` (file or empty directory), `RemoveAll` (recursive, with a guard that refuses `/` and a path
      shorter than a documented minimum), `Rename` (atomic replace where the OS allows, documented where not), `Copy` (data copy; metadata policy documented), `CreateDirectory`, `CreateDirectories` (recursive, existing is success), `CreatePrivateDirectory`
      (the checks of today's `EnsurePrivateDirectory`: owner, no symlink, mode 0700).
- [ ] Resolution: `CurrentDirectory`, `SetCurrentDirectory` (documented as process-global and discouraged), `Absolute` (lexical against the current directory), `Canonical` (resolves links; the file must exist), `ReadLink`.
- [ ] Special folders: `TempDirectory`, `ExecutablePath`, `ExecutableDirectory`, `UserConfigDirectory`, `UserCacheDirectory`, `UserDataDirectory` (Linux: `$XDG_*` with the documented fallbacks to `$HOME`), each taking the application name and creating nothing.
      `CrashDumpDirectory` moves here (today `os::GetCrashDumpDirectory`) in the Day 121 port; here only the function exists.
- [ ] `TempFile`/`TempDirectory` creation in a given directory (unique names, `O_EXCL`), used by atomic replace on Day 118.
- [ ] Tests (phase 8): the errno table; every operation on missing paths, directories where files are expected and the reverse, read-only directories, a symlink loop, names with spaces and Unicode, over-long names; `RemoveAll` refusing dangerous
      arguments; `Rename` over an existing file; the special folders with a scrubbed environment (`HOME` unset, `XDG_*` set to relative paths, which the spec says to ignore); all in a per-test temporary directory removed afterwards (a small fixture).
- [ ] **Documentation:** labels on every function; the guide gets "Errors", "Looking at the disk" and "Special folders" sections; reference regenerated; `docgen lint` zero; `ctest -L docs`.
- [ ] Run `scripts/test-phase.sh --affected`; quote the line.
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high`; record it. Extra scrutiny: symlink and race handling in `CreatePrivateDirectory` and `RemoveAll`.
- [ ] Update `STATUS.md`; commit.

**Done:** every operation above works on Linux with the documented errors, the fixture-based tests pass, and the guide sections are committed.
