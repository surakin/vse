#pragma once

#include "string_view.h"
#include "types.h"

//! Word boundaries for editing widgets (Phase 15, Day 197): a word is a run of letters, digits and underscore; a run of punctuation is a word of its own; white space separates.
//!
//! The classes come from code point ranges, not a full Unicode table (the foundation has none): ASCII, the Latin-1 and general punctuation blocks, the CJK and full-width
//! punctuation and the usual spaces are told apart; every other code point counts as a letter. Offsets are byte offsets into UTF-8 text and fall on code point starts; a caller
//! that must stay on grapheme boundaries snaps them (the text field does, through `text/cursor.h`).

namespace engine_placeholder::ui {

/// What kind of character a code point is for word movement.
enum class CharClass : u8 {
    Space, ///< White space and zero-width separators.
    Word,  ///< Letters, digits, underscore and everything not listed as another class.
    Punct, ///< Punctuation and symbols of the listed blocks.
};

/// Returns the class of a code point.
/// - Param codepoint: a Unicode scalar value.
/// - Returns: its class.
/// - Fails: never.
CharClass ClassOf(u32 codepoint);

/// Returns where Ctrl+Left goes from `offset`: the start of the word before it (white space before `offset` is skipped first); 0 at the start.
/// - Param text: UTF-8 text.
/// - Param offset: a byte offset on a code point start; clamped to the text.
/// - Returns: a byte offset on a code point start.
/// - Fails: never.
u32 WordStart(StringView text, u32 offset);

/// Returns where Ctrl+Right goes from `offset`: the end of the word after it (white space at `offset` is skipped first); the length at the end.
/// - Param text: UTF-8 text.
/// - Param offset: a byte offset on a code point start; clamped to the text.
/// - Returns: a byte offset on a code point start.
/// - Fails: never.
u32 WordEnd(StringView text, u32 offset);

/// Finds the run of characters of one class around `offset`, what a double click selects.
/// - Param text: UTF-8 text.
/// - Param offset: a byte offset; the run is the one that starts at it, or at the end of the text the last one.
/// - Param begin: receives the start of the run.
/// - Param end: receives the end of the run.
/// - Ownership: writes through `begin` and `end`, which must be non-null.
/// - Fails: never; an empty text gives 0 and 0.
void WordRangeAt(StringView text, u32 offset, u32* begin, u32* end);

} // namespace engine_placeholder::ui
