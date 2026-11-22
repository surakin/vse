#pragma once

#include "containers/vector.h"
#include "text/layout.h"
#include "types.h"
#include "ui/theme.h"

//! A string laid out in one font, cached: what a label, a button and a check box draw their text
//! from.

namespace vse::ui
{

class Surface;

/// One string with its laid-out form, redone only when something it depends on changes.
///
/// The layout depends on the text, the alignment, the font object, the pixel size, the wrap width and the surface
/// scale. `Fit` gives the layout that fits a width: the full one, or the text cut at a cluster boundary with an
/// ellipsis (U+2026) appended when it is wider; the cut layout is cached too.
///
/// - Thread-safety: owner-thread-only.
/// - Ownership: owns the text and the layouts; borrows the font only during `Ensure`.
class TextRun
{
public:
    /// Makes a run with no text.
    TextRun() = default;
    /// Sets the text; a different text invalidates the layouts. Returns whether it changed.
    /// - Fails: never.
    bool SetText(StringView text);
    /// Returns the text.
    StringView Text() const { return StringView(text_.Data(), text_.Size()); }
    /// Sets the alignment of the lines inside the wrap width; a change invalidates the layouts. Returns whether it
    /// changed.
    /// - Fails: never.
    bool SetAlign(text::Align align);

    /// Makes the full layout current for the given font and sizes.
    ///
    /// - Param face: the font; its pixel size is set here.
    /// - Param family: the font's family key (part of the cache key).
    /// - Param size_px: the pixel size, at least 1.
    /// - Param wrap_px: the wrap width in pixels, 0 for no wrapping.
    /// - Param scale: the surface scale (part of the cache key; `Width`/`Height` divide by it).
    /// - Returns: whether the layout is usable.
    /// - Ownership: borrows `face` for the call only.
    /// - Fails: returns false for a null font or a font the layout refuses.
    bool Ensure(text::Font* face, u32 family, f32 size_px, f32 wrap_px, f32 scale);
    /// Like `Ensure`, with the font taken from a resolved style (size in physical pixels, rounded to whole pixels) and
    /// the face from the surface's font provider.
    /// - Ownership: borrows `surface` for the call only.
    /// - Returns: whether the layout is usable; false without a surface, a font provider or a face for the style's
    /// family.
    bool EnsureForStyle(Surface* surface, const ResolvedStyle& style, f32 wrap_px);
    /// Returns the full layout from the last successful `Ensure`.
    const text::TextLayout& Full() const { return full_; }
    /// Returns the layout to draw in `limit_px` pixels: the full one when it fits (or `limit_px` is 0), else the cut
    /// one with an ellipsis. Call after `Ensure`.
    const text::TextLayout& Fit(f32 limit_px);
    /// Returns the style (font and pixel size) the layouts were made with.
    const text::TextStyle& Style() const { return style_; }
    /// Returns the width of the full layout in logical units.
    f32 Width() const { return full_.width / scale_; }
    /// Returns the height of the full layout in logical units.
    f32 Height() const { return full_.height / scale_; }
    /// Returns how many times the full layout was made (so a test can see the cache work).
    u64 LayoutCount() const { return layout_count_; }

private:
    Vector<char> text_;  // not NUL-terminated; assigning keeps the capacity, so a row that is re-used for other text
                         // allocates nothing once warm
    text::Align align_ = text::Align::Left;
    text::TextStyle style_;
    text::TextLayout full_;
    text::TextLayout cut_;
    bool full_valid_ = false;
    bool cut_valid_ = false;
    f32 cut_limit_ = 0;
    f32 wrap_px_ = 0;
    u32 pixel_size_ = 0;
    u32 family_ = 0;
    const void* font_ = nullptr;
    f32 scale_ = 1;
    u64 layout_count_ = 0;
};

}  // namespace vse::ui
