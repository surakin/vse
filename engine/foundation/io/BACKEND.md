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

- **Windows (Phase 13):** UTF-8 to UTF-16 at the boundary; drive letters, UNC and `\\?\` long paths (Path's `PathStyle::Windows` parses all of them
  already); `CreateFileW`, overlapped I/O (`kCapNativelyAsync`), `MoveFileExW` for atomic replace, known folders through `SHGetKnownFolderPath`.
  A registry-backed settings store is Phase 11/12's, not this layer's.
- **macOS (Phase 24):** case-insensitive default volumes, `dispatch_io` for native asynchronous reads, application sandbox containers for the
  user directories, `NSFileManager` for the locations POSIX does not give.
- **Android and iOS (Phase 25):** app-private directories as the user directories; the asset bundle is a read-only archive opened as a `File` backend
  with a scheme (`RegisterFileScheme`), not a path; no `Writable` capability there.
- **Consoles (Phase 26):** title storage and save-data APIs behind the same `File`; the install directory is read-only; asynchronous requests map to the
  platform's I/O queue.

## Sharing (every platform)

One rule, which every backend keeps (decided on Phase 13 Day 151, tested by `tests/io/share_test.cpp` on every machine): **a file can be open any
number of times at once, in any modes, by this process and by others, and every open sees the others' writes at once.** Nothing is locked and
nothing is exclusive; a file that is open can be renamed, replaced or removed (the opens keep reading the old file). Linux has no mandatory
locks, so it holds by itself. The Windows backend opens every file with `FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE` to keep it;
a file that another program opened without sharing fails with `Busy` (the code is `ERROR_SHARING_VIOLATION`). A caller that needs exclusive
access arranges it itself (a lock file, `kOpenCreateNew`); the layer offers no lock.

## Windows paths

`engine/foundation/io/windows/win_path.h` turns a `Path` into what a Win32 file function takes (Phase 13 Day 151), in this order:

- A path written with the `\\?\` or `\\.\` prefix passes through as it is, and the name checks below are skipped: the caller asked for the
  literal name or a device. A device path is a UNC root to `Path`, so it reads with the root's separator (`\\.\NUL\`), which Win32 accepts for
  the devices; the volume device `\\.\C:` (without a separator) cannot be named through `Path`.
- Every other path is refused with `Invalid` when a component is a reserved device name (`CON`, `PRN`, `AUX`, `NUL`, `COM0`-`COM9`, `LPT0`-`LPT9`,
  the superscript `COM¹²³`/`LPT¹²³`, `CONIN$`, `CONOUT$`; in any case, with an extension, with spaces before the dot), ends in a dot or a space,
  or holds a colon (an alternate data stream), and when the text is not valid UTF-8. `Path` keeps such names as text (it is lexical); the backend
  refuses them because Win32 would reach a device, strip the dots and spaces (`a.` and `a` would be one file), or open a stream. Windows 11
  no longer turns `dir\CON` into the console, earlier versions do; the rule is the same on all of them. `<>"|?*` and control characters are
  refused by Win32 itself (`ERROR_INVALID_NAME`, `Invalid`).
- A path shorter than 248 characters (the limit of the Win32 functions that create directories; the others stop at 259) is used as it is,
  relative or not.
- A longer one is resolved with `GetFullPathNameW` (the current directory, the drive's own current directory for `C:foo`, `.` and `..`,
  lexically, as Win32 always resolves) and gets the `\\?\` prefix (`\\?\UNC\` for a UNC path), which lifts the limit whatever
  `LongPathsEnabled` says. Resolving first matters: the prefix turns off Win32's own handling of `..` and of relative paths.

## The File interface

Not a function with a `Backend:` label because it is a class: a backend subclasses `io::File` and implements `IsOpen`, `Capabilities`, `Size`,
`Read` (short reads allowed, zero is the end), `Write` (all or an error), `Append`, `Flush`, `Sync`, `Truncate` and `Close` (idempotent;
every other call fails with `Invalid` afterwards). The stream position, the helpers and the atomic write work through these.

## Functions

### `Stat`

Windows: GetFileAttributesExW / GetFileInformationByHandle (reparse points followed); macOS: stat.


Report the modification time as nanoseconds since 1970-01-01 UTC (FILETIME counts 100 ns units since 1601). `permissions` is synthesized from the read-only attribute (0444 or 0644, plus the execute bits for directories). `read_only` also considers the ACL for the current user.

Windows, as built (Day 151): `CreateFileW(FILE_READ_ATTRIBUTES | READ_CONTROL, FILE_FLAG_BACKUP_SEMANTICS)` and `GetFileInformationByHandleEx`
(attribute tag, standard and basic information). A handle that is not a disk file (`\\.\NUL`, a pipe) is `Other`. The read-only attribute
counts for files only: on a directory Windows uses it to mark a customized folder, so a directory is 0755. `read_only` is the attribute or an
`AccessCheck` of the file's ACL against the process token (write data for a file, add a file for a directory); when the ACL itself cannot be
read, the attribute alone decides. A parent that is a file gives `NotADirectory` (Win32 says "path not found": the parents are looked at).


### `Lstat`

Windows: the same without following reparse points; macOS: lstat.


A symbolic link is a reparse point with the symlink tag; junctions and other tags are reported as `Other`.

Windows, as built (Day 151): the open adds `FILE_FLAG_OPEN_REPARSE_POINT`. `Other` is for the name-surrogate tags (junctions, mount points); a
reparse point that is not a name surrogate (a cloud placeholder, a deduplicated file) is the file or directory it stands for, as `Stat` reports it.


### `Remove`

Windows: DeleteFileW or RemoveDirectoryW (clear the read-only attribute first); macOS: remove.

Windows, as built (Day 152): `SetFileInformationByHandle(FileDispositionInfoEx)` with `DELETE | POSIX_SEMANTICS | IGNORE_READONLY_ATTRIBUTE` on a
handle opened with `FILE_FLAG_OPEN_REPARSE_POINT`: the name is gone at once even while the file is open (the sharing rule), a read-only file is
removed as on Linux, a link or junction is removed as itself. A file system without the POSIX information classes (FAT) gets `DeleteFileW` or
`RemoveDirectoryW`, with the read-only attribute cleared first and put back if the removal fails.

### `RemoveAll`

Windows: a walk with FindFirstFileExW, never following reparse points; macOS: fts or openat/unlinkat.


Keep the contract: never follow a link (a reparse point is removed as itself), refuse the empty path, a root and fewer than two components, and a tree deeper than 128 levels (`TooLong`). Deleting a directory tree on Windows can fail while a file is open without FILE_SHARE_DELETE: report `Busy`.

Windows, as built (Day 152): the same refusals as on Linux; the walk uses `FindFirstFileExW` on the `\\?\` form from the start (so a tree deeper than
`MAX_PATH` is no problem), removes every name with the POSIX disposition above (read-only and open files included), never enters a reparse
point (a junction or link is removed as itself; tested with a junction that points outside the tree) and sweeps a directory up to three times
when it is still not empty.

### `Rename`

Windows: MoveFileExW(MOVEFILE_REPLACE_EXISTING); macOS: rename.


Replacing an existing file must be atomic where the file system allows it (MoveFileExW does that on NTFS); a directory replaces only an empty directory. Report `CrossDevice` for a different volume (the code is ERROR_NOT_SAME_DEVICE).

Windows, as built (Day 152): `SetFileInformationByHandle(FileRenameInfoEx)` with `REPLACE_IF_EXISTS | POSIX_SEMANTICS | IGNORE_READONLY_ATTRIBUTE` on a
handle to the source (opened without following a link). Measured on Day 152: `MoveFileExW(MOVEFILE_REPLACE_EXISTING)` refuses a target that is
open, read-only or an empty directory (all `ERROR_ACCESS_DENIED`), which `rename(2)` replaces; the POSIX rename replaces all three, so the sharing
rule (an open file can be replaced) holds and `WriteFileAtomic` is atomic. Without the POSIX information classes (FAT) it falls back to
`MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`. `ReplaceFileW` is not used: it is a sequence of steps, not one rename. A file
onto a directory is `IsADirectory`, a directory onto a non-empty one `NotEmpty`, onto a file `NotADirectory`, another volume `CrossDevice`.

### `Copy`

Windows: CopyFile2; macOS: copyfile or a read/write loop.

Windows, as built (Day 152): `CopyFile2` (`COPY_FILE_FAIL_IF_EXISTS` unless overwriting), which copies the read-only attribute as the mode is copied
on Linux. The same file under two names (a hard link) is detected by volume serial and 128-bit file id first and refused with `Invalid`.

### `SetPermissions`

Windows: the read-only attribute from the owner write bit; macOS: chmod.

Windows, as built (Day 152): only the owner write bit (0200) has a counterpart, the read-only attribute of a file; on a directory the call succeeds
and changes nothing (Windows uses that attribute for customized folders). `Stat` then reports 0644 or 0444; the other bits are not kept. Access
for other users is the ACL's business (`CreatePrivateDirectory`).

### `SyncDirectory`

Windows: Unsupported (NTFS journals directory changes); macOS: fsync on the directory (F_FULLFSYNC for durability).


Where the platform has no directory sync, return `Unsupported`; `WriteFileAtomic` treats that as success of the replace but reports it.

Windows, as built (Day 152, measured): `FlushFileBuffers` on a directory handle succeeds when the handle is opened for writing (`GENERIC_WRITE`,
`FILE_FLAG_BACKUP_SEMANTICS`) and is refused with read access only, so the directory is opened that way; `WriteFileAtomic` gets the same
durability step as on Linux instead of `Unsupported`.

### `CreateDirectory`

Windows: CreateDirectoryW; macOS: mkdir.


### `CreateDirectories`

Windows: CreateDirectoryW from the root down; macOS: mkdir from the root down.


### `CreatePrivateDirectory`

Windows: an ACL granting only the current user, refusing a reparse point; macOS: mode 0700 and an owner check, refusing a symbolic link.


Parents that are missing are created private as well. Report `SymlinkLoop` when the final component is a link (a reparse point), `PermissionDenied` when it belongs to another user.

Windows, as built (Day 152): created with a protected DACL that grants the current user everything (inherited by what is created inside; Windows
stores it as an effective and an inherit-only entry) and nobody else anything; missing parents get it too. An existing directory is opened
without following a reparse point (one is `SymlinkLoop`), must be owned by the current user (or by the Administrators group when the process is
elevated, the token's default owner) and gets the same DACL, as `fchmod(0700)` on Linux. Not done: the Linux check that no ancestor is
writable by others (on Windows, renaming the directory away needs `DELETE` on it, which the private ACL does not grant, or
`FILE_DELETE_CHILD` on the parent, which the parent's ACL decides).

### `CreateTempFile`

Windows: CreateFileW(CREATE_NEW) with a random name; macOS: mkstemp.


The file is created exclusively and owner-only; return its path. The unique part is at least 6 random characters.

Windows, as built (Day 152): `<prefix>.` and 8 characters from `BCryptGenRandom`, created with `CREATE_NEW` (or `CreateDirectoryW`), up to 100
attempts. The file is not given an owner-only ACL of its own: it inherits the directory's (the user's temporary directory and folders are
private already), because a file keeps its ACL when renamed, and `WriteFileAtomic` renames its temporary over the target, which would otherwise
change the target's ACL.

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

Windows, as built (Day 152): `GetFinalPathNameByHandleW(FILE_NAME_NORMALIZED | VOLUME_NAME_DOS)` on a handle that follows links; the `\\?\` (or
`\\?\UNC\`) prefix is always removed: a `Path` never carries it, and the backend adds it again where it is needed.

### `ReadLink`

Windows: DeviceIoControl(FSCTL_GET_REPARSE_POINT); macOS: readlink.

Windows, as built (Day 152): the print name of a symbolic link (relative as written) or of a junction (absolute), or the substitute name without its
`\??\` when there is no print name. Any other reparse point, and a name that is none, is `Invalid` (`ERROR_NOT_A_REPARSE_POINT`).

### `TempDirectory`

Windows: GetTempPath2W; macOS: NSTemporaryDirectory or $TMPDIR.


### `ExecutablePath`

Windows: GetModuleFileNameW; macOS: _NSGetExecutablePath resolved with realpath.


### `ExecutableDirectory`

Windows and macOS: the parent of ExecutablePath.


### `UserConfigDirectory`

Windows: `%APPDATA%\<app>` when the variable holds an absolute path, else FOLDERID_RoamingAppData; macOS: ~/Library/Application Support (or the sandbox container's).


### `UserCacheDirectory`

Windows: `%LOCALAPPDATA%` (when absolute) or FOLDERID_LocalAppData, then `<app>\cache`; macOS: ~/Library/Caches.


### `UserDataDirectory`

Windows: `%LOCALAPPDATA%` (when absolute) or FOLDERID_LocalAppData, then `<app>`; macOS: ~/Library/Application Support.


### `UserStateDirectory`

Windows: `%LOCALAPPDATA%` (when absolute) or FOLDERID_LocalAppData, then `<app>\state`; macOS: ~/Library/Application Support/<app>/state.

Windows, as built (Day 152): `SHGetKnownFolderPath`; configuration `RoamingAppData\<app>`, data `LocalAppData\<app>`, cache `LocalAppData\<app>\cache`, state
`LocalAppData\<app>\state` (grouped per application, the Windows convention). Nothing is created. An application name that Windows cannot keep
(a device name, a colon, a trailing dot or space, `\`) is `Invalid` besides the POSIX rules. From Day 161 `%APPDATA%` and `%LOCALAPPDATA%` come first when they
hold an absolute path (the session sets them to the same folders), so a test or a launcher can point them elsewhere, as `$XDG_*` on Linux; the crash dump
directory follows `%LOCALAPPDATA%` too.

### `CrashDumpDirectory`

Windows: %LOCALAPPDATA%/<app>/crashes; macOS: ~/Library/Application Support/<app>/crashes; Android and iOS: the app-private files directory.


Do not guess a location without the user's profile. Create it private to the user (Windows: an ACL for the current user only).

Windows, as built (Day 152): `LocalAppData\<app>\crashes`, `<app>` from `ENGINE_APP_NAME` or the executable's name without `.exe`, created with
`CreatePrivateDirectory`.

### `Info`

Windows: the attributes the find call already returned, plus GetFileInformationByHandle for the rest; macOS: fstatat with AT_SYMLINK_NOFOLLOW.


### `Open`

Windows: FindFirstFileExW (or NtQueryDirectoryFile) over a handle opened with FILE_FLAG_BACKUP_SEMANTICS; macOS: opendir.

Windows: CreateFileW with FILE_FLAG_OVERLAPPED (so the file is NativelyAsync); macOS: open with O_CLOEXEC and dispatch_io for asynchronous work.


`File::Open` and `Directory::Open` share this section. A file backend implements every pure virtual of `File` (see "The File interface" below) and must pass the conformance suite.


`File::Open` on Windows, as built (Day 151): `CreateFileW` with the sharing rule above and the disposition from the mode and flags (`OPEN_ALWAYS` when the mode
or `kOpenCreate` creates, `CREATE_NEW` for `kOpenCreateNew`, else `OPEN_EXISTING`); truncation (`Write`, `kOpenTruncate`) is a separate
end-of-file of 0 on the open handle, as `O_TRUNC` keeps the file: `CREATE_ALWAYS` and `TRUNCATE_EXISTING` refuse a hidden or system file and
`CREATE_ALWAYS` replaces its attributes. The handle is
synchronous, and asynchronous requests run on the executor exactly as on Linux (decided on Day 152: one design on both platforms; `LocalFile`
does not claim `kCapNativelyAsync`, and overlapped I/O with a completion port is left for a measured need). Reads and writes at an offset pass
it in an `OVERLAPPED`, so concurrent offset reads share no file pointer. An append
writes at the offset `0xFFFFFFFF'FFFFFFFF`, which is Win32's atomic append. `Flush` has nothing to flush (no buffer in the process); `Sync` is
`FlushFileBuffers`. `Truncate` that grows a file first marks it sparse (`FSCTL_SET_SPARSE`), so the new part is a hole as on Linux and not
gigabytes of zeros written by the next write past the old end; a file system without sparse files grows it plainly. A directory opened as a
file is `IsADirectory` (`AlreadyExists` with `kOpenCreateNew`, as `O_EXCL` says).


### `Next`

Windows: FindNextFileW, skipping the dot entries, the type from the attributes (a reparse point is a Symlink); macOS: readdir.

Windows, as built (Day 152): `io/windows/directory.cpp`. The type follows `Lstat`: the symlink tag is `Symlink`, a junction or another name surrogate
`Other` (so a walk never enters it), any other reparse point the file or directory it stands for. An empty root directory with no entries at all
(no `.` either) is an empty listing. The listing, sorting, wildcards and walking are the neutral `io/directory.cpp` on every platform; a backend
provides only `Open`, `Next`, `Close`, `DirectoryEntry::Info` and `detail::GetIdentity` (`io/detail/directory_identity.h`; Windows: volume
serial and the 128-bit file id).

### `Close`

Windows: FindClose; macOS: closedir.


### `ErrorFromOsCode`

Windows: GetLastError() codes (ERROR_FILE_NOT_FOUND, ERROR_ACCESS_DENIED, ERROR_ALREADY_EXISTS, ERROR_DISK_FULL, ...); macOS: errno as on Linux.


One function with a test table (`tests/io/file_system_test.cpp` shows the Linux one). Codes with no portable meaning collapse into `Io`.

Windows, as built (Day 151): `io/windows/error.cpp`, tested by `tests/io/io_windows_test.cpp`. Not found: `ERROR_FILE_NOT_FOUND`,
`ERROR_PATH_NOT_FOUND`, `ERROR_INVALID_DRIVE`, `ERROR_BAD_NETPATH`, `ERROR_BAD_NET_NAME`. Busy: `ERROR_SHARING_VIOLATION`, `ERROR_LOCK_VIOLATION`,
`ERROR_DELETE_PENDING`, `ERROR_USER_MAPPED_FILE`. `ERROR_DIRECTORY` is `NotADirectory`, `ERROR_CANT_RESOLVE_FILENAME` (a link loop) `SymlinkLoop`,
`ERROR_HANDLE_EOF` `EndOfFile`; `ERROR_NOT_READY`, `ERROR_BROKEN_PIPE`, `ERROR_IO_PENDING` and the device errors are `Io`. A message is the
engine's text and the number (`Status::os_code`); `FormatMessageW` is not called.


### `NativePathStyle`

Windows: PathStyle::Windows; macOS, Android, iOS and consoles: PathStyle::Posix.


A constexpr function; the whole layer picks its path rules from it. Windows paths are stored with `/` and converted back by `Path::ToNative`. The extended-length prefix is not `Path`'s: the Windows backend adds it at the Win32 boundary ("Windows paths" above).

