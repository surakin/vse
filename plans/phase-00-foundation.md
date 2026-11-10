# Phase 0 — Foundation Layer

**Depends on:** nothing (freestanding of third-party code; links libc
normally — see `PLAN.md` assumption #2).
**Goal:** the language-level building blocks every later layer needs —
memory, containers, strings, math, logging/assert — hand-rolled on top
of libc rather than the C++ STL.
**Definition of done:** custom allocators/containers/string/math all pass
the custom test harness (`ctest`, all green).

See root `PLAN.md` for guiding principles and scope assumptions.

## Days

Each day is its own file with a specific, checkable sub-task list and a
Done condition — see `CLAUDE.md`'s "How the plan is structured."

1. [Repository & build skeleton](phase-00-foundation/day-01-repository-build-skeleton.md)
2. [OS interaction wrappers (libc-backed)](phase-00-foundation/day-02-os-interaction-wrappers.md)
3. [Base types & mem primitives](phase-00-foundation/day-03-base-types-mem-primitives.md)
4. [Arena allocator](phase-00-foundation/day-04-arena-allocator.md)
5. [Pool & stack allocators](phase-00-foundation/day-05-pool-stack-allocators.md)
6. [Dynamic array container](phase-00-foundation/day-06-dynamic-array.md)
7. [Hash map container](phase-00-foundation/day-07-hash-map.md)
8. [String type & builder](phase-00-foundation/day-08-string-type-builder.md)
9. [Logging & assertions](phase-00-foundation/day-09-logging-assertions.md)
10. [UTF-8 core](phase-00-foundation/day-10-utf8-core.md)
11. [Grapheme clusters](phase-00-foundation/day-11-grapheme-clusters.md)
12. [Normalization (NFC/NFD)](phase-00-foundation/day-12-normalization.md)
13. [Math: vectors & matrices](phase-00-foundation/day-13-math-vectors-matrices.md)
14. [Math: quaternions & transforms](phase-00-foundation/day-14-math-quaternions-transforms.md)
15. [Foundation test harness](phase-00-foundation/day-15-test-harness.md)

## Notes / open questions

- **Log config format/source (resolved, implemented).** Day 9
  implements a minimal hand-rolled `key=value` text format
  (`ChannelName.verbosity=Level` / `ChannelName.enabled=true|false`),
  read via Day 2's `os::Open`/`os::Read` and parsed with Day 8's
  `StringView` (extended with `IndexOf`/`Find`/`Contains`) — no general
  config-file parser needed since this format is deliberately dumb.
  Revisit if it proves too limited once real usage shows up (e.g. once
  the asset pipeline's own format conventions land in Phase 22).
- **libc linkage (resolved).** Phase 0 links libc normally from Day 1 —
  no `-nostdlib`/`-ffreestanding` — and Day 2's OS-interaction layer
  wraps libc functions directly rather than hand-rolling raw syscalls.
  See `PLAN.md` assumption #2. Phase 1 Day 13 (now written) resolved
  this the same way: a standard `main()` entry via libc's own startup,
  not a hand-rolled `_start`.
- **Unicode string support (resolved, being implemented).** Days 10-12
  extend Day 8's byte-oriented `String`/`StringView`/`StringBuilder`
  with full UTF-8 codepoint/grapheme-cluster support and NFC/NFD
  normalization — see
  `docs/superpowers/specs/2026-08-14-unicode-string-support-design.md`
  for the design (one canonical UTF-8 representation with byte/
  codepoint/grapheme views, not a second narrow/wide string type;
  table-driven grapheme-break and normalization data generated offline
  from Unicode's published files, not a shipped third-party library;
  collation explicitly out of scope). Inserted after Day 9, pushing the
  original Days 10-12 (math, test harness) to 13-15.
