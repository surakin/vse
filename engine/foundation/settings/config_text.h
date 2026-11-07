#pragma once

//! The settings text format and its document model (no I/O).
//!
//! ```
//! # a comment on its own line      ; this style works too
//! version = 1
//! [renderer]                       # the section: keys below it are renderer.<name>
//! present_mode = "mailbox"         # a trailing comment
//! width = 1920
//! ```
//!
//! A file is lines. A blank line, or a line whose first non-blank character is `#` or `;`, is a comment. `[a]` or `[a.b]` starts a section;
//! `name = value` sets the key `<section>.<name>` (before any section the name is the whole, possibly dotted, key). A value is a quoted
//! text (`"..."` with the escapes `\"`, `\\`, `\n`, `\t`, `\xHH`) or a bare run of characters that ends at the end of the line or at a blank
//! followed by `#` or `;` (so a bare value cannot start with `#` or `;`: quote it). Lines are at most 4096 bytes and the file is UTF-8 (a byte order mark is accepted and kept).
//!
//! A [[settings::ConfigDocument]] keeps every line as written, so changing a value rewrites only the value's own bytes, a new key goes
//! at the end of its section, and a document that is not changed serializes to the bytes it was parsed from, including line endings,
//! comments, spacing and lines it could not understand (those are reported as errors and never touched).

#include "containers/vector.h"
#include "settings/key.h"
#include "string/string.h"
#include "types.h"

namespace engine_placeholder::settings {

/// The most bytes in one line of a settings file.
inline constexpr usize kMaxConfigLineBytes = 4096;
/// The most problems of each kind a [[settings::ConfigReport]] records (the count goes on past it).
inline constexpr u32 kMaxConfigProblems = 20;
/// The most bytes in a problem's message.
inline constexpr usize kMaxProblemMessageBytes = 80;

/// A problem found while parsing: a 1-based line and column and a message.
///
/// Thread-safety: thread-compatible plain data.
struct ConfigProblem {
    /// The 1-based line.
    u32 line = 0;
    /// The 1-based byte column.
    u32 column = 0;
    /// What is wrong, NUL-terminated.
    char message[kMaxProblemMessageBytes + 1] = {};
};

/// What a parse found.
///
/// Thread-safety: thread-compatible plain data.
struct ConfigReport {
    /// The first errors (lines that were not understood and are kept verbatim).
    ConfigProblem errors[kMaxConfigProblems];
    /// How many errors there were in all (it may exceed [[settings::kMaxConfigProblems]]).
    u32 error_count = 0;
    /// The first warnings (a repeated key: the last one wins).
    ConfigProblem warnings[kMaxConfigProblems];
    /// How many warnings there were in all.
    u32 warning_count = 0;
};

/// A settings file as a list of lines that can be queried and edited in place.
///
/// Thread-safety: thread-compatible.
/// Ownership: owns its lines.
class ConfigDocument {
public:
    /// Creates an empty document.
    ConfigDocument();
    /// Moves a document.
    ConfigDocument(ConfigDocument&&) noexcept;
    /// Moves a document.
    ConfigDocument& operator=(ConfigDocument&&) noexcept;
    /// Destroys the document.
    ~ConfigDocument();

    /// Replaces the contents by parsing `text`. A line that cannot be understood is kept verbatim and reported; the rest is read.
    ///
    /// Param text: the file's bytes.
    /// Param report: receives the problems; may be null.
    /// Fails: reports problems in `report`; never fails as a whole.
    void Parse(StringView text, ConfigReport* report);

    /// Finds the value of `key` (the last entry for it wins). The view is the value as written: a quoted text keeps its quotes.
    ///
    /// Param key: a valid key.
    /// Param value: receives a view valid until the document is changed.
    /// Returns: whether the key is present.
    bool Find(StringView key, StringView* value) const;

    /// Sets the value of `key`: the last entry for it is edited in place (its trailing comment stays); otherwise a new line goes at the end of
    /// the key's section (the section is added if it is missing).
    ///
    /// Param key: a valid key.
    /// Param value: the value as it will be written; see [[settings::ConfigDocument::IsValidValue]].
    /// Returns: false (and nothing changes) if `value` is not a valid value text.
    bool Set(StringView key, StringView value);

    /// Removes every entry line for `key`. Comments around them stay: only a person writes comments.
    ///
    /// Returns: whether anything was removed.
    bool Remove(StringView key);

    /// Appends the keys under `prefix` (equal to it, or starting with `prefix.`; empty means all) in file order, each once.
    void Keys(StringView prefix, Vector<Key>* out) const;

    /// Deletes the lines [[settings::ConfigDocument::Parse]] reported as errors (kept verbatim until now). Used when a damaged file is
    /// deliberately replaced.
    void RemoveUnreadable();

    /// Returns whether `value` can be written as a value: no line break, no blanks at either end, at most 4000 bytes, a well-formed quoted
    /// text if it starts with `"`, and otherwise no blank followed by `#` or `;`.
    static bool IsValidValue(StringView value);

    /// Writes the document back as bytes (the byte order mark, if there was one, included).
    ///
    /// Param skip_unreadable: leave out the lines [[settings::ConfigDocument::Parse]] reported as errors.
    void Serialize(Vector<char>* out, bool skip_unreadable = false) const;

    /// Returns the number of lines.
    usize LineCount() const { return lines_.Size(); }

private:
    struct Line;
    Vector<Line> lines_;
    bool bom_ = false;
    u8 eol_ = 1; // 1 = "\n", 2 = "\r\n": what a new line ends with
    usize FindLast(const Key& key) const;
    void AppendLine(StringView text);
};

} // namespace engine_placeholder::settings
