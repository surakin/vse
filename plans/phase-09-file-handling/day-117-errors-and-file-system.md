# Phase 9 / Day 117 — Errors, `FileInfo` and File-System Operations

**Phase:** [Phase 9 — File Handling](../phase-09-file-handling.md)
**Depends on:** Day 116.

What `File` and `Directory` both need: an error type, a description of what is on disk, and the operations that work on a path rather than an open file. New files
`io/error.h`, `io/file_system.h`, `io/linux/file_system.cpp`.

## Tasks

- [x] `io::Error` (`None`, `NotFound`, `PermissionDenied`, `AlreadyExists`, `NotADirectory`, `IsADirectory`, `NotEmpty`, `NoSpace`, `TooLong`, `Interrupted`, `Unsupported`, `Io`, `Invalid`, ...), `ErrorName`, and
      `io::Status` (the error plus the OS code, for logs only); the Linux mapping from `errno` is one function with a test table; document which POSIX codes collapse into `Io`.
- [x] `FileType` (`Regular`, `Directory`, `Symlink`, `Other`, `None`) and `FileInfo` (type, size, modification time in nanoseconds since the epoch, permissions as a portable subset, whether it is read-only for this process).
- [x] Operations on paths, each returning `Status`: `Stat` (following links) and `Lstat`, `Exists`, `IsFile`, `IsDirectory`, `Remove` (file or empty directory), `RemoveAll` (recursive, with a guard that refuses `/` and a path
      shorter than a documented minimum), `Rename` (atomic replace where the OS allows, documented where not), `Copy` (data copy; metadata policy documented), `CreateDirectory`, `CreateDirectories` (recursive, existing is success), `CreatePrivateDirectory`
      (the checks of today's `EnsurePrivateDirectory`: owner, no symlink, mode 0700).
- [x] Resolution: `CurrentDirectory`, `SetCurrentDirectory` (documented as process-global and discouraged), `Absolute` (lexical against the current directory), `Canonical` (resolves links; the file must exist), `ReadLink`.
- [x] Special folders: `TempDirectory`, `ExecutablePath`, `ExecutableDirectory`, `UserConfigDirectory`, `UserCacheDirectory`, `UserDataDirectory` (Linux: `$XDG_*` with the documented fallbacks to `$HOME`), each taking the application name and creating nothing.
      `CrashDumpDirectory` moves here (today `os::GetCrashDumpDirectory`) in the Day 121 port; here only the function exists.
- [x] `TempFile`/`TempDirectory` creation in a given directory (unique names, `O_EXCL`), used by atomic replace on Day 118.
- [x] Tests (phase 9): the errno table; every operation on missing paths, directories where files are expected and the reverse, read-only directories, a symlink loop, names with spaces and Unicode, over-long names; `RemoveAll` refusing dangerous
      arguments; `Rename` over an existing file; the special folders with a scrubbed environment (`HOME` unset, `XDG_*` set to relative paths, which the spec says to ignore); all in a per-test temporary directory removed afterwards (a small fixture).
- [x] **Documentation:** labels on every function; the guide gets "Errors", "Looking at the disk" and "Special folders" sections; reference regenerated; `docgen lint` zero; `ctest -L docs`.
- [x] Run `scripts/test-phase.sh --affected`; quote the line.
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high`; record it. Extra scrutiny: symlink and race handling in `CreatePrivateDirectory` and `RemoveAll`.
- [ ] Update `STATUS.md`; commit.

**Done:** every operation above works on Linux with the documented errors, the fixture-based tests pass, and the guide sections are committed.

## Result

- `io/error.h` (`Error` with 19 values, `Status`, `ErrorName`, `ErrorFromOsCode`), `io/file_system.h` (`FileType`, `FileInfo`, `Stat`/`Lstat`/`Exists`/`IsFile`/`IsDirectory`, `Remove`, `RemoveAll`, `Rename`, `Copy`, `CreateDirectory`/`CreateDirectories`/`CreatePrivateDirectory`, `CreateTempFile`/`CreateTempDirectory`, `CurrentDirectory`/`SetCurrentDirectory`/`Absolute`/`Canonical`/`ReadLink`, `TempDirectory`, `ExecutablePath`/`ExecutableDirectory`, `UserConfig`/`Cache`/`Data`/`StateDirectory`) with the Linux backend in `io/linux/error.cpp` and `io/linux/file_system.cpp`.
- Tests: `tests/io/file_system_test.cpp` (+11 cases; the io executables now run 22 cases, 17078 assertions) plus a `TempDir` fixture (`tests/io/temp_dir.h`), in plain/ASan/TSan.
- Decisions: error names follow this day file (`TooLong`, `Invalid`), not the spec's draft (`NameTooLong`, `InvalidPath`, `Unknown`); `UserStateDirectory` added (crash dumps live under the state directory today); `RemoveAll` walks with directory descriptors (`openat`/`unlinkat`, `O_NOFOLLOW`) and refuses depth over 128; `CreateTempFile` closes the file it creates (the open handle comes with `File` on Day 118); `HOME` that is not absolute falls back to the password database.
- `x11_enable.h`/`x11_disable.h` now also save `XkbKeyboard` (adding source files moved the unity grouping and exposed the leak: step 1 of the CLAUDE.md unity rule).
- Docs: guide sections "Errors", "Looking at the disk", "Special folders"; reference regenerated; baseline 3250/3250; `docs/samples-prelude.h` includes the file-system header.
- Scoped runs: `ran 1059 of 2804 tests (phases: all; flavors: plain; display: included; crash tests: excluded)` (only the known X11 window test failed), `ran 776 of 2804 ... flavors: asan`, `ran 496 of 2804 ... flavors: tsan`, all pass; `ctest -L docs` 4/4.

## Post-implementation code review

`/code-review high`: 10 findings, 9 fixed, 1 partly (bookkeeping is this commit).

Fixed (each with a regression test where practical):
1. `Copy(p, p, true)` (or onto a hard link) truncated the source before reading: `to` is now opened without `O_TRUNC`, compared by device and inode, and refused with `Invalid`; the truncate happens after the check. Test: self-copy and hard-link copy leave the content intact.
2. `CreatePrivateDirectory` on a symbolic link returned `NotADirectory` (Linux gives `ENOTDIR` for `O_DIRECTORY|O_NOFOLLOW` on a link, not `ELOOP`): it now asks `lstat` and returns the documented `SymlinkLoop`. The test now asserts the error, not just failure.
3. `RemoveAll` normalized lexically and deleted the normalized path, which differs from what the kernel resolves when `..` follows a link: it now deletes the absolute path as given and refuses any `.` or `..` component (`Invalid`). Test: `link/../real` is refused and the victim survives.
4. `ReadLink` cut a target of 4096 bytes or more silently: it reads into a buffer one byte larger and returns `TooLong`.
5. `getcwd`'s `ERANGE` mapped to `Io`: `ERANGE` now maps to `TooLong` (table test row added).
6. `read_only` and the writability check used the real user id: both use `faccessat(..., AT_EACCESS)` (effective user).
7. `MakeTemp` had a dead parsed `Path` and accepted a prefix with a NUL byte: removed, and the NUL is `Invalid` (test).
8. `TempDir` shelled out through `std::system` with an interpolated path and its helpers were defined in one test file: it now walks the tree with `chmod` through the POSIX API, and the header is self-contained.
9. The passwd buffer for the home directory fallback grew from 1 KiB to 16 KiB.
10. Bookkeeping: `STATUS.md`, this section and a `KNOWN-ISSUES.md` entry for the X11 window test (failed again in the plain scoped run).
