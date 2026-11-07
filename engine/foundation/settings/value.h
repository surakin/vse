#pragma once

//! A setting's value: a small tagged value that is cheap to copy and owns no heap memory.

#include "string/string.h"
#include "types.h"

namespace engine_placeholder::settings {

/// The most bytes (excluding the terminator) in a text value.
inline constexpr usize kMaxTextBytes = 511;

/// The kind of value a setting holds.
enum class Type : u8 {
    Bool = 0, ///< True or false.
    Int,      ///< A whole number (`i64`) within a range.
    Float,    ///< A finite `f64` within a range.
    Enum,     ///< One of a fixed list of names, held as an index.
    Text,     ///< UTF-8 text of at most 511 bytes.
};

/// Returns a type's name (`bool`, `int`, `float`, `enum`, `text`).
///
/// Returns: a static string.
/// Fails: never.
/// Ownership: the string is static and never freed.
const char* TypeName(Type type);

/// A typed value. Trivially copyable; about 540 bytes because it holds a text inline.
///
/// Thread-safety: thread-compatible plain data.
/// Ownership: a value; owns its bytes.
class Value {
public:
    /// Creates `false`.
    Value() = default;

    /// Makes a `Bool`.
    static Value FromBool(bool b);
    /// Makes an `Int`.
    static Value FromInt(i64 i);
    /// Makes a `Float`.
    ///
    /// Aborts: `f` is not finite (NaN and infinity are never values; the text parser refuses them).
    static Value FromFloat(f64 f);
    /// Makes an `Enum` holding an index into the setting's names.
    static Value FromEnum(u32 index);
    /// Makes a `Text`.
    ///
    /// Aborts: `text` is longer than [[settings::kMaxTextBytes]].
    static Value FromText(StringView text);

    /// Returns the type.
    Type GetType() const { return type_; }
    /// Returns the bool; the type must be `Bool`.
    ///
    /// Aborts: the type is not `Bool`.
    bool AsBool() const;
    /// Returns the integer; the type must be `Int`.
    ///
    /// Aborts: the type is not `Int`.
    i64 AsInt() const;
    /// Returns the float; the type must be `Float`.
    ///
    /// Aborts: the type is not `Float`.
    f64 AsFloat() const;
    /// Returns the enum index; the type must be `Enum`.
    ///
    /// Aborts: the type is not `Enum`.
    u32 AsEnum() const;
    /// Returns the text; the type must be `Text`. Valid while this value lives and is unchanged.
    ///
    /// Aborts: the type is not `Text`.
    StringView AsText() const;

    /// Returns whether two values have the same type and content (floats compare bitwise, so `-0.0` differs from `0.0`).
    bool operator==(const Value& other) const;
    /// Returns whether two values differ.
    bool operator!=(const Value& other) const { return !(*this == other); }

private:
    Type type_ = Type::Bool;
    u16 text_length_ = 0;
    i64 int_ = 0;
    f64 float_ = 0;
    char text_[kMaxTextBytes + 1] = {};
};

} // namespace engine_placeholder::settings
