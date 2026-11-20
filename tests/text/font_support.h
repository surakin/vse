#pragma once

#include <catch2/catch_test_macros.hpp>

#include "containers/vector.h"
#include "io/file.h"
#include "io/path.h"
#include "text/font.h"

// Shared helpers of the text tests: the shipped font's path and bytes.
namespace text_test
{

inline vse::io::Path FontPath()
{
    return vse::io::Path(vse::StringView(TEXT_TEST_FONT_FILE));
}

/// The shipped font file's bytes (read once per call; the font is small).
inline vse::Vector<vse::byte> FontBytes()
{
    vse::Vector<vse::byte> bytes;
    (void)vse::io::ReadFile(FontPath(), &bytes);
    return bytes;
}

/// A library with the shipped font opened from its path.
struct Opened
{
    vse::text::FontLibrary library;
    vse::text::Font font;
    explicit Opened(vse::text::Hinting hinting = vse::text::Hinting::Light)
    {
        REQUIRE(library.Initialize().Ok());
        vse::text::FontOptions options;
        options.hinting = hinting;
        REQUIRE(vse::text::Font::OpenFile(library, FontPath(), options, &font).Ok());
    }
};

}  // namespace text_test
