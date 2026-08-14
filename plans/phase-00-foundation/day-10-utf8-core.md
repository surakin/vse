# Phase 0 / Day 10 — UTF-8 Core

**Phase:** [Phase 0 — Foundation Layer](../phase-00-foundation.md)
**Depends on:** Day 9 (logging & assertions), Day 8 (string type &
builder) — extends `StringView`/`String`/`StringBuilder` with
codepoint-level operations, per the Unicode design spec
(`docs/superpowers/specs/2026-08-14-unicode-string-support-design.md`).

**File split rationale:** codepoint encode/decode (`EncodeUtf8`/
`DecodeUtf8At`) needs to be callable from `string.h` (for
`StringBuilder::AppendCodepoint`), and the `StringView`-level helpers
(`CodepointCount`, `IsValidUtf8`, the codepoint iterator) need
`StringView` from `string.h` — if both lived in one `utf8.h`, that file
and `string.h` would depend on each other. Split into
`utf8_codec.h`/`.cpp` (raw `const char*`/length encode-decode, no
`StringView` dependency — only `types.h`) and `utf8.h` (header-only,
`StringView`-level, depends on both `utf8_codec.h` and `string.h`).
`string.h` depends only on `utf8_codec.h`, never on `utf8.h` — no cycle.

**Precondition, not an assert:** the Unicode design spec called for an
`ENGINE_ASSERT` on `String::Append(char)` rejecting non-ASCII bytes, but
`log.h` (which defines `ENGINE_ASSERT`) itself takes `StringView`
parameters and includes `string.h` — adding `string.h` → `log.h` would
create the same kind of cycle. Simpler fix, and more consistent with
how this codebase already handles this exact situation (`Array::Insert`/
`Remove`, `PoolAllocator::Free` all document unchecked preconditions
rather than asserting them): `Append(char)` gets a documented
precondition comment instead of a runtime check. Not a loss of
correctness — `AppendCodepoint` (this day) is the correct way to append
any non-ASCII codepoint, and internal call sites never pass one to
`Append(char)`.

## Tasks

- [x] Create `/engine/foundation/utf8_codec.h`/`.cpp`:
  - `struct DecodedCodepoint { u32 codepoint; usize byte_length; };`
  - `DecodedCodepoint DecodeUtf8At(const char* data, usize length, usize
    offset, bool* out_valid = nullptr)` — decodes the codepoint starting
    at byte `offset` per UTF-8's structural rules (1-byte `0xxxxxxx`,
    2-byte `110xxxxx 10xxxxxx`, 3-byte `1110xxxx 10xxxxxx 10xxxxxx`,
    4-byte `11110xxx 10xxxxxx 10xxxxxx 10xxxxxx`). On a malformed
    sequence (truncated at the end of the buffer, a continuation byte
    where a lead byte is expected, an overlong encoding, a codepoint in
    the surrogate range `0xD800`-`0xDFFF`, or a codepoint above
    `0x10FFFF`), sets `*out_valid = false` if `out_valid` is non-null and
    returns `{0xFFFD, 1}` (the Unicode replacement character, advancing
    one byte) so callers always make forward progress even on garbage
    input.
  - `usize EncodeUtf8(u32 codepoint, char out[4])` — encodes into 1-4
    bytes, returns the byte count. A codepoint in the surrogate range or
    above `0x10FFFF` encodes as U+FFFD instead (defensive; should not
    happen for codepoints obtained from `DecodeUtf8At` or ordinary
    literals).
- [x] Create `/engine/foundation/utf8.h` (header-only): `usize
  CodepointCount(StringView)`; `bool IsValidUtf8(StringView)` (loops
  `DecodeUtf8At` with `out_valid`, returns false on the first invalid
  step); a `Utf8CodepointIterator`/`Utf8CodepointRange` pair with a
  `Utf8Codepoints(StringView)` factory function, supporting
  `for (u32 cp : Utf8Codepoints(sv))`.
- [x] Extend `/engine/foundation/string.h`:
  - `#include "utf8_codec.h"`.
  - Add `String<Allocator>::AppendRawBytes(const char* bytes, usize
    count)` — appends already-encoded bytes verbatim, trusting the
    caller (used internally by `AppendCodepoint`); unlike `Append(char)`,
    this has no single-byte ASCII expectation since it's for
    already-correct multi-byte sequences.
  - Add a precondition comment to `String::Append(char)` /
    `StringBuilder::Append(char)`: `c` must be ASCII (`< 0x80`) — a lone
    non-ASCII byte would break the well-formed-UTF-8 invariant; use
    `AppendCodepoint` for non-ASCII codepoints. No runtime check (see
    rationale above).
  - Add `StringBuilder<Allocator>::AppendCodepoint(u32 codepoint)`:
    encodes via `EncodeUtf8` into a 4-byte stack buffer, appends via
    `AppendRawBytes`.
- [x] Write boundary-value round-trip tests (`EncodeUtf8` then
  `DecodeUtf8At` recovers the original codepoint) at every UTF-8 length
  transition: U+007F/U+0080 (1↔2 byte), U+07FF/U+0800 (2↔3 byte),
  U+FFFF/U+10000 (3↔4 byte), and U+10FFFF (max valid codepoint).
- [x] Write a mixed-content test: a string built from `Append`(ASCII) +
  `AppendCodepoint`(2-byte "é" U+00E9, 3-byte "中" U+4E2D, 4-byte "😀"
  U+1F600) — verify `CodepointCount()` and that `Utf8Codepoints`
  iteration yields exactly those codepoint values in order.
- [x] Write `IsValidUtf8` rejection tests against known-malformed
  sequences: a truncated multi-byte lead byte at end-of-buffer, a lone
  continuation byte (`0x80`-`0xBF`) with no lead byte, an overlong
  encoding of `'A'` (`0xC1 0x81` instead of `0x41`), and a UTF-8-encoded
  surrogate (`0xED 0xA0 0x80`, an attempt to encode U+D800).
- [x] Commit.

**Done:** boundary round-trips recover the original codepoint at every
length transition; mixed ASCII/2/3/4-byte content counts and iterates
correctly; all four malformed-sequence cases are rejected by
`IsValidUtf8`.
