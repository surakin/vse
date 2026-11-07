#include <catch2/catch_test_macros.hpp>

#include "catch_engine_types.h"
#include "settings/key.h"
#include "test_text.h"

using namespace engine_placeholder;
using namespace engine_placeholder::settings;

namespace {
struct Row {
    const char* text;
    KeyError expected;
};
} // namespace

TEST_CASE("Key grammar: the valid and invalid forms", "[settings][key]") {
    const Row rows[] = {
        {"a", KeyError::None}, {"width", KeyError::None}, {"renderer.width", KeyError::None}, {"a.b.c", KeyError::None},
        {"renderer.present_mode", KeyError::None}, {"x1", KeyError::None}, {"x_1", KeyError::None}, {"log.main.verbosity", KeyError::None},
        {"", KeyError::Empty}, {".", KeyError::EmptySegment}, {"a.", KeyError::EmptySegment}, {".a", KeyError::EmptySegment},
        {"a..b", KeyError::EmptySegment}, {"..", KeyError::EmptySegment}, {"a.b.c.d", KeyError::TooManySegments},
        {"A", KeyError::BadCharacter}, {"aB", KeyError::BadCharacter}, {"Renderer.width", KeyError::BadCharacter}, {"1a", KeyError::BadCharacter},
        {"_a", KeyError::BadCharacter}, {"a._b", KeyError::BadCharacter}, {"a.1b", KeyError::BadCharacter}, {"a-b", KeyError::BadCharacter},
        {"a b", KeyError::BadCharacter}, {" a", KeyError::BadCharacter}, {"a ", KeyError::BadCharacter}, {"a=b", KeyError::BadCharacter},
        {"a[b]", KeyError::BadCharacter}, {"a/b", KeyError::BadCharacter}, {"a:b", KeyError::BadCharacter}, {"a\tb", KeyError::BadCharacter},
        {"a\nb", KeyError::BadCharacter}, {"caf\xc3\xa9", KeyError::BadCharacter}, {"\xe6\x97\xa5", KeyError::BadCharacter},
        {"a#", KeyError::BadCharacter}, {"a;", KeyError::BadCharacter}, {"a\"b", KeyError::BadCharacter}, {"a\\b", KeyError::BadCharacter},
        {"a*", KeyError::BadCharacter}, {"a?", KeyError::BadCharacter}, {"a$", KeyError::BadCharacter}, {"a@b", KeyError::BadCharacter},
        {"0", KeyError::BadCharacter}, {"9.a", KeyError::BadCharacter}, {"a.9", KeyError::BadCharacter}, {"-a", KeyError::BadCharacter},
        {"a.b.", KeyError::EmptySegment}, {"a.b..c", KeyError::EmptySegment}, {"A.b.c", KeyError::BadCharacter}, {"a.B.c", KeyError::BadCharacter},
        {"abcdefghijklmnopqrstuvwxyz01234", KeyError::None},          // 31 bytes
        {"abcdefghijklmnopqrstuvwxyz012345", KeyError::SegmentTooLong}, // 32 bytes
        {"abcdefghijklmnopqrstuvwxyz01234.abcdefghijklmnopqrstuvwxyz01234.abcdefghijklmnopqrstuvwxyz01234", KeyError::None}, // 95 bytes
        {"abcdefghijklmnopqrstuvwxyz01234.abcdefghijklmnopqrstuvwxyz01234.abcdefghijklmnopqrstuvwxyz012345", KeyError::TooLong},
    };
    for (const Row& r : rows) {
        INFO(r.text);
        CHECK(Key::Parse(StringView(r.text), nullptr) == r.expected);
    }
    CHECK(sizeof(rows) / sizeof(rows[0]) >= 50);
}

TEST_CASE("Key: sections, names, segments, ordering and hashing", "[settings][key]") {
    Key a, b, c;
    REQUIRE(Key::Parse("renderer.present_mode", &a) == KeyError::None);
    REQUIRE(Key::Parse("width", &b) == KeyError::None);
    REQUIRE(Key::Parse("log.main.verbosity", &c) == KeyError::None);
    CHECK(a.View() == "renderer.present_mode");
    CHECK(a.Section() == "renderer");
    CHECK(a.Name() == "present_mode");
    CHECK(a.Segments() == 2);
    CHECK(b.Section() == "");
    CHECK(b.Name() == "width");
    CHECK(b.Segments() == 1);
    CHECK(c.Section() == "log.main");
    CHECK(c.Name() == "verbosity");
    CHECK(c.Segments() == 3);
    CHECK(a == a);
    CHECK(a != b);
    CHECK(c < a);
    CHECK(a < b);
    Key a2;
    REQUIRE(Key::Parse("renderer.present_mode", &a2) == KeyError::None);
    CHECK(a.Hash() == a2.Hash());
    CHECK(a.Hash() != b.Hash());
    Key empty;
    CHECK_FALSE(empty.IsValid());
    CHECK(empty.Segments() == 0);
}

TEST_CASE("Key::Parse leaves the output unchanged on failure", "[settings][key]") {
    Key k;
    REQUIRE(Key::Parse("keep", &k) == KeyError::None);
    CHECK(Key::Parse("Bad", &k) == KeyError::BadCharacter);
    CHECK(k.View() == "keep");
    CHECK(StringView(KeyErrorName(KeyError::TooLong)) == "too_long");
}
