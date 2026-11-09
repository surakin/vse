#include <catch2/catch_test_macros.hpp>

#include "ui/theme.h"

using namespace engine_placeholder;
using namespace engine_placeholder::ui;

namespace {

Theme Override(WidgetKind kind, VariantId variant, u32 background) {
    Style s;
    s.At(StateKind::Normal).background = background;
    ThemeBuilder b;
    b.SetStyle(kind, variant, s);
    return b.Build();
}

} // namespace

TEST_CASE("Theme: a value falls through the four levels of the chain", "[ui][theme]") {
    ThemeSet set;
    const ResolvedStyle base = set.Resolve(widget_kind::kButton, 0, 0, 1.0f);
    CHECK(base.background == DefaultLightTheme().FindStyle(widget_kind::kButton, 0)->At(StateKind::Normal).background.value); // the built-in default

    const Theme surface = Override(widget_kind::kButton, 0, 0xFF111111u);
    set.SetTheme(&surface);
    CHECK(set.Resolve(widget_kind::kButton, 0, 0, 1.0f).background == 0xFF111111u); // the surface's theme beats the default
    CHECK(set.Resolve(widget_kind::kButton, 0, 0, 1.0f).text_color == base.text_color); // what it leaves unset still comes from the default

    const Theme ancestor = Override(widget_kind::kButton, 0, 0xFF222222u);
    const Theme own = Override(widget_kind::kButton, 0, 0xFF333333u);
    const Theme* ancestor_only[] = {&ancestor};
    const Theme* both[] = {&ancestor, &own}; // least specific first
    CHECK(set.Resolve(widget_kind::kButton, 0, 0, 1.0f, ancestor_only).background == 0xFF222222u);
    CHECK(set.Resolve(widget_kind::kButton, 0, 0, 1.0f, both).background == 0xFF333333u);
    // An override that sets one field leaves the others alone.
    CHECK(set.Resolve(widget_kind::kButton, 0, 0, 1.0f, both).border_color == base.border_color);
    // A null entry is skipped.
    const Theme* with_null[] = {nullptr, &own};
    CHECK(set.Resolve(widget_kind::kButton, 0, 0, 1.0f, with_null).background == 0xFF333333u);
}

TEST_CASE("Theme: states layer on the normal style, the strongest state last", "[ui][theme]") {
    ThemeSet set;
    const ResolvedStyle normal = set.Resolve(widget_kind::kButton, 0, 0, 1.0f);
    const ResolvedStyle hover = set.Resolve(widget_kind::kButton, 0, kStateHover, 1.0f);
    const ResolvedStyle pressed = set.Resolve(widget_kind::kButton, 0, kStateHover | kStatePressed, 1.0f);
    const ResolvedStyle disabled = set.Resolve(widget_kind::kButton, 0, kStateHover | kStatePressed | kStateDisabled, 1.0f);
    CHECK(hover.background != normal.background);
    CHECK(pressed.background != hover.background);
    CHECK(hover.border_color == normal.border_color); // a state changes only what it sets
    CHECK(disabled.text_color == set.Color(ColorId::TextDisabled));
    CHECK(disabled.background != pressed.background);
}

TEST_CASE("Theme: a variant is found, and a missing one falls back to the default and is counted once in the log", "[ui][theme]") {
    ThemeSet set;
    const ResolvedStyle plain = set.Resolve(widget_kind::kButton, 0, 0, 1.0f);
    const ResolvedStyle primary = set.Resolve(widget_kind::kButton, StringView("primary"), 0, 1.0f);
    CHECK(primary.background != plain.background);
    CHECK(set.Stats().missing_styles == 0);
    const ResolvedStyle missing = set.Resolve(widget_kind::kButton, StringView("no-such-variant"), 0, 1.0f);
    CHECK(missing.background == plain.background);
    CHECK(set.Stats().missing_styles == 1);
    set.Resolve(widget_kind::kButton, StringView("no-such-variant"), 0, 1.0f); // cached, still counted
    CHECK(set.Stats().missing_styles == 2);
    // A variant that only a surface theme defines is found for that kind.
    const VariantId fancy = RegisterVariant("fancy");
    const Theme surface = Override(widget_kind::kLabel, fancy, 0xFF445566u);
    set.SetTheme(&surface);
    CHECK(set.Resolve(widget_kind::kLabel, fancy, 0, 1.0f).background == 0xFF445566u);
    CHECK(set.Stats().missing_styles == 2);
    // The same variant for a kind that does not have it is missing.
    set.Resolve(widget_kind::kPanel, fancy, 0, 1.0f);
    CHECK(set.Stats().missing_styles == 3);
}

TEST_CASE("Theme: sizes scale and borders and paddings round to whole pixels", "[ui][theme]") {
    ThemeSet set;
    const ResolvedStyle x1 = set.Resolve(widget_kind::kButton, 0, 0, 1.0f);
    const ResolvedStyle x125 = set.Resolve(widget_kind::kButton, 0, 0, 1.25f);
    const ResolvedStyle x2 = set.Resolve(widget_kind::kButton, 0, 0, 2.0f);
    CHECK(x1.border_width == 1.0f);
    CHECK(x125.border_width == 1.0f); // never 1.25
    CHECK(x2.border_width == 2.0f);
    CHECK(x1.padding_x == 12.0f);
    CHECK(x125.padding_x == 15.0f);
    CHECK(x2.padding_x == 24.0f);
    CHECK(x125.padding_y == 8.0f); // 6 * 1.25 = 7.5 rounds to 8
    CHECK(x125.radius == 5.0f);    // radius is scaled, not rounded
    CHECK(x1.font.size == 14.0f);
    CHECK(x2.font.size == 28.0f);
    // A thin border never vanishes: 1 unit at 0.5x is still a pixel.
    CHECK(set.Resolve(widget_kind::kButton, 0, 0, 0.5f).border_width == 1.0f);
    // A zero border stays zero.
    CHECK(set.Resolve(widget_kind::kLabel, 0, 0, 2.0f).border_width == 0.0f);
    CHECK(set.Metric(MetricId::Spacing, 1.25f) == 7.5f);
}

TEST_CASE("Theme: SetTheme bumps the revision, drops the cache and restyles", "[ui][theme]") {
    ThemeSet set;
    const u64 r0 = set.Revision();
    set.Resolve(widget_kind::kButton, 0, 0, 1.0f);
    set.Resolve(widget_kind::kButton, 0, 0, 1.0f);
    CHECK(set.Stats().cache_hits == 1);
    const Theme dark_override = Override(widget_kind::kButton, 0, 0xFF010203u);
    set.SetTheme(&dark_override);
    CHECK(set.Revision() == r0 + 1);
    CHECK(set.Resolve(widget_kind::kButton, 0, 0, 1.0f).background == 0xFF010203u); // not the stale cached value
    CHECK(set.Stats().cache_hits == 1);
    // The same pointer again, after an edit, re-resolves.
    set.SetTheme(&dark_override);
    CHECK(set.Revision() == r0 + 2);
    // A style with overrides is not cached.
    const Theme own = Override(widget_kind::kButton, 0, 0xFF0A0B0Cu);
    const Theme* chain[] = {&own};
    set.Resolve(widget_kind::kButton, 0, 0, 1.0f, chain);
    const u64 hits = set.Stats().cache_hits;
    CHECK(set.Resolve(widget_kind::kButton, 0, 0, 1.0f, chain).background == 0xFF0A0B0Cu);
    CHECK(set.Stats().cache_hits == hits);
}

TEST_CASE("Theme: SetTheme tells whether metrics or fonts changed", "[ui][theme]") {
    ThemeSet set;
    ThemeBuilder colors_only;
    colors_only.SetColor(ColorId::Accent, 0xFF123456u);
    const Theme t1 = colors_only.Build();
    CHECK_FALSE(set.SetTheme(&t1)); // colors do not move layout
    ThemeBuilder bigger;
    bigger.SetMetric(MetricId::Padding, 20);
    const Theme t2 = bigger.Build();
    CHECK(set.SetTheme(&t2));
    CHECK_FALSE(set.SetTheme(&t2));
    CHECK(set.SetTheme(nullptr)); // back to the default padding
    FontRef f;
    f.size = 30;
    ThemeBuilder font;
    font.SetFont(FontId::Body, f);
    const Theme t3 = font.Build();
    CHECK(set.SetTheme(&t3));
}

TEST_CASE("Theme: the built-in themes are readable and different", "[ui][theme]") {
    for (const Theme* theme : {&DefaultLightTheme(), &DefaultDarkTheme()}) {
        ThemeSet set;
        set.SetTheme(theme);
        const u32 text = set.Color(ColorId::Text);
        CHECK(ContrastRatio(text, set.Color(ColorId::WindowBackground)) >= 4.5f);
        CHECK(ContrastRatio(text, set.Color(ColorId::Panel)) >= 4.5f);
        const ResolvedStyle button = set.Resolve(widget_kind::kButton, 0, 0, 1.0f);
        CHECK(ContrastRatio(button.text_color, button.background) >= 4.5f);
        const ResolvedStyle primary = set.Resolve(widget_kind::kButton, StringView("primary"), 0, 1.0f);
        CHECK(ContrastRatio(primary.text_color, primary.background) >= 4.5f);
        const ResolvedStyle field = set.Resolve(widget_kind::kTextField, 0, 0, 1.0f);
        CHECK(ContrastRatio(field.text_color, field.background) >= 4.5f);
        // Hover and pressed shades stay readable too.
        const ResolvedStyle pressed = set.Resolve(widget_kind::kButton, 0, kStatePressed | kStateHover, 1.0f);
        CHECK(ContrastRatio(pressed.text_color, pressed.background) >= 4.5f);
    }
    ThemeSet light, dark;
    dark.SetTheme(&DefaultDarkTheme());
    CHECK(light.Resolve(widget_kind::kButton, 0, 0, 1.0f).background != dark.Resolve(widget_kind::kButton, 0, 0, 1.0f).background);
    CHECK(light.Resolve(widget_kind::kLabel, 0, 0, 1.0f).text_color != dark.Resolve(widget_kind::kLabel, 0, 0, 1.0f).text_color);
    CHECK(ContrastRatio(0xFF000000u, 0xFFFFFFFFu) > 20.9f);
    CHECK(ContrastRatio(0xFF808080u, 0xFF808080u) == 1.0f);
}

TEST_CASE("Theme: kind and variant ids are stable and custom kinds read theme data", "[ui][theme]") {
    CHECK(RegisterWidgetKind("button") == widget_kind::kButton);
    const WidgetKind gauge = RegisterWidgetKind("test-gauge");
    CHECK(gauge > widget_kind::kDropMarker);
    CHECK(RegisterWidgetKind("test-gauge") == gauge);
    CHECK(RegisterWidgetKind("test-gauge-2") != gauge);
    CHECK(WidgetKindName(gauge) == StringView("test-gauge"));
    CHECK(RegisterWidgetKind("") == 0);
    CHECK(WidgetKindName(0).Length() == 0);
    CHECK(RegisterVariant("") == 0);
    const VariantId v = RegisterVariant("test-variant");
    CHECK(v != 0);
    CHECK(RegisterVariant("test-variant") == v);
    CHECK(VariantName(v) == StringView("test-variant"));
    char long_name[80];
    for (char& c : long_name) c = 'x';
    CHECK(RegisterWidgetKind(StringView(long_name, 80)) == 0);

    // A game's own widget gets its look from theme data.
    ThemeSet set;
    const Theme game = Override(gauge, 0, 0xFFAABBCCu);
    set.SetTheme(&game);
    CHECK(set.Resolve(gauge, 0, 0, 1.0f).background == 0xFFAABBCCu);
    // A kind no theme styles resolves to the neutral defaults without a variant warning.
    const WidgetKind unstyled = RegisterWidgetKind("test-unstyled");
    const ResolvedStyle r = set.Resolve(unstyled, 0, 0, 1.0f);
    CHECK(r.background == 0);
    CHECK(r.text_color == set.Color(ColorId::Text));
    CHECK(set.Stats().missing_styles == 0);
}

TEST_CASE("Theme: named colors, metrics and fonts follow the chain", "[ui][theme]") {
    ThemeSet set;
    ThemeBuilder b;
    b.SetColor(ColorId::Accent, 0xFF0000FFu).SetMetric(MetricId::Padding, 3);
    FontRef f;
    f.family = 7;
    f.size = 10;
    b.SetFont(FontId::Mono, f);
    const Theme over = b.Build();
    const Theme* chain[] = {&over};
    CHECK(set.Color(ColorId::Accent, chain) == 0xFF0000FFu);
    CHECK(set.Color(ColorId::Accent) != 0xFF0000FFu);
    CHECK(set.Metric(MetricId::Padding, 2.0f, chain) == 6.0f);
    const FontRef got = set.Font(FontId::Mono, 2.0f, chain);
    CHECK(got.family == 7);
    CHECK(got.size == 20.0f);
    CHECK(set.Font(FontId::Body, 1.0f).family == 0);
}

TEST_CASE("Switching to a theme that differs only in a style's padding reports a layout change", "[ui][theme]") {
    ThemeSet set;
    Style roomy;
    roomy.At(StateKind::Normal).padding_x = 40.0f;
    ThemeBuilder a;
    a.SetStyle(widget_kind::kButton, 0, roomy);
    Theme with_padding = a.Build();
    Style tight;
    tight.At(StateKind::Normal).padding_x = 2.0f;
    ThemeBuilder b;
    b.SetStyle(widget_kind::kButton, 0, tight);
    Theme other = b.Build();
    set.SetTheme(&with_padding);
    CHECK(set.SetTheme(&other)); // metrics and fonts are the same: before the fix this said nothing changed and the buttons kept their old size
    Theme same = [&] {
        ThemeBuilder c;
        c.SetStyle(widget_kind::kButton, 0, tight);
        return c.Build();
    }();
    CHECK_FALSE(set.SetTheme(&same)); // the same look in another object: nothing to lay out again
}
