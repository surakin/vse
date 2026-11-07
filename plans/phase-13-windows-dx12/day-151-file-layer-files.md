# Phase 13 / Day 151 — File Layer I: Paths, Errors, Local Files

**Phase:** [Phase 13 — Windows Port + DX12 Backend](../phase-13-windows-dx12.md)
**Depends on:** Day 150; `engine/foundation/io/BACKEND.md`.

The first half of the Windows file backend: path style, error mapping, and the synchronous local file. New directory `engine/foundation/io/windows/`.

## Tasks

- [x] **`NativePathStyle`** returns `PathStyle::Windows`; the `Path` tests that are already written for the Windows style (Phase 9 tests them on Linux) now
      also run natively, and new native cases cover drive-relative paths (`C:foo`), UNC (`\\server\share\x`), `\\?\` and `\\.\` prefixes, trailing dots and
      spaces, reserved names (`CON`, `NUL`, `COM1`, `LPT1`, with and without extensions) and forward slashes accepted on input. What `Path` refuses versus passes
      through is the spec's rule; record it in `BACKEND.md`.
      *Done:* `NativePathStyle()` already returned `Windows` on `_WIN32`; `tests/io/path_test.cpp` now runs natively, and `io_windows_test.cpp` covers
      `C:foo`, UNC, both prefixes, mixed separators and case. Rule (`BACKEND.md`, "Windows paths"): `Path` passes everything through as text; the
      backend (`io/windows/win_path.cpp`) refuses reserved device names (with `COM0`/`LPT0` and the superscript forms, with extensions and spaces before
      the dot), names ending in a dot or space, a colon in a name and invalid UTF-8 with `Invalid`, and lets `\\?\` and `\\.\` paths through
      untouched. A device path is a UNC root to `Path`, so it reads `\\.\NUL\` (Win32 opens that). Windows 11 no longer maps `dir\CON` to the
      console (probed); the rule refuses it anyway, the same on every Windows version.
- [x] **`ErrorFromOsCode`** for Win32 errors (`ERROR_FILE_NOT_FOUND`, `ERROR_PATH_NOT_FOUND`, `ERROR_ACCESS_DENIED`, `ERROR_SHARING_VIOLATION`,
      `ERROR_LOCK_VIOLATION`, `ERROR_ALREADY_EXISTS`, `ERROR_FILE_EXISTS`, `ERROR_DISK_FULL`, `ERROR_HANDLE_DISK_FULL`, `ERROR_DIR_NOT_EMPTY`, `ERROR_INVALID_NAME`,
      `ERROR_FILENAME_EXCED_RANGE`, `ERROR_NOT_SAME_DEVICE`, `ERROR_WRITE_PROTECT`, `ERROR_NOT_READY`, `ERROR_OPERATION_ABORTED`, `ERROR_IO_PENDING`,
      `ERROR_HANDLE_EOF`, `ERROR_BROKEN_PIPE`, ...) onto `io::Status`; the table is a test; the message keeps the Win32 code (`FormatMessageW` is not used in
      the hot path: codes are printed as numbers plus the engine's own text).
      *Done:* `io/windows/error.cpp`, table test in `io_windows_test.cpp`; `ErrorName` moved to the neutral `io/error.cpp`. A "path not found" whose
      parent is a file is `NotADirectory`, as on Linux (`RefineNotFound` looks at the parents on that error only).
- [x] **`io/windows/local_file.cpp`:** `Open` with `CreateFileW` (access and creation disposition from the open mode; share modes chosen per the spec:
      readers share read, writers exclusive by default, `FILE_SHARE_DELETE` so atomic replace works while a reader has the file open — decide and test);
      `Read`/`Write` at offsets (`OVERLAPPED.Offset/OffsetHigh` on a synchronous handle), `Size`, `SetSize` (`SetFileInformationByHandle(FileEndOfFileInfo)`),
      `Flush` (`FlushFileBuffers`), `Close`; a read that crosses the end returns the short count; 4 GiB+ offsets tested with a sparse file.
      *Done:* every open shares read, write and delete (the "Sharing" rule below). The interface's `Flush` is a no-op (nothing is buffered in the process)
      and `Sync` is `FlushFileBuffers`; `Truncate` is `FileEndOfFileInfo` and marks the file sparse before it grows it, so the 5 GiB case of `file_test.cpp`
      runs in milliseconds; appends write at the all-ones offset; truncation on open is an end-of-file of 0 on the handle (review finding 2). The handle
      is synchronous until Day 152.
- [x] **`Stat`/`Lstat`** with `GetFileInformationByHandleEx` (basic, standard and attribute tag info): size, kind (file, directory, symlink, junction — a
      junction is reported as the spec says), times converted from `FILETIME` to the engine's epoch, read-only attribute as permissions; `Lstat` opens with
      `FILE_FLAG_OPEN_REPARSE_POINT`.
      *Done:* a junction is `Other` for `Lstat` (`BACKEND.md`), a reparse point that is not a name surrogate is the file or directory it stands for; the
      read-only attribute counts on files only (a directory is 0755); `read_only` adds an `AccessCheck` of the ACL. A file held without any sharing
      (the paging file) is described from its directory entry (review finding 4). The symbolic-link cases skip here: Developer Mode is off.
- [x] **Long paths:** this PC has `LongPathsEnabled = 0` (checked 2026-11-07), so the manifest's `longPathAware` has no effect here. Paths over `MAX_PATH`
      must work through the `\\?\` prefix either way; a test creates, reads, renames and removes a file 300+ characters deep and passes with the setting off,
      and records the setting's value; the behavior with it on is checked once by hand if the developer turns it on (an elevated system change, theirs to
      make), else filed. `BUILD.md` lists the setting.
      *Done:* `LongPathsEnabled = 0` here (the test records it). Past 248 characters the backend resolves the path with `GetFullPathNameW` (current
      directory, `C:foo`, `.` and `..`) and adds the prefix; the test creates a file 360+ characters deep, reads it, `Stat`s it through a `..` and a
      long relative path, and renames and removes it with Win32 (the layer's `Rename`/`Remove` are Day 152's). The check with the setting on was not
      done (filed).
- [x] **Share modes and locks** (filed in Phase 9 as "revisit with Phase 13"): decide in `BACKEND.md` what the interface promises on both platforms (Windows
      share flags versus Linux's advisory nothing); a test opens a file twice in each mode combination and checks the documented result on both machines.
      *Done:* one rule, `BACKEND.md` "Sharing" and the `File::Open` contract: nothing is locked, any number of opens in any modes, an open file can be
      renamed, replaced or removed; on Windows a foreign exclusive opener gives `Busy`. `tests/io/share_test.cpp` (shared; Linux at the phase close)
      opens every pair of modes; `io_windows_test.cpp` renames and deletes a file the engine holds open and checks `Busy`.
- [x] **The `File` conformance suite** (`tests/io`) runs on the Windows local file: the synchronous half passes; the asynchronous half is Day 152 and skips with
      that reason today; `tests/io/temp_dir.h` and the other POSIX helpers are ported (raw file-system shapes planted on purpose stay allow-listed).
      *Done:* `file_test.cpp`, `path_test.cpp`, `share_test.cpp` and `io_windows_test.cpp` build on Windows (plain and ASan); `temp_dir.h` plants and
      removes with Win32 there (and an ACL helper replaces `chmod`). The two cases that need `WriteFileAtomic` `SKIP` with the Day 152 reason; the
      asynchronous tests (`async_file_test.cpp`) need the job system and join with Days 152-153. The other functions of `file_system.h` return
      `Unsupported` until Day 152 so the library links (`KNOWN-ISSUES.md`).
- [x] **Documentation:** `BACKEND.md` Windows notes per function touched; `docs/guide/io.md` gets "Windows paths" (drive letters, UNC, long paths, reserved
      names, case folding); `docgen lint` zero; reference regenerated.
      *Done:* `BACKEND.md` ("Sharing", "Windows paths", Windows notes under `Stat`, `Lstat`, `Open`, `ErrorFromOsCode`, `NativePathStyle`);
      `docs/guide/io.md` "Windows paths"; `File::Open` and `Stat` contracts in `file.h`/`file_system.h`; `BUILD.md`. `docgen lint` and the reference
      regeneration cannot run on Windows before Day 157: left for the Linux run (`KNOWN-ISSUES.md`).
- [x] **Runs:** Windows `--affected` plain and asan; Linux `--affected` (the share-mode test and any shared `Path` change); quote the lines.
      *Windows, after the review fixes:* `scripts/test-phase.sh --affected` printed
      `ran 283 of 357 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 13 s` and `--affected --flavor asan`
      `ran 74 of 357 tests (phases: all; flavors: asan; display: included; crash tests: excluded) in 4 s`; `ctest --preset windows-msvc` 357 of 357
      (4 skipped by the Day 152 `SKIP`s), `windows-clang-cl` 314 of 314, the non-unity MSVC build 357 of 357. Linux: at the phase close.
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: handle leaks on every error path (count open handles with `GetProcessHandleCount` before and
      after the conformance suite), the long-path prefix on relative paths, and reserved device names reaching `CreateFileW`.
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** paths, errors, `Stat`/`Lstat` and synchronous local files work on Windows, the synchronous conformance cases pass, and share modes have one documented
rule tested on both machines.

- **For the Linux run at the phase close:** shared files changed today: `io/error.cpp` (new, `ErrorName` moved out of `io/linux/error.cpp`),
  `io/file.h` and `io/file_system.h` (contracts), `io/BACKEND.md`, `engine/foundation/CMakeLists.txt`, `unity/windows_enable.h`, `tests/io/temp_dir.h`,
  `tests/io/file_test.cpp`, the new `tests/io/share_test.cpp`, `tests/io/CMakeLists.txt`, `tests/support/test_text.h` (`ENGINE_PRINTF_FORMAT`),
  `tests/phases.txt`, `tools/module_deps_check/modules.txt`, `tools/file_access_check/allowlist.txt`, `ARCHITECTURE.md`, `BUILD.md`, `docs/guide/io.md`.
  On Linux: `scripts/test-phase.sh --affected` with asan and tsan, `ctest -L docs`, `docgen lint` and the reference regeneration.

## Post-implementation code review

`/code-review high` over the day's diff: **10 findings, 6 fixed, 4 declined.**

Fixed, each with a test that failed before the fix: (1) the Windows `TempDir` removed its tree through unprefixed paths, so the long-path test's tree
stayed in `%TEMP%` on every run (15 directories had piled up; removed): it now walks the tree in the `\\?\` form, and a test checks the removal. (2)
`Write` and `kOpenTruncate` used `CREATE_ALWAYS`/`TRUNCATE_EXISTING`, which refuse hidden and system files: truncation is now an end-of-file of 0 on the
open handle, which also keeps the attributes as `O_TRUNC` keeps the mode. (3) The wrapper's documented way to include more SDK headers (their own
enable/disable pair) failed for any header that spells `FAR`/`NEAR`: `windows_enable.h` now defines `far` and `near` empty inside the region, and
`winioctl.h` builds in its own pair (it did not). (4) `Stat` failed with `Busy` on files held without any sharing (the paging file): the directory
entry describes them (the test skips where the paging file is not held so). (5) `io_backend_lists` was not in the Windows build: registered (it
checks 32 functions). (6) `Exists`, `IsFile` and `IsDirectory` paid for the ACL check they discard: skipped for them.

Declined: (7) the Day 152 functions return `Unsupported`: planned, listed in `KNOWN-ISSUES.md`. (8) `docgen lint` and the reference regeneration for the
changed contracts cannot run on Windows before Day 157: left to the Linux run. (9) The symbolic-link cases skip without Developer Mode: filed as
unverified. (10) The directory-entry fallback describes a link entry rather than its target for `Stat`: reached only when the target holds no sharing
at all, which cannot be opened either way.
