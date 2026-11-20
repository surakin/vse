#pragma once

#include <catch2/catch_test_macros.hpp>

#include "io/path.h"
#include "text/font.h"

// The shipped font opened for the UI tests that draw text.
namespace ui_test
{

struct Opened
{
    vse::text::FontLibrary library;
    vse::text::Font font;
    Opened()
    {
        REQUIRE(library.Initialize().Ok());
        vse::text::FontOptions options;
        REQUIRE(
            vse::text::Font::OpenFile(library, vse::io::Path(vse::StringView(UI_TEST_FONT_FILE)), options, &font).Ok());
    }
};

}  // namespace ui_test
