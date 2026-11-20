#pragma once

#include "string/string.h"
#include "types.h"
#include "string/utf8_codec.h"

//! UTF-8 helpers on StringView: codepoint counting, validation and iteration.
namespace vse
{

/// Counts the Unicode codepoints in `sv`.
///
/// Each malformed byte sequence counts as one (replacement) codepoint, so the
/// count is well defined for invalid input.
///
/// - Param sv: UTF-8 text.
/// - Returns: the number of codepoints.
inline usize CodepointCount(StringView sv)
{
    usize count = 0;
    usize offset = 0;
    while (offset < sv.Length())
    {
        DecodedCodepoint dc = DecodeUtf8At(sv.Data(), sv.Length(), offset, nullptr);
        offset += dc.byte_length;
        ++count;
    }
    return count;
}

/// Reports whether `sv` is entirely well-formed UTF-8.
///
/// - Param sv: text to check.
/// - Returns: true if every sequence decodes validly, false at the first
///   malformed one.
inline bool IsValidUtf8(StringView sv)
{
    usize offset = 0;
    while (offset < sv.Length())
    {
        bool valid = false;
        DecodedCodepoint dc = DecodeUtf8At(sv.Data(), sv.Length(), offset, &valid);
        if (!valid)
        {
            return false;
        }
        offset += dc.byte_length;
    }
    return true;
}

/// Forward iterator over the codepoints of a UTF-8 [[StringView]].
///
/// Malformed bytes are yielded as U+FFFD, one per bad byte. Obtain iterators
/// from [[Utf8Codepoints]] rather than constructing them directly.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: holds a view; the underlying text must outlive the iterator.
class Utf8CodepointIterator
{
public:
    /// Creates an iterator positioned at byte `offset` of `sv`.
    ///
    /// - Param sv: text to iterate.
    /// - Param offset: byte index of a codepoint start, or `sv.Length()` for the end.
    Utf8CodepointIterator(StringView sv, usize offset) : sv_(sv), offset_(offset) {}

    /// Returns the codepoint at the current position; must not be called at the end.
    u32 operator*() const
    {
        DecodedCodepoint dc = DecodeUtf8At(sv_.Data(), sv_.Length(), offset_, nullptr);
        return dc.codepoint;
    }

    /// Advances past the current codepoint and returns this iterator.
    Utf8CodepointIterator& operator++()
    {
        DecodedCodepoint dc = DecodeUtf8At(sv_.Data(), sv_.Length(), offset_, nullptr);
        offset_ += dc.byte_length;
        return *this;
    }

    /// Returns true if the two iterators are at different byte offsets.
    bool operator!=(const Utf8CodepointIterator& other) const { return offset_ != other.offset_; }

private:
    StringView sv_;
    usize offset_;
};

/// Range adaptor that makes the codepoints of a string usable in a range-for loop.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: holds a view; the underlying text must outlive the range.
class Utf8CodepointRange
{
public:
    /// Creates a range over `sv`.
    ///
    /// - Param sv: UTF-8 text to iterate.
    explicit Utf8CodepointRange(StringView sv) : sv_(sv) {}

    /// Returns an iterator at the first codepoint.
    Utf8CodepointIterator begin() const { return Utf8CodepointIterator(sv_, 0); }
    /// Returns the past-the-end iterator.
    Utf8CodepointIterator end() const { return Utf8CodepointIterator(sv_, sv_.Length()); }

private:
    StringView sv_;
};

/// Returns a range over the codepoints of `sv`, for use in a range-for loop.
///
/// - Param sv: UTF-8 text; must outlive the returned range.
inline Utf8CodepointRange Utf8Codepoints(StringView sv)
{
    return Utf8CodepointRange(sv);
}

}  // namespace vse
