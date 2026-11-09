#include "ui/theme.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "log/assert.h"
#include "log/log.h"

namespace engine_placeholder::ui {

namespace {

constexpr usize kMaxName = 63;
constexpr usize kMaxCache = 4096;

struct Name {
    char text[kMaxName + 1];
    u8 length;
};

// A process-wide table of names to ids; ids are 1-based indices.
class NameTable {
public:
    // `builtin`: seed the ids of `widget_kind` (1 to 10, in this order).
    explicit NameTable(bool builtin) {
        if (builtin) {
            const char* const names[] = {"label", "button", "checkbox", "radio", "slider", "textfield", "scrollbar", "panel", "menu", "tooltip", "tab", "splitter", "window", "titlebar", "modaldim", "droptarget", "dropmarker"};
            for (const char* name : names) Register(StringView(name));
        }
    }
    u32 Register(StringView name) {
        if (name.Length() == 0 || name.Length() > kMaxName) return 0;
        std::lock_guard<std::mutex> lock(mutex_);
        for (usize i = 0; i < names_.Size(); ++i) {
            if (names_[i].length == name.Length() && std::memcmp(names_[i].text, name.Data(), name.Length()) == 0) return static_cast<u32>(i + 1);
        }
        Name n;
        std::memcpy(n.text, name.Data(), name.Length());
        n.text[name.Length()] = '\0';
        n.length = static_cast<u8>(name.Length());
        names_.Push(n);
        return static_cast<u32>(names_.Size());
    }
    String<HeapAllocator> Get(u32 id) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (id == 0 || id > names_.Size()) return String<HeapAllocator>(HeapAllocator::Default());
        return String<HeapAllocator>(HeapAllocator::Default(), StringView(names_[id - 1].text, names_[id - 1].length));
    }

private:
    std::mutex mutex_;
    Vector<Name> names_;
};

NameTable& Kinds() {
    static NameTable table(true);
    return table;
}

NameTable& Variants() {
    static NameTable table(false);
    return table;
}

const StateKind kOrder[] = {StateKind::Normal, StateKind::Checked, StateKind::Focused, StateKind::Hover, StateKind::Pressed, StateKind::Disabled};

bool Applies(StateKind kind, u32 state) {
    switch (kind) {
        case StateKind::Normal: return true;
        case StateKind::Hover: return (state & kStateHover) != 0;
        case StateKind::Pressed: return (state & kStatePressed) != 0;
        case StateKind::Focused: return (state & kStateFocused) != 0;
        case StateKind::Disabled: return (state & kStateDisabled) != 0;
        case StateKind::Checked: return (state & kStateChecked) != 0;
        case StateKind::Count: break;
    }
    return false;
}

template <typename T>
void Overlay(Opt<T>* dst, const Opt<T>& src) {
    if (src.set) *dst = src;
}

void OverlayState(StateStyle* dst, const StateStyle& src) {
    Overlay(&dst->background, src.background);
    Overlay(&dst->border_color, src.border_color);
    Overlay(&dst->text_color, src.text_color);
    Overlay(&dst->border_width, src.border_width);
    Overlay(&dst->radius, src.radius);
    Overlay(&dst->padding_x, src.padding_x);
    Overlay(&dst->padding_y, src.padding_y);
}

f32 RoundPixels(f32 logical, f32 scale) {
    const f32 v = std::round(logical * scale);
    return v < 0 ? 0 : v;
}

// Calls fn(theme) for every theme of the chain, least specific first.
template <typename Fn>
void ForChain(const Theme* surface, Span<const Theme* const> overrides, Fn fn) {
    fn(DefaultLightTheme());
    if (surface != nullptr) fn(*surface);
    for (usize i = 0; i < overrides.Size(); ++i) {
        if (overrides[i] != nullptr) fn(*overrides[i]);
    }
}

struct Palette {
    u32 text, text_disabled, window, panel, accent, accent_text, danger, border, hover, pressed, field;
};

Theme BuildTheme(const Palette& p) {
    ThemeBuilder b;
    b.SetColor(ColorId::Text, p.text).SetColor(ColorId::TextDisabled, p.text_disabled).SetColor(ColorId::WindowBackground, p.window).SetColor(ColorId::Panel, p.panel);
    b.SetColor(ColorId::Accent, p.accent).SetColor(ColorId::Danger, p.danger).SetColor(ColorId::Border, p.border);
    b.SetMetric(MetricId::Padding, 8).SetMetric(MetricId::Spacing, 6).SetMetric(MetricId::BorderWidth, 1).SetMetric(MetricId::CornerRadius, 4);
    b.SetMetric(MetricId::ScrollbarThickness, 10).SetMetric(MetricId::MinHitSize, 24).SetMetric(MetricId::IconSize, 16).SetMetric(MetricId::DragSlop, 4);
    FontRef body;
    body.family = 0;
    body.size = 14;
    FontRef heading = body;
    heading.size = 20;
    FontRef mono = body;
    mono.family = 1;
    b.SetFont(FontId::Body, body).SetFont(FontId::Heading, heading).SetFont(FontId::Mono, mono);

    // label: transparent, text color from the theme; the heading variant is larger.
    Style label;
    label.At(StateKind::Normal).text_color = p.text;
    label.At(StateKind::Normal).padding_x = 0.0f;
    label.At(StateKind::Normal).padding_y = 0.0f;
    label.At(StateKind::Disabled).text_color = p.text_disabled;
    label.font = body;
    b.SetStyle(widget_kind::kLabel, 0, label);
    Style heading_label;
    heading_label.font = heading;
    b.SetStyle(widget_kind::kLabel, RegisterVariant("heading"), heading_label);

    // button: panel-colored, a border, hover and pressed shades; the primary variant is the accent.
    Style button;
    StateStyle& n = button.At(StateKind::Normal);
    n.background = p.panel;
    n.border_color = p.border;
    n.text_color = p.text;
    n.border_width = 1.0f;
    n.radius = 4.0f;
    n.padding_x = 12.0f;
    n.padding_y = 6.0f;
    button.At(StateKind::Hover).background = p.hover;
    button.At(StateKind::Pressed).background = p.pressed;
    button.At(StateKind::Focused).border_color = p.accent;
    button.At(StateKind::Disabled).text_color = p.text_disabled;
    button.At(StateKind::Disabled).background = p.window;
    button.At(StateKind::Checked).background = p.pressed;
    b.SetStyle(widget_kind::kButton, 0, button);
    Style primary;
    primary.At(StateKind::Normal).background = p.accent;
    primary.At(StateKind::Normal).border_color = p.accent;
    primary.At(StateKind::Normal).text_color = p.accent_text;
    primary.At(StateKind::Hover).background = p.accent;
    primary.At(StateKind::Pressed).background = p.accent;
    primary.At(StateKind::Disabled).background = p.border;
    primary.At(StateKind::Disabled).text_color = p.text_disabled;
    b.SetStyle(widget_kind::kButton, RegisterVariant("primary"), primary);

    Style panel;
    panel.At(StateKind::Normal).background = p.panel;
    panel.At(StateKind::Normal).border_color = p.border;
    panel.At(StateKind::Normal).border_width = 1.0f;
    panel.At(StateKind::Normal).radius = 6.0f;
    panel.At(StateKind::Normal).padding_x = 8.0f;
    panel.At(StateKind::Normal).padding_y = 8.0f;
    b.SetStyle(widget_kind::kPanel, 0, panel);

    Style field;
    field.At(StateKind::Normal).background = p.field;
    field.At(StateKind::Normal).border_color = p.border;
    field.At(StateKind::Normal).text_color = p.text;
    field.At(StateKind::Normal).border_width = 1.0f;
    field.At(StateKind::Normal).radius = 3.0f;
    field.At(StateKind::Normal).padding_x = 6.0f;
    field.At(StateKind::Normal).padding_y = 4.0f;
    field.At(StateKind::Focused).border_color = p.accent;
    field.At(StateKind::Disabled).text_color = p.text_disabled;
    b.SetStyle(widget_kind::kTextField, 0, field);

    Style check;
    check.At(StateKind::Normal).background = p.field;
    check.At(StateKind::Normal).border_color = p.border;
    check.At(StateKind::Normal).text_color = p.text;
    check.At(StateKind::Normal).border_width = 1.0f;
    check.At(StateKind::Normal).radius = 3.0f;
    check.At(StateKind::Checked).background = p.accent;
    check.At(StateKind::Focused).border_color = p.accent;
    check.At(StateKind::Disabled).text_color = p.text_disabled;
    b.SetStyle(widget_kind::kCheckbox, 0, check);

    Style radio = check; // a radio button is a check box drawn round
    radio.At(StateKind::Normal).radius = 8.0f;
    b.SetStyle(widget_kind::kRadio, 0, radio);

    Style slider; // the track; the thumb is drawn in the scroll bar's colors
    slider.At(StateKind::Normal).background = p.border;
    slider.At(StateKind::Normal).radius = 3.0f;
    slider.At(StateKind::Focused).border_color = p.accent;
    slider.At(StateKind::Focused).border_width = 1.0f;
    slider.At(StateKind::Disabled).background = p.window;
    b.SetStyle(widget_kind::kSlider, 0, slider);

    Style menu; // a row of a list or a menu: transparent, hover shade, selected in the accent
    menu.At(StateKind::Normal).text_color = p.text;
    menu.At(StateKind::Normal).padding_x = 6.0f;
    menu.At(StateKind::Normal).padding_y = 3.0f;
    menu.At(StateKind::Hover).background = p.hover;
    menu.At(StateKind::Checked).background = p.accent;
    menu.At(StateKind::Checked).text_color = p.accent_text;
    menu.At(StateKind::Disabled).text_color = p.text_disabled;
    b.SetStyle(widget_kind::kMenu, 0, menu);

    Style tooltip;
    tooltip.At(StateKind::Normal).background = p.text;
    tooltip.At(StateKind::Normal).text_color = p.window;
    tooltip.At(StateKind::Normal).radius = 3.0f;
    tooltip.At(StateKind::Normal).padding_x = 6.0f;
    tooltip.At(StateKind::Normal).padding_y = 4.0f;
    b.SetStyle(widget_kind::kTooltip, 0, tooltip);

    Style tab; // a tab header cell: transparent, hover shade, the selected tab on the panel color with a border
    tab.At(StateKind::Normal).text_color = p.text;
    tab.At(StateKind::Normal).padding_x = 10.0f;
    tab.At(StateKind::Normal).padding_y = 5.0f;
    tab.At(StateKind::Hover).background = p.hover;
    tab.At(StateKind::Pressed).background = p.pressed;
    tab.At(StateKind::Checked).background = p.panel;
    tab.At(StateKind::Checked).border_color = p.border;
    tab.At(StateKind::Checked).border_width = 1.0f;
    tab.At(StateKind::Disabled).text_color = p.text_disabled;
    b.SetStyle(widget_kind::kTab, 0, tab);

    Style splitter;
    splitter.At(StateKind::Hover).background = p.border;
    splitter.At(StateKind::Pressed).background = p.accent;
    b.SetStyle(widget_kind::kSplitter, 0, splitter);

    Style window;
    window.At(StateKind::Normal).background = p.panel;
    window.At(StateKind::Normal).border_color = p.border;
    window.At(StateKind::Normal).border_width = 1.0f;
    window.At(StateKind::Normal).radius = 6.0f;
    window.At(StateKind::Normal).padding_x = 12.0f;
    window.At(StateKind::Normal).padding_y = 12.0f;
    window.At(StateKind::Focused).border_color = p.accent;
    b.SetStyle(widget_kind::kWindow, 0, window);

    Style title;
    title.At(StateKind::Normal).background = p.window;
    title.At(StateKind::Normal).text_color = p.text;
    title.At(StateKind::Normal).padding_x = 10.0f;
    title.At(StateKind::Normal).padding_y = 6.0f;
    title.At(StateKind::Hover).background = p.danger; // the close button under the pointer
    b.SetStyle(widget_kind::kTitleBar, 0, title);

    Style dim;
    dim.At(StateKind::Normal).background = 0x80000000u; // half-transparent black, 0xAABBGGRR
    b.SetStyle(widget_kind::kModalDim, 0, dim);

    Style drop_target;
    drop_target.At(StateKind::Normal).background = ((p.accent & 0x00FFFFFFu) | 0x28000000u);
    drop_target.At(StateKind::Normal).border_color = p.accent;
    drop_target.At(StateKind::Normal).border_width = 1.0f;
    b.SetStyle(widget_kind::kDropTarget, 0, drop_target);

    Style drop_marker;
    drop_marker.At(StateKind::Normal).background = p.accent;
    b.SetStyle(widget_kind::kDropMarker, 0, drop_marker);

    Style scrollbar;
    scrollbar.At(StateKind::Normal).background = p.border;
    scrollbar.At(StateKind::Normal).radius = 5.0f;
    scrollbar.At(StateKind::Hover).background = p.text_disabled;
    scrollbar.At(StateKind::Pressed).background = p.accent;
    b.SetStyle(widget_kind::kScrollbar, 0, scrollbar);
    return b.Build();
}

} // namespace

WidgetKind RegisterWidgetKind(StringView name) { return Kinds().Register(name); }
String<HeapAllocator> WidgetKindName(WidgetKind kind) { return Kinds().Get(kind); }
VariantId RegisterVariant(StringView name) { return Variants().Register(name); }
String<HeapAllocator> VariantName(VariantId variant) { return Variants().Get(variant); }

const Style* Theme::FindStyle(WidgetKind kind, VariantId variant) const {
    for (usize i = 0; i < styles.Size(); ++i) {
        if (styles[i].kind == kind && styles[i].variant == variant) return &styles[i].style;
    }
    return nullptr;
}

ThemeBuilder& ThemeBuilder::SetColor(ColorId id, u32 abgr) {
    theme_.colors[static_cast<usize>(id)] = abgr;
    return *this;
}
ThemeBuilder& ThemeBuilder::SetMetric(MetricId id, f32 value) {
    theme_.metrics[static_cast<usize>(id)] = value;
    return *this;
}
ThemeBuilder& ThemeBuilder::SetFont(FontId id, FontRef font) {
    theme_.fonts[static_cast<usize>(id)] = font;
    return *this;
}
ThemeBuilder& ThemeBuilder::SetStyle(WidgetKind kind, VariantId variant, const Style& style) {
    for (usize i = 0; i < theme_.styles.Size(); ++i) {
        if (theme_.styles[i].kind == kind && theme_.styles[i].variant == variant) {
            theme_.styles[i].style = style;
            return *this;
        }
    }
    StyleEntry entry;
    entry.kind = kind;
    entry.variant = variant;
    entry.style = style;
    theme_.styles.Push(entry);
    return *this;
}

const Theme& DefaultLightTheme() {
    // Colors are 0xAABBGGRR.
    static const Theme theme = BuildTheme(
        Palette{0xFF202020u, 0xFF8A8A8Au, 0xFFF2F2F2u, 0xFFFFFFFFu, 0xFFC05F1Fu, 0xFFFFFFFFu, 0xFF2F2FC0u, 0xFFC8C8C8u, 0xFFEDEDEDu, 0xFFDADADAu, 0xFFFFFFFFu});
    return theme;
}

const Theme& DefaultDarkTheme() {
    static const Theme theme = BuildTheme(
        Palette{0xFFE8E8E8u, 0xFF8C8C8Cu, 0xFF1E1E1Eu, 0xFF2B2B2Bu, 0xFFC0742Au, 0xFFFFFFFFu, 0xFF5050E0u, 0xFF4A4A4Au, 0xFF383838u, 0xFF454545u, 0xFF262626u});
    return theme;
}

f32 ContrastRatio(u32 a, u32 b) {
    auto channel = [](u32 v) {
        const f32 c = static_cast<f32>(v) / 255.0f;
        return c <= 0.03928f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
    };
    auto luminance = [&](u32 abgr) { return 0.2126f * channel(abgr & 0xFF) + 0.7152f * channel((abgr >> 8) & 0xFF) + 0.0722f * channel((abgr >> 16) & 0xFF); };
    const f32 la = luminance(a);
    const f32 lb = luminance(b);
    const f32 hi = la > lb ? la : lb;
    const f32 lo = la > lb ? lb : la;
    return (hi + 0.05f) / (lo + 0.05f);
}

namespace {

// The padding or font a surface theme gives a style as the chain resolves it: its own value when it sets one, else the built-in default's.
template <typename T>
bool SameEffective(const Opt<T>& a, const Opt<T>& a_default, const Opt<T>& b, const Opt<T>& b_default) {
    const Opt<T>& x = a.set ? a : a_default;
    const Opt<T>& y = b.set ? b : b_default;
    return x.set == y.set && (!x.set || x.value == y.value);
}

// Whether two themes size the widgets of (kind, variant) differently: the font and the padding of any state.
bool LayoutStyleDiffers(const Theme& before, const Theme& after, WidgetKind kind, VariantId variant) {
    const Style empty;
    const Style* base = DefaultLightTheme().FindStyle(kind, variant);
    if (base == nullptr) base = &empty;
    const Style* a = before.FindStyle(kind, variant);
    const Style* b = after.FindStyle(kind, variant);
    if (a == nullptr) a = &empty;
    if (b == nullptr) b = &empty;
    if (!SameEffective(a->font, base->font, b->font, base->font)) return true;
    for (usize k = 0; k < static_cast<usize>(StateKind::Count); ++k) {
        if (!SameEffective(a->states[k].padding_x, base->states[k].padding_x, b->states[k].padding_x, base->states[k].padding_x)) return true;
        if (!SameEffective(a->states[k].padding_y, base->states[k].padding_y, b->states[k].padding_y, base->states[k].padding_y)) return true;
    }
    return false;
}

bool ThemesDifferInLayoutStyles(const Theme& before, const Theme& after) {
    for (usize i = 0; i < after.styles.Size(); ++i) {
        if (LayoutStyleDiffers(before, after, after.styles[i].kind, after.styles[i].variant)) return true;
    }
    for (usize i = 0; i < before.styles.Size(); ++i) {
        if (LayoutStyleDiffers(before, after, before.styles[i].kind, before.styles[i].variant)) return true;
    }
    return false;
}

} // namespace

bool ThemeSet::SetTheme(const Theme* theme) {
    const Theme& before = theme_ != nullptr ? *theme_ : DefaultLightTheme();
    const Theme& after = theme != nullptr ? *theme : DefaultLightTheme();
    // Metrics and fonts as the chain sees them: compare what the surface theme layers over the default.
    bool changed = false;
    for (usize i = 0; i < static_cast<usize>(MetricId::Count); ++i) {
        const f32 a = before.metrics[i].set ? before.metrics[i].value : DefaultLightTheme().metrics[i].value;
        const f32 b = after.metrics[i].set ? after.metrics[i].value : DefaultLightTheme().metrics[i].value;
        if (a != b) changed = true;
    }
    for (usize i = 0; i < static_cast<usize>(FontId::Count); ++i) {
        const FontRef a = before.fonts[i].set ? before.fonts[i].value : DefaultLightTheme().fonts[i].value;
        const FontRef b = after.fonts[i].set ? after.fonts[i].value : DefaultLightTheme().fonts[i].value;
        if (!(a == b)) changed = true;
    }
    if (!changed && ThemesDifferInLayoutStyles(before, after)) changed = true; // a style's padding or font sizes the widgets of its kind
    theme_ = theme;
    ++revision_;
    cache_.Clear();
    return changed;
}

ResolvedStyle ThemeSet::Compute(WidgetKind kind, VariantId variant, u32 state, f32 scale, Span<const Theme* const> overrides, bool* variant_found) const {
    StateStyle acc;
    Opt<FontRef> font;
    bool found = variant == 0;
    ForChain(theme_, overrides, [&](const Theme& t) {
        for (int pass = 0; pass < 2; ++pass) {
            if (pass == 1 && variant == 0) break;
            const Style* s = t.FindStyle(kind, pass == 0 ? 0 : variant);
            if (s == nullptr) continue;
            if (pass == 1) found = true;
            for (StateKind k : kOrder) {
                if (Applies(k, state)) OverlayState(&acc, s->At(k));
            }
            Overlay(&font, s->font);
        }
    });
    *variant_found = found;

    ResolvedStyle r;
    r.background = acc.background.set ? acc.background.value : 0;
    r.border_color = acc.border_color.set ? acc.border_color.value : Color(ColorId::Border, overrides);
    r.text_color = acc.text_color.set ? acc.text_color.value : Color(ColorId::Text, overrides);
    const f32 bw = acc.border_width.set ? acc.border_width.value : 0.0f;
    r.border_width = bw > 0 ? (RoundPixels(bw, scale) < 1.0f ? 1.0f : RoundPixels(bw, scale)) : 0.0f;
    r.radius = (acc.radius.set ? acc.radius.value : 0.0f) * scale;
    r.padding_x = RoundPixels(acc.padding_x.set ? acc.padding_x.value : Metric(MetricId::Padding, 1.0f, overrides), scale);
    r.padding_y = RoundPixels(acc.padding_y.set ? acc.padding_y.value : Metric(MetricId::Padding, 1.0f, overrides), scale);
    r.font = font.set ? font.value : Font(FontId::Body, 1.0f, overrides);
    r.font.size *= scale;
    return r;
}

void ThemeSet::NoteMissing(WidgetKind kind, VariantId variant, StringView name) {
    ++stats_.missing_styles;
    const u64 key = (static_cast<u64>(kind) << 32) | variant;
    for (usize i = 0; i < logged_missing_.Size(); ++i) {
        if (logged_missing_[i] == key) return;
    }
    logged_missing_.Push(key);
    const String<HeapAllocator> kind_name = WidgetKindName(kind);
    char line[160];
    std::snprintf(line, sizeof(line), "ui theme: no style '%.*s' for widget kind '%s'; the default style of the kind is used", static_cast<int>(name.Length()), name.Data(),
                  kind_name.CStr());
    Log(LogCore, LogVerbosity::Warn, StringView(line));
}

ResolvedStyle ThemeSet::Resolve(WidgetKind kind, VariantId variant, u32 state, f32 scale, Span<const Theme* const> overrides) {
    ENGINE_ASSERT(scale > 0, "ThemeSet::Resolve: the scale must be above 0");
    ++stats_.resolves;
    overrides = Chain(overrides);
    const bool cacheable = overrides.Size() == 0;
    if (cacheable) {
        for (usize i = 0; i < cache_.Size(); ++i) {
            const CacheEntry& e = cache_[i];
            if (e.kind == kind && e.variant == variant && e.state == state && e.scale == scale) {
                ++stats_.cache_hits;
                if (!e.variant_found) NoteMissing(kind, variant, StringView(VariantName(variant).CStr()));
                return e.value;
            }
        }
    }
    bool found = true;
    const ResolvedStyle r = Compute(kind, variant, state, scale, overrides, &found);
    if (!found) NoteMissing(kind, variant, StringView(VariantName(variant).CStr()));
    if (cacheable) {
        if (cache_.Size() >= kMaxCache) cache_.Clear();
        CacheEntry e;
        e.kind = kind;
        e.variant = variant;
        e.state = state;
        e.scale = scale;
        e.variant_found = found;
        e.value = r;
        cache_.Push(e);
    }
    return r;
}

ResolvedStyle ThemeSet::Resolve(WidgetKind kind, StringView variant, u32 state, f32 scale, Span<const Theme* const> overrides) {
    return Resolve(kind, RegisterVariant(variant), state, scale, overrides);
}

u32 ThemeSet::Color(ColorId id, Span<const Theme* const> overrides) const {
    u32 value = 0;
    ForChain(theme_, Chain(overrides), [&](const Theme& t) {
        const Opt<u32>& c = t.colors[static_cast<usize>(id)];
        if (c.set) value = c.value;
    });
    return value;
}

f32 ThemeSet::Metric(MetricId id, f32 scale, Span<const Theme* const> overrides) const {
    f32 value = 0;
    ForChain(theme_, Chain(overrides), [&](const Theme& t) {
        const Opt<f32>& m = t.metrics[static_cast<usize>(id)];
        if (m.set) value = m.value;
    });
    return value * scale;
}

FontRef ThemeSet::Font(FontId id, f32 scale, Span<const Theme* const> overrides) const {
    FontRef value;
    ForChain(theme_, Chain(overrides), [&](const Theme& t) {
        const Opt<FontRef>& f = t.fonts[static_cast<usize>(id)];
        if (f.set) value = f.value;
    });
    value.size *= scale;
    return value;
}

} // namespace engine_placeholder::ui
