# Phase 12 / Day 144 — File Layer I: Paths, Errors, Local Files

**Phase:** [Phase 12 — Windows Port + DX12 Backend](../phase-12-windows-dx12.md)
**Depends on:** Day 143; `engine/foundation/io/BACKEND.md`.

The first half of the Windows file backend: path style, error mapping, and the synchronous local file. New directory `engine/foundation/io/windows/`.

## Tasks

- [ ] **`NativePathStyle`** returns `PathStyle::Windows`; the `Path` tests that are already written for the Windows style (Phase 9 tests them on Linux) now
      also run natively, and new native cases cover drive-relative paths (`C:foo`), UNC (`\\server\share\x`), `\\?\` and `\\.\` prefixes, trailing dots and
      spaces, reserved names (`CON`, `NUL`, `COM1`, `LPT1`, with and without extensions) and forward slashes accepted on input. What `Path` refuses versus passes
      through is the spec's rule; record it in `BACKEND.md`.
- [ ] **`ErrorFromOsCode`** for Win32 errors (`ERROR_FILE_NOT_FOUND`, `ERROR_PATH_NOT_FOUND`, `ERROR_ACCESS_DENIED`, `ERROR_SHARING_VIOLATION`,
      `ERROR_LOCK_VIOLATION`, `ERROR_ALREADY_EXISTS`, `ERROR_FILE_EXISTS`, `ERROR_DISK_FULL`, `ERROR_HANDLE_DISK_FULL`, `ERROR_DIR_NOT_EMPTY`, `ERROR_INVALID_NAME`,
      `ERROR_FILENAME_EXCED_RANGE`, `ERROR_NOT_SAME_DEVICE`, `ERROR_WRITE_PROTECT`, `ERROR_NOT_READY`, `ERROR_OPERATION_ABORTED`, `ERROR_IO_PENDING`,
      `ERROR_HANDLE_EOF`, `ERROR_BROKEN_PIPE`, ...) onto `io::Status`; the table is a test; the message keeps the Win32 code (`FormatMessageW` is not used in
      the hot path: codes are printed as numbers plus the engine's own text).
- [ ] **`io/windows/local_file.cpp`:** `Open` with `CreateFileW` (access and creation disposition from the open mode; share modes chosen per the spec:
      readers share read, writers exclusive by default, `FILE_SHARE_DELETE` so atomic replace works while a reader has the file open — decide and test);
      `Read`/`Write` at offsets (`OVERLAPPED.Offset/OffsetHigh` on a synchronous handle), `Size`, `SetSize` (`SetFileInformationByHandle(FileEndOfFileInfo)`),
      `Flush` (`FlushFileBuffers`), `Close`; a read that crosses the end returns the short count; 4 GiB+ offsets tested with a sparse file.
- [ ] **`Stat`/`Lstat`** with `GetFileInformationByHandleEx` (basic, standard and attribute tag info): size, kind (file, directory, symlink, junction — a
      junction is reported as the spec says), times converted from `FILETIME` to the engine's epoch, read-only attribute as permissions; `Lstat` opens with
      `FILE_FLAG_OPEN_REPARSE_POINT`.
- [ ] **Share modes and locks** (filed in Phase 9 as "revisit with Phase 12"): decide in `BACKEND.md` what the interface promises on both platforms (Windows
      share flags versus Linux's advisory nothing); a test opens a file twice in each mode combination and checks the documented result on both machines.
- [ ] **The `File` conformance suite** (`tests/io`) runs on the Windows local file: the synchronous half passes; the asynchronous half is Day 145 and skips with
      that reason today; `tests/io/temp_dir.h` and the other POSIX helpers are ported (raw file-system shapes planted on purpose stay allow-listed).
- [ ] **Documentation:** `BACKEND.md` Windows notes per function touched; `docs/guide/io.md` gets "Windows paths" (drive letters, UNC, long paths, reserved
      names, case folding); `docgen lint` zero; reference regenerated.
- [ ] **Runs:** Windows `--affected` plain and asan; Linux `--affected` (the share-mode test and any shared `Path` change); quote the lines.
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: handle leaks on every error path (count open handles with `GetProcessHandleCount` before and
      after the conformance suite), the long-path prefix on relative paths, and reserved device names reaching `CreateFileW`.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** paths, errors, `Stat`/`Lstat` and synchronous local files work on Windows, the synchronous conformance cases pass, and share modes have one documented
rule tested on both machines.
