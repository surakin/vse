#pragma once

#include "string/string.h"
#include "types.h"
#include "string/utf8.h"

//! Grapheme cluster segmentation (UAX #29): iterate or count user-perceived
//! characters in UTF-8 text.
namespace engine_placeholder {

/// Grapheme_Cluster_Break property value of a codepoint (UAX #29), with
/// Extended_Pictographic from the emoji data folded into the same enum.
enum class GraphemeBreakProperty : u8 {
    Other,                ///< Any codepoint not listed in the property table.
    CR,                   ///< Carriage return, U+000D.
    LF,                   ///< Line feed, U+000A.
    Control,              ///< Control and format characters that always break.
    Extend,               ///< Combining marks and other extenders.
    ZWJ,                  ///< Zero width joiner, U+200D.
    RegionalIndicator,    ///< Regional indicator symbols, paired into flags.
    Prepend,              ///< Characters that join to the following one.
    SpacingMark,          ///< Spacing combining marks.
    L,                    ///< Hangul leading consonant jamo.
    V,                    ///< Hangul vowel jamo.
    T,                    ///< Hangul trailing consonant jamo.
    LV,                   ///< Precomposed Hangul syllable with no trailing consonant.
    LVT,                  ///< Precomposed Hangul syllable with a trailing consonant.
    ExtendedPictographic, ///< Emoji and other pictographic symbols.
};

/// Looks up the grapheme-break property of `codepoint` with a binary search over
/// the generated table.
///
/// - Param codepoint: Unicode scalar value.
/// - Returns: its property, or `GraphemeBreakProperty::Other` if it is not listed.
GraphemeBreakProperty LookupGraphemeBreakProperty(u32 codepoint);

/// Forward iterator over the grapheme clusters of a UTF-8 [[StringView]].
///
/// Each element is a view of one cluster. Obtain iterators from
/// [[GraphemeClusters]] rather than constructing them directly.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: holds a view; the underlying text must outlive the iterator and
///   the cluster views it yields.
class GraphemeClusterIterator {
public:
    /// Creates an iterator positioned at byte `offset` of `sv`.
    ///
    /// - Param sv: text to iterate.
    /// - Param offset: byte index of a cluster start, or `sv.Length()` for the end.
    GraphemeClusterIterator(StringView sv, usize offset) : sv_(sv), offset_(offset) {
        if (offset_ < sv_.Length()) {
            cluster_end_ = FindNextBoundary(offset_);
        }
    }

    /// Returns a view of the current cluster; must not be called at the end.
    StringView operator*() const { return sv_.Substring(offset_, cluster_end_ - offset_); }

    /// Advances to the next cluster and returns this iterator.
    GraphemeClusterIterator& operator++() {
        offset_ = cluster_end_;
        if (offset_ < sv_.Length()) {
            cluster_end_ = FindNextBoundary(offset_);
        }
        return *this;
    }

    /// Returns true if the two iterators are at different byte offsets.
    bool operator!=(const GraphemeClusterIterator& other) const { return offset_ != other.offset_; }

private:
    // Returns the byte offset of the first boundary at or after `start`
    // (i.e. the end of the cluster beginning at `start`).
    usize FindNextBoundary(usize start) const;

    StringView sv_;
    usize offset_;
    usize cluster_end_ = 0;
};

/// Range adaptor that makes the grapheme clusters of a string usable in a range-for loop.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: holds a view; the underlying text must outlive the range.
class GraphemeClusterRange {
public:
    /// Creates a range over `sv`.
    ///
    /// - Param sv: UTF-8 text to segment.
    explicit GraphemeClusterRange(StringView sv) : sv_(sv) {}

    /// Returns an iterator at the first cluster.
    GraphemeClusterIterator begin() const { return GraphemeClusterIterator(sv_, 0); }
    /// Returns the past-the-end iterator.
    GraphemeClusterIterator end() const { return GraphemeClusterIterator(sv_, sv_.Length()); }

private:
    StringView sv_;
};

/// Returns a range over the grapheme clusters of `sv`, for use in a range-for loop.
///
/// - Param sv: UTF-8 text; must outlive the returned range.
inline GraphemeClusterRange GraphemeClusters(StringView sv) {
    return GraphemeClusterRange(sv);
}

/// Counts the grapheme clusters in `sv`.
///
/// - Param sv: UTF-8 text.
/// - Returns: the number of clusters; 0 for empty text.
usize GraphemeClusterCount(StringView sv);

} // namespace engine_placeholder
