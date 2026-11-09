#include <catch2/catch_test_macros.hpp>

#include "lang/scalar.h"
#include <cstring>

#include "catch_engine_types.h"
#include "settings/desc.h"
#include "settings/registry.h"
#include "test_text.h"

using namespace engine_placeholder;
using namespace engine_placeholder::settings;

namespace {

const char* const kModes[] = {"fifo", "mailbox", "immediate"};

// Formats and parses back; the result must equal the input.
void RoundTrip(const SettingDesc& desc, const Value& v) {
    char text[kMaxValueTextBytes];
    const usize n = FormatValue(desc, v, text, sizeof(text));
    REQUIRE(n > 0);
    Value back;
    INFO(text);
    CHECK(ParseValue(desc, StringView(text, n), &back) == ValueError::None);
    CHECK(back == v);
}

} // namespace

TEST_CASE("Value: factories, accessors and equality", "[settings][value]") {
    CHECK(Value::FromBool(true).AsBool());
    CHECK(Value::FromInt(-7).AsInt() == -7);
    CHECK(Value::FromFloat(2.5).AsFloat() == 2.5);
    CHECK(Value::FromEnum(2).AsEnum() == 2);
    CHECK(Value::FromText("hi").AsText() == "hi");
    CHECK(Value::FromInt(1) != Value::FromBool(true)); // equal bits, different types
    CHECK(Value::FromText("a") != Value::FromText("b"));
    CHECK(Value::FromText("a") == Value::FromText("a"));
    CHECK(Value().GetType() == Type::Bool);
    CHECK(StringView(TypeName(Type::Enum)) == "enum");
}

TEST_CASE("Value text round trips for every type", "[settings][value]") {
    RoundTrip(MakeBool("a", true), Value::FromBool(true));
    RoundTrip(MakeBool("a", false), Value::FromBool(false));
    const SettingDesc i = MakeInt("i", 0, -9223372036854775807ll - 1, 9223372036854775807ll);
    for (i64 v : {i64{0}, i64{1}, i64{-1}, i64{42}, i64{-9223372036854775807ll - 1}, i64{9223372036854775807ll}}) RoundTrip(i, Value::FromInt(v));
    const SettingDesc f = MakeFloat("f", 0, -1.7e308, 1.7e308);
    for (f64 v : {0.0, -0.0, 1.0, -1.0, 0.1, 1e-30, 1e30, 3.141592653589793, 123456789.125, 5e-324, 1.7976931348623157e300}) RoundTrip(f, Value::FromFloat(v));
    const SettingDesc e = MakeEnum("e", kModes, 3, 0);
    for (u32 v = 0; v < 3; ++v) RoundTrip(e, Value::FromEnum(v));
    const SettingDesc t = MakeText("t", "");
    for (const char* v : {"", "plain", "with space", "quote\"inside", "back\\slash", "line\nbreak\ttab", "unicode \xe6\x97\xa5\xe6\x9c\xac", "a=b#c;d"}) {
        RoundTrip(t, Value::FromText(v));
    }
    char ctrl[3] = {'\x01', 'x', '\x7f'};
    RoundTrip(t, Value::FromText(StringView(ctrl, 3)));
}

TEST_CASE("A float always reads back as a float, and -0 keeps its sign", "[settings][value]") {
    const SettingDesc f = MakeFloat("f", 0, -10, 10);
    char text[64];
    REQUIRE(FormatValue(f, Value::FromFloat(1.0), text, sizeof(text)) > 0);
    CHECK(StringView(text) == "1.0");
    REQUIRE(FormatValue(f, Value::FromFloat(-0.0), text, sizeof(text)) > 0);
    CHECK(StringView(text) == "-0.0");
    Value v;
    REQUIRE(ParseValue(f, "-0", &v) == ValueError::None);
    CHECK(SignBit(v.AsFloat()));
    REQUIRE(FormatValue(f, Value::FromFloat(1e-30), text, sizeof(text)) > 0);
    CHECK(StringView(text) == "1e-30");
}

TEST_CASE("Float text: NaN, infinity and junk are refused", "[settings][value]") {
    const SettingDesc f = MakeFloat("f", 0, -1e308, 1e308, RangePolicy::Reject);
    Value v = Value::FromFloat(7);
    for (const char* bad : {"nan", "NaN", "inf", "-inf", "infinity", "0x1p3", "1.5x", "1,5", " 1", "1 ", "--1", "1e", "e1", ".", "+", "1e999", "-1e999", "1.2.3"}) {
        INFO(bad);
        CHECK(ParseValue(f, bad, &v) == ValueError::BadNumber);
    }
    CHECK(ParseValue(f, "", &v) == ValueError::Empty);
    CHECK(v.AsFloat() == 7); // unchanged on failure
}

TEST_CASE("Int text: bases, signs, overflow and ranges", "[settings][value]") {
    const SettingDesc clamp = MakeInt("i", 0, -10, 10);
    const SettingDesc reject = MakeInt("i", 0, -10, 10, RangePolicy::Reject);
    Value v;
    CHECK(ParseValue(clamp, "7", &v) == ValueError::None);
    CHECK(v.AsInt() == 7);
    CHECK(ParseValue(clamp, "+7", &v) == ValueError::None);
    CHECK(ParseValue(clamp, "0x0a", &v) == ValueError::None);
    CHECK(v.AsInt() == 10);
    CHECK(ParseValue(clamp, "-0xA", &v) == ValueError::None);
    CHECK(v.AsInt() == -10);
    CHECK(ParseValue(clamp, "99", &v) == ValueError::Clamped);
    CHECK(v.AsInt() == 10);
    CHECK(ParseValue(clamp, "-99", &v) == ValueError::Clamped);
    CHECK(v.AsInt() == -10);
    CHECK(ParseValue(reject, "99", &v) == ValueError::OutOfRange);
    CHECK(v.AsInt() == -10); // unchanged
    for (const char* bad : {"1.5", "abc", "-", "+", "0x", "12a", "9223372036854775808", "-9223372036854775809", "99999999999999999999", "0xFFFFFFFFFFFFFFFFF", "1 2"}) {
        INFO(bad);
        CHECK(ParseValue(clamp, bad, &v) == ValueError::BadNumber);
    }
    const SettingDesc wide = MakeInt("i", 0, -9223372036854775807ll - 1, 9223372036854775807ll);
    CHECK(ParseValue(wide, "-9223372036854775808", &v) == ValueError::None);
    CHECK(v.AsInt() == -9223372036854775807ll - 1);
    CHECK(ParseValue(wide, "9223372036854775807", &v) == ValueError::None);
    CHECK(ParseValue(wide, "", &v) == ValueError::Empty);
}

TEST_CASE("Bool, enum and text parsing", "[settings][value]") {
    const SettingDesc b = MakeBool("b", false);
    Value v;
    for (const char* yes : {"true", "TRUE", "True", "yes", "on", "1"}) {
        CHECK(ParseValue(b, yes, &v) == ValueError::None);
        CHECK(v.AsBool());
    }
    for (const char* no : {"false", "No", "OFF", "0"}) {
        CHECK(ParseValue(b, no, &v) == ValueError::None);
        CHECK_FALSE(v.AsBool());
    }
    CHECK(ParseValue(b, "maybe", &v) == ValueError::BadBool);
    CHECK(ParseValue(b, "", &v) == ValueError::Empty);
    const SettingDesc e = MakeEnum("e", kModes, 3, 0);
    CHECK(ParseValue(e, "mailbox", &v) == ValueError::None);
    CHECK(v.AsEnum() == 1);
    CHECK(ParseValue(e, "Mailbox", &v) == ValueError::NotInEnum); // names are case-sensitive
    CHECK(ParseValue(e, "nope", &v) == ValueError::NotInEnum);
    CHECK(ParseValue(e, "", &v) == ValueError::Empty);
    const SettingDesc t = MakeText("t", "");
    CHECK(ParseValue(t, "bare words", &v) == ValueError::None);
    CHECK(v.AsText() == "bare words");
    CHECK(ParseValue(t, "\"quoted \\\"x\\\" \\x41\"", &v) == ValueError::None);
    CHECK(v.AsText() == "quoted \"x\" A");
    CHECK(ParseValue(t, "", &v) == ValueError::None);
    CHECK(v.AsText() == "");
    for (const char* bad : {"\"unterminated", "\"bad \\q escape\"", "\"trailing backslash\\\"", "\"inner\"quote\"", "\"\\x4\"", "\"\\xZZ\""}) {
        INFO(bad);
        CHECK(ParseValue(t, bad, &v) == ValueError::BadEscape);
    }
    engine_test::Text long_text = engine_test::Repeated(600, 'x');
    CHECK(ParseValue(t, long_text.View(), &v) == ValueError::TooLong);
}

TEST_CASE("FormatValue reports a buffer that is too small", "[settings][value]") {
    const SettingDesc t = MakeText("t", "");
    char small[4] = {'z', 'z', 'z', 'z'};
    CHECK(FormatValue(t, Value::FromText("abcdef"), small, sizeof(small)) == 0);
    CHECK(small[0] == '\0');
    CHECK(StringView(ValueErrorName(ValueError::NotInEnum)) == "not_in_enum");
}

TEST_CASE("Float text is plain decimals for ordinary sizes and an exponent only when shorter", "[settings][value]") {
    const SettingDesc f = MakeFloat("f", 0, -1e300, 1e300);
    struct Row { f64 value; const char* text; };
    const Row rows[] = {{100.0, "100.0"}, {1000.0, "1000.0"}, {1280.0, "1280.0"}, {0.25, "0.25"}, {0.5, "0.5"}, {-3.75, "-3.75"}, {1e-30, "1e-30"}, {1e30, "1e+30"}, {123456789.125, "123456789.125"}};
    for (const Row& r : rows) {
        char text[64];
        REQUIRE(FormatValue(f, Value::FromFloat(r.value), text, sizeof(text)) > 0);
        INFO(r.text);
        CHECK(StringView(text) == StringView(r.text));
    }
}

TEST_CASE("Value: -0.0 and 0.0 are different values", "[settings][value]") {
    CHECK(Value::FromFloat(-0.0) != Value::FromFloat(0.0));
    CHECK(Value::FromFloat(0.5) == Value::FromFloat(0.5));
}

TEST_CASE("A text setting's own length limit is enforced in ParseValue and ValidateValue", "[settings][value]") {
    SettingDesc d = MakeText("t.short", "");
    d.max_text = 5;
    Value v;
    CHECK(ParseValue(d, "12345", &v) == ValueError::None);
    CHECK(ParseValue(d, "123456", &v) == ValueError::TooLong);
    CHECK(ParseValue(d, "\"123456\"", &v) == ValueError::TooLong);
    CHECK(ParseValue(d, "\"\\x41\\x42\"", &v) == ValueError::None);
    CHECK(ValidateValue(d, Value::FromText("123456"), &v) == ValueError::TooLong);
    CHECK(ValidateValue(d, Value::FromText("12345"), &v) == ValueError::None);
    Registry registry;
    RegisterError e = RegisterError::None;
    d.default_value = Value::FromText("toolong");
    CHECK_FALSE(registry.Register(d, &e).IsValid());
    CHECK(e == RegisterError::BadDefault);
    SettingDesc huge = MakeText("t.huge", "");
    huge.max_text = 600;
    CHECK_FALSE(registry.Register(huge, &e).IsValid());
}
