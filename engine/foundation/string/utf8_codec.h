#pragma once

#include "types.h"

//! Raw UTF-8 decode and encode on `const char*` buffers, with no dependency on
//! the string module.
namespace vse
{

/// Result of decoding one UTF-8 sequence.
struct DecodedCodepoint
{
    u32 codepoint;      ///< Decoded Unicode scalar value, or U+FFFD for malformed input.
    usize byte_length;  ///< Number of bytes the sequence occupied (1 for malformed input).
};

/// Decodes the codepoint starting at byte `offset` in `data[0, length)`.
///
/// Malformed input never stops progress: it yields `{0xFFFD, 1}` so a caller
/// advancing by `byte_length` always moves forward.
///
/// - Param data: start of the buffer; not retained.
/// - Param length: buffer size in bytes.
/// - Param offset: byte index where the sequence starts.
/// - Param out_valid: if non-null, always written: true for a well-formed
///   sequence, false for a malformed one.
/// - Returns: the codepoint and its encoded length.
/// - Fails: malformed sequences are reported through `out_valid` and the
///   replacement result.
DecodedCodepoint DecodeUtf8At(const char* data, usize length, usize offset, bool* out_valid = nullptr);

/// Encodes `codepoint` as UTF-8 into `out` and returns the byte count (1-4).
///
/// A surrogate or a value above 0x10FFFF is encoded as U+FFFD instead.
///
/// - Param codepoint: Unicode scalar value to encode.
/// - Param out: buffer of at least 4 bytes; not NUL-terminated.
/// - Returns: number of bytes written.
usize EncodeUtf8(u32 codepoint, char out[4]);

}  // namespace vse
