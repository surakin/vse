# Phase 26 / Day 271 — File Layer I: Paths, Errors, Local Files

**Phase:** [Phase 26 — macOS Port + Metal Backend](../phase-26-macos-metal.md)
**Depends on:** Day 270.

The first half of `engine/foundation/io/BACKEND.md` on macOS: errors, path style, the local file (synchronous), atomic writes. Code identical with
Linux moves to `io/posix/`; macOS-only code goes to `io/macos/`.

## Tasks

- [ ] **Split** `io/linux/{error,local_file,file_system}.cpp` per the inventory: the POSIX-identical parts to `io/posix/`, Linux-only calls (`statx`,
      `O_TMPFILE`, `copy_file_range`, `renameat2`, `/proc/self/exe`) stay in `io/linux/`, their macOS counterparts in `io/macos/`. `modules.txt`,
      `file_access_check`'s allow list (the backend's own files, with the reason) and `io_backend_lists` updated.
- [ ] **`ErrorFromOsCode`:** errno as on Linux, plus the codes macOS adds (`EAUTH`, `ENOATTR`, `EFTYPE`, `ENEEDAUTH`) and the ones with different values;
      a table test over every code the backend maps.
- [ ] **`NativePathStyle`** is POSIX; `Path` needs no change. Document in `io.md` that a case-insensitive volume is the file system's rule (`Path` does not
      fold) and that APFS keeps the normalization form it was given (HFS+ decomposed); a test creates `é` in NFC and opens it in NFD and records the
      result on this volume (both behaviors are allowed; the test asserts only that the layer reports what the file system does, without folding).
- [ ] **Local file** (`Open`, `Read`, `Write`, `ReadAt`, `WriteAt`, `Size`, `Flush`, `Close`, `Stat`, `Lstat`): `open` with `O_CLOEXEC`, `pread`/`pwrite`,
      `fstat`; `Flush` with durability uses `fcntl(F_FULLFSYNC)` (plain `fsync` does not reach the platter on macOS), falling back to `fsync` where the file
      system refuses it, documented on the function.
- [ ] **Atomic writes** (`WriteFileAtomic`): temp file plus `rename`; `SyncDirectory` is `fsync` on the directory descriptor (`F_FULLFSYNC` where accepted);
      measured once: the cost of `F_FULLFSYNC` on this SSD for a 4 KiB settings file.
- [ ] **Executable path:** `_NSGetExecutablePath` resolved with `realpath` (both through the backend; the gate allows them there).
- [ ] **Conformance:** the `File` conformance suite (`tests/io`) runs against the macOS local file; the case-insensitive volume changes the expected result
      of any test that relies on two names differing only in case: such a test asks the volume (`pathconf(_PC_CASE_SENSITIVE)`) and checks the matching
      behavior instead of skipping.
- [ ] **Documentation:** `io/BACKEND.md` sections get "macOS, as built (Day 271)" notes where behavior differs from Linux; `docs/guide/io.md` gets a macOS
      section; `docgen lint` zero; reference regenerated on macOS.
- [ ] **Runs:** `scripts/test-phase.sh --affected` on macOS (plain, asan, tsan; quote the lines); the Linux run of the split files filed.
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: every failure path closes what it opened, `F_FULLFSYNC` fallback, partial reads and
      writes, `EINTR`.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** the local file, errors and atomic writes pass the `File` conformance suite on macOS in three flavors and both unity modes; the POSIX part is
shared with Linux (whose run is filed); `io.md` and `BACKEND.md` describe the macOS behavior.
