# Phase 9 / Day 116 — `io::Path`

**Phase:** [Phase 9 — File Handling](../phase-09-file-handling.md)
**Depends on:** Day 115 (spec, the Spike C table).

A pure value type: no syscalls, no allocation beyond its string. New files `engine/foundation/io/path.h/.cpp`, tests in `tests/foundation`.

## Tasks

- [x] `PathStyle` (`Posix`, `Windows`; `Native()` for the build) and `io::Path` (allocator story from the spec): construct from a `StringView` of UTF-8, store in the canonical form (`/`), keep the
      root name (`C:`, `//server/share`), root directory flag and the relative part; `kMaxPathBytes` limit enforced with a documented failure (not a truncation).
- [x] Queries: `IsEmpty`, `IsAbsolute`, `IsRelative`, `HasRoot`, `RootName`, `Parent` (of `a/b` is `a`, of `a` is empty, of `/` is `/`), `FileName`, `Stem`, `Extension` (the leading dot rules for `.gitignore`,
      `a.tar.gz`, `a.`), `Components` (a non-allocating iterator over `StringView`s), `ToString`/`View` (canonical), `ToNative` (separators and, on Windows, the drive form).
- [x] Builders: `Join` (`a` / `b`, joining an absolute right operand replaces, empty operands), `operator/`, `Append`, `WithFileName`, `WithExtension`, `WithoutExtension`, `AddTrailingSeparator`/`RemoveTrailingSeparator`.
- [x] Lexical normalization: `Normalize` (remove `.`, resolve `..` where possible, keep leading `..` in relative paths, never above a root, collapse repeated separators), and `RelativeTo(base)` / `IsInside(base)`
      without touching the disk; document that symbolic links are not considered (the file-system helpers on Day 117 resolve those).
- [x] Comparison: `operator==` and ordering by component with the style's case rule (POSIX sensitive; Windows insensitive for ASCII and documented for non-ASCII), `Hash`, `StartsWith(component prefix)`. Equality is of
      the normalized lexical form, documented.
- [x] A `PathBuilder` for fixed-capacity (stack) construction without heap, for code that cannot allocate (the cache directory lookup at startup).
- [x] Tests (phase 9): the 60-row Spike C table for both styles; Joins and normalization properties (1000 random component lists: `Normalize` is idempotent, `Parent(Join(a,b)) == a` for a relative single component `b`,
      `RelativeTo` then `Join` returns the original for paths under the base); Unicode names; the maximum length boundary; the style switch (Windows parsing on Linux).
- [x] **Documentation:** `///` comments with labels; a first version of `docs/guide/io.md` (Path section with a tagged sample) added to `docs/guide/README.md`; reference regenerated; `docgen lint` at zero; `ctest -L docs`.
- [x] Run `scripts/test-phase.sh --affected`; quote the line; sanitizer flavors (string handling).
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high`; record it.
- [ ] Update `STATUS.md`; commit.

**Done:** `Path` parses, builds, normalizes and compares paths for both styles with the Spike C table and the property tests passing, with no syscall in the module, and the guide section is committed.

**Added on Day 115:** this day also adds `flavors 8 ...` to `tests/phases.txt` the `io` module to `docs/reference/modules.txt`, and `engine/foundation/io` to `tests/module_map.txt` (`0,1,2,2.5,3,4,6,8`); Spike C's tables (spec Appendix A) are the test data.

## Result

- `engine/foundation/io/path.h/.cpp`: `PathStyle`, `Path`, `PathBuilder<N>`, all operations of the Spike C table; tests in `tests/io/path_test.cpp` (11 cases, 16878 assertions) in plain, ASan and TSan (`tests/io/` has its own executables, so the io tests have sanitizer flavors; `flavors 8 plain,asan,tsan`). Spike C's tables are `tests/io/path_table.inc` (generated, committed).
- Amendments to the plan, decided while building: (1) **no allocator template, no `PathView`**: `Path` is a plain class on the default heap allocator (copyable); operations take `const Path&` or `StringView`; arena users use `PathBuilder`. `String` is move-only and allocator-templated, which does not fit value semantics. (2) **`AddTrailingSeparator`/`RemoveTrailingSeparator` dropped**: canonical form has no trailing separator, so the former would break the invariant. (3) Builders **abort** on a too-long result; `Path::Parse` and `TryJoin` report failure for untrusted text. (4) `View()` is not NUL-terminated (`ToNative` gives a terminated copy).
- `StringView` gained `LastIndexOf`, `StartsWith`, `EndsWith` and `Substring(start)` (tested in `tests/foundation/string_test.cpp`), so the path code works on views instead of byte loops.
- Docs: `docs/guide/io.md` (two run samples), `docs/reference/foundation-io.md`, baseline refreshed (3180/3180).
- Scoped runs: `ran 1048 of 2771 tests (phases: all; flavors: plain; display: included; crash tests: excluded)` (only the known X11 window test failed), `ran 765 of 2771 ... flavors: asan`, `ran 485 of 2771 ... flavors: tsan`, all pass; `ctest -L docs` 4/4.

## Post-implementation code review

`/code-review high` on the day's diff: 9 findings, 7 fixed, 2 declined. Before the review the user asked for views instead of byte-by-byte loops (done, see above).

Fixed:
1. `Path::Set` ignored allocation failure (copy constructor, assignment, `Parent`, `Normalize` could return an empty path under memory pressure). `Set` now aborts on out-of-memory like the rest of the foundation containers; `Parse`/`TryJoin` use `TrySet` and report; the class doc says so.
2. `WithFileName` accepted `D:x` on Windows style, which `Join` read as a drive and replaced the whole path. It now aborts on a colon in the name (Windows style); `WithExtension` inherits it. (No death test: the repo's abort tests need a child process; the assertion is the guard.)
3. Doc labels did not match behavior (`TryJoin` and `RelativeTo` styles, `WithExtension` separator/colon, `RelativeTo` overflow): corrected.
4. `NativePathStyle()` was hard-wired to POSIX: now `Windows` under `_WIN32`.
5. The `tests/io/` entry sat under the Phase 2 heading in `tests/phases.txt`: moved to its own Phase 9 section.
6. `engine/foundation` in `tests/module_map.txt` did not list phase 9 although the io tests link the foundation: added, and the `test_phase_script` case that pins the foundation-source selection now expects phase 9.
7. `Join(StringView)`/`Append(StringView)` built two heap temporaries: they now canonicalize and join on the stack (`TryJoinText`), one allocation for the result.

Declined: `PathBuilder::Append` uses two extra stack arrays of `Capacity` bytes: canonicalizing straight into the member would not survive a failed append leaving the contents unchanged, which the contract promises; the stack cost is documented in the class comment instead. The bookkeeping finding (STATUS.md and this section) is this commit.
