#pragma once

// Catch2 printers for the engine's own types, so an assertion on a StringView, a String, a Path, a Status, a u128 or a
// Span prints a readable value without converting to std::string first. Include this header in a test file that
// compares or prints these types. (Catch2 itself needs std::string for the text it returns; that is the one place a
// std type is used here.)

#include <catch2/catch_tostring.hpp>

#include <string>

#include "containers/span.h"
#include "int128.h"
#include "io/error.h"
#include "io/path.h"
#include "string/string.h"

namespace engine_test {

inline std::string ToStd(engine_placeholder::StringView view) { return std::string(view.Data(), view.Length()); }

} // namespace engine_test

namespace Catch {

template <>
struct StringMaker<engine_placeholder::StringView> {
    static std::string convert(const engine_placeholder::StringView& view) { return "\"" + engine_test::ToStd(view) + "\""; }
};

template <typename A>
struct StringMaker<engine_placeholder::String<A>> {
    static std::string convert(const engine_placeholder::String<A>& text) { return "\"" + engine_test::ToStd(text.View()) + "\""; }
};

template <>
struct StringMaker<engine_placeholder::io::Path> {
    static std::string convert(const engine_placeholder::io::Path& path) { return "Path(\"" + engine_test::ToStd(path.View()) + "\")"; }
};

template <>
struct StringMaker<engine_placeholder::io::Error> {
    static std::string convert(engine_placeholder::io::Error error) { return engine_placeholder::io::ErrorName(error); }
};

template <>
struct StringMaker<engine_placeholder::io::Status> {
    static std::string convert(const engine_placeholder::io::Status& status) {
        return std::string("Status(") + engine_placeholder::io::ErrorName(status.error) + ")";
    }
};

template <>
struct StringMaker<engine_placeholder::u128> {
    static std::string convert(const engine_placeholder::u128& value) {
        char text[engine_placeholder::u128::kMaxTextBytes];
        value.ToText(text, sizeof(text), 10);
        return text;
    }
};

template <>
struct StringMaker<engine_placeholder::i128> {
    static std::string convert(const engine_placeholder::i128& value) {
        char text[engine_placeholder::i128::kMaxTextBytes];
        value.ToText(text, sizeof(text), 10);
        return text;
    }
};

template <typename T>
struct StringMaker<engine_placeholder::Span<T>> {
    static std::string convert(const engine_placeholder::Span<T>& span) {
        std::string out = "Span[" + std::to_string(span.Size()) + "]{";
        for (engine_placeholder::usize i = 0; i < span.Size() && i < 16; ++i) {
            out += (i ? ", " : "") + Catch::Detail::stringify(span[i]);
        }
        return out + (span.Size() > 16 ? ", ...}" : "}");
    }
};

} // namespace Catch
