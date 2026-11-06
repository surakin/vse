# File layer: what a platform backend implements

The file layer (`engine/foundation/io/`) is shaped so Windows, macOS, Android, iOS and the consoles can each implement it without changing
`Path`, `File`, `Directory` or any caller. This list is kept in step with the headers by the test `io_backend_lists`: every function whose
documentation carries a `Backend:` label has a `###` section here, and every section names such a function. The neutral headers compile
for the Windows and macOS targets (`io_cross_syntax_windows`, `io_cross_syntax_macos`). Design and reasons:
`docs/superpowers/specs/2026-11-06-file-layer-design.md`.

Linux is implemented in `engine/foundation/io/linux/`. A new platform adds `engine/foundation/io/<platform>/` and selects it in
`engine/foundation/CMakeLists.txt` next to the Linux sources.

## Rules every backend follows

- **Paths are UTF-8 in the API.** Convert to the platform encoding at the boundary (Windows: UTF-16 for the `W` functions; macOS: the
  decomposed form HFS+ stores is the file system's business, compare with the layer's rules). `Path` is canonical (`/` separators, no repeated
  or trailing ones); `Path::ToNative` produces the platform form.
- **Errors are values.** Map the platform code with `ErrorFromOsCode` and keep the code in `Status::os_code` for logs. Never throw, never abort on a
  bad path.
- **No handle leaks.** `File` exposes no descriptor or `HANDLE`. A backend closes what it opened on every failure path.
- **Case rules.** `Path` compares by the style's rule (Windows folds ASCII only). A case-insensitive volume on macOS is the file system's property, not
  `Path`'s: do not fold in the backend.
- **Asynchronous work.** Leave `File::StartAsync` alone to get the default executor, or override it for native overlapped I/O (Windows, `dispatch_io`)
  and complete from another thread, never from inside `StartAsync` (except to report a failure). Call `WaitForAsyncRequests` first in the destructor.
- **Conformance.** Run `tests/io/file_conformance.h` (`RunReadConformance`, `RunWriteConformance`) against the new `File`, and the `tests/io`
  file-system tests against the new operations (they use POSIX to plant their fixtures: port the plant helpers in `tests/io/temp_dir.h`).

## Platform notes

- **Windows (Phase 12):** UTF-8 to UTF-16 at the boundary; drive letters, UNC and `\\?\` long paths (Path's `PathStyle::Windows` parses all of them
  already); `CreateFileW`, overlapped I/O (`kCapNativelyAsync`), `MoveFileExW` for atomic replace, known folders through `SHGetKnownFolderPath`.
  A registry-backed settings store is Phase 11/12's, not this layer's.
- **macOS (Phase 13):** case-insensitive default volumes, `dispatch_io` for native asynchronous reads, application sandbox containers for the
  user directories, `NSFileManager` for the locations POSIX does not give.
- **Android and iOS (Phase 21):** app-private directories as the user directories; the asset bundle is a read-only archive opened as a `File` backend
  with a scheme (`RegisterFileScheme`), not a path; no `Writable` capability there.
- **Consoles (Phase 22):** title storage and save-data APIs behind the same `File`; the install directory is read-only; asynchronous requests map to the
  platform's I/O queue.

## The File interface

Not a function with a `Backend:` label because it is a class: a backend subclasses `io::File` and implements `IsOpen`, `Capabilities`, `Size`,
`Read` (short reads allowed, zero is the end), `Write` (all or an error), `Append`, `Flush`, `Sync`, `Truncate` and `Close` (idempotent;
every other call fails with `Invalid` afterwards). The stream position, the helpers and the atomic write work through these.

## Functions

### `Stat`

Windows: GetFileAttributesExW / GetFileInformationByHandle (reparse points followed); macOS: stat.


Report the modification time as nanoseconds since 1970-01-01 UTC (FILETIME counts 100 ns units since 1601). `permissions` is synthesized from the read-only attribute (0444 or 0644, plus the execute bits for directories). `read_only` also considers the ACL for the current user.


### `Lstat`

Windows: the same without following reparse points; macOS: lstat.


A symbolic link is a reparse point with the symlink tag; junctions and other tags are reported as `Other`.


### `Remove`

Windows: DeleteFileW or RemoveDirectoryW (clear the read-only attribute first); macOS: remove.


### `RemoveAll`

Windows: a walk with FindFirstFileExW, never following reparse points; macOS: fts or openat/unlinkat.


Keep the contract: never follow a link (a reparse point is removed as itself), refuse the empty path, a root and fewer than two components, and a tree deeper than 128 levels (`TooLong`). Deleting a directory tree on Windows can fail while a file is open without FILE_SHARE_DELETE: report `Busy`.


### `Rename`

Windows: MoveFileExW(MOVEFILE_REPLACE_EXISTING); macOS: rename.


Replacing an existing file must be atomic where the file system allows it (MoveFileExW does that on NTFS); a directory replaces only an empty directory. Report `CrossDevice` for a different volume (the code is ERROR_NOT_SAME_DEVICE).


### `Copy`

Windows: CopyFile2; macOS: copyfile or a read/write loop.


### `SetPermissions`

Windows: the read-only attribute from the owner write bit; macOS: chmod.


### `SyncDirectory`

Windows: Unsupported (NTFS journals directory changes); macOS: fsync on the directory (F_FULLFSYNC for durability).


Where the platform has no directory sync, return `Unsupported`; `WriteFileAtomic` treats that as success of the replace but reports it.


### `CreateDirectory`

Windows: CreateDirectoryW; macOS: mkdir.


### `CreateDirectories`

Windows: CreateDirectoryW from the root down; macOS: mkdir from the root down.


### `CreatePrivateDirectory`

Windows: an ACL granting only the current user, refusing a reparse point; macOS: mode 0700 and an owner check, refusing a symbolic link.


Parents that are missing are created private as well. Report `SymlinkLoop` when the final component is a link (a reparse point), `PermissionDenied` when it belongs to another user.


### `CreateTempFile`

Windows: CreateFileW(CREATE_NEW) with a random name; macOS: mkstemp.


The file is created exclusively and owner-only; return its path. The unique part is at least 6 random characters.


### `CreateTempDirectory`

Windows: CreateDirectoryW with a random name; macOS: mkdtemp.


### `CurrentDirectory`

Windows: GetCurrentDirectoryW; macOS: getcwd.


### `SetCurrentDirectory`

Windows: SetCurrentDirectoryW; macOS: chdir.


### `Absolute`

Windows: lexical against GetCurrentDirectoryW (the drive-relative form needs the per-drive directory); macOS: lexical against getcwd.


Lexical only: do not resolve links and keep `..`. A path with a root directory but no drive takes the drive of the current directory.


### `Canonical`

Windows: GetFinalPathNameByHandleW with the extended-length prefix removed; macOS: realpath.


The file must exist. Return the final path without the `\\?\\` prefix unless the path is longer than the classic limit.


### `ReadLink`

Windows: DeviceIoControl(FSCTL_GET_REPARSE_POINT); macOS: readlink.


### `TempDirectory`

Windows: GetTempPath2W; macOS: NSTemporaryDirectory or $TMPDIR.


### `ExecutablePath`

Windows: GetModuleFileNameW; macOS: _NSGetExecutablePath resolved with realpath.


### `ExecutableDirectory`

Windows and macOS: the parent of ExecutablePath.


### `UserConfigDirectory`

Windows: FOLDERID_RoamingAppData; macOS: ~/Library/Application Support (or the sandbox container's).


### `UserCacheDirectory`

Windows: FOLDERID_LocalAppData\cache; macOS: ~/Library/Caches.


### `UserDataDirectory`

Windows: FOLDERID_LocalAppData; macOS: ~/Library/Application Support.


### `UserStateDirectory`

Windows: FOLDERID_LocalAppData\state; macOS: ~/Library/Application Support/<app>/state.


### `CrashDumpDirectory`

Windows: %LOCALAPPDATA%/<app>/crashes; macOS: ~/Library/Application Support/<app>/crashes; Android and iOS: the app-private files directory.


Do not guess a location without the user's profile. Create it private to the user (Windows: an ACL for the current user only).


### `Info`

Windows: the attributes the find call already returned, plus GetFileInformationByHandle for the rest; macOS: fstatat with AT_SYMLINK_NOFOLLOW.


### `Open`

Windows: FindFirstFileExW (or NtQueryDirectoryFile) over a handle opened with FILE_FLAG_BACKUP_SEMANTICS; macOS: opendir.

Windows: CreateFileW with FILE_FLAG_OVERLAPPED (so the file is NativelyAsync); macOS: open with O_CLOEXEC and dispatch_io for asynchronous work.


`File::Open` and `Directory::Open` share this section. A file backend implements every pure virtual of `File` (see "The File interface" below) and must pass the conformance suite.


### `Next`

Windows: FindNextFileW, skipping the dot entries, the type from the attributes (a reparse point is a Symlink); macOS: readdir.


### `Close`

Windows: FindClose; macOS: closedir.


### `ErrorFromOsCode`

Windows: GetLastError() codes (ERROR_FILE_NOT_FOUND, ERROR_ACCESS_DENIED, ERROR_ALREADY_EXISTS, ERROR_DISK_FULL, ...); macOS: errno as on Linux.


One function with a test table (`tests/io/file_system_test.cpp` shows the Linux one). Codes with no portable meaning collapse into `Io`.


### `NativePathStyle`

Windows: PathStyle::Windows; macOS, Android, iOS and consoles: PathStyle::Posix.


A constexpr function; the whole layer picks its path rules from it. Windows paths are stored with `/` and converted back by `Path::ToNative`; the extended-length prefix is added there for paths over 259 characters.

