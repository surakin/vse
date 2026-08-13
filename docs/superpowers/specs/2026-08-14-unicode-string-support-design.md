# Unicode String Support — Design Spec

**Date:** 2026-08-14
**Status:** Approved, not yet implemented

## Problem

Phase 0 Day 8 (`engine/foundation/string.h`, commit `f6b7e2f`) built `StringView`/
`String<Allocator>`/`StringBuilder<Allocator>` as plain byte sequences —
no encoding awareness at all. While reviewing that code, the user flagged
that the engine needs full Unicode support: "strings can be single-byte
or multi-byte backed."

## Decision: UTF-8 everywhere, three views over the same buffer

One canonical text representation — UTF-8 — rather than a second
narrow/wide string type (Unreal's `ANSICHAR`/`TCHAR` split was
considered and rejected). UTF-8 is inherently what "single-byte or
multi-byte backed" describes: ASCII codepoints occupy 1 byte, other
codepoints occupy 2-4 bytes, all within the same buffer. This is also
the smallest change from Day 8's existing byte-oriented foundation and
matches modern engine practice (moving away from UTF-16-everywhere).

**Invariant:** a `String`/`StringView` always holds well-formed UTF-8.
Internal code trusts this (no per-call validation — PLAN.md's "trust
internal code, validate only at boundaries" rule); an explicit
`IsValidUtf8(StringView)` check exists for text arriving from outside
the engine (files, user input) to call before trusting external bytes.
`StringBuilder::Append(char c)` gets an `ENGINE_ASSERT` that `c` is
ASCII (`< 0x80`), since a lone non-ASCII byte would violate the
invariant — non-ASCII codepoints go through a new `AppendCodepoint(u32)`
that correctly encodes 1-4 UTF-8 bytes.

### Three views over the same bytes

- **Byte-level** (existing, unchanged): `Length()` stays byte length
  (matches `std::string`/Rust `str::len()` convention — `.length()`
  meaning codepoint count would be the surprising choice, not the
  default one), `Substring`/`operator[]`/`IndexOf`/`Find` stay
  byte-oriented, for raw buffer work, file I/O, C-string interop.
- **Codepoint-level** (new): `CodepointCount()`; `DecodeUtf8At(StringView,
  usize byte_offset) -> { u32 codepoint; usize byte_length; }`; a
  `Utf8Codepoints(StringView)` range type supporting
  `for (u32 cp : Utf8Codepoints(sv))`.
- **Grapheme-cluster-level** (new): `GraphemeClusterCount()` and a
  `GraphemeClusters(StringView)` range type yielding one `StringView`
  slice per user-perceived character (combining accents, Hangul
  syllables, and — first pass — most emoji sequences count as one
  "character," not several).

## Grapheme cluster segmentation

Implements UAX #29's rule set: given the grapheme-break property of the
codepoints on each side of a potential boundary (`CR`, `LF`, `Control`,
`Extend`, `ZWJ`, `Regional_Indicator`, `Prepend`, `SpacingMark`,
`L`/`V`/`T`/`LV`/`LVT` for Hangul, `Extended_Pictographic`), an ordered
rule list decides whether a boundary is allowed (never break between
`CR`/`LF`; never break before `Extend`/`ZWJ`; don't break within an
emoji ZWJ sequence). `GraphemeClusters` walks codepoint-by-codepoint
(reusing the codepoint iterator) applying these rules.

**Scope call:** core rules (combining marks, Hangul, CR/LF,
`Prepend`/`SpacingMark`) are in scope from day one. Emoji-specific rules
(`Regional_Indicator` flag-pairing, `Extended_Pictographic` ZWJ
sequences) reuse the identical table-driven mechanism — one more
generated property table (from `emoji-data.txt`) plus two more rules in
the same ordered list, not a separate mechanism — so a first pass may
simply be less exercised on exotic emoji sequences rather than
architecturally incomplete.

## Normalization (NFC/NFD)

Two new free functions producing an owned result (normalization can
change byte length, so it can't be in-place):

```cpp
String<Allocator> NormalizeNFC(Allocator&, StringView);  // canonical composition
String<Allocator> NormalizeNFD(Allocator&, StringView);  // canonical decomposition
```

Built from a **canonical decomposition table** (codepoint → sequence it
decomposes into — e.g. "é" U+00E9 → "e" U+0065 + combining-acute
U+0301) and a **combining-class table** (needed to canonically reorder
combining marks before composing/comparing). NFD is decomposition +
reordering; NFC is NFD followed by canonical composition (recombining
adjacent decomposed pairs where a precomposed codepoint exists, skipping
pairs on the composition-exclusions list). Hangul syllable
composition/decomposition follows a documented algorithmic formula, not
a table lookup, and gets its own small code path alongside the
table-driven one — a shortcut here would silently produce wrong answers
on all Hangul text, so this is implemented in full rather than
approximated.

## Data tables: generation strategy

Grapheme-break properties, decomposition mappings, combining classes,
and composition exclusions are all published by Unicode as plain-text
data files (`GraphemeBreakProperty.txt`, `UnicodeData.txt`,
`CompositionExclusions.txt`, `emoji-data.txt`). An **offline generator**
(a small script, run once at implementation time, not shipped in the
engine/game binary — same "dev-time tool" category as CMake, PLAN.md
assumption #8) downloads a pinned Unicode version's data files and emits
compact C++ range tables (`{codepoint_start, codepoint_end,
property_value}` triples, sorted for binary search) — the standard
technique every conformant implementation uses (ICU, Rust's
`unicode-segmentation`/`unicode-normalization` crates), since the
codepoint space is sparse enough that a flat array would waste
megabytes. The **generated tables** (static `.cpp` data, checked into
the repo like any generated-but-committed artifact) are what Foundation
code actually depends on at runtime — pinned to one Unicode version,
refreshed manually later only if something needs it.

**Collation is explicitly out of scope**, and unlike the rest of this
spec, not even flagged as "deferred with a plan." Locale-aware sorting
needs CLDR per-locale tailoring data on top of the Default Unicode
Collation Element Table — a substantially bigger, more open-ended
commitment than grapheme-break/normalization data alone. Byte/codepoint
order is what's available until a real need for locale-aware sorting
shows up; revisit as its own scoped decision then, not now.

## Day sequencing (Phase 0)

Day 8 (already complete) is extended, not rewritten — same pattern as
Day 9 extending Day 8's `StringView` with `IndexOf`/`Find`/`Contains`.
Three new days inserted after Day 9 (logging, done), before the current
Day 10 (math), which shifts down along with Day 11 (quaternions) and
Day 12 (test harness):

- **New Day 10 — UTF-8 core:** codepoint decode/encode, iteration,
  validation, `CodepointCount`, `AppendCodepoint`, the `Append(char)`
  ASCII assert.
- **New Day 11 — Grapheme clusters:** the offline UCD table generator
  (built here first, as its first consumer) + grapheme-break property
  table + segmentation algorithm + iterator.
- **New Day 12 — Normalization:** decomposition/combining-class/
  composition-exclusion tables (via Day 11's generator) + NFC/NFD.
- Math (vectors/matrices, quaternions/transforms) becomes Days 13-14;
  the Catch2 test harness becomes Day 15.

Same renumbering mechanics as the Phase 1 Wayland/X11 reorder: new
files, `git mv` shifts, fix every cross-reference (Day 12's current file
already says "Days 1-11" for its dependency — becomes "Days 1-14" at
its new position).

## Testing approach

Same ad hoc `Expect()`-in-`main.cpp` style as Days 1-9 (Catch2 migration
is still the last day, unchanged). Known-answer tests against
well-known Unicode conformance cases: mixed ASCII/multi-byte strings for
codepoint counting/iteration; a combining-character sequence (e.g. "é"
as `e` + combining-acute, a classic textbook case) for grapheme
boundaries and NFC/NFD round-tripping (`NFD(NFC(s)) == NFD(s)`,
`NFC("e"+combining-acute) == "é"` as a single precomposed codepoint); a
Hangul syllable for the algorithmic (non-table) composition/
decomposition path specifically; malformed byte sequences for
`IsValidUtf8` rejecting them.
