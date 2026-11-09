#pragma once

#include "mem/heap_allocator.h"
#include "string/string.h"
#include "text/layout.h"
#include "types.h"
#include "ui/text_run.h"
#include "ui/theme.h"
#include "ui/widget.h"

//! A block of text that sizes itself from its content (Phase 15, Day 193): the base of the label widget.

namespace engine_placeholder::ui {

/// Draws one string in one font and reports its size to the layout.
///
/// The text is laid out in physical pixels (font size times the surface scale) and the result converted back to logical units. With wrapping on, the width given to `Measure` is the
/// wrap width, so the height depends on the width: a container measures the block at the width it will give it. The layout is cached on (text, font, pixel size, wrap width, font
/// object); an unchanged block does not lay out again. The default color is the theme's `Text`, the default font the theme's `Body`; a block with a style kind (a [[ui::Label]]) takes both from
/// its style in the theme instead, in the widget's state (so a disabled label uses the disabled color).
///
/// Thread-safety: owner-thread-only.
/// Ownership: owns its text and the cached layout; borrows the font from the surface's font provider.
class TextBlock : public Widget {
public:
    /// Makes a block.
    /// Param name: the widget name.
    /// Param text: the UTF-8 text.
    explicit TextBlock(StringView name = StringView(), StringView text = StringView());
    /// Sets the text; marks layout dirty when it changes.
    void SetText(StringView text);
    /// Returns the text.
    StringView GetText() const { return run_.Text(); }
    /// Uses `font` (family key and logical size) instead of the theme's font; marks layout dirty.
    void SetFont(FontRef font);
    /// Uses `color` (packed RGBA, red lowest) instead of the theme's text color; marks the draw dirty.
    void SetColor(u32 abgr);
    /// Wraps the text at the width it is measured or arranged at (default off); marks layout dirty.
    void SetWrap(bool wrap);
    /// Aligns the text horizontally in the bounds (default left); marks the draw dirty. With wrapping the lines are aligned inside the wrap width.
    void SetAlign(text::Align align);
    /// Cuts a single line that is wider than the bounds and ends it with an ellipsis (default off); ignored when wrapping. Marks the draw dirty.
    void SetEllipsis(bool ellipsis);
    /// Returns how many times the text was laid out (so a test can see the cache work).
    u64 LayoutCount() const { return run_.LayoutCount(); }
    /// Returns the size of the text, wrapped at `constraints.max_w` when wrapping is on.
    Size Measure(const Constraints& constraints) override;
    /// Draws the text at the top of the bounds.
    void Draw(Canvas& canvas) override;

protected:
    /// Takes the font and color from the style of (`kind`, `variant`) in the theme; marks layout dirty.
    void SetLook(WidgetKind kind, VariantId variant);

private:
    bool EnsureLayout(f32 wrap_width);
    TextRun run_;
    FontRef font_;
    bool font_set_ = false;
    u32 color_ = 0;
    bool color_set_ = false;
    bool wrap_ = false;
    bool ellipsis_ = false;
    text::Align align_ = text::Align::Left;
    WidgetKind kind_ = 0;
    VariantId variant_ = 0;
};

} // namespace engine_placeholder::ui
