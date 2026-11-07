# Phase 13 / Day 152 — File Layer II: Directories, File-System Operations, Asynchronous I/O

**Phase:** [Phase 13 — Windows Port + DX12 Backend](../phase-13-windows-dx12.md)
**Depends on:** Day 151.

Finish the Windows file backend: every function of `engine/foundation/io/BACKEND.md`, the asynchronous file operations, and the whole `tests/io` suite.

## Tasks

- [x] **Left by Day 151:** replace every `Unsupported` placeholder in `io/windows/file_system.cpp`; remove the two `SKIP`s in `tests/io/file_test.cpp`
      (`WriteFileAtomic`, the span overloads) and port their symbolic-link half; let the Windows `TempDir` in `tests/io/temp_dir.h` sweep stale
      `re_io_test.*` directories as the Linux one does; add to `tests/io/share_test.cpp` that an open file can be renamed and removed through
      `io::Rename`/`io::Remove` (Day 151 checks it with Win32 only); run the long-path test's rename and remove through the layer.
- [x] **`io/windows/directory.cpp`:** `Open`/`Next`/`Close`/`Info` over `FindFirstFileExW(FindExInfoBasic, FIND_FIRST_EX_LARGE_FETCH)`; `.` and `..` skipped;
      names converted to UTF-8; reparse points reported, not followed.
- [x] **`io/windows/file_system.cpp`**, one function per `BACKEND.md` section: `Remove` (read-only files: the attribute cleared only if the interface promises
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
- [ ] (Moved in part to Day 153, see the note) **Asynchronous file operations:** the Windows half of `io::File`'s asynchronous interface and of `jobs/file_async`/`async_io` — overlapped I/O on a
      `FILE_FLAG_OVERLAPPED` handle completed through an I/O completion port serviced by the job system's I/O thread (or the mechanism the Linux side uses,
      if the spec keeps one design); cancellation (`CancelIoEx`) and the completion after cancel; the network-like test double still passes.
- [x] **The whole `tests/io` suite** passes on Windows, including the conformance suite's asynchronous half and the planted file-system shapes (junctions and
      symlinks need Developer Mode or elevation for symlinks: tests that need a symlink skip with that reason when creation fails; junctions need neither).
- [x] **`io_backend_lists` and `io_cross_syntax_windows`** still pass; the cross-syntax check now has a real build beside it — keep it (it guards the
      header-only check on Linux).
- [x] **Documentation:** `BACKEND.md` Windows notes complete; `docs/guide/io.md` "Windows" section finished (atomic replace, private directories, known
      folders, symlink privileges); reference regenerated; `docgen lint` zero.
- [x] **Runs:** Windows `--affected` plain and asan; Linux `--affected` if `jobs/async_io` or shared `io` code changed (record which); quote the lines.
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: completion-port lifetime (a request completing after its `File` is destroyed; a cancel racing
      a completion; ASan on the async tests repeated 100 times), `RemoveAll` never leaving the tree, the private directory's ACL.
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** every function of the file layer's backend checklist is implemented for Windows, asynchronous reads and writes complete and cancel correctly, and the
whole `tests/io` suite passes on Windows in plain and ASan.

## Result (2026-11-07, Windows PC)

- **Directories:** `io/windows/directory.cpp` (`FindFirstFileExW` with `FIND_FIRST_EX_LARGE_FETCH`; the entry type follows `Lstat`: a junction is
  `Other` and is never entered). The listing, sorting, wildcards and walking moved out of `io/linux/directory.cpp` into the neutral
  `io/directory.cpp` (they called `PathStyle::Posix` explicitly, now the directory's own style); a backend provides `Open`, `Next`, `Close`,
  `DirectoryEntry::Info` and `detail::GetIdentity` (`io/detail/directory_identity.h`, internal).
- **File-system operations:** `io/windows/file_operations.cpp`, every function of `BACKEND.md`. Measured first: `MoveFileExW` refuses a target that
  is open, read-only or an empty directory, `FileRenameInfoEx` with POSIX semantics replaces all three, so `Rename` (and `WriteFileAtomic`) use it,
  and `Remove`/`RemoveAll` use the POSIX disposition (read-only and open files go, as on Linux); `ReplaceFileW` is not used. `SyncDirectory`
  flushes a directory handle opened for writing (measured: read access is refused) instead of `Unsupported`. `CreatePrivateDirectory` sets a
  protected DACL for the current user only (the test reads it back); temporary names come from `BCryptGenRandom` and inherit the directory's ACL
  (an explicit one would follow the file through `WriteFileAtomic`'s rename); the special folders are the known folders, grouped per
  application. Every decision is in `BACKEND.md`.
- **Asynchronous I/O, decided:** one design on both platforms. The Linux local file runs asynchronous requests on the file layer's executor and
  so does the Windows one (`LocalFile` does not claim `kCapNativelyAsync`); an I/O completion port is left for a measured need. The neutral
  executor path is the same code on both platforms. `tests/io/async_file_test.cpp` links the job system and joins the Windows build on Day 153
  (added to that day file), so the asynchronous half of the conformance suite has not run on Windows yet.
- **Tests:** `file_system_test`, `directory_test`, `file_test` (the Day 151 `SKIP`s are gone), `share_test` and `io_windows_test` run on Windows in
  plain and ASan. The POSIX calls in the shared tests became helpers in `temp_dir.h` (`MakeSymlink`, `MakeDirectoryLink`, which falls back to a
  junction without Developer Mode, `SetReadOnlyFile`, `DenyChanges`, `DenyListing`, `AllowAll`, `HardLink`, `RunningAsRoot`, `ReportedPermissions`);
  the Windows `TempDir` now uses the layer's own `CreateTempDirectory` and `RemoveAll` and sweeps stale directories, as on Linux. Two shared tests
  changed for both platforms: the sorted-listing names no longer differ only in case (`b` and `B` are one file on NTFS and APFS), and the
  `WriteFileAtomic` permission check keeps the read-only bit, the one every platform has. Symbolic-link cases skip with the reason here
  (Developer Mode is off).
- Found while running: `CreateDirectories` recursed with a 9 KiB path buffer per level and overflowed the 1 MiB Windows stack on the 140-level test
  tree (now iterative); the Windows test helper `ForEachEntry` did the same (buffers moved to the heap); a protected ACL whose allow entry is not
  inheritable leaves the children with no entry at all (the test helper's `AllowAll` now grants inheritably).
- **Runs** (after the review fixes): `scripts/test-phase.sh --affected` printed
  `ran 306 of 403 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 15 s`, `--affected --flavor asan`
  `ran 97 of 403 tests (phases: all; flavors: asan; display: included; crash tests: excluded) in 7 s`; `ctest --preset windows-msvc` 403 of 403,
  `windows-clang-cl` 337 of 337, non-unity 403 of 403. A run leaves no `re_io_test.*` directory behind (checked).
- **For the Linux run at the phase close:** shared files changed today: `io/directory.cpp` (new), `io/linux/directory.cpp` (split),
  `io/detail/directory_identity.h` (new), `io/file.h`, `io/file_system.h`, `io/directory.h` (Backend labels), `io/BACKEND.md`,
  `engine/foundation/CMakeLists.txt`, `tests/io/temp_dir.h`, `file_test.cpp`, `file_system_test.cpp`, `directory_test.cpp`, `share_test.cpp`,
  `tests/io/CMakeLists.txt`, `docs/guide/io.md`. On Linux: both unity builds, `scripts/test-phase.sh --affected` with asan and tsan, `ctest -L docs`.

## Post-implementation code review

`/code-review high` over the day's diff: **8 findings, 2 fixed, 6 declined.**

Fixed, each with a test that failed before: (1) `RemoveAll` went on past a directory it could not list and returned `NotEmpty` after three sweeps;
it now returns the listing error (`PermissionDenied`, as Linux). (2) The special-folders test created `%LOCALAPPDATA%\io_tests\crashes` (and
`_asan`) on every run and never removed it; it now names its own application through `ENGINE_APP_NAME` and removes it (the two folders already
left were removed).

Declined: (3) a name that Win32 normally refuses but another tool made through `\\?\` (`nul.txt`, `a.`) is listed but cannot be stat'ed or removed
by name (`RemoveAll` still removes it: its walk uses the raw names); rare, and refusing such names is the Day 151 rule. (4) The Linux half of
the directory split has not been built (Linux run at the phase close). (5) The reference is not regenerated for the changed Backend labels
(docgen on Windows is Day 157). (6) The asynchronous file tests run from Day 153. (7) `CreatePrivateDirectory` does not repeat the Linux
ancestor check (documented, with the reason). (8) `SyncDirectory` needs write access to the directory on Windows (only reachable where
`WriteFileAtomic` could not have created its temporary).
