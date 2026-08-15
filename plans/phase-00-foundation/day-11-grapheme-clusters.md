# Phase 0 / Day 11 — Grapheme Clusters

**Phase:** [Phase 0 — Foundation Layer](../phase-00-foundation.md)
**Depends on:** Day 10 (UTF-8 core).

Implements UAX #29 grapheme cluster segmentation, table-driven per the
Unicode design spec
(`docs/superpowers/specs/2026-08-14-unicode-string-support-design.md`).
The offline table generator built here is reused as-is by Day 12
(normalization) for its own data tables.

## Tasks

- [x] Create `/tools/unicode_data_gen/` (dev-time only, never linked or
  shipped — same category as CMake itself, `PLAN.md` assumption #8): a
  Python script that, given a pinned Unicode version's downloaded
  `GraphemeBreakProperty.txt` and `emoji-data.txt`, parses each
  property line (`codepoint` or `codepoint..codepoint ; PropertyName`),
  merges adjacent/overlapping codepoint ranges sharing a property value
  into a minimal sorted non-overlapping range list, and emits a C++
  source file: a static sorted array of `{start, end, property}` plus a
  binary-search lookup function. Record the pinned Unicode version
  number in the generator's output (a comment header) and in this repo
  (e.g. a `UNICODE_VERSION` file alongside the generated sources).
- [x] Define `GraphemeBreakProperty` enum in
  `/engine/foundation/string/grapheme.h`: `Other`, `CR`, `LF`, `Control`,
  `Extend`, `ZWJ`, `RegionalIndicator`, `Prepend`, `SpacingMark`, `L`,
  `V`, `T`, `LV`, `LVT`, `ExtendedPictographic` (this last one from
  `emoji-data.txt`'s `Extended_Pictographic` property, not
  `GraphemeBreakProperty.txt` — the generator handles both source
  files, tagging table entries with which property they represent).
- [x] Run the generator against Unicode's published data for the pinned
  version; commit the generated
  `/engine/foundation/string/generated/grapheme_break_table.cpp` (checked in,
  not regenerated at build time) plus a `LookupGraphemeBreakProperty(u32
  codepoint) -> GraphemeBreakProperty` function (binary search over the
  generated table; codepoints outside any listed range default to
  `Other`).
- [x] Implement the UAX #29 boundary rules in
  `/engine/foundation/string/grapheme.cpp` as an ordered sequence of
  before/after-property checks between consecutive codepoints (walked
  via Day 10's `Utf8Codepoints`): never break `CR`×`LF`; always break
  around other `Control`/`CR`/`LF`; Hangul composition (`L`×`(L|V|LV|LVT)`,
  `(LV|V)`×`(V|T)`, `(LVT|T)`×`T`); never break before `Extend`/`ZWJ`;
  never break before `SpacingMark`; never break after `Prepend`; never
  break within an `ExtendedPictographic Extend* ZWJ`×`ExtendedPictographic`
  sequence (emoji ZWJ sequences); `RegionalIndicator`×`RegionalIndicator`
  only pairs (an odd-numbered `RegionalIndicator` in a run starts a new
  cluster, tracked via a running count reset at each non-RI codepoint);
  otherwise, break.
- [x] Implement `GraphemeClusterIterator`/`GraphemeClusterRange` with a
  `GraphemeClusters(StringView)` factory (same shape as Day 10's
  `Utf8Codepoints`), yielding one `StringView` slice per cluster, and
  `usize GraphemeClusterCount(StringView)`.
- [x] Write a plain-ASCII test: grapheme count equals codepoint count
  equals byte count (no multi-codepoint clusters).
- [x] Write a combining-character test: `"e"` (U+0065) + combining
  acute (U+0301) is one grapheme cluster spanning both codepoints'
  bytes, not two.
- [x] Write a decomposed-Hangul test: an `L` jamo (U+1100) + a `V` jamo
  (U+1161) is one grapheme cluster (tests the Hangul composition rules
  specifically, since precomposed Hangul syllables are already single
  codepoints and wouldn't exercise this rule).
- [x] Write a CR+LF test: `"\r\n"` is one grapheme cluster, not two.
- [x] Write an emoji ZWJ-sequence test and a regional-indicator-pair
  (flag emoji) test, plus a three-in-a-row regional-indicator test
  confirming it splits into a pair + a singleton, not one cluster of
  three (tests the even/odd pairing rule specifically).
- [x] Commit.

**Done:** all seven test cases (plain ASCII, combining character,
decomposed Hangul, CR+LF, emoji ZWJ sequence, regional-indicator pair,
regional-indicator triple) produce the expected cluster count and
boundaries.

## Notes / open questions

- Pinned Unicode version is a Day-11-time decision (latest published
  stable version at implementation time), recorded in the generated
  table's header comment and a repo-level `UNICODE_VERSION` file —
  not fixed by this plan.
