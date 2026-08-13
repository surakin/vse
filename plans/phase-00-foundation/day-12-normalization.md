# Phase 0 / Day 12 — Normalization (NFC/NFD)

**Phase:** [Phase 0 — Foundation Layer](../phase-00-foundation.md)
**Depends on:** Day 11 (grapheme clusters) — reuses its offline UCD
table generator for this day's own data tables.

Implements canonical Unicode normalization per the design spec
(`docs/superpowers/specs/2026-08-14-unicode-string-support-design.md`).
Collation stays explicitly out of scope (see the spec) — not attempted
here or anywhere else in this plan.

## Tasks

- [ ] Extend Day 11's `/tools/unicode_data_gen/` generator to also parse
  `UnicodeData.txt` (field 3 = combining class, field 5 = canonical
  decomposition mapping — skip entries tagged `<compat>` and similar;
  only canonical decompositions matter for NFC/NFD) and
  `CompositionExclusions.txt`, against the same pinned Unicode version
  as Day 11.
- [ ] Emit three generated C++ tables under
  `/engine/foundation/generated/`: `decomposition_table.cpp` (sorted
  `{codepoint, decomposed[1..2], decomposed_length}` entries, binary
  search by codepoint — Hangul syllables are deliberately excluded, see
  below), `combining_class_table.cpp` (sorted `{codepoint,
  combining_class}` entries for the non-zero-combining-class codepoints;
  anything absent defaults to combining class 0), and
  `composition_exclusions_table.cpp` (sorted list of excluded
  codepoints, binary-search membership test).
- [ ] Implement Hangul syllable decomposition/composition in
  `/engine/foundation/normalize.cpp` as the documented arithmetic
  formula (syllable index arithmetic over the `L`/`V`/`T` jamo ranges),
  not a table lookup — this is why Hangul is excluded from the
  generated decomposition table above. A shortcut here would silently
  produce wrong output for all Hangul text, so this path is implemented
  in full, not approximated.
- [ ] Implement canonical decomposition in `/engine/foundation/
  normalize.h`/`.cpp`: for each codepoint, look up (or algorithmically
  compute, for Hangul) its decomposition; recursively decompose the
  result codepoints too, since some decompositions are multi-step, until
  every codepoint has no further canonical decomposition.
- [ ] Implement the canonical ordering algorithm: after decomposition,
  stable-sort each maximal run of consecutive non-starter codepoints
  (combining class != 0) into ascending combining-class order.
  `NormalizeNFD(Allocator&, StringView) -> String<Allocator>` is
  decomposition + this ordering step.
- [ ] Implement canonical composition: scan the NFD result left to
  right, maintaining the current "starter" codepoint; for each following
  combining codepoint, compose it into the starter if a composition
  mapping exists (the inverse of a decomposition table entry, skipping
  anything on the composition-exclusions list) and no intervening
  codepoint blocks it (an intervening codepoint blocks composition if
  its combining class is 0, or is greater than or equal to the current
  codepoint's combining class — per UAX #15's blocking rule).
  `NormalizeNFC(Allocator&, StringView) -> String<Allocator>` is NFD
  followed by this composition pass (using the same Hangul algorithmic
  path in reverse for jamo sequences).
- [ ] Write an NFD test: precomposed `"é"` (U+00E9) decomposes to `"e"`
  (U+0065) + combining acute (U+0301) — 2 codepoints.
- [ ] Write an NFC test: `"e"` + combining acute composes to precomposed
  `"é"` (U+00E9) — 1 codepoint.
- [ ] Write round-trip and idempotency tests on mixed
  precomposed/already-decomposed input: `NFD(NFC(s)) == NFD(s)`,
  `NFC(NFC(s)) == NFC(s)`, `NFD(NFD(s)) == NFD(s)`.
- [ ] Write a Hangul round-trip test: a precomposed syllable (U+AC00)
  decomposes via NFD into its `L`+`V` jamo and recomposes via NFC back
  to U+AC00 — exercises the algorithmic Hangul path specifically
  (distinct from Day 11's grapheme test, which fed already-separate
  jamo in rather than testing decompose/recompose).
- [ ] Write a composition-exclusion test: pick an entry from the
  generated exclusions table, confirm decomposing then recomposing it
  does *not* recreate the original precomposed form (exact test
  codepoint chosen once the real table exists — not fixed by this plan).
- [ ] Commit.

**Done:** NFD/NFC produce correct results on the precomposed/decomposed
pair test, round-trip and idempotency hold, the Hangul algorithmic path
round-trips correctly, and the composition-exclusion case is correctly
*not* recomposed.
