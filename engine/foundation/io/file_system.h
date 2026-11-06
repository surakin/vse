#pragma once

#include "io/error.h"
#include "io/path.h"
#include "string/string.h"
#include "types.h"

//! Operations on paths: looking at the disk, creating, removing and renaming, resolving, and the special folders.
//!
//! Every function returns a [[io::Status]] (or a bool for pure questions) and never throws or aborts on a bad path; the
//! lexical part of a path stays in [[io::Path]]. Functions follow symbolic links unless their name or comment says
//! otherwise.
namespace engine_placeholder::io {

/// What a name refers to.
enum class FileType : u8 {
    None,      ///< Nothing is there (only from [[io::Exists]]-style answers; [[io::Stat]] fails instead).
    Regular,   ///< An ordinary file.
    Directory, ///< A directory.
    Symlink,   ///< A symbolic link (only from [[io::Lstat]]).
    Other,     ///< A device, socket, pipe or anything else.
};

/// A snapshot of what is on disk for one name.
///
/// Thread-safety: immutable value.
struct FileInfo {
    FileType type = FileType::None; ///< What the name refers to.
    u64 size = 0;                   ///< Size in bytes (for a regular file); other types: unspecified.
    i64 modified_unix_ns = 0;       ///< Last modification, nanoseconds since 1970-01-01 UTC (may be coarser than a nanosecond).
    u32 permissions = 0;            ///< POSIX permission bits (`0644`); platforms without them synthesize owner bits from the read-only flag.
    bool read_only = false;         ///< True when this process may not write it (checked against the effective user).
};

/// Describes `path`, following symbolic links.
///
/// Param path: the name to look at.
/// Param out: receives the description; unchanged on failure.
/// Fails: `NotFound`, `PermissionDenied`, `NotADirectory`, `SymlinkLoop`, `TooLong`.
Status Stat(const Path& path, FileInfo* out);
/// Like [[io::Stat]] but describes a final symbolic link itself.
///
/// Fails: as [[io::Stat]].
Status Lstat(const Path& path, FileInfo* out);
/// Returns true when `path` names something (following links; a dangling link does not exist).
bool Exists(const Path& path);
/// Returns true when `path` names a regular file (following links).
bool IsFile(const Path& path);
/// Returns true when `path` names a directory (following links).
bool IsDirectory(const Path& path);

/// Removes a file, a symbolic link (not its target) or an empty directory.
///
/// Fails: `NotFound`, `PermissionDenied`, `NotEmpty`, `ReadOnly`, `Busy`.
Status Remove(const Path& path);
/// The shortest absolute path [[io::RemoveAll]] accepts, in components (`/home/me` has two; `/home` has one).
constexpr usize kRemoveAllMinComponents = 2;
/// Removes `path` and everything below it. Symbolic links inside the tree are removed, never followed; the walk holds
/// directory descriptors, so a link swapped in during the walk cannot redirect it. A tree deeper than 128 levels is refused.
///
/// Param path: a file, link or directory. A relative path is made absolute against the current directory first.
/// Fails: `Invalid` (the empty path, a root, or fewer than [[io::kRemoveAllMinComponents]] components after making it absolute
///   and normalizing), `NotFound`, `PermissionDenied`, `TooLong` (too deep). Entries removed before a failure stay removed;
///   the first error is the one returned.
Status RemoveAll(const Path& path);
/// Renames `from` to `to`. An existing regular file at `to` is replaced atomically; a directory replaces only an empty
/// directory. Both must be on the same file system.
///
/// Fails: `NotFound`, `CrossDevice`, `NotEmpty`, `IsADirectory`, `NotADirectory`, `PermissionDenied`, `Busy`.
Status Rename(const Path& from, const Path& to);
/// Copies the bytes of the regular file `from` to `to`, keeping the permission bits (not the times or owner). The copy is
/// not atomic: a failure can leave a partial `to`.
///
/// Param overwrite: when false an existing `to` is an error; when true it is truncated and rewritten.
/// Fails: `NotFound`, `AlreadyExists`, `IsADirectory`, `NoSpace`, `PermissionDenied`.
Status Copy(const Path& from, const Path& to, bool overwrite);

/// Sets the permission bits of `path` (following links); platforms without them map the owner-write bit to the read-only attribute.
///
/// Fails: `NotFound`, `PermissionDenied`, `ReadOnly`.
Status SetPermissions(const Path& path, u32 permissions);
/// Makes the entries of directory `path` durable (`fsync` of the directory): call it after creating, renaming or removing a file
/// when the change must survive a crash.
///
/// Fails: `NotFound`, `NotADirectory`, `PermissionDenied`, `Io`; `Unsupported` on a platform that has no such operation.
Status SyncDirectory(const Path& path);

/// Creates one directory (permissions 0777 minus the umask).
///
/// Fails: `AlreadyExists` (also when a file is there), `NotFound` (the parent is missing), `PermissionDenied`, `ReadOnly`.
Status CreateDirectory(const Path& path);
/// Creates a directory and any missing parents; an existing directory is success.
///
/// Fails: `NotADirectory` when a component exists and is not a directory, plus the errors of [[io::CreateDirectory]].
Status CreateDirectories(const Path& path);
/// Creates (or accepts) a directory that only this user can use: mode 0700, owned by the effective user, not a symbolic link,
/// writable. Missing parents are created too, with mode 0700 as well (existing parents are left alone and are not checked). An existing directory with group or other access is tightened.
///
/// Fails: `PermissionDenied` (another user's, or not writable), `SymlinkLoop` (the final component is a link), `NotADirectory`.
Status CreatePrivateDirectory(const Path& path);
/// Creates an empty file with a unique name `<prefix>.<random>` in `directory` (mode 0600, exclusive creation) and returns its path.
///
/// Param directory: must exist.
/// Param prefix: a single name component, for example `cache.tmp`.
/// Param out: receives the path of the new file.
/// Fails: `NotFound`, `PermissionDenied`, `Invalid` (the prefix has a separator), `TooLong`.
Status CreateTempFile(const Path& directory, StringView prefix, Path* out);
/// Creates a directory with a unique name `<prefix>.<random>` in `directory` (mode 0700) and returns its path.
///
/// Fails: as [[io::CreateTempFile]].
Status CreateTempDirectory(const Path& directory, StringView prefix, Path* out);

/// Returns the process working directory.
///
/// Fails: `NotFound` (the directory was removed), `TooLong`.
Status CurrentDirectory(Path* out);
/// Changes the process working directory. Process-global: every thread sees it, so libraries should not call it; prefer
/// absolute paths.
///
/// Fails: `NotFound`, `NotADirectory`, `PermissionDenied`.
Status SetCurrentDirectory(const Path& path);
/// Returns `path` made absolute against the current directory, lexically (no links resolved, `..` kept; see
/// [[io::Path::Normalize]]). An absolute path is returned as it is.
///
/// Fails: as [[io::CurrentDirectory]]; `TooLong` when the result is longer than [[io::kMaxPathBytes]].
Status Absolute(const Path& path, Path* out);
/// Returns the absolute path with every symbolic link resolved and `.`/`..` removed. The file must exist.
///
/// Fails: `NotFound`, `SymlinkLoop`, `PermissionDenied`, `TooLong`.
Status Canonical(const Path& path, Path* out);
/// Returns the target of the symbolic link `path`, as written in the link.
///
/// Fails: `NotFound`, `Invalid` (not a link), `PermissionDenied`.
Status ReadLink(const Path& path, Path* out);

/// The directory for temporary files: `$TMPDIR` when absolute, else `/tmp` on Linux. Creates nothing.
Status TempDirectory(Path* out);
/// Returns the path of the running executable.
///
/// Fails: `NotFound`, `Unsupported`.
Status ExecutablePath(Path* out);
/// Returns the directory containing the running executable.
Status ExecutableDirectory(Path* out);
/// The per-user configuration directory for an application: `$XDG_CONFIG_HOME/<app>` (only when absolute; a relative value is
/// ignored), else `<home>/.config/<app>` on Linux. Creates nothing.
///
/// Param app_name: one path component (no separators, not `.` or `..`).
/// Fails: `Invalid` (a bad app name), `NotFound` (no home directory is known).
Status UserConfigDirectory(StringView app_name, Path* out);
/// The per-user cache directory: `$XDG_CACHE_HOME/<app>` or `<home>/.cache/<app>` on Linux. Creates nothing.
///
/// Fails: as [[io::UserConfigDirectory]].
Status UserCacheDirectory(StringView app_name, Path* out);
/// The per-user data directory: `$XDG_DATA_HOME/<app>` or `<home>/.local/share/<app>` on Linux. Creates nothing.
///
/// Fails: as [[io::UserConfigDirectory]].
Status UserDataDirectory(StringView app_name, Path* out);
/// The per-user state directory: `$XDG_STATE_HOME/<app>` or `<home>/.local/state/<app>` on Linux. Creates nothing.
///
/// Fails: as [[io::UserConfigDirectory]].
Status UserStateDirectory(StringView app_name, Path* out);

/// The directory where this program writes crash dumps, created (and made private to this user) if needed: `<user state directory>/<app>/crashes`
/// (on Linux `$XDG_STATE_HOME` or `~/.local/state`). `<app>` is the environment variable `ENGINE_APP_NAME` or, without it, the executable's
/// file name. Never the working directory: a sandboxed or read-only working directory is not a place to put a dump.
///
/// Param out: receives the directory.
/// Fails: `NotFound` (neither `$XDG_STATE_HOME` nor `$HOME` is set to an absolute path, or the executable cannot be found), `Invalid` (an unusable application name: `.`, `..`,
///   empty after trimming, containing a slash or longer than 200 bytes), the errors of [[io::CreatePrivateDirectory]].
/// Thread-safety: thread-safe, except that it reads the environment, so another thread must not change it (`setenv`) at the same time.
/// Backend: Windows: %LOCALAPPDATA%/<app>/crashes; macOS: ~/Library/Application Support/<app>/crashes; Android and iOS: the app-private files directory.
Status CrashDumpDirectory(Path* out);

} // namespace engine_placeholder::io
