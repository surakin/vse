#pragma once

#include "types.h"

// Raw UTF-8 codec: `const char*`/length in, no StringView dependency (only
// types.h) so string.h can depend on this without a cycle — see the
// file-split rationale in plans/phase-00-foundation/day-10-utf8-core.md.
namespace engine_placeholder {

struct DecodedCodepoint {
    u32 codepoint;
    usize byte_length;
};

// Decodes the codepoint starting at byte `offset` in `data[0, length)`.
// On a malformed sequence, sets `*out_valid = false` (if `out_valid` is
// non-null) and returns {0xFFFD, 1} so callers always make forward
// progress even on garbage input.
DecodedCodepoint DecodeUtf8At(const char* data, usize length, usize offset, bool* out_valid = nullptr);

// Encodes `codepoint` into 1-4 bytes at `out`, returns the byte count. A
// codepoint in the surrogate range or above 0x10FFFF encodes as U+FFFD
// instead (defensive; should not happen for codepoints obtained from
// DecodeUtf8At or ordinary literals).
usize EncodeUtf8(u32 codepoint, char out[4]);

} // namespace engine_placeholder
