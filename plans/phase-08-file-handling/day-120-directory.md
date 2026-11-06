# Phase 8 / Day 120 — `io::Directory`

**Phase:** [Phase 8 — File Handling](../phase-08-file-handling.md)
**Depends on:** Days 117-118.

Listing and walking directories, built so a big tree is never loaded whole. New files `io/directory.h`, `io/linux/directory.cpp`.

## Tasks

- [x] `io::Directory` (move-only, owns the open directory): `Directory::Open(path, out)`, `Next(DirectoryEntry*)` returning `Status` (`None` at the end), `DirectoryEntry` (name as a `StringView` valid until the next call, type, and `Info()` that stats lazily), `Close`;
      `.` and `..` never returned; the order unspecified and documented, with `ListSorted` helpers for callers that need stability (the minters: same input, same output).
- [x] `WalkDirectory(root, options, visitor)`: recursive, depth limit, `FollowSymlinks` off by default (with loop protection when on), pre- or post-order, a visitor that can skip a subtree or stop; an error on one entry is reported to the visitor and the walk goes on unless it says otherwise.
- [x] A name filter without regular expressions: `MatchesWildcard(pattern, name)` (`*`, `?`, a documented escape), used by `WalkDirectory` options and by callers; tests over the pattern cases.
- [x] Mutation: `Directory::Create`/`Remove` as the thin statics over Day 117's functions (so the class is the one place a caller looks), and `IsEmpty`.
- [x] Tests (phase 8): empty, large (10000 entries), nested directories, symlinks to files, to directories and loops, unreadable subdirectories, entries removed or added during iteration (no crash, documented behavior), names with spaces, newlines and Unicode, the sorted helper, the wildcard table, the walk stopping and skipping; ASan and TSan clean.
- [x] **Documentation:** labels; the guide gets "Directories and walking a tree" with a tagged sample; reference regenerated; `docgen lint` zero; `ctest -L docs`.
- [x] Run `scripts/test-phase.sh --affected` and the asan flavor; quote the lines.
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high`; record it.
- [ ] Update `STATUS.md`; commit.

**Done:** directories are listed and walked through `io::Directory` and `WalkDirectory` with the stated order, error and symlink behavior tested, and the new API is complete and documented.

## Result

- `io/directory.h` and `io/linux/directory.cpp`: `Directory` (move-only streaming listing; `.`/`..` skipped; `DT_UNKNOWN` resolved; lazy `Info`), `ListDirectory` (optionally sorted bytewise by a hand-written merge sort, OOM reported not aborted), `MatchesWildcard` (`*`, `?` per UTF-8 sequence, `\` escape, linear-time backtracking), `WalkDirectory` (depth limit 1-128, links off by default with ancestor-identity loop detection when on, pre/post order, sorted option, name filter, `Continue`/`SkipSubtree`/`Stop`, errors reported once per entry to the visitor), and the thin statics `Create`/`CreateAll`/`Remove`/`RemoveAll`/`IsEmpty`.
- Tests: `tests/io/directory_test.cpp` (+9 cases; the io executables run 52 cases / 58614 assertions in plain, ASan and TSan): empty, 10000 entries, odd names (space, newline, Unicode, quotes, tab), sorted order, entries added and removed during iteration, a 40-row wildcard table plus a pathological pattern, walk order/post-order/depth/filter/skip/stop, symlinks to files and directories, loops, an unreadable subdirectory.
- Docs: guide section "Directories and walking a tree" with a run sample; baseline 3393/3393; `docs/samples-prelude.h` includes `io/directory.h`.
- Scoped runs: `ran 1089 of 2894 tests (phases: all; flavors: plain; display: included; crash tests: excluded)` (only the known X11 window test failed), `ran 806 of 2894 ... flavors: asan`, `ran 526 of 2894 ... flavors: tsan`, all pass; `ctest -L docs` 4/4.

## Post-implementation code review

`/code-review high`: 9 findings, 7 fixed, 2 declined.

Fixed:
1. An entry whose directory could not be opened was reported twice (and three times in post-order): a walk now settles whether an entry can be entered, and opens it, before reporting, so every entry is reported once, with its error (tests count the reports).
2. Failures listing or opening a nested directory were silently skipped: they are now the entry's `error`; for the root they are the return value.
3. An uninitialized directory identity could be pushed on the loop-detection stack: the identity is taken once, before the open, and its failure is the entry's error (with the real status, not `PermissionDenied`); the repeated `stat`/`open` calls per directory dropped from about five to two.
4. `ListDirectory` aborted on allocation failure in the sorting path: all growth is now checked and returns `OutOfMemory`.
5. A read error in the middle of a directory re-reported the directory with the parent's depth: documented as the one extra report (the directory's own path and depth, 0 for the root) and kept.
6. `DirectoryEntry::Info` held a pointer to the `Directory` object, dangling after a move: the entry now holds the open handle, which moves with the directory (test).
7. `WalkEntry::path` copied a path per report: it is a reference now.

Declined: "`DirectoryItem` costs 4 KB per entry" and "each walk frame holds 4 KB paths": `Path` stores its text in a heap `Vector`, so an item is a few dozen bytes plus the name; the 4 KB buffers are transient locals inside `Path` operations, not part of any stored object or frame (a 10000-entry listing in the test is small). The walk recursion depth is capped at 128. Bookkeeping: this commit.
