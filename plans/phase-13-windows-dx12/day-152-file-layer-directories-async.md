# Phase 13 / Day 152 — File Layer II: Directories, File-System Operations, Asynchronous I/O

**Phase:** [Phase 13 — Windows Port + DX12 Backend](../phase-13-windows-dx12.md)
**Depends on:** Day 151.

Finish the Windows file backend: every function of `engine/foundation/io/BACKEND.md`, the asynchronous file operations, and the whole `tests/io` suite.

## Tasks

- [ ] **`io/windows/directory.cpp`:** `Open`/`Next`/`Close`/`Info` over `FindFirstFileExW(FindExInfoBasic, FIND_FIRST_EX_LARGE_FETCH)`; `.` and `..` skipped;
      names converted to UTF-8; reparse points reported, not followed.
- [ ] **`io/windows/file_system.cpp`**, one function per `BACKEND.md` section: `Remove` (read-only files: the attribute cleared only if the interface promises
      it — decide), `RemoveAll` (does not follow junctions or symlinks out of the tree; test with a junction pointing outside), `Rename`
      (`MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`), atomic replace (`ReplaceFileW` when the target exists, `MoveFileExW` otherwise; the
      failure modes of `ReplaceFileW` mapped and tested), `Copy` (`CopyFile2`), `SetPermissions` (read-only attribute; what POSIX bits mean on Windows is
      documented), `SyncDirectory` (no Windows counterpart: a documented no-op or `FlushFileBuffers` on a directory handle — measured and decided),
      `CreateDirectory`/`CreateDirectories`, `CreatePrivateDirectory` (a DACL granting only the current user, `SetNamedSecurityInfoW` or a security
      descriptor at creation; test reads the ACL back), `CreateTempFile`/`CreateTempDirectory` (random names, exclusive create), `CurrentDirectory`/
      `SetCurrentDirectory`, `Absolute` (`GetFullPathNameW` semantics without touching the disk), `Canonical` (`GetFinalPathNameByHandleW`, prefix stripped),
      `ReadLink` (reparse data), `TempDirectory` (`GetTempPath2W`), `ExecutablePath`/`ExecutableDirectory` (`GetModuleFileNameW`, growing buffer),
      `UserConfigDirectory`/`UserDataDirectory` (`FOLDERID_RoamingAppData`), `UserCacheDirectory`/`UserStateDirectory` (`FOLDERID_LocalAppData`, with the
      subfolders the spec names), `CrashDumpDirectory` (`%LOCALAPPDATA%/<app>/crashes`, private).
- [ ] **Asynchronous file operations:** the Windows half of `io::File`'s asynchronous interface and of `jobs/file_async`/`async_io` — overlapped I/O on a
      `FILE_FLAG_OVERLAPPED` handle completed through an I/O completion port serviced by the job system's I/O thread (or the mechanism the Linux side uses,
      if the spec keeps one design); cancellation (`CancelIoEx`) and the completion after cancel; the network-like test double still passes.
- [ ] **The whole `tests/io` suite** passes on Windows, including the conformance suite's asynchronous half and the planted file-system shapes (junctions and
      symlinks need Developer Mode or elevation for symlinks: tests that need a symlink skip with that reason when creation fails; junctions need neither).
- [ ] **`io_backend_lists` and `io_cross_syntax_windows`** still pass; the cross-syntax check now has a real build beside it — keep it (it guards the
      header-only check on Linux).
- [ ] **Documentation:** `BACKEND.md` Windows notes complete; `docs/guide/io.md` "Windows" section finished (atomic replace, private directories, known
      folders, symlink privileges); reference regenerated; `docgen lint` zero.
- [ ] **Runs:** Windows `--affected` plain and asan; Linux `--affected` if `jobs/async_io` or shared `io` code changed (record which); quote the lines.
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: completion-port lifetime (a request completing after its `File` is destroyed; a cancel racing
      a completion; ASan on the async tests repeated 100 times), `RemoveAll` never leaving the tree, the private directory's ACL.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** every function of the file layer's backend checklist is implemented for Windows, asynchronous reads and writes complete and cancel correctly, and the
whole `tests/io` suite passes on Windows in plain and ASan.
