#pragma once

//! The declaration of one setting, and the text form of its values.

#include "settings/value.h"
#include "string/string.h"
#include "types.h"

namespace engine_placeholder::settings {

/// What a number outside its range does.
enum class RangePolicy : u8 {
    Clamp = 0, ///< The value moves to the nearest end of the range and the call reports `ValueError::Clamped`.
    Reject,    ///< The value is refused and the call reports `ValueError::OutOfRange`.
};

/// Flags of a [[settings::SettingDesc]].
enum SettingFlags : u32 {
    kRestartRequired = 1u << 0, ///< A change takes effect only after a restart (a UI shows a note).
    kAdvanced = 1u << 1,        ///< Hidden from a basic settings screen.
    kHidden = 1u << 2,          ///< Never shown in a settings screen or a generated help list.
};

/// The most names in an enum.
inline constexpr u32 kMaxEnumNames = 32;
/// The most deprecated keys of one setting.
inline constexpr u32 kMaxDeprecatedNames = 8;
/// The most bytes in one enum name.
inline constexpr usize kMaxEnumNameBytes = 31;

/// The declaration of one setting. Plain data; the strings are borrowed and must outlive the registry (use string literals).
///
/// Thread-safety: thread-compatible plain data.
/// Ownership: borrows every string.
struct SettingDesc {
    /// The setting's key, for example `renderer.present_mode`; see [[settings::Key]].
    const char* key = nullptr;
    /// The kind of value; `default_value` must have this type.
    Type type = Type::Bool;
    /// The default.
    Value default_value;
    /// The smallest `Int`.
    i64 min_int = 0;
    /// The largest `Int`.
    i64 max_int = 0;
    /// The smallest `Float`.
    f64 min_float = 0;
    /// The largest `Float`.
    f64 max_float = 0;
    /// What an out-of-range number does.
    RangePolicy policy = RangePolicy::Clamp;
    /// The names of an `Enum`'s values, in index order (1 to 32 names).
    const char* const* enum_names = nullptr;
    /// How many names `enum_names` holds.
    u32 enum_count = 0;
    /// What a change costs, as a number the owner of the setting interprets (the renderer's `CostClass`); it also names the owner's table.
    u8 apply_class = 0;
    /// A short name for a settings screen, or null.
    const char* label = nullptr;
    /// A sentence for `--help` and tooltips, or null.
    const char* description = nullptr;
    /// A group name for a settings screen, or null.
    const char* group = nullptr;
    /// The capability the setting needs, or null if it always works.
    const char* capability = nullptr;
    /// The environment variable that supplies the setting (Day 134), or null.
    const char* env = nullptr;
    /// A command-line flag that sets it, without the leading dashes (Day 138), or null.
    const char* flag = nullptr;
    /// Older keys this setting was known by (Day 136); at most 8.
    const char* const* deprecated_keys = nullptr;
    /// How many names `deprecated_keys` holds.
    u32 deprecated_count = 0;
    /// A combination of [[settings::SettingFlags]].
    u32 flags = 0;
};

/// Builds a `Bool` declaration.
SettingDesc MakeBool(const char* key, bool def);
/// Builds an `Int` declaration with the range `min` to `max`.
SettingDesc MakeInt(const char* key, i64 def, i64 min, i64 max, RangePolicy policy = RangePolicy::Clamp);
/// Builds a `Float` declaration with the range `min` to `max`.
SettingDesc MakeFloat(const char* key, f64 def, f64 min, f64 max, RangePolicy policy = RangePolicy::Clamp);
/// Builds an `Enum` declaration; `def` is an index into `names`.
SettingDesc MakeEnum(const char* key, const char* const* names, u32 count, u32 def);
/// Builds a `Text` declaration.
SettingDesc MakeText(const char* key, const char* def);

/// Why a value was not accepted, or a note about an accepted one.
enum class ValueError : u8 {
    None = 0,   ///< The text parsed to a value inside the range.
    Clamped,    ///< Accepted, but moved to the nearest end of the range (`RangePolicy::Clamp`).
    Empty,      ///< No text where a value is needed.
    BadBool,    ///< Not `true`, `false`, `yes`, `no`, `on`, `off`, `1` or `0`.
    BadNumber,  ///< Not a number, a number with trailing characters, an overflowing integer, NaN or infinity.
    OutOfRange, ///< A number outside the range (`RangePolicy::Reject`).
    NotInEnum,  ///< Not one of the enum's names.
    TooLong,    ///< Text of more than 511 bytes.
    BadEscape,  ///< An unknown escape or an unterminated quoted text.
};

/// Returns a short name for a value error (`none`, `clamped`, `empty`, `bad_bool`, `bad_number`, `out_of_range`, `not_in_enum`, `too_long`, `bad_escape`).
///
/// Returns: a static string.
/// Fails: never.
/// Ownership: the string is static and never freed.
const char* ValueErrorName(ValueError error);

/// Checks a typed value against its declaration: a number outside the range is clamped or refused by the declaration's policy, an enum
/// index outside the names is refused, a value of another type is refused.
///
/// Param desc: the declaration.
/// Param value: the candidate.
/// Param out: receives the (possibly clamped) value; unchanged on failure.
/// Returns: `None`, `Clamped`, `OutOfRange`, `NotInEnum`, or `BadNumber` for a value of another type.
/// Fails: reports the reason as the return value.
ValueError ValidateValue(const SettingDesc& desc, const Value& value, Value* out);

/// The most bytes [[settings::FormatValue]] writes, including the terminator.
inline constexpr usize kMaxValueTextBytes = 2100;

/// Parses the text form of a value for the setting `desc` declares.
///
/// The forms: `true`/`false` (also `yes`, `no`, `on`, `off`, `1`, `0`) for a bool; a decimal or `0x` hexadecimal integer; a
/// decimal float with an optional exponent; an enum name; text, bare or double-quoted with the escapes `\"`, `\\`, `\n`, `\t` and `\xHH`.
///
/// Param desc: the declaration; its range, policy and enum names apply.
/// Param text: the value text, without surrounding blanks.
/// Param out: receives the value; unchanged on failure.
/// Returns: `None` or `Clamped` (the value was stored), or the reason it was refused.
/// Fails: reports the reason as the return value; never aborts.
ValueError ParseValue(const SettingDesc& desc, StringView text, Value* out);

/// Writes the canonical text of `value` for the setting `desc` declares: `true`/`false`; a decimal integer; the shortest decimal float
/// that reads back as the same number (always with a `.` or an exponent); an enum name; text in double quotes with escapes. Reading it
/// back with [[settings::ParseValue]] gives an equal value.
///
/// Param value: a value of the type `desc` declares.
/// Param out: destination; at least [[settings::kMaxValueTextBytes]] bytes recommended.
/// Param capacity: size of `out`.
/// Returns: the number of bytes written, excluding the terminator; 0 if `capacity` is too small (then `out[0]` is NUL when `capacity` is not 0).
/// Fails: returns 0 when the buffer is too small.
/// Aborts: the value's type differs from `desc.type`, or an enum index is outside the names.
usize FormatValue(const SettingDesc& desc, const Value& value, char* out, usize capacity);

} // namespace engine_placeholder::settings
