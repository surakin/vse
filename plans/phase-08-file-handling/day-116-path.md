# Phase 8 / Day 116 — `io::Path`

**Phase:** [Phase 8 — File Handling](../phase-08-file-handling.md)
**Depends on:** Day 115 (spec, the Spike C table).

A pure value type: no syscalls, no allocation beyond its string. New files `engine/foundation/io/path.h/.cpp`, tests in `tests/foundation`.

## Tasks

- [ ] `PathStyle` (`Posix`, `Windows`; `Native()` for the build) and `io::Path` (allocator story from the spec): construct from a `StringView` of UTF-8, store in the canonical form (`/`), keep the
      root name (`C:`, `//server/share`), root directory flag and the relative part; `kMaxPathBytes` limit enforced with a documented failure (not a truncation).
- [ ] Queries: `IsEmpty`, `IsAbsolute`, `IsRelative`, `HasRoot`, `RootName`, `Parent` (of `a/b` is `a`, of `a` is empty, of `/` is `/`), `FileName`, `Stem`, `Extension` (the leading dot rules for `.gitignore`,
      `a.tar.gz`, `a.`), `Components` (a non-allocating iterator over `StringView`s), `ToString`/`View` (canonical), `ToNative` (separators and, on Windows, the drive form).
- [ ] Builders: `Join` (`a` / `b`, joining an absolute right operand replaces, empty operands), `operator/`, `Append`, `WithFileName`, `WithExtension`, `WithoutExtension`, `AddTrailingSeparator`/`RemoveTrailingSeparator`.
- [ ] Lexical normalization: `Normalize` (remove `.`, resolve `..` where possible, keep leading `..` in relative paths, never above a root, collapse repeated separators), and `RelativeTo(base)` / `IsInside(base)`
      without touching the disk; document that symbolic links are not considered (the file-system helpers on Day 117 resolve those).
- [ ] Comparison: `operator==` and ordering by component with the style's case rule (POSIX sensitive; Windows insensitive for ASCII and documented for non-ASCII), `Hash`, `StartsWith(component prefix)`. Equality is of
      the normalized lexical form, documented.
- [ ] A `PathBuilder` for fixed-capacity (stack) construction without heap, for code that cannot allocate (the cache directory lookup at startup).
- [ ] Tests (phase 8): the 60-row Spike C table for both styles; Joins and normalization properties (1000 random component lists: `Normalize` is idempotent, `Parent(Join(a,b)) == a` for a relative single component `b`,
      `RelativeTo` then `Join` returns the original for paths under the base); Unicode names; the maximum length boundary; the style switch (Windows parsing on Linux).
- [ ] **Documentation:** `///` comments with labels; a first version of `docs/guide/io.md` (Path section with a tagged sample) added to `docs/guide/README.md`; reference regenerated; `docgen lint` at zero; `ctest -L docs`.
- [ ] Run `scripts/test-phase.sh --affected`; quote the line; sanitizer flavors (string handling).
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high`; record it.
- [ ] Update `STATUS.md`; commit.

**Done:** `Path` parses, builds, normalizes and compares paths for both styles with the Spike C table and the property tests passing, with no syscall in the module, and the guide section is committed.
