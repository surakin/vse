#pragma once

#include "containers/vector.h"
#include "io/error.h"
#include "io/file_system.h"
#include "io/path.h"
#include "string/string.h"
#include "types.h"

//! Listing and walking directories without loading a whole tree: [[io::Directory]], [[io::ListDirectory]],
//! [[io::WalkDirectory]] and [[io::MatchesWildcard]].
namespace engine_placeholder::io {

class Directory;

/// One entry returned by [[io::Directory::Next]].
///
/// - Thread-safety: thread-compatible. Ownership: `name` and the entry itself are valid until the next call to `Next` on the same
/// [[io::Directory]] and while that directory is open (moving the `Directory` does not invalidate them).
struct DirectoryEntry {
    StringView name;                 ///< The entry's name (one component, never `.` or `..`).
    FileType type = FileType::None;  ///< What the entry is without following links (a link is `Symlink`); never `None` for a returned entry.

    /// Describes the entry now (`lstat`: a link is described, not its target); lazy because most callers only need `name` and `type`.
    ///
    /// - Fails: `NotFound` (removed since it was listed), `PermissionDenied`.
    /// - Backend: Windows: the attributes the find call already returned, plus GetFileInformationByHandle for the rest; macOS: fstatat with AT_SYMLINK_NOFOLLOW.
    Status Info(FileInfo* out) const;

private:
    friend class Directory;
    void* owner = nullptr; // the open directory's handle (it moves with a moved Directory)
};

/// An open directory that yields its entries one at a time, so a directory of a million files costs one entry of memory.
///
/// The order of entries is unspecified (it is the file system's). Use [[io::ListDirectory]] with `sorted` when you need a stable order,
/// for example in tools whose output must be reproducible. Entries created or removed while iterating may or may not be seen, and an
/// entry is never returned twice; nothing crashes.
///
/// - Thread-safety: thread-compatible; one thread iterates one directory. Ownership: owns the open directory; closes it on destruction. Move-only.
class Directory {
public:
    Directory() = default;
    Directory(const Directory&) = delete;
    Directory& operator=(const Directory&) = delete;
    /// Takes over the open directory of `other`, which becomes closed.
    Directory(Directory&& other) noexcept;
    /// Closes this directory and takes over `other`'s.
    Directory& operator=(Directory&& other) noexcept;
    /// Closes the directory.
    ~Directory();

    /// Opens `path` for listing.
    ///
    /// - Param out: receives the open directory (closing a previous one); unchanged on failure.
    /// - Fails: `NotFound`, `NotADirectory`, `PermissionDenied`, `TooManyOpenFiles`, `TooLong`.
    /// - Backend: Windows: FindFirstFileExW(FindExInfoBasic, FIND_FIRST_EX_LARGE_FETCH) on `<path>\*`; macOS: opendir.
    static Status Open(const Path& path, Directory* out);

    /// Returns true while the directory is open.
    bool IsOpen() const { return handle_ != nullptr; }
    /// Returns the path it was opened with.
    const Path& GetPath() const { return path_; }
    /// Reads the next entry, skipping `.` and `..`.
    ///
    /// - Param entry: receives the entry when `*found` is true.
    /// - Param found: set to false at the end of the directory.
    /// - Fails: `Invalid` (closed), `Io` (a read error; the position is then undefined).
    /// - Backend: Windows: FindNextFileW, skipping the dot entries, the type from the attributes and reparse tag (as Lstat); macOS: readdir.
    Status Next(DirectoryEntry* entry, bool* found);
    /// Closes the directory; idempotent.
    /// - Backend: Windows: FindClose; macOS: closedir.
    void Close();

    /// Creates a directory ([[io::CreateDirectory]]).
    static Status Create(const Path& path) { return CreateDirectory(path); }
    /// Creates a directory and its parents ([[io::CreateDirectories]]).
    static Status CreateAll(const Path& path) { return CreateDirectories(path); }
    /// Removes an empty directory ([[io::Remove]]).
    static Status Remove(const Path& path) { return io::Remove(path); }
    /// Removes a directory and everything in it ([[io::RemoveAll]]).
    static Status RemoveAll(const Path& path) { return io::RemoveAll(path); }
    /// Reports whether the directory has no entries.
    ///
    /// - Fails: as [[io::Directory::Open]].
    static Status IsEmpty(const Path& path, bool* empty);

private:
    friend struct DirectoryEntry;
    void* handle_ = nullptr; // the platform's directory object
    Path path_;
};

/// One name in a [[io::ListDirectory]] result.
struct DirectoryItem {
    Path name;                      ///< The entry's name as a single-component path.
    FileType type = FileType::None; ///< As [[io::DirectoryEntry::type]].
};

/// Reads all entries of one directory. With `sorted` they are ordered bytewise by name, which makes the result the same on every
/// file system and run.
///
/// - Param out: receives the items (replacing its contents); unchanged on failure.
/// - Fails: as [[io::Directory::Open]] and [[io::Directory::Next]]; `OutOfMemory`.
Status ListDirectory(const Path& path, bool sorted, Vector<DirectoryItem>* out);

/// Returns true when `name` matches `pattern`: `*` matches any run of characters (including none), `?` exactly one character (a whole
/// UTF-8 sequence), `\` makes the next pattern character literal, everything else matches itself exactly (case-sensitive). There is
/// no special treatment of a leading dot or of `/`.
bool MatchesWildcard(StringView pattern, StringView name);

/// What a walk visitor wants next.
enum class WalkAction : u8 {
    Continue,    ///< Go on (into a directory's children, for a directory entry in pre-order).
    SkipSubtree, ///< Do not descend into this directory (ignored for non-directories and in post-order).
    Stop,        ///< End the walk; [[io::WalkDirectory]] returns success.
};

/// What a walk reports for each entry.
struct WalkEntry {
    const Path& path;                ///< Root joined with the relative path; valid during the call (copy it to keep it).
    StringView name;                 ///< The last component of `path` (valid during the call).
    FileType type = FileType::None;  ///< As [[io::DirectoryEntry::type]] (a link is `Symlink`).
    u32 depth = 0;                   ///< 1 for the root's children.
    bool after_children = false;     ///< True when a directory that was entered is reported after its children (post-order walks); files are reported once, false.
    Status error;                    ///< Why the entry could not be entered (`type` is what is known); success normally. Each entry is reported once, with the error if it has one. A read error in the middle of a directory is reported as one extra entry: that directory's own path, at its own depth (0 for the root).
};

/// Options of [[io::WalkDirectory]].
struct WalkOptions {
    u32 max_depth = 32;              ///< Deepest level visited (1 = the root's children only); from 1 to 128.
    bool follow_symlinks = false;    ///< Descend into symbolic links to directories; a link back to an ancestor is reported with `SymlinkLoop`.
    bool post_order = false;         ///< Visit a directory after its children instead of before (each directory is visited once either way).
    bool sorted = false;             ///< Visit each directory's entries in bytewise name order (costs one directory of memory per level).
    StringView name_pattern;         ///< Only report entries whose name matches ([[io::MatchesWildcard]]); directories are still entered. Empty: all.
};

/// Called for each entry. Return what the walk should do next.
using WalkVisitor = WalkAction (*)(void* user, const WalkEntry& entry);

/// Walks the tree below `root` depth first. The root itself is not reported. An entry that cannot be examined or entered (an unreadable
/// subdirectory, a loop) is reported to the visitor with `error` set, and the walk continues unless the visitor returns `Stop`.
///
/// - Param root: the directory to walk.
/// - Param options: depth, links, order, filter.
/// - Param visitor: called per entry; must not be null.
/// - Param user: passed to the visitor.
/// - Fails: the errors of opening `root` (`NotFound`, `NotADirectory`, `PermissionDenied`); `Invalid` for `max_depth` of 0 or over 128, or a null visitor.
Status WalkDirectory(const Path& root, const WalkOptions& options, WalkVisitor visitor, void* user);

} // namespace engine_placeholder::io
