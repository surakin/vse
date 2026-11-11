#pragma once

#include <catch2/catch_test_macros.hpp>

#include "io/path.h"
#include "text/font.h"

// The shipped font opened for the UI tests that draw text.
namespace ui_test
{

struct Opened
{
    engine_placeholder::text::FontLibrary library;
    engine_placeholder::text::Font font;
    Opened()
    {
        REQUIRE(library.Initialize().Ok());
        engine_placeholder::text::FontOptions options;
        REQUIRE(engine_placeholder::text::Font::OpenFile(
                    library, engine_placeholder::io::Path(engine_placeholder::StringView(UI_TEST_FONT_FILE)), options,
                    &font)
                    .Ok());
    }
};

}  // namespace ui_test
