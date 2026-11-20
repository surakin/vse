#include <catch2/catch_test_macros.hpp>

#include "platform/linux/x11/x11_scale.h"

using vse::os::x11::ContentScaleFromResources;
using vse::os::x11::kMaxContentScale;
using vse::os::x11::kMinContentScale;

TEST_CASE("Xft.dpi over 96 is the content scale", "[x11][scale]")
{
    CHECK(ContentScaleFromResources("Xft.dpi:\t192\n") == 2.0f);
    CHECK(ContentScaleFromResources("Xft.dpi: 96\n") == 1.0f);
    CHECK(ContentScaleFromResources("Xft.dpi:144\n") == 1.5f);
    CHECK(ContentScaleFromResources("Xft.dpi:   120   \n") == 1.25f);
    CHECK(ContentScaleFromResources("Xft.dpi: 144.0\n") == 1.5f);
    CHECK(ContentScaleFromResources("Xft.dpi: 129.6\n") > 1.349f);
    CHECK(ContentScaleFromResources("Xft.dpi: 129.6\n") < 1.351f);
    CHECK(ContentScaleFromResources("Xft*dpi: 192\n") == 2.0f);
}

TEST_CASE("The scale is read from the right line of a resource database", "[x11][scale]")
{
    const char* kDatabase =
        "Xcursor.size:\t48\nXcursor.theme:\tbreeze_cursors\nXft.antialias:\t1\nXft.dpi:\t192\nXft.hinting:\t1\n"
        "Xft.hintstyle:\thintslight\nXft.rgba:\trgb\n";
    CHECK(ContentScaleFromResources(kDatabase) == 2.0f);
    CHECK(ContentScaleFromResources("Xft.dpi: 192") == 2.0f);                       // no final newline
    CHECK(ContentScaleFromResources("  \tXft.dpi: 192\n") == 2.0f);                 // leading blanks
    CHECK(ContentScaleFromResources("Xft.dpi: 192\r\nXft.rgba: rgb\r\n") == 2.0f);  // CRLF
    CHECK(ContentScaleFromResources("*.foreground: red\nXft.dpi: 288\n") == 3.0f);
    // As in an X resource database: a later line replaces an earlier one, and the tight binding beats the loose one.
    CHECK(ContentScaleFromResources("Xft.dpi: 144\nXft.dpi: 192\n") == 2.0f);
    CHECK(ContentScaleFromResources("Xft.dpi: 96\nXft.antialias: 1\nXft.dpi: 192\n") ==
          2.0f);  // an appended xrdb -merge
    CHECK(ContentScaleFromResources("Xft.dpi: 192\nXft*dpi: 144\n") == 2.0f);
    CHECK(ContentScaleFromResources("Xft*dpi: 144\nXft.dpi: 192\n") == 2.0f);
    CHECK(ContentScaleFromResources("Xft*dpi: 144\nXft*dpi: 192\n") == 2.0f);
}

TEST_CASE("A missing or unusable Xft.dpi is scale 1", "[x11][scale]")
{
    CHECK(ContentScaleFromResources("") == kMinContentScale);
    CHECK(ContentScaleFromResources("Xft.antialias: 1\n") == kMinContentScale);
    CHECK(ContentScaleFromResources("Xft.dpix: 192\n") == kMinContentScale);  // another resource
    CHECK(ContentScaleFromResources("xft.dpi: 192\n") == kMinContentScale);   // names are case sensitive
    CHECK(ContentScaleFromResources("# Xft.dpi: 192\n") == kMinContentScale);
    CHECK(ContentScaleFromResources("Xft.dpi:\n") == kMinContentScale);
    CHECK(ContentScaleFromResources("Xft.dpi: abc\n") == kMinContentScale);
    CHECK(ContentScaleFromResources("Xft.dpi: -192\n") == kMinContentScale);
    CHECK(ContentScaleFromResources("Xft.dpi: 0\n") == kMinContentScale);
    CHECK(ContentScaleFromResources("Xft.dpi: 192px\n") == kMinContentScale);
    CHECK(ContentScaleFromResources("Xft.dpi 192\n") == kMinContentScale);  // no colon
    CHECK(ContentScaleFromResources("Xft.dpi: 192 192\n") == kMinContentScale);
    // The last Xft.dpi line decides: a malformed one after a good one gives the default, a good one after a malformed
    // one wins.
    CHECK(ContentScaleFromResources("Xft.dpi: x\nXft.dpi: 192\n") == 2.0f);
    CHECK(ContentScaleFromResources("Xft.dpi: 192\nXft.dpi: x\n") == kMinContentScale);
}

TEST_CASE("The scale is clamped to the supported range", "[x11][scale]")
{
    CHECK(ContentScaleFromResources("Xft.dpi: 48\n") == kMinContentScale);  // below 100%
    CHECK(ContentScaleFromResources("Xft.dpi: 95\n") == kMinContentScale);
    CHECK(ContentScaleFromResources("Xft.dpi: 384\n") == kMaxContentScale);
    CHECK(ContentScaleFromResources("Xft.dpi: 4000\n") == kMaxContentScale);
    CHECK(ContentScaleFromResources("Xft.dpi: 99999999999999999999\n") == kMaxContentScale);
}
