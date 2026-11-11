#pragma once

//! [[database::SqlText]]: the only type the database interface accepts as SQL, constructible only from a string
//! literal.

#include "string_view.h"
#include "types.h"

namespace engine_placeholder::database
{

/// SQL text fixed at compile time. `Connection::Prepare` and `Execute` take nothing else, so SQL cannot be built from
/// data by concatenation or formatting: the constructor is `consteval`, and a pointer that is not a compile-time
/// constant does not compile. Data reaches a statement only through bound parameters.
///
/// A backend that must generate a statement from a validated identifier (a savepoint name) uses
/// [[database::SqlText::Unchecked]]; the gate `database_sql_check` allows that only under `engine/database/` and in
/// tests.
///
/// - Thread-safety: immutable value.
/// - Ownership: borrows the literal, which lives for the whole program.
class SqlText
{
public:
    /// Wraps a string literal (or any other constant-evaluated `const char*`).
    ///
    /// - Param literal: NUL-terminated text known at compile time.
    consteval SqlText(const char* literal) : text_(literal, Measure(literal)) {}

    /// Wraps text that is not a literal. Not for engine or game code: see the class comment.
    ///
    /// - Param text: the SQL; must outlive every use of the result.
    /// - Ownership: borrows `text`.
    static SqlText Unchecked(StringView text) { return SqlText(text); }

    /// The SQL text.
    StringView View() const { return text_; }

private:
    explicit SqlText(StringView text) : text_(text) {}
    static constexpr usize Measure(const char* text)
    {
        usize n = 0;
        while (text[n] != '\0')
        {
            ++n;
        }
        return n;
    }
    StringView text_;
};

}  // namespace engine_placeholder::database
