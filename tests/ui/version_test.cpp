#include <catch2/catch_test_macros.hpp>

#include "platform/input.h"
#include "ui/version.h"

using namespace engine_placeholder;

TEST_CASE("The ui library links against the text layer and sees the platform's input types", "[ui]")
{
    CHECK(ui::LayoutVersion() == ui::kUiLayoutVersion);
    // The platform's pure-data input header is reachable without linking the display libraries (the platform_input
    // target).
    CHECK(os::KeyboardState::kKeyCount == static_cast<usize>(os::Key::Count));
}
