#pragma once

#include "string/string.h"
#include "types.h"
#include "string/utf8.h"

// UAX #29 grapheme cluster segmentation. Depends on Day 10's utf8.h
// (Utf8Codepoints) and, through it, string.h/utf8_codec.h.
namespace engine_placeholder {

// Grapheme-Cluster-Break property values (UAX #29), plus
// Extended_Pictographic from emoji-data.txt folded into the same enum —
// see the file-split rationale in
// plans/phase-00-foundation/day-11-grapheme-clusters.md. `Other` is the
// default for any codepoint not explicitly listed in the generated table.
enum class GraphemeBreakProperty : u8 {
    Other,
    CR,
    LF,
    Control,
    Extend,
    ZWJ,
    RegionalIndicator,
    Prepend,
    SpacingMark,
    L,
    V,
    T,
    LV,
    LVT,
    ExtendedPictographic,
};

// Binary search over the generated table
// (engine/foundation/generated/grapheme_break_table.cpp).
GraphemeBreakProperty LookupGraphemeBreakProperty(u32 codepoint);

class GraphemeClusterIterator {
public:
    GraphemeClusterIterator(StringView sv, usize offset) : sv_(sv), offset_(offset) {
        if (offset_ < sv_.Length()) {
            cluster_end_ = FindNextBoundary(offset_);
        }
    }

    StringView operator*() const { return sv_.Substring(offset_, cluster_end_ - offset_); }

    GraphemeClusterIterator& operator++() {
        offset_ = cluster_end_;
        if (offset_ < sv_.Length()) {
            cluster_end_ = FindNextBoundary(offset_);
        }
        return *this;
    }

    bool operator!=(const GraphemeClusterIterator& other) const { return offset_ != other.offset_; }

private:
    // Returns the byte offset of the first boundary at or after `start`
    // (i.e. the end of the cluster beginning at `start`).
    usize FindNextBoundary(usize start) const;

    StringView sv_;
    usize offset_;
    usize cluster_end_ = 0;
};

class GraphemeClusterRange {
public:
    explicit GraphemeClusterRange(StringView sv) : sv_(sv) {}

    GraphemeClusterIterator begin() const { return GraphemeClusterIterator(sv_, 0); }
    GraphemeClusterIterator end() const { return GraphemeClusterIterator(sv_, sv_.Length()); }

private:
    StringView sv_;
};

inline GraphemeClusterRange GraphemeClusters(StringView sv) {
    return GraphemeClusterRange(sv);
}

usize GraphemeClusterCount(StringView sv);

} // namespace engine_placeholder
