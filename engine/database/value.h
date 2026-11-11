#pragma once

//! Typed database values: [[database::ValueView]] (borrowed) and [[database::Value]] (owning).

#include "containers/span.h"
#include "containers/vector.h"
#include "string_view.h"
#include "types.h"

namespace engine_placeholder::database {

/// The five storage classes of a value.
enum class ValueType : u8 {
    Null,    ///< No value (SQL `NULL`).
    Integer, ///< A signed 64-bit integer.
    Real,    ///< An IEEE 754 binary64 number.
    Text,    ///< UTF-8 text with an explicit length (embedded NUL bytes are kept).
    Blob,    ///< A sequence of bytes. An empty blob is a blob, not null.
};

/// Returns a stable lower-case name for `type` (`"integer"`); never null.
///
/// Ownership: the string is static.
/// Fails: never.
const char* ValueTypeName(ValueType type);

/// A value that borrows its text or bytes: what a statement binds and what a column getter returns.
///
/// Two views are equal when their types match and the contents are bitwise equal (a real compares by its bit pattern, so `-0` differs from `0` and
/// a NaN equals the same NaN). There are no implicit conversions between types: `Integer(1)` and `Real(1.0)` differ.
///
/// Thread-safety: immutable value.
/// Ownership: borrows the text or bytes; they must outlive the view.
class ValueView {
public:
    /// Creates a null view.
    constexpr ValueView() = default;

    /// Returns a null view.
    static constexpr ValueView Null() { return ValueView(); }
    /// Returns an integer view.
    static constexpr ValueView Integer(i64 value) {
        ValueView v;
        v.type_ = ValueType::Integer;
        v.integer_ = value;
        return v;
    }
    /// Returns a real view.
    static constexpr ValueView Real(f64 value) {
        ValueView v;
        v.type_ = ValueType::Real;
        v.real_ = value;
        return v;
    }
    /// Returns a text view over `text` (an empty string is empty text, not null).
    static constexpr ValueView Text(StringView text) {
        ValueView v;
        v.type_ = ValueType::Text;
        v.text_ = text;
        return v;
    }
    /// Returns a blob view over `bytes` (an empty span is an empty blob, not null).
    static constexpr ValueView Blob(ByteSpan bytes) {
        ValueView v;
        v.type_ = ValueType::Blob;
        v.bytes_ = bytes;
        return v;
    }

    /// The type of the value.
    constexpr ValueType Type() const { return type_; }
    /// Returns true for a null value.
    /// Fails: never.
    constexpr bool IsNull() const { return type_ == ValueType::Null; }
    /// The integer. Aborts: the type is not `Integer`.
    i64 AsInteger() const;
    /// The real. Aborts: the type is not `Real`.
    f64 AsReal() const;
    /// The text. Aborts: the type is not `Text`.
    StringView AsText() const;
    /// The bytes. Aborts: the type is not `Blob`.
    ByteSpan AsBlob() const;

    /// Returns true when both have the same type and bitwise-equal contents.
    bool operator==(const ValueView& other) const;
    /// Returns true when `*this == other` is false.
    bool operator!=(const ValueView& other) const { return !(*this == other); }

private:
    ValueType type_ = ValueType::Null;
    i64 integer_ = 0;
    f64 real_ = 0.0;
    StringView text_;
    ByteSpan bytes_;
};

/// A value that owns its text or bytes, for results that must outlive the statement step that produced them.
///
/// Thread-safety: not-thread-safe; synchronize externally if shared.
/// Ownership: owns a copy of the text or bytes.
class Value {
public:
    /// Creates a null value.
    Value() = default;

    /// Returns a null value.
    static Value Null() { return Value(); }
    /// Returns an integer value.
    static Value Integer(i64 value);
    /// Returns a real value.
    static Value Real(f64 value);
    /// Returns a text value holding a copy of `text`.
    static Value Text(StringView text);
    /// Returns a blob value holding a copy of `bytes`.
    static Value Blob(ByteSpan bytes);
    /// Returns an owning copy of `view`.
    static Value From(const ValueView& view);

    /// A view of this value, valid while this value is alive and unchanged.
    ValueView View() const;
    /// The type of the value.
    ValueType Type() const { return type_; }
    /// Returns true when both views are equal ([[database::ValueView::operator==]]).
    bool operator==(const Value& other) const { return View() == other.View(); }

private:
    ValueType type_ = ValueType::Null;
    i64 integer_ = 0;
    f64 real_ = 0.0;
    Vector<byte> bytes_;
};

} // namespace engine_placeholder::database
