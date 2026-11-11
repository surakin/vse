#pragma once

//! Setting keys: validated dotted names such as `renderer.present_mode`.
//!
//! A key has one to three segments separated by `.`; a segment is a lowercase letter followed by lowercase letters, digits and
//! `_`, at most 31 bytes; the whole key is at most 95 bytes. In a text file the key `renderer.width` is the name `width` in the
//! section `[renderer]`, and `a.b.c` is `c` in `[a.b]`; [[settings::Key::Section]] and [[settings::Key::Name]] give that split.

#include "string/string.h"
#include "types.h"

namespace engine_placeholder::settings {

/// The most bytes in a key.
inline constexpr usize kMaxKeyBytes = 95;
/// The most bytes in one segment of a key.
inline constexpr usize kMaxSegmentBytes = 31;
/// The most segments in a key.
inline constexpr u32 kMaxKeySegments = 3;

/// Why a key was refused.
enum class KeyError : u8 {
    None = 0,         ///< The key is valid.
    Empty,            ///< No characters.
    TooLong,          ///< More than 95 bytes.
    BadCharacter,     ///< A character other than a lowercase letter, digit, `_` or `.`, or a segment that does not start with a letter.
    EmptySegment,     ///< Two dots in a row, or a dot at either end.
    TooManySegments,  ///< More than three segments.
    SegmentTooLong,   ///< A segment of more than 31 bytes.
};

/// Returns a short name for a key error (`none`, `empty`, `too_long`, `bad_character`, `empty_segment`, `too_many_segments`, `segment_too_long`).
///
/// - Returns: a static string.
/// - Fails: never.
/// - Ownership: the string is static and never freed.
const char* KeyErrorName(KeyError error);

/// A validated setting key, stored inline (no allocation).
///
/// - Thread-safety: thread-compatible plain data; a const key may be read from many threads.
/// - Ownership: a value; owns its bytes.
class Key {
public:
    /// Creates the empty, invalid key.
    Key() = default;

    /// Checks `text` and, if it is a valid key, stores it in `*out`.
    ///
    /// - Param text: the candidate.
    /// - Param out: receives the key; unchanged on failure. May be null to only check.
    /// - Returns: `KeyError::None` or the first problem found.
    /// - Fails: reports the reason as the return value.
    static KeyError Parse(StringView text, Key* out);

    /// Returns the key's text.
    StringView View() const { return StringView(text_, length_); }
    /// Returns whether this holds a key (a default-constructed one does not).
    bool IsValid() const { return length_ != 0; }
    /// Returns the number of segments (0 for the invalid key).
    u32 Segments() const;
    /// Returns everything before the last `.` (the file section), or an empty view for a one-segment key.
    StringView Section() const;
    /// Returns the last segment (the name within the section).
    StringView Name() const;
    /// Returns a hash of the key's bytes.
    usize Hash() const;

    /// Returns whether two keys are the same text.
    bool operator==(const Key& other) const { return View() == other.View(); }
    /// Returns whether two keys differ.
    bool operator!=(const Key& other) const { return !(*this == other); }
    /// Orders keys bytewise.
    bool operator<(const Key& other) const { return View().Compare(other.View()) < 0; }

private:
    char text_[kMaxKeyBytes + 1] = {};
    u8 length_ = 0;
};

} // namespace engine_placeholder::settings
