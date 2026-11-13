# Phase 21 / Day 287 — File Layer I: Paths, Errors, Local Files

**Phase:** [Phase 21 — macOS Port + Metal Backend](../phase-21-macos-metal.md)
**Depends on:** Day 286.

The first half of `engine/foundation/io/BACKEND.md` on macOS: errors, path style, the local file (synchronous), atomic writes. Code identical with
Linux moves to `io/posix/`; macOS-only code goes to `io/macos/`.

## Tasks

- [x] **Split** `io/linux/{error,local_file,file_system}.cpp` per the inventory: the POSIX-identical parts to `io/posix/`, Linux-only calls (`statx`,
      `O_TMPFILE`, `copy_file_range`, `renameat2`, `/proc/self/exe`) stay in `io/linux/`, their macOS counterparts in `io/macos/`. `modules.txt`,
      `file_access_check`'s allow list (the backend's own files, with the reason) and `io_backend_lists` updated.
- [x] **`st_mtim`** (`io/linux/file_system.cpp:42`, `directory.cpp:50`): `st_mtimespec` on macOS, through one helper (Day 283 Spike A).
- [x] **`ErrorFromOsCode`:** errno as on Linux; `ENOTSUP` and `EOPNOTSUPP` are different numbers on macOS (`io/linux/error.cpp:25` assumes they are one), plus the codes macOS adds (`EAUTH`, `ENOATTR`, `EFTYPE`, `ENEEDAUTH`) and the ones with different values;
      a table test over every code the backend maps.
- [x] **`NativePathStyle`** is POSIX; `Path` needs no change. Document in `io.md` that a case-insensitive volume is the file system's rule (`Path` does not
      fold) and that APFS keeps the normalization form it was given (HFS+ decomposed); a test creates `é` in NFC and opens it in NFD and records the
      result on this volume (both behaviors are allowed; the test asserts only that the layer reports what the file system does, without folding).
- [x] **Local file** (`Open`, `Read`, `Write`, `ReadAt`, `WriteAt`, `Size`, `Flush`, `Close`, `Stat`, `Lstat`): `open` with `O_CLOEXEC`, `pread`/`pwrite`,
      `fstat`; `Flush` with durability uses `fcntl(F_FULLFSYNC)` (plain `fsync` does not reach the platter on macOS), falling back to `fsync` where the file
      system refuses it, documented on the function.
- [x] **Atomic writes** (`WriteFileAtomic`): temp file plus `rename`; `SyncDirectory` is `fsync` on the directory descriptor (`F_FULLFSYNC` where accepted);
      measured once: the cost of `F_FULLFSYNC` on this SSD for a 4 KiB settings file.
- [x] **Executable path:** `_NSGetExecutablePath` resolved with `realpath` (both through the backend; the gate allows them there).
- [x] **Conformance:** the `File` conformance suite (`tests/io`) runs against the macOS local file; the case-insensitive volume changes the expected result
      of any test that relies on two names differing only in case: such a test asks the volume (`pathconf(_PC_CASE_SENSITIVE)`) and checks the matching
      behavior instead of skipping.
- [x] **Documentation:** `io/BACKEND.md` sections get "macOS, as built (Day 287)" notes where behavior differs from Linux; `docs/guide/io.md` gets a macOS
      section; `docgen lint` zero; reference regenerated on macOS.
- [x] **Runs:** `scripts/test-phase.sh --affected` on macOS (plain, asan, tsan; quote the lines); the Linux run of the split files filed.
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: every failure path closes what it opened, `F_FULLFSYNC` fallback, partial reads and
      writes, `EINTR`.
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** the local file, errors and atomic writes pass the `File` conformance suite on macOS in three flavors and both unity modes; the POSIX part is
shared with Linux (whose run is filed); `io.md` and `BACKEND.md` describe the macOS behavior.

## Result (2026-11-11, the Mac)

- **The split.** `io/linux/{error,local_file,directory,file_system}.cpp` moved whole to `io/posix/` (git sees the renames): the inventory's
  Linux-only calls (`statx`, `O_TMPFILE`, `copy_file_range`, `renameat2`) were not there to begin with. What differs is behind
  `io/posix/posix_io.h`: two hooks (`ModifiedUnixNs`: `st_mtim` / `st_mtimespec`; `SyncDescriptor`: `fsync` / `F_FULLFSYNC`), plus each system's
  `special_folders.cpp` (the executable path, the temporary and the user directories, the crash dump directory). Linux's are the old code
  unchanged (`io/linux/special_folders.cpp`, `posix_hooks.cpp`); nothing of it has been compiled on Linux yet (filed). Directories moved with the
  rest (the layer links as a whole), so Day 288 starts from a working `Directory`.
- **Errors.** The shared table, with what Darwin adds: `EOPNOTSUPP` (102, distinct from `ENOTSUP` 45) `Unsupported`, `EAUTH`/`ENEEDAUTH`
  `PermissionDenied`, `EFTYPE` `Invalid`, `ENOATTR` `NotFound`; the Darwin rows are in the shared table test (`tests/io/file_system_test.cpp`,
  `#if ENGINE_PLATFORM_MACOS`), which runs over every mapped code.
- **Names on this volume** (APFS, the temporary volume): case-insensitive (`pathconf(_PC_CASE_SENSITIVE)` 0; the test checks the matching behavior,
  including a rename that changes only the case), normalization-insensitive (an NFC name opens by its NFD spelling) and form-preserving (it lists as
  written). The layer folds nothing; the test asserts it reports what the raw `access` call says.
- **Durability.** `File::Sync` and `SyncDirectory` use `F_FULLFSYNC`, falling back to `fsync` on `ENOTSUP`/`EINVAL`/`ENOTTY`. Measured on this SSD:
  `fsync` 0.39 ms, `F_FULLFSYNC` 18.23 ms for 4 KiB (mean of 10); `WriteFileAtomic` of 4 KiB, which syncs the file and the directory, 39.0 ms mean,
  40.2 ms slowest of 10.
- **Folders (interim until Day 288's `NSSearchPathForDirectoriesInDomains`).** `$HOME/Library/Application Support/<app>` (configuration, data),
  `.../Caches/<app>`, `.../Application Support/<app>/state`, `.../crashes`; `TempDirectory` `$TMPDIR`, else `confstr(_CS_DARWIN_USER_TEMP_DIR)`,
  never `/tmp` unless both fail; `ExecutablePath` `_NSGetExecutablePath` resolved with `realpath`.
- **Tests.** `tests/io` joins the macOS build in three flavors without `async_file_test.cpp` (it needs the job system, Day 289). Three Linux-only checks
  are now `#if ENGINE_PLATFORM_LINUX` (the XDG directories, two `/proc/self/status` reads); macOS has its own in `io_macos_test.cpp` (the `~/Library`
  table with a fixed `HOME`, a relative `HOME`, bad names, `TMPDIR`; a FIFO that reports no size read whole). The shared `EnvGuard` moved to
  `tests/io/temp_dir.h` (the two files that define it merge in a unity build: shared, not skipped). The File conformance suite passes on the macOS
  local file.
- **Not done here:** `docgen lint` and the reference (moved to Day 293 on Day 286; `file.h`'s `Sync` comment changed: filed for Linux).
- **Runs (macOS, `/bin/bash scripts/test-phase.sh`):** `--affected` (everything: CMake changed) plain `ran 405 of 657 tests (phases: all; flavors: plain;
  ...)`, asan `ran 126 of 657`, tsan `ran 126 of 657`, all passed; non-unity `ran 657 of 657 tests (phases: all; flavors: all; ...)`, passed. No warnings.
  (After the review's fixes; the macOS errno test merged into the shared one, three tests fewer.)

## Post-implementation code review

`/code-review high` on the day's diff: 10 findings. **8 fixed, 1 in part, 1 declined.**

1. **Fixed: the FIFO test could hang or kill the test process** (a blocking `open(O_WRONLY)` if the reader never opens; `SIGPIPE` if it stops early).
   The writer now opens with `O_NONBLOCK`, retried on `ENXIO` for at most five seconds, and the test ignores `SIGPIPE` while it runs.
2. **Fixed: the `F_FULLFSYNC` fallback missed `EOPNOTSUPP`**, a number of its own on Darwin, so such a volume would have failed `Sync` instead of
   getting `fsync`.
3. **Declined: `F_BARRIERFSYNC` for the file step of `WriteFileAtomic`** (half the 39 ms). It rests on the directory's `F_FULLFSYNC` flushing the
   file's data too, a drive-cache property this day did not verify; `WriteFileAtomic` goes through `File::Sync`, whose promise is durability. The
   cost is measured and documented; a cheaper path for frequent saves is for the storage work to ask for with a measurement.
4. **Fixed: the arena `ReadFile` of a source with no size** was no longer tested on macOS: the FIFO test now reads through both overloads.
5. **Fixed: the crash dump directory's app-name logic was copied** into both special-folder files: one `posix::CrashAppName` now
   (`io/posix/file_system.cpp`).
6. **Fixed: the macOS errno table duplicated the shared one**: its Darwin rows are in `file_system_test.cpp` under `#if ENGINE_PLATFORM_MACOS`, and
   the copy is gone.
7. **Fixed: `io.md` said directories come on Day 288**; they work today (the shared backend).
8. **Fixed: `ARCHITECTURE.md`** names `io/posix/` beside `os/posix/`.
9. **In part: `docs/reference/` not regenerated** (`file.h`'s `Sync` comment changed): docgen is not built on the Mac before Day 293 (Day 286's
   plan change), filed for the Linux machine. `STATUS.md` and this file are updated in the commit.
10. **Fixed: a stale `KNOWN-ISSUES.md` entry** said neutral code still lived under `io/linux/`; checked and rewritten. `io/linux/special_folders.cpp`'s
   header no longer claims to call the file API directly.
