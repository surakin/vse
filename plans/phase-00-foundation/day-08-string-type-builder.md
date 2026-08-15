# Phase 0 / Day 8 — String Type & Builder

**Phase:** [Phase 0 — Foundation Layer](../phase-00-foundation.md)
**Depends on:** Day 7 (hash map container).

There's no `printf`/`sprintf` here — formatting stays hand-rolled per
`PLAN.md` assumption #2's "still hand-rolls... string handling," same as
Day 3's mem primitives.

## Tasks

- [x] Implement `StringView` in `/engine/foundation/string/string.h`:
  non-owning length-prefixed pointer+length pair, with
  equality/comparison/substring operations.
- [x] Implement `String<Allocator>` (owning, allocator-backed) built on
  Day 6's `Array<char, Allocator>` — always keeps a trailing `'\0'` in
  the backing array so `CStr()`/`Length()` are O(1) with no separate
  bookkeeping.
- [x] Implement `StringBuilder<Allocator>`: `AppendInt`/`AppendUInt`
  (handles `INT64_MIN` correctly via unsigned magnitude, avoiding UB
  from negating it directly) and `AppendFloat` (fixed-point,
  round-half-up, zero-padded fractional digits) on top of `String`'s
  `Append`.
- [x] Write a test that formats a mix of known ints/floats/strings and
  compares the resulting buffer, byte-for-byte, against expected text:
  `"answer=-42 count=7 pi=3.25 name=engine"`, reusing Day 3's
  `BytesEqual` helper.
- [x] Write that formatted output via Day 2's `os::Write` and confirm
  the observed stdout matches the expected text exactly — the same
  buffer already verified byte-for-byte is written to fd 1 and visibly
  appears in the program's actual output.
- [x] Commit.

**Done:** formatted output written via `os::Write` matches expected
text exactly. Verified: `engine_main` runs all tests through Day 8,
formatted line appears correctly in stdout, exit code 0; also clean
under a one-off `-fsanitize=address,undefined` build.
