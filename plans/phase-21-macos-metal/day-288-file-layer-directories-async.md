# Phase 21 / Day 288 — File Layer II: Directories, File-System Operations, Asynchronous I/O

**Phase:** [Phase 21 — macOS Port + Metal Backend](../phase-21-macos-metal.md)
**Depends on:** Day 287.

The rest of `engine/foundation/io/BACKEND.md`: directories, file-system operations, the standard directories and native asynchronous reads through
`dispatch_io`.

## Tasks

- [x] **Directories** (`Open`, `Next`, `Info`, `Close`): `opendir`/`readdir`/`fstatat(AT_SYMLINK_NOFOLLOW)`, dot entries skipped, `d_type` used when not
      `DT_UNKNOWN`; shared with Linux in `io/posix/directory.cpp` if the inventory says so.
- [x] **File-system operations:** `Remove`, `RemoveAll` (`openat`/`unlinkat` walk, never following a link, the 128-level limit), `Rename`, `Copy`
      (`copyfile` with `COPYFILE_DATA | COPYFILE_STAT | COPYFILE_EXCL` unless overwriting; `COPYFILE_CLONE` for an APFS clone when the flags allow it;
      the same file under two names refused by `st_dev`/`st_ino`), `SetPermissions` (`chmod`), `CreateDirectory`/`CreateDirectories`,
      `CreatePrivateDirectory` (0700 with an owner check, refusing a link), `CreateTempFile` (`mkstemp`), `CreateTempDirectory` (`mkdtemp`),
      `CurrentDirectory`/`SetCurrentDirectory`, `Absolute`, `Canonical` (`realpath`), `ReadLink`.
- [x] **Standard directories:** `TempDirectory` (`confstr(_CS_DARWIN_USER_TEMP_DIR)` or `$TMPDIR`, not `/tmp`), `UserConfigDirectory` and
      `UserDataDirectory` (`~/Library/Application Support/<app>`), `UserCacheDirectory` (`~/Library/Caches/<app>`), `UserStateDirectory`
      (`.../Application Support/<app>/state`), `CrashDumpDirectory` (`.../Application Support/<app>/crashes`, 0700), from `NSSearchPathForDirectoriesInDomains`
      in an `io/macos/special_folders.mm` with `$HOME` as the fallback; a sandboxed process gets its container's paths from the same call (documented, not
      tested: no sandboxed test runner).
- [x] **Asynchronous I/O:** override `File::StartAsync` with `dispatch_io` (a `DISPATCH_IO_RANDOM` channel per file, `dispatch_io_read`/`write` at an
      offset, completion on a private serial queue, never inside `StartAsync`); `WaitForAsyncRequests` in the destructor; cancellation and a closed file
      tested; measured against the default executor for 1,000 4 KiB reads and one 64 MiB read (keep the default if `dispatch_io` is not faster, and
      record why).
- [x] **Jobs' asynchronous I/O** (amended: `tests/io/async_file_test.cpp` holds the io-level async cases and the network-like double's suite in one file with
      the jobs cases, so all of it runs from Day 289, when `engine/jobs` builds on macOS; the default executor ran 1,000 concurrent reads correctly here in
      the measurement below) (`engine/jobs` uses `File::StartAsync`): its tests run here on Day 289; today the io-level async tests pass.
- [x] **Conformance** (the double's suite with Day 289, above): the `File` and `Directory` conformance suites and the network-like double's tests pass on macOS; the case-insensitivity rule of
      Day 287 applies to `Rename` tests (a rename that changes only case is a real rename on APFS: tested).
- [x] **Documentation:** `BACKEND.md` "macOS, as built (Day 288)" notes; `io.md` macOS section completed (standard directories table, `dispatch_io`);
      `KNOWN-ISSUES.md` Phase 9 macOS line removed; `docgen lint` zero; reference regenerated.
- [x] **Runs:** `scripts/test-phase.sh --affected` (plain, asan, tsan; quote); the Linux run of any shared file filed.
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: completion ordering and lifetime in the `dispatch_io` path (a file destroyed with reads in
      flight), `RemoveAll` never following a link, `CreatePrivateDirectory`'s race with a planted link.
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** every function of `io/BACKEND.md` is implemented on macOS and the file layer's suites pass in three flavors and both unity modes; the
`dispatch_io` measurement and the standard-directory table are recorded; the Phase 9 macOS deferral is closed.

## Result (2026-11-11, the Mac)

- **Directories** were shared on Day 287 (`io/posix/directory.cpp`, `st_mtimespec` through the hook); `directory_test.cpp` passes in three flavors.
- **File-system operations.** All shared POSIX code (Day 287). `Copy` moves its data through a new hook, `posix::CopyData`: `fcopyfile(COPYFILE_DATA)`
  on the descriptors `Copy` already holds on macOS for a regular-file source, the shared read/write loop (`posix::CopyLoop`, Linux's old code)
  for anything else and when `fcopyfile` is interrupted or refused (both rewound, the target emptied first); Linux uses the loop. **Not `COPYFILE_CLONE`** (the plan's "when the flags allow"):
  an APFS clone keeps the source's times and extended attributes, which `Copy` promises not to copy, and it is path-based, which reopens the race the
  descriptor same-file check closes. Test: a copy of a file dated 2001 is dated now, keeps mode 0640, refuses an existing target, overwrites with
  truncation. A FIFO source copies whole (`fcopyfile` refuses it with `ENOTSUP`).
- **Standard directories** (`io/macos/special_folders.mm`, Objective-C++ now): **amended order**: `$HOME` first when absolute (as `$XDG_*` and
  `%APPDATA%` on the other two systems; a sandboxed process has `$HOME` set to its container), then `NSSearchPathForDirectoriesInDomains`
  (`NSApplicationSupportDirectory`, `NSCachesDirectory`), then the password database. Checked: Foundation ignores `$HOME` (it answered
  `/Users/user/Library/...` with `HOME=/home/test` and with `HOME` unset), so Foundation first would have made the folders impossible to redirect.
  Test: with `HOME` unset the folders are the account's (`getpwuid`), and the crash dump directory is `NotFound`.
- **Asynchronous I/O: `dispatch_io` measured and not used.** A throwaway benchmark (warm cache, three rounds) against the default executor:
  1,000 random 4 KiB reads 13.0-15.6 ms on the executor, 232-239 ms on a `DISPATCH_IO_RANDOM` channel; one 64 MiB read 50-55 ms against 70-75 ms.
  `dispatch_io` delivers `dispatch_data_t` that must be copied into the caller's buffer and schedules per read; the macOS `LocalFile` keeps the
  default `StartAsync` and does not claim `kCapNativelyAsync`. Recorded in `BACKEND.md` (`Open`) and `io.md`.
- **Documentation:** the `Backend:` labels of `File::Open`, `Copy`, `TempDirectory` and the user directories in `file.h`/`file_system.h` say what was built;
  `BACKEND.md` "macOS, as built" notes for `Open`, `Copy`, the user directories and the platform notes; `io.md` macOS section completed. The Phase 9
  `KNOWN-ISSUES.md` line for the macOS backend is closed. `docgen lint` and the reference: Day 293 (Day 286's plan change), filed for Linux.
- **Runs (macOS, `/bin/bash scripts/test-phase.sh`, after the review's fixes):** `--affected` (everything: CMake changed) plain `ran 408 of 666 tests
  (phases: all; flavors: plain; ...)`, asan `ran 129 of 666`, tsan `ran 129 of 666`, all passed; non-unity `ran 666 of 666 tests (phases: all; flavors: all; ...)`, passed. No
  warnings. The plain run took 422 s against
  about 80 s on Day 287: Spotlight (`corespotlightd` at 117% CPU, load average about 30) was indexing at the time; the slowest tests were the
  source gates (`platform_neutral_check` 92 s), nothing failed.

## Post-implementation code review

`/code-review high` on the day's diff: 9 findings. **7 fixed, 1 in part, 1 declined.**

1. **Fixed (a real regression, with a test shown to fail first): `Copy` from a FIFO or device failed on macOS** (`fcopyfile` answers `ENOTSUP` for a
   source that is not a regular file; the reviewer checked it with a C program). `fcopyfile` is used for regular sources only; anything else goes
   through the shared loop. `io on macOS: Copy from a FIFO copies what it delivers, as on Linux` failed before (size 0) and passes.
2. **Fixed: no `KNOWN-ISSUES.md` entry for the day's shared files** waiting for Linux: added.
3. **Fixed: the async suites that wait for Day 289 were recorded only here**: a `KNOWN-ISSUES.md` entry now says they have not run on macOS.
4. **Fixed: an interrupted `fcopyfile` failed `Copy`** where Linux retries: an `EINTR` (or `ENOTSUP`/`EOPNOTSUPP` from the volume) rewinds both
   descriptors, empties the target and finishes with the loop.
5. **Declined: the "without HOME" test cannot tell Foundation's answer from the password database's** (both are the account's home). Telling them
   apart needs a seam in production code only for the test; the Foundation call itself was checked by hand (it ignores `$HOME`, above).
6. **Fixed: `FolderBase` repeated `HomeDirectory`'s `$HOME` rule**: Foundation is asked only without an absolute `$HOME`, then `HomeDirectory` decides.
7. **Fixed: the `Backend:` labels of `UserStateDirectory` and `CrashDumpDirectory`** said `~/Library`: they name the resolution now (and that the crash
   directory needs `$HOME`).
8. **Fixed: `foundation_io` relied on `foundation_base` for Foundation**: it links `-framework Foundation` itself.
9. **In part: the `CopyData` contract** now says what each system does; with the fallback, a FIFO source is no longer `Unsupported`, so the public
   failure list stands. The reference is regenerated with Day 293 (filed).
