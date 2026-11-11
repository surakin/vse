#pragma once

#include "diagnostics.h"

//! The only place docgen reaches the file system: thin StringView wrappers over the engine's file layer (`io::`), so
//! the rest of the tool keeps its arena-owned path strings. An empty path means the current directory.
namespace docgen
{

// Reads the whole file at `path` into `arena` (which must have enough
// reserved capacity). Returns false on failure.
//
// Ownership: the bytes live in `arena` until it is reset or destroyed.
bool LoadSourceFile(StringView path, ArenaAllocator& arena, StringView& out);

enum class EntryKind
{
    Missing,
    File,
    Directory,
    Other
};

// What `path` is, following symbolic links (`FollowLinks::No`: a link itself is `Other`).
EntryKind KindOf(StringView path, bool follow_links = true);

// Appends the names of every entry of directory `dir` (not `.`/`..`), copied into `arena`, in the file system's order.
// False if `dir` cannot be read.
bool ListNames(StringView dir, ArenaAllocator& arena, Vector<StringView, ArenaAllocator>& names);

// Test hook: with a nonzero seed `ListNames` returns the names in a shuffled order (the file system's own order is
// arbitrary), so a test can prove that a result does not depend on it. 0 (the default) leaves the order alone.
void SetNameOrderSeedForTesting(engine_placeholder::u32 seed);

// mkdir -p; true if the directory exists afterwards.
bool EnsureDirectories(StringView path);

// Replaces `path` with `content` atomically (a temporary file in the same directory, then a rename): a failed write
// keeps the old file. A new file is mode 0644. The mutating functions below refuse an empty path (it would mean the
// current directory).
bool WriteTextAtomic(StringView path, StringView content);

// Writes `content` to `path` (create or truncate), not atomically.
bool WriteText(StringView path, StringView content);

// Removes a file; false if it could not be removed.
bool RemoveFile(StringView path);

// Creates a uniquely named directory `<base>/<prefix>.XXXXXX` and returns its path in `arena`; `base` empty means the
// system's temporary directory.
bool MakeScratchDirectory(StringView base, StringView prefix, ArenaAllocator& arena, StringView& out);

// Removes `path` and everything below it.
bool RemoveTree(StringView path);

// The canonical path of `path` (links resolved) if it exists, otherwise `path` unchanged; copied into `arena`.
StringView CanonicalOrSame(StringView path, ArenaAllocator& arena);

// The current directory, copied into `arena`; false if it cannot be found.
bool WorkingDirectory(ArenaAllocator& arena, StringView& out);

}  // namespace docgen
