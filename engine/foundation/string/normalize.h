#pragma once

#include "mem/arena_allocator.h"
#include "containers/vector.h"
#include "string/string.h"
#include "types.h"
#include "string/utf8.h"
#include "string/utf8_codec.h"

//! Canonical Unicode normalization (NFC and NFD) per UAX #15, plus the Unicode
//! property lookups it is built on.
namespace vse
{

/// Looks up the canonical decomposition of a codepoint in the generated tables.
///
/// Hangul syllables are not in the tables; they are handled algorithmically by
/// the normalization functions.
///
/// - Param codepoint: codepoint to decompose.
/// - Param out_decomposed: receives one or two codepoints of the decomposition.
/// - Param out_length: receives how many entries of `out_decomposed` are valid.
/// - Returns: true if the codepoint has a canonical decomposition.
bool LookupCanonicalDecomposition(u32 codepoint, u32 out_decomposed[2], u8* out_length);
/// Looks up the primary composite for a pair of codepoints in the generated tables.
///
/// - Param first: starter codepoint.
/// - Param second: following codepoint.
/// - Param out_composed: receives the composite when one exists.
/// - Returns: true if the pair composes.
bool LookupCanonicalComposition(u32 first, u32 second, u32* out_composed);
/// Returns the canonical combining class of `codepoint`, or 0 for a starter.
///
/// - Param codepoint: Unicode scalar value.
u8 CombiningClass(u32 codepoint);
/// Reports whether `codepoint` is excluded from canonical composition.
///
/// - Param codepoint: Unicode scalar value.
/// - Returns: true if it is composition-excluded.
bool IsCompositionExcluded(u32 codepoint);

/// Appends the full canonical decomposition of `codepoint` to `out`.
///
/// - Internal: visible only because the NFD and NFC templates call it; not part
///   of the public API.
void Detail_FullyDecomposeAppend(u32 codepoint, Vector<u32, ArenaAllocator>& out);
/// Reorders runs of combining marks in `codepoints` into canonical order.
///
/// - Internal: visible only because the NFD and NFC templates call it; not part
///   of the public API.
void Detail_CanonicalOrder(Vector<u32, ArenaAllocator>& codepoints);
/// Composes canonically ordered, decomposed `codepoints` in place (the NFC step).
///
/// - Internal: visible only because the NFC template calls it; not part of the
///   public API.
void Detail_ComposeInPlace(Vector<u32, ArenaAllocator>& codepoints);

namespace detail
{

// Generous relative to input size: worst-case canonical decomposition
// expansion is small (a handful of codepoints per input codepoint), and
// this is scratch space reclaimed when the function returns, not part of
// the result.
inline usize NormalizeScratchBytes(StringView sv)
{
    return 4096 + sv.Length() * 32;
}

}  // namespace detail

/// Converts `sv` to Normalization Form D (canonical decomposition, canonically ordered).
///
/// Works in a temporary [[ArenaAllocator]] scratch buffer sized from the input
/// and released before returning; only the result uses `allocator`. Malformed
/// UTF-8 in the input is normalized as U+FFFD.
///
/// - Param allocator: allocator for the returned string; must outlive it.
/// - Param sv: UTF-8 text to normalize.
/// - Returns: the normalized text as a new [[String]].
/// - Aborts: the scratch buffer or `allocator` is out of memory.
/// - Thread-safety: thread-safe provided `allocator` is not shared unsynchronized.
/// - Ownership: the result is allocated from `allocator`, which owns the memory.
template <typename Allocator> String<Allocator> NormalizeNFD(Allocator& allocator, StringView sv)
{
    ArenaAllocator scratch(detail::NormalizeScratchBytes(sv));
    Vector<u32, ArenaAllocator> codepoints(scratch);
    for (u32 cp : Utf8Codepoints(sv))
    {
        Detail_FullyDecomposeAppend(cp, codepoints);
    }
    Detail_CanonicalOrder(codepoints);

    String<Allocator> result(allocator);
    for (usize i = 0; i < codepoints.Size(); ++i)
    {
        char buf[4];
        usize len = EncodeUtf8(codepoints[i], buf);
        result.AppendRawBytes(buf, len);
    }
    return result;
}

/// Converts `sv` to Normalization Form C (canonical decomposition followed by canonical composition).
///
/// Works in a temporary [[ArenaAllocator]] scratch buffer sized from the input
/// and released before returning; only the result uses `allocator`. Malformed
/// UTF-8 in the input is normalized as U+FFFD.
///
/// - Param allocator: allocator for the returned string; must outlive it.
/// - Param sv: UTF-8 text to normalize.
/// - Returns: the normalized text as a new [[String]].
/// - Aborts: the scratch buffer or `allocator` is out of memory.
/// - Thread-safety: thread-safe provided `allocator` is not shared unsynchronized.
/// - Ownership: the result is allocated from `allocator`, which owns the memory.
template <typename Allocator> String<Allocator> NormalizeNFC(Allocator& allocator, StringView sv)
{
    ArenaAllocator scratch(detail::NormalizeScratchBytes(sv));
    Vector<u32, ArenaAllocator> codepoints(scratch);
    for (u32 cp : Utf8Codepoints(sv))
    {
        Detail_FullyDecomposeAppend(cp, codepoints);
    }
    Detail_CanonicalOrder(codepoints);
    Detail_ComposeInPlace(codepoints);

    String<Allocator> result(allocator);
    for (usize i = 0; i < codepoints.Size(); ++i)
    {
        char buf[4];
        usize len = EncodeUtf8(codepoints[i], buf);
        result.AppendRawBytes(buf, len);
    }
    return result;
}

}  // namespace vse
