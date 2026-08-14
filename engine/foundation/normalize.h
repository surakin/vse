#pragma once

#include "arena_allocator.h"
#include "array.h"
#include "string.h"
#include "types.h"
#include "utf8.h"
#include "utf8_codec.h"

// Canonical Unicode normalization (NFC/NFD), per UAX #15. Depends on
// Day 10's utf8.h/utf8_codec.h (codepoint iteration/encoding) and Day 4's
// ArenaAllocator (internal scratch buffer for the codepoint working set —
// see the rationale on NormalizeNFD/NormalizeNFC below).
namespace engine_placeholder {

// Table lookups over engine/foundation/generated/*.cpp (produced by
// tools/unicode_data_gen/generate_normalization_tables.py). Hangul
// syllables are deliberately absent from these tables — normalize.cpp
// handles them via the algorithmic Hangul formula instead.
bool LookupCanonicalDecomposition(u32 codepoint, u32 out_decomposed[2], u8* out_length);
bool LookupCanonicalComposition(u32 first, u32 second, u32* out_composed);
u8 CombiningClass(u32 codepoint);
bool IsCompositionExcluded(u32 codepoint);

// Implementation detail (normalize.cpp). Non-template — operates on a
// concrete ArenaAllocator-backed scratch buffer rather than the caller's
// Allocator, since the codepoint working set is transient and internal;
// only the final encode step below uses the caller's Allocator.
void Detail_FullyDecomposeAppend(u32 codepoint, Array<u32, ArenaAllocator>& out);
void Detail_CanonicalOrder(Array<u32, ArenaAllocator>& codepoints);
void Detail_ComposeInPlace(Array<u32, ArenaAllocator>& codepoints);

namespace detail {

// Generous relative to input size: worst-case canonical decomposition
// expansion is small (a handful of codepoints per input codepoint), and
// this is scratch space reclaimed when the function returns, not part of
// the result.
inline usize NormalizeScratchBytes(StringView sv) {
    return 4096 + sv.Length() * 32;
}

} // namespace detail

template <typename Allocator>
String<Allocator> NormalizeNFD(Allocator& allocator, StringView sv) {
    ArenaAllocator scratch(detail::NormalizeScratchBytes(sv));
    Array<u32, ArenaAllocator> codepoints(scratch);
    for (u32 cp : Utf8Codepoints(sv)) {
        Detail_FullyDecomposeAppend(cp, codepoints);
    }
    Detail_CanonicalOrder(codepoints);

    String<Allocator> result(allocator);
    for (usize i = 0; i < codepoints.Size(); ++i) {
        char buf[4];
        usize len = EncodeUtf8(codepoints[i], buf);
        result.AppendRawBytes(buf, len);
    }
    return result;
}

template <typename Allocator>
String<Allocator> NormalizeNFC(Allocator& allocator, StringView sv) {
    ArenaAllocator scratch(detail::NormalizeScratchBytes(sv));
    Array<u32, ArenaAllocator> codepoints(scratch);
    for (u32 cp : Utf8Codepoints(sv)) {
        Detail_FullyDecomposeAppend(cp, codepoints);
    }
    Detail_CanonicalOrder(codepoints);
    Detail_ComposeInPlace(codepoints);

    String<Allocator> result(allocator);
    for (usize i = 0; i < codepoints.Size(); ++i) {
        char buf[4];
        usize len = EncodeUtf8(codepoints[i], buf);
        result.AppendRawBytes(buf, len);
    }
    return result;
}

} // namespace engine_placeholder
