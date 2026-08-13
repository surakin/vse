# Phase 0 / Day 3 — Base Types & Mem Primitives

**Phase:** [Phase 0 — Foundation Layer](../phase-00-foundation.md)
**Depends on:** Day 2 (OS interaction wrappers).

`memcpy`/`memset`/`memmove`/`strlen` stay hand-rolled regardless of the
libc decision (`PLAN.md` assumption #2 explicitly keeps string/mem
handling hand-rolled) — the compiler emits calls to these for
struct/array ops even with libc linked, and defining them ourselves
keeps that codepath under our control.

## Tasks

- [x] Define fixed-width integer typedefs (`i8/i16/i32/i64`,
  `u8/u16/u32/u64`, `usize`, `f32/f64`) in
  `/engine/foundation/types.h`; confirm native `bool` is usable as-is.
- [x] Update `/engine/foundation/os.h`/`os.cpp` (Day 2) to use `usize`
  in place of the `std::size_t` placeholder they were written with —
  see Day 2's note on why `usize` couldn't be used from the start.
- [x] Implement hand-written `memcpy`, `memset`, `memmove`, `strlen` in
  `/engine/foundation/mem.cpp`, declared with C linkage in `mem.h` so
  the compiler's implicitly generated calls resolve to these instead
  of libc's — confirmed via `nm` that the linked binary's `memcpy`/
  `memset`/`memmove`/`strlen` symbols are our own definitions (`T` in
  the executable), not libc's.
- [x] Write hand-computed test cases: `memcpy`/`memset` over several
  sizes and alignments, `memmove` over overlapping regions (forward and
  backward overlap), `strlen` over known-length strings including the
  empty string.
- [x] Run the cases (ad hoc via Day 2's `os::Write` for pass/fail
  output — Day 15's formal harness doesn't exist yet) and confirm every
  case matches the hand-computed expected result. All 13 cases pass.
- [x] Commit.

**Done:** custom assert-based tests validate each function against
hand-computed cases.
