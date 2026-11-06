# Phase 8 / Day 120 — `io::Directory`

**Phase:** [Phase 8 — File Handling](../phase-08-file-handling.md)
**Depends on:** Days 117-118.

Listing and walking directories, built so a big tree is never loaded whole. New files `io/directory.h`, `io/linux/directory.cpp`.

## Tasks

- [ ] `io::Directory` (move-only, owns the open directory): `Directory::Open(path, out)`, `Next(DirectoryEntry*)` returning `Status` (`None` at the end), `DirectoryEntry` (name as a `StringView` valid until the next call, type, and `Info()` that stats lazily), `Close`;
      `.` and `..` never returned; the order unspecified and documented, with `ListSorted` helpers for callers that need stability (the minters: same input, same output).
- [ ] `WalkDirectory(root, options, visitor)`: recursive, depth limit, `FollowSymlinks` off by default (with loop protection when on), pre- or post-order, a visitor that can skip a subtree or stop; an error on one entry is reported to the visitor and the walk goes on unless it says otherwise.
- [ ] A name filter without regular expressions: `MatchesWildcard(pattern, name)` (`*`, `?`, a documented escape), used by `WalkDirectory` options and by callers; tests over the pattern cases.
- [ ] Mutation: `Directory::Create`/`Remove` as the thin statics over Day 117's functions (so the class is the one place a caller looks), and `IsEmpty`.
- [ ] Tests (phase 8): empty, large (10000 entries), nested directories, symlinks to files, to directories and loops, unreadable subdirectories, entries removed or added during iteration (no crash, documented behavior), names with spaces, newlines and Unicode, the sorted helper, the wildcard table, the walk stopping and skipping; ASan and TSan clean.
- [ ] **Documentation:** labels; the guide gets "Directories and walking a tree" with a tagged sample; reference regenerated; `docgen lint` zero; `ctest -L docs`.
- [ ] Run `scripts/test-phase.sh --affected` and the asan flavor; quote the lines.
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high`; record it.
- [ ] Update `STATUS.md`; commit.

**Done:** directories are listed and walked through `io::Directory` and `WalkDirectory` with the stated order, error and symlink behavior tested, and the new API is complete and documented.
