#include "settings/desc.h"
#include "string/utf8.h"

#include "cstr/number.h"
#include "cstr/cstr.h"

#include "log/assert.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"

namespace engine_placeholder::settings {

SettingDesc MakeBool(const char* key, bool def) {
    SettingDesc d;
    d.key = key;
    d.type = Type::Bool;
    d.default_value = Value::FromBool(def);
    return d;
}

SettingDesc MakeInt(const char* key, i64 def, i64 min, i64 max, RangePolicy policy) {
    SettingDesc d;
    d.key = key;
    d.type = Type::Int;
    d.default_value = Value::FromInt(def);
    d.min_int = min;
    d.max_int = max;
    d.policy = policy;
    return d;
}

SettingDesc MakeFloat(const char* key, f64 def, f64 min, f64 max, RangePolicy policy) {
    SettingDesc d;
    d.key = key;
    d.type = Type::Float;
    d.default_value = Value::FromFloat(def);
    d.min_float = min;
    d.max_float = max;
    d.policy = policy;
    return d;
}

SettingDesc MakeEnum(const char* key, const char* const* names, u32 count, u32 def) {
    SettingDesc d;
    d.key = key;
    d.type = Type::Enum;
    d.default_value = Value::FromEnum(def);
    d.enum_names = names;
    d.enum_count = count;
    return d;
}

SettingDesc MakeText(const char* key, const char* def) {
    SettingDesc d;
    d.key = key;
    d.type = Type::Text;
    d.default_value = Value::FromText(def == nullptr ? StringView() : StringView(def));
    return d;
}

const char* ValueErrorName(ValueError error) {
    switch (error) {
    case ValueError::None: return "none";
    case ValueError::Clamped: return "clamped";
    case ValueError::Empty: return "empty";
    case ValueError::BadBool: return "bad_bool";
    case ValueError::BadNumber: return "bad_number";
    case ValueError::OutOfRange: return "out_of_range";
    case ValueError::NotInEnum: return "not_in_enum";
    case ValueError::TooLong: return "too_long";
    case ValueError::BadEscape: return "bad_escape";
    }
    return "unknown";
}

namespace {

bool Same(StringView a, const char* b) { return a == StringView(b); }

char Lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

bool SameIgnoringCase(StringView a, const char* b) {
    const usize n = CStrLength(b);
    if (a.Length() != n) return false;
    for (usize i = 0; i < n; ++i) {
        if (Lower(a[i]) != b[i]) return false;
    }
    return true;
}

int HexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

ValueError ParseInt(const SettingDesc& desc, StringView text, Value* out) {
    usize i = 0;
    bool negative = false;
    if (i < text.Length() && (text[i] == '-' || text[i] == '+')) {
        negative = text[i] == '-';
        ++i;
    }
    if (i >= text.Length()) return ValueError::BadNumber;
    u64 magnitude = 0;
    bool overflow = false;
    if (text.Length() - i > 2 && text[i] == '0' && (text[i + 1] == 'x' || text[i + 1] == 'X')) {
        for (i += 2; i < text.Length(); ++i) {
            const int d = HexDigit(text[i]);
            if (d < 0) return ValueError::BadNumber;
            if (magnitude > (~u64{0} >> 4)) overflow = true;
            magnitude = (magnitude << 4) | static_cast<u64>(d);
        }
    } else {
        for (; i < text.Length(); ++i) {
            if (text[i] < '0' || text[i] > '9') return ValueError::BadNumber;
            const u64 d = static_cast<u64>(text[i] - '0');
            if (magnitude > (~u64{0} - d) / 10) overflow = true;
            magnitude = magnitude * 10 + d;
        }
    }
    if (overflow) return ValueError::BadNumber;
    i64 value;
    if (negative) {
        if (magnitude > (u64{1} << 63)) return ValueError::BadNumber;
        value = magnitude == (u64{1} << 63) ? static_cast<i64>(-9223372036854775807ll - 1) : -static_cast<i64>(magnitude);
    } else {
        if (magnitude > static_cast<u64>(9223372036854775807ll)) return ValueError::BadNumber;
        value = static_cast<i64>(magnitude);
    }
    ValueError result = ValueError::None;
    if (value < desc.min_int || value > desc.max_int) {
        if (desc.policy == RangePolicy::Reject) return ValueError::OutOfRange;
        value = value < desc.min_int ? desc.min_int : desc.max_int;
        result = ValueError::Clamped;
    }
    *out = Value::FromInt(value);
    return result;
}

ValueError ParseFloat(const SettingDesc& desc, StringView text, Value* out) {
    if (text.Length() == 0 || text.Length() > 64) return ValueError::BadNumber;
    for (usize i = 0; i < text.Length(); ++i) {
        const char c = text[i];
        const bool ok = (c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E';
        if (!ok) return ValueError::BadNumber; // refuses hex floats, nan, inf, blanks
    }
    // std::from_chars is independent of the process locale (strtod is not) and takes no leading '+'.
    const char* begin = text.Data();
    const char* const end = text.Data() + text.Length();
    if (*begin == '+') ++begin;
    const ParseDoubleResult parsed = ParseDoubleExact(begin, end);
    f64 value = parsed.value;
    if (!parsed.ok || parsed.end != end || begin == end || !(value == value) || value - value != 0.0) return ValueError::BadNumber;
    ValueError result = ValueError::None;
    if (value < desc.min_float || value > desc.max_float) {
        if (desc.policy == RangePolicy::Reject) return ValueError::OutOfRange;
        value = value < desc.min_float ? desc.min_float : desc.max_float;
        result = ValueError::Clamped;
    }
    *out = Value::FromFloat(value);
    return result;
}

// The length of a text in its stored (quoted, escaped) form, which has to fit a Value; and whether the text is something a file can hold.
usize StoredTextLength(StringView t) {
    usize n = 2;
    for (usize i = 0; i < t.Length(); ++i) {
        const char c = t[i];
        if (c == '"' || c == '\\' || c == '\n' || c == '\t') n += 2;
        else if (static_cast<u8>(c) < 0x20 || c == 0x7f) n += 4;
        else n += 1;
    }
    return n;
}

ValueError CheckText(const SettingDesc& desc, StringView t) {
    if (desc.max_text != 0 && t.Length() > desc.max_text) return ValueError::TooLong;
    if (!IsValidUtf8(t)) return ValueError::BadEscape; // settings text is UTF-8 (the file format refuses anything else)
    if (StoredTextLength(t) > kMaxTextBytes) return ValueError::TooLong; // quotes and escapes count: the stored form is what the stores carry
    return ValueError::None;
}

ValueError ParseText(StringView text, Value* out) {
    if (text.Length() >= 2 && text[0] == '"') {
        const usize end = text.Length() - 1; // index of the closing quote
        if (text[end] != '"') return ValueError::BadEscape;
        char buffer[kMaxTextBytes + 1];
        usize n = 0;
        for (usize i = 1; i < end; ++i) {
            char c = text[i];
            if (c == '"') return ValueError::BadEscape; // an unescaped quote inside
            if (c == '\\') {
                if (i + 1 >= end) return ValueError::BadEscape; // the backslash would escape the closing quote
                const char e = text[i + 1];
                if (e == '"' || e == '\\') {
                    c = e;
                    i += 1;
                } else if (e == 'n') {
                    c = '\n';
                    i += 1;
                } else if (e == 't') {
                    c = '\t';
                    i += 1;
                } else if (e == 'x') {
                    if (i + 3 >= end) return ValueError::BadEscape;
                    const int hi = HexDigit(text[i + 2]), lo = HexDigit(text[i + 3]);
                    if (hi < 0 || lo < 0) return ValueError::BadEscape;
                    c = static_cast<char>(hi * 16 + lo);
                    i += 3;
                } else {
                    return ValueError::BadEscape;
                }
            }
            if (n >= kMaxTextBytes) return ValueError::TooLong;
            buffer[n++] = c;
        }
        *out = Value::FromText(StringView(buffer, n));
        return ValueError::None;
    }
    if (text.Length() > kMaxTextBytes) return ValueError::TooLong;
    *out = Value::FromText(text);
    return ValueError::None;
}

} // namespace

ValueError ParseValue(const SettingDesc& desc, StringView text, Value* out) {
    switch (desc.type) {
    case Type::Bool:
        if (text.Length() == 0) return ValueError::Empty;
        for (const char* yes : {"true", "yes", "on", "1"}) {
            if (SameIgnoringCase(text, yes)) { *out = Value::FromBool(true); return ValueError::None; }
        }
        for (const char* no : {"false", "no", "off", "0"}) {
            if (SameIgnoringCase(text, no)) { *out = Value::FromBool(false); return ValueError::None; }
        }
        return ValueError::BadBool;
    case Type::Int:
        if (text.Length() == 0) return ValueError::Empty;
        return ParseInt(desc, text, out);
    case Type::Float:
        if (text.Length() == 0) return ValueError::Empty;
        return ParseFloat(desc, text, out);
    case Type::Enum:
        if (text.Length() == 0) return ValueError::Empty;
        for (u32 i = 0; i < desc.enum_count; ++i) {
            if (Same(text, desc.enum_names[i])) { *out = Value::FromEnum(i); return ValueError::None; }
        }
        return ValueError::NotInEnum;
    case Type::Text: {
        Value parsed;
        const ValueError e = ParseText(text, &parsed);
        if (e != ValueError::None) return e;
        const ValueError checked = CheckText(desc, parsed.AsText());
        if (checked != ValueError::None) return checked;
        *out = parsed;
        return ValueError::None;
    }
    }
    return ValueError::BadNumber;
}

ValueError ValidateValue(const SettingDesc& desc, const Value& value, Value* out) {
    if (value.GetType() != desc.type) return ValueError::BadNumber;
    switch (desc.type) {
    case Type::Int: {
        i64 v = value.AsInt();
        ValueError result = ValueError::None;
        if (v < desc.min_int || v > desc.max_int) {
            if (desc.policy == RangePolicy::Reject) return ValueError::OutOfRange;
            v = v < desc.min_int ? desc.min_int : desc.max_int;
            result = ValueError::Clamped;
        }
        *out = Value::FromInt(v);
        return result;
    }
    case Type::Float: {
        f64 v = value.AsFloat();
        ValueError result = ValueError::None;
        if (v < desc.min_float || v > desc.max_float) {
            if (desc.policy == RangePolicy::Reject) return ValueError::OutOfRange;
            v = v < desc.min_float ? desc.min_float : desc.max_float;
            result = ValueError::Clamped;
        }
        *out = Value::FromFloat(v);
        return result;
    }
    case Type::Enum:
        if (value.AsEnum() >= desc.enum_count) return ValueError::NotInEnum;
        *out = value;
        return ValueError::None;
    case Type::Text:
        {
            const ValueError checked = CheckText(desc, value.AsText());
            if (checked != ValueError::None) return checked;
        }
        *out = value;
        return ValueError::None;
    case Type::Bool: *out = value; return ValueError::None;
    }
    return ValueError::BadNumber;
}

usize FormatValue(const SettingDesc& desc, const Value& value, char* out, usize capacity) {
    ENGINE_ASSERT(value.GetType() == desc.type, "settings::FormatValue: the value's type differs from the declaration's");
    char buffer[kMaxValueTextBytes];
    usize n = 0;
    switch (desc.type) {
    case Type::Bool: n = static_cast<usize>(FormatTo(buffer, sizeof(buffer), "%s", value.AsBool() ? "true" : "false")); break;
    case Type::Int: n = static_cast<usize>(FormatTo(buffer, sizeof(buffer), "%lld", static_cast<long long>(value.AsInt()))); break;
    case Type::Float: {
        // The shortest text that reads back as the same number, locale-independent: plain decimals for ordinary
        // sizes (`100`, `1280`, `0.25`), an exponent only when that is shorter (`1e-30`).
        const FormatShortestResult written = FormatShortest(buffer, buffer + sizeof(buffer) - 4, value.AsFloat());
        n = static_cast<usize>(written.end - buffer);
        buffer[n] = '\0';
        if (CStrFindAny(buffer, ".eE") == nullptr) { // "1" must not read back as an integer
            buffer[n++] = '.';
            buffer[n++] = '0';
            buffer[n] = '\0';
        }
        break;
    }
    case Type::Enum: {
        const u32 index = value.AsEnum();
        ENGINE_ASSERT(index < desc.enum_count, "settings::FormatValue: the enum index is outside the names");
        n = static_cast<usize>(FormatTo(buffer, sizeof(buffer), "%s", desc.enum_names[index]));
        break;
    }
    case Type::Text: {
        const StringView t = value.AsText();
        buffer[n++] = '"';
        for (usize i = 0; i < t.Length(); ++i) {
            const char c = t[i];
            if (c == '"' || c == '\\') { buffer[n++] = '\\'; buffer[n++] = c; }
            else if (c == '\n') { buffer[n++] = '\\'; buffer[n++] = 'n'; }
            else if (c == '\t') { buffer[n++] = '\\'; buffer[n++] = 't'; }
            else if (static_cast<u8>(c) < 0x20 || c == 0x7f) n += static_cast<usize>(FormatTo(buffer + n, 5, "\\x%02x", static_cast<unsigned>(static_cast<u8>(c))));
            else buffer[n++] = c;
        }
        buffer[n++] = '"';
        buffer[n] = '\0';
        break;
    }
    }
    if (n + 1 > capacity) {
        if (capacity != 0) out[0] = '\0';
        return 0;
    }
    MemCopy(out, buffer, n + 1);
    return n;
}

} // namespace engine_placeholder::settings
