#include "string/grapheme.h"

#include "string/utf8_codec.h"

// UAX #29 grapheme cluster boundary rules (GB1-GB999), applied pairwise
// while walking codepoints left to right. Two bits of extra running state
// carry information a strictly pairwise check can't see on its own:
//   - `ri_run_len_at_prev`: length of the maximal run of consecutive
//     Regional_Indicator codepoints ending at (and including) `prev`, used
//     for the odd/even flag-pairing rule (GB12/GB13).
//   - `emoji_run_at_prev`: whether the codepoints since the start of the
//     current \p{Extended_Pictographic} Extend* run (through `prev`,
//     possibly `prev` itself being the ZWJ that follows it) are still
//     "live", used for the emoji ZWJ-sequence rule (GB11).
namespace engine_placeholder
{

namespace
{

bool BreakBetween(GraphemeBreakProperty prev, GraphemeBreakProperty curr, usize ri_run_len_at_prev,
                  bool emoji_run_at_prev)
{
    using GBP = GraphemeBreakProperty;

    // GB3: CR x LF
    if (prev == GBP::CR && curr == GBP::LF)
    {
        return false;
    }
    // GB4: (Control|CR|LF) ÷
    if (prev == GBP::Control || prev == GBP::CR || prev == GBP::LF)
    {
        return true;
    }
    // GB5: ÷ (Control|CR|LF)
    if (curr == GBP::Control || curr == GBP::CR || curr == GBP::LF)
    {
        return true;
    }
    // GB6: L x (L|V|LV|LVT)
    if (prev == GBP::L && (curr == GBP::L || curr == GBP::V || curr == GBP::LV || curr == GBP::LVT))
    {
        return false;
    }
    // GB7: (LV|V) x (V|T)
    if ((prev == GBP::LV || prev == GBP::V) && (curr == GBP::V || curr == GBP::T))
    {
        return false;
    }
    // GB8: (LVT|T) x T
    if ((prev == GBP::LVT || prev == GBP::T) && curr == GBP::T)
    {
        return false;
    }
    // GB9: x (Extend|ZWJ)
    if (curr == GBP::Extend || curr == GBP::ZWJ)
    {
        return false;
    }
    // GB9a: x SpacingMark
    if (curr == GBP::SpacingMark)
    {
        return false;
    }
    // GB9b: Prepend x
    if (prev == GBP::Prepend)
    {
        return false;
    }
    // GB11: \p{Extended_Pictographic} Extend* ZWJ x \p{Extended_Pictographic}
    if (prev == GBP::ZWJ && emoji_run_at_prev && curr == GBP::ExtendedPictographic)
    {
        return false;
    }
    // GB12/GB13: sot (RI RI)* RI x RI — no break on the first RI of a pair.
    if (prev == GBP::RegionalIndicator && curr == GBP::RegionalIndicator && (ri_run_len_at_prev % 2) == 1)
    {
        return false;
    }
    // GB999: Any ÷ Any
    return true;
}

}  // namespace

usize GraphemeClusterIterator::FindNextBoundary(usize start) const
{
    usize offset = start;
    DecodedCodepoint first = DecodeUtf8At(sv_.Data(), sv_.Length(), offset, nullptr);
    GraphemeBreakProperty prev = LookupGraphemeBreakProperty(first.codepoint);
    usize ri_run = (prev == GraphemeBreakProperty::RegionalIndicator) ? 1 : 0;
    bool emoji_run = (prev == GraphemeBreakProperty::ExtendedPictographic);
    offset += first.byte_length;

    while (offset < sv_.Length())
    {
        DecodedCodepoint dc = DecodeUtf8At(sv_.Data(), sv_.Length(), offset, nullptr);
        GraphemeBreakProperty curr = LookupGraphemeBreakProperty(dc.codepoint);
        if (BreakBetween(prev, curr, ri_run, emoji_run))
        {
            break;
        }
        ri_run = (curr == GraphemeBreakProperty::RegionalIndicator) ? ri_run + 1 : 0;
        emoji_run = (curr == GraphemeBreakProperty::ExtendedPictographic) ||
                    ((curr == GraphemeBreakProperty::Extend || curr == GraphemeBreakProperty::ZWJ) && emoji_run);
        prev = curr;
        offset += dc.byte_length;
    }
    return offset;
}

usize GraphemeClusterCount(StringView sv)
{
    usize count = 0;
    for (StringView cluster : GraphemeClusters(sv))
    {
        (void)cluster;
        ++count;
    }
    return count;
}

}  // namespace engine_placeholder
