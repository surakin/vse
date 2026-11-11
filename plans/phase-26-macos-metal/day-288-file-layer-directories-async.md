# Phase 26 / Day 288 — File Layer II: Directories, File-System Operations, Asynchronous I/O

**Phase:** [Phase 26 — macOS Port + Metal Backend](../phase-26-macos-metal.md)
**Depends on:** Day 287.

The rest of `engine/foundation/io/BACKEND.md`: directories, file-system operations, the standard directories and native asynchronous reads through
`dispatch_io`.

## Tasks

- [ ] **Directories** (`Open`, `Next`, `Info`, `Close`): `opendir`/`readdir`/`fstatat(AT_SYMLINK_NOFOLLOW)`, dot entries skipped, `d_type` used when not
      `DT_UNKNOWN`; shared with Linux in `io/posix/directory.cpp` if the inventory says so.
- [ ] **File-system operations:** `Remove`, `RemoveAll` (`openat`/`unlinkat` walk, never following a link, the 128-level limit), `Rename`, `Copy`
      (`copyfile` with `COPYFILE_DATA | COPYFILE_STAT | COPYFILE_EXCL` unless overwriting; `COPYFILE_CLONE` for an APFS clone when the flags allow it;
      the same file under two names refused by `st_dev`/`st_ino`), `SetPermissions` (`chmod`), `CreateDirectory`/`CreateDirectories`,
      `CreatePrivateDirectory` (0700 with an owner check, refusing a link), `CreateTempFile` (`mkstemp`), `CreateTempDirectory` (`mkdtemp`),
      `CurrentDirectory`/`SetCurrentDirectory`, `Absolute`, `Canonical` (`realpath`), `ReadLink`.
- [ ] **Standard directories:** `TempDirectory` (`confstr(_CS_DARWIN_USER_TEMP_DIR)` or `$TMPDIR`, not `/tmp`), `UserConfigDirectory` and
      `UserDataDirectory` (`~/Library/Application Support/<app>`), `UserCacheDirectory` (`~/Library/Caches/<app>`), `UserStateDirectory`
      (`.../Application Support/<app>/state`), `CrashDumpDirectory` (`.../Application Support/<app>/crashes`, 0700), from `NSSearchPathForDirectoriesInDomains`
      in an `io/macos/special_folders.mm` with `$HOME` as the fallback; a sandboxed process gets its container's paths from the same call (documented, not
      tested: no sandboxed test runner).
- [ ] **Asynchronous I/O:** override `File::StartAsync` with `dispatch_io` (a `DISPATCH_IO_RANDOM` channel per file, `dispatch_io_read`/`write` at an
      offset, completion on a private serial queue, never inside `StartAsync`); `WaitForAsyncRequests` in the destructor; cancellation and a closed file
      tested; measured against the default executor for 1,000 4 KiB reads and one 64 MiB read (keep the default if `dispatch_io` is not faster, and
      record why).
- [ ] **Jobs' asynchronous I/O** (`engine/jobs` uses `File::StartAsync`): its tests run here on Day 289; today the io-level async tests pass.
- [ ] **Conformance:** the `File` and `Directory` conformance suites and the network-like double's tests pass on macOS; the case-insensitivity rule of
      Day 287 applies to `Rename` tests (a rename that changes only case is a real rename on APFS: tested).
- [ ] **Documentation:** `BACKEND.md` "macOS, as built (Day 288)" notes; `io.md` macOS section completed (standard directories table, `dispatch_io`);
      `KNOWN-ISSUES.md` Phase 9 macOS line removed; `docgen lint` zero; reference regenerated.
- [ ] **Runs:** `scripts/test-phase.sh --affected` (plain, asan, tsan; quote); the Linux run of any shared file filed.
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: completion ordering and lifetime in the `dispatch_io` path (a file destroyed with reads in
      flight), `RemoveAll` never following a link, `CreatePrivateDirectory`'s race with a planted link.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** every function of `io/BACKEND.md` is implemented on macOS and the file layer's suites pass in three flavors and both unity modes; the
`dispatch_io` measurement and the standard-directory table are recorded; the Phase 9 macOS deferral is closed.
