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
10. [Math: vectors & matrices](phase-00-foundation/day-10-math-vectors-matrices.md)
11. [Math: quaternions & transforms](phase-00-foundation/day-11-math-quaternions-transforms.md)
12. [Foundation test harness](phase-00-foundation/day-12-test-harness.md)

## Notes / open questions

- **Log config format/source.** Day 9 needs per-user, no-recompile
  control over channel verbosity/enable state, but Phase 0 has no
  general config-file parser (Day 8 only gives a string type, not a
  parser, and the asset pipeline with its own format conventions doesn't
  land until Phase 8). Day 9 implements a minimal hand-rolled
  `key=value` text format directly as the leading candidate, kept
  intentionally dumb — revisit if that proves too limited once real
  usage shows up.
- **libc linkage (resolved).** Phase 0 links libc normally from Day 1 —
  no `-nostdlib`/`-ffreestanding` — and Day 2's OS-interaction layer
  wraps libc functions directly rather than hand-rolling raw syscalls.
  See `PLAN.md` assumption #2. Phase 1 Day 13 (now written) resolved
  this the same way: a standard `main()` entry via libc's own startup,
  not a hand-rolled `_start`.
