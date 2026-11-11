#pragma once

#include "containers/span.h"
#include "containers/vector.h"
#include "mem/heap_allocator.h"
#include "string/string.h"
#include "types.h"

//! The look of the toolkit as data: colors, metrics, fonts and per-widget styles that every widget draws from (Phase
//! 15, Day 191).
//!
//! A [[ui::Theme]] is plain data. A widget asks a [[ui::ThemeSet]] for its look by kind, variant and state; the set
//! walks a chain (the built-in default, the surface's theme, the overrides of the widget and its ancestors), a value
//! left unset falls to the next level, and the result is scaled to physical pixels. Colors are `0xAABBGGRR` (the draw
//! list's format); all metrics and font sizes are logical units.

namespace engine_placeholder::ui
{

/// Identifies a widget kind: `label`, `button`, or one a game registers. 0 is invalid.
using WidgetKind = u32;
/// Identifies a style variant of a kind (`primary`, `heading`). 0 is the kind's default style.
using VariantId = u32;

/// The kinds the toolkit ships with; their ids are fixed.
namespace widget_kind
{
constexpr WidgetKind kLabel = 1;        ///< Static text.
constexpr WidgetKind kButton = 2;       ///< A push button.
constexpr WidgetKind kCheckbox = 3;     ///< A check box.
constexpr WidgetKind kRadio = 4;        ///< A radio button.
constexpr WidgetKind kSlider = 5;       ///< A slider.
constexpr WidgetKind kTextField = 6;    ///< A one-line text field.
constexpr WidgetKind kScrollbar = 7;    ///< A scroll bar.
constexpr WidgetKind kPanel = 8;        ///< A container with a background.
constexpr WidgetKind kMenu = 9;         ///< A menu or popup list.
constexpr WidgetKind kTooltip = 10;     ///< A tooltip.
constexpr WidgetKind kTab = 11;         ///< A tab header cell (the checked state is the selected tab).
constexpr WidgetKind kSplitter = 12;    ///< A splitter handle.
constexpr WidgetKind kWindow = 13;      ///< The frame of a window or dialog.
constexpr WidgetKind kTitleBar = 14;    ///< The title bar of a window or dialog.
constexpr WidgetKind kModalDim = 15;    ///< The shade drawn over everything beneath a modal dialog.
constexpr WidgetKind kDropTarget = 16;  ///< The highlight drawn over a widget that a drag is over and would accept.
constexpr WidgetKind kDropMarker = 17;  ///< The insertion line drawn where a dragged item would land.
}  // namespace widget_kind

/// Returns the id of the widget kind called `name`, registering it on first use. Ids are stable for the life of the
/// process and the same for the same name.
///
/// A name is never forgotten, so register a fixed set of names (literals), not text that varies.
///
/// - Param name: 1 to 63 bytes.
/// - Returns: the id, or 0 if the name is empty or too long.
/// - Thread-safety: thread-safe.
WidgetKind RegisterWidgetKind(StringView name);
/// Returns the name registered for `kind`, or an empty string for an unknown id.
///
/// - Thread-safety: thread-safe.
String<HeapAllocator> WidgetKindName(WidgetKind kind);
/// Returns the id of the variant called `name`, registering it on first use; the empty name is variant 0, the kind's
/// default style.
///
/// - Returns: the id, or 0 for the empty name and for a name longer than 63 bytes.
/// - Thread-safety: thread-safe.
VariantId RegisterVariant(StringView name);
/// Returns the name registered for `variant`, or an empty string for 0 and unknown ids.
///
/// - Thread-safety: thread-safe.
String<HeapAllocator> VariantName(VariantId variant);

/// A value of a theme that may be left unset, so that a lower level of the chain supplies it.
///
/// - Thread-safety: thread-compatible plain data.
template <typename T> struct Opt
{
    T value{};         ///< The value, meaningful when `set`.
    bool set = false;  ///< Whether the theme gives a value.
    /// Makes an unset value.
    Opt() = default;
    /// Makes a set value.
    Opt(T v) : value(v), set(true) {}
};

/// The named colors of a theme.
enum class ColorId : u8
{
    Text,              ///< Text on backgrounds.
    TextDisabled,      ///< Text of a disabled widget.
    WindowBackground,  ///< The window's background.
    Panel,             ///< Panels and buttons.
    Accent,            ///< The highlight: primary buttons, focus, checked marks.
    Danger,            ///< Errors and destructive actions.
    Border,            ///< Borders and separators.
    Count              ///< Number of colors; not a color.
};
/// The named metrics of a theme, in logical units.
enum class MetricId : u8
{
    Padding,             ///< Default padding.
    Spacing,             ///< Gap between neighbours.
    BorderWidth,         ///< Default border thickness.
    CornerRadius,        ///< Default corner radius.
    ScrollbarThickness,  ///< Scroll bar thickness.
    MinHitSize,          ///< Smallest comfortable hit target.
    IconSize,            ///< Icon edge length.
    DragSlop,            ///< How far the pointer must move after a press before a drag starts.
    Count                ///< Number of metrics; not a metric.
};
/// The named fonts of a theme.
enum class FontId : u8
{
    Body,     ///< Running text.
    Heading,  ///< Headings.
    Mono,     ///< Fixed-width text.
    Count     ///< Number of fonts; not a font.
};

/// A font by family key and size.
///
/// The surface's font provider turns the key into a font. The size is in logical units in a theme, physical pixels in a
/// resolved style.
///
/// - Thread-safety: thread-compatible plain data.
struct FontRef
{
    u32 family = 0;    ///< Key of the family in the font provider.
    f32 size = 14.0f;  ///< Size.
    /// Returns whether both fields are equal.
    bool operator==(const FontRef& o) const { return family == o.family && size == o.size; }
};

/// The states a widget style distinguishes.
enum class StateKind : u8
{
    Normal,    ///< The base style, applied in every state.
    Hover,     ///< The pointer is over the widget.
    Pressed,   ///< The widget is being pressed.
    Focused,   ///< The widget has the keyboard focus.
    Disabled,  ///< The widget does not react.
    Checked,   ///< The widget is checked, selected or on.
    Count      ///< Number of states; not a state.
};

/// Bits of a widget's state, passed to `ThemeSet::Resolve`.
///
/// Several can be set; `Disabled` wins over `Pressed` over `Hover` over `Focused` over `Checked` over normal.
constexpr u32 kStateHover = 1;      ///< The pointer is over the widget.
constexpr u32 kStatePressed = 2;    ///< The widget is being pressed.
constexpr u32 kStateFocused = 4;    ///< The widget has the keyboard focus.
constexpr u32 kStateDisabled = 8;   ///< The widget does not react.
constexpr u32 kStateChecked = 16;   ///< The widget is checked, selected or on.
constexpr u32 kStateDragOver = 32;  ///< A drag it would accept is over the widget (kept by the surface; the widget
                                    ///< draws the `droptarget` style, it is not passed to `Resolve`).

/// The look of one widget in one state; every field may be unset.
///
/// - Thread-safety: thread-compatible plain data.
struct StateStyle
{
    Opt<u32> background;    ///< Fill color.
    Opt<u32> border_color;  ///< Border color.
    Opt<u32> text_color;    ///< Text color.
    Opt<f32> border_width;  ///< Border thickness; 0 draws none.
    Opt<f32> radius;        ///< Corner radius.
    Opt<f32> padding_x;     ///< Left and right padding.
    Opt<f32> padding_y;     ///< Top and bottom padding.
};

/// The style of one (kind, variant): a table over the states and a font.
///
/// - Thread-safety: thread-compatible plain data.
struct Style
{
    StateStyle states[static_cast<usize>(StateKind::Count)];  ///< Indexed by [[ui::StateKind]]; `Normal` applies in
                                                              ///< every state, the others on top of it.
    Opt<FontRef> font;                                        ///< The font of the widget's text.
    /// Returns the entry of `kind`.
    StateStyle& At(StateKind kind) { return states[static_cast<usize>(kind)]; }
    /// Returns the entry of `kind`.
    const StateStyle& At(StateKind kind) const { return states[static_cast<usize>(kind)]; }
};

/// A style entry of a theme.
///
/// - Thread-safety: thread-compatible plain data.
struct StyleEntry
{
    WidgetKind kind = 0;    ///< The widget kind.
    VariantId variant = 0;  ///< The variant, 0 for the kind's default.
    Style style;            ///< The look.
};

/// Plain theme data: a full set of values for a base theme, or just the few a widget's override changes.
///
/// - Thread-safety: thread-compatible; do not change a theme while a [[ui::ThemeSet]] resolves from it (call `SetTheme`
/// again afterwards).
/// - Ownership: owns its entries.
struct Theme
{
    Opt<u32> colors[static_cast<usize>(ColorId::Count)];    ///< Named colors.
    Opt<f32> metrics[static_cast<usize>(MetricId::Count)];  ///< Named metrics.
    Opt<FontRef> fonts[static_cast<usize>(FontId::Count)];  ///< Named fonts.
    Vector<StyleEntry> styles;                              ///< Style entries, at most one per (kind, variant).

    /// Returns the style of (`kind`, `variant`), or null.
    ///
    /// - Ownership: borrowed from this theme; valid until the theme changes.
    /// - Fails: returns null when the theme has no such entry.
    const Style* FindStyle(WidgetKind kind, VariantId variant) const;
};

/// Builds a [[ui::Theme]] in code (a data file loader arrives with Phase 21).
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns the theme until `Build`.
class ThemeBuilder
{
public:
    /// Sets a named color.
    ThemeBuilder& SetColor(ColorId id, u32 abgr);
    /// Sets a named metric (logical units).
    ThemeBuilder& SetMetric(MetricId id, f32 value);
    /// Sets a named font.
    ThemeBuilder& SetFont(FontId id, FontRef font);
    /// Sets the style of (`kind`, `variant`), replacing an earlier entry for the same pair.
    ThemeBuilder& SetStyle(WidgetKind kind, VariantId variant, const Style& style);
    /// Returns the theme; the builder is empty afterwards.
    Theme Build() { return static_cast<Theme&&>(theme_); }

private:
    Theme theme_;
};

/// The built-in light theme. Ordinary data, no special mode.
///
/// - Thread-safety: thread-safe; a constant.
const Theme& DefaultLightTheme();
/// The built-in dark theme.
///
/// - Thread-safety: thread-safe; a constant.
const Theme& DefaultDarkTheme();

/// The contrast ratio of two colors as WCAG defines it (1 to 21), ignoring alpha.
///
/// - Thread-safety: thread-safe.
f32 ContrastRatio(u32 abgr_a, u32 abgr_b);

/// A style resolved for a state and a scale: every value is set, sizes are in physical pixels.
///
/// - Thread-safety: thread-compatible plain data.
struct ResolvedStyle
{
    u32 background = 0;    ///< Fill color.
    u32 border_color = 0;  ///< Border color.
    u32 text_color = 0;    ///< Text color.
    f32 border_width = 0;  ///< Border thickness in whole pixels; at least 1 when the theme gives a positive width.
    f32 radius = 0;        ///< Corner radius in pixels (not rounded).
    f32 padding_x = 0;     ///< Left and right padding in whole pixels.
    f32 padding_y = 0;     ///< Top and bottom padding in whole pixels.
    FontRef font;          ///< The font with its size in pixels.
};

/// Counters of a [[ui::ThemeSet]].
///
/// - Thread-safety: thread-compatible plain data.
struct ThemeStats
{
    u64 resolves = 0;        ///< Calls of `Resolve`.
    u64 cache_hits = 0;      ///< Of those, answered from the cache.
    u64 missing_styles = 0;  ///< Calls that named a variant no theme in the chain defines (they got the kind's default
                             ///< style).
};

/// What a surface keeps: the theme in use and the cache of resolved styles.
///
/// The application owns the themes.
/// `Resolve` walks the chain least specific first: the built-in light theme, the surface's theme, then the `overrides`
/// (the nearest ancestor's override before the widget's own). For each theme the kind's default style is laid over the
/// result, then the variant's. A style with overrides is not cached; the one without is, keyed by kind, variant, state
/// and scale, and the cache is dropped by `SetTheme`.
///
/// - Thread-safety: owner-thread-only.
/// - Ownership: borrows the surface theme, which must outlive its use here.
class ThemeSet
{
public:
    /// Uses `theme` as the surface's theme and bumps the revision; the cache is dropped. Passing the same pointer again
    /// after the theme was edited forces a re-resolve.
    ///
    /// - Param theme: the theme, or null for the built-in default alone.
    /// - Returns: whether any metric or font differs from the previous theme, so the caller marks layout dirty.
    bool SetTheme(const Theme* theme);
    /// Returns the surface's theme, or null.
    ///
    /// - Ownership: borrowed; the application owns it.
    /// - Fails: returns null when no theme was set.
    const Theme* GetTheme() const { return theme_; }
    /// Returns a counter that changes on every `SetTheme`; a widget re-resolves when it differs from the one it saw.
    u64 Revision() const { return revision_; }

    /// Resolves the style of a widget.
    ///
    /// - Param kind: the widget kind.
    /// - Param variant: the variant, 0 for the default.
    /// - Param state: `kState*` bits.
    /// - Param scale: the surface's scale factor, above 0.
    /// - Param overrides: override themes, least specific first (may be empty); each borrowed for the call.
    /// - Aborts: `scale` is not above 0.
    ResolvedStyle Resolve(WidgetKind kind, VariantId variant, u32 state, f32 scale,
                          Span<const Theme* const> overrides = {});
    /// Like the first overload with the variant given by name (registered if new). A name no theme defines logs once
    /// and counts in `Stats().missing_styles`.
    ResolvedStyle Resolve(WidgetKind kind, StringView variant, u32 state, f32 scale,
                          Span<const Theme* const> overrides = {});

    /// Returns a named color.
    u32 Color(ColorId id, Span<const Theme* const> overrides = {}) const;
    /// Returns a named metric multiplied by `scale` (not rounded).
    f32 Metric(MetricId id, f32 scale, Span<const Theme* const> overrides = {}) const;
    /// Returns a named font with its size multiplied by `scale`.
    FontRef Font(FontId id, f32 scale, Span<const Theme* const> overrides = {}) const;

    /// Starts a subtree override: until the matching `PopOverride`, every call that is given no `overrides` uses
    /// `theme` after the overrides already pushed (an inner subtree's theme wins). A widget with
    /// `Widget::SetThemeOverride` is wrapped in a push and a pop while it is measured and drawn, so its whole subtree
    /// is styled by it. A style resolved under an override is not cached.
    ///
    /// - Param theme: the theme, not null; borrowed until the pop.
    void PushOverride(const Theme* theme) { active_.Push(theme); }
    /// Ends the innermost `PushOverride`.
    void PopOverride()
    {
        if (!active_.Empty())
        {
            active_.Pop();
        }
    }
    /// Returns how many overrides are pushed now.
    ///
    /// - Fails: never.
    usize ActiveOverrides() const { return active_.Size(); }

    /// Returns the counters.
    const ThemeStats& Stats() const { return stats_; }

private:
    struct CacheEntry
    {
        WidgetKind kind;
        VariantId variant;
        u32 state;
        f32 scale;
        bool variant_found;
        ResolvedStyle value;
    };
    ResolvedStyle Compute(WidgetKind kind, VariantId variant, u32 state, f32 scale, Span<const Theme* const> overrides,
                          bool* variant_found) const;
    void NoteMissing(WidgetKind kind, VariantId variant, StringView name);

    Span<const Theme* const> Chain(Span<const Theme* const> overrides) const
    {
        return overrides.Size() == 0 ? Span<const Theme* const>(active_.Data(), active_.Size()) : overrides;
    }

    const Theme* theme_ = nullptr;
    u64 revision_ = 0;
    Vector<const Theme*> active_;
    Vector<CacheEntry> cache_;
    Vector<u64> logged_missing_;
    ThemeStats stats_;
};

}  // namespace engine_placeholder::ui
