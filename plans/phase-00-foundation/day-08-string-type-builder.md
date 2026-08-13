# Phase 0 / Day 8 — String Type & Builder

**Phase:** [Phase 0 — Foundation Layer](../phase-00-foundation.md)
**Depends on:** Day 7 (hash map container).

There's no `printf`/`sprintf` here — formatting stays hand-rolled per
`PLAN.md` assumption #2's "still hand-rolls... string handling," same as
Day 3's mem primitives.

## Tasks

- [ ] Implement `StringView` in `/engine/foundation/string.h`:
  non-owning length-prefixed pointer+length pair, with
  equality/comparison/substring operations.
- [ ] Implement `String` (owning, allocator-backed) built on Day 6's
  `Array<char>` or a dedicated buffer.
- [ ] Implement a `StringBuilder`/formatter: integer-to-string,
  float-to-string, and string-append formatting into a buffer.
- [ ] Write a test that formats a mix of known ints/floats/strings and
  compares the resulting buffer, byte-for-byte, against expected text.
- [ ] Write that formatted output via Day 2's `os::Write` and confirm
  the observed stdout matches the expected text exactly.
- [ ] Commit.

**Done:** formatted output written via `os::Write` matches expected
text exactly.
