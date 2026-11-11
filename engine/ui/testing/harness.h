#pragma once

#include "text/font.h"
#include "text/glyph_atlas.h"
#include "types.h"
#include "ui/draw_list.h"
#include "ui/surface.h"
#include "ui/testing/software_raster.h"

//! The offscreen test harness (Phase 15, Day 192): a surface with no window, a font, an atlas and helpers to send input, run frames and read rectangles and pixels.

namespace engine_placeholder::ui {

/// Everything a widget test needs: an offscreen [[ui::Surface]], a font provider that serves one font for every family key, a glyph atlas, the draw list of the last frame and
/// the software rasteriser.
///
/// Every input helper posts one event and runs a frame with `dt` 0, so the effect is visible when it returns.
///
/// - Thread-safety: owner-thread-only.
/// - Ownership: owns the surface, the font and the atlas.
class Harness final : public FontProvider {
public:
    /// Makes a harness.
    /// - Param width: surface width in logical units.
    /// - Param height: surface height in logical units.
    /// - Param scale: pixels per logical unit.
    /// - Param font_file: path of the TrueType font served for every family; if it cannot be opened `Valid()` is false and text is dropped.
    Harness(f32 width, f32 height, f32 scale, StringView font_file);

    /// Returns whether the font opened.
    ///
    /// - Fails: never.
    bool Valid() const { return font_ok_; }
    /// Returns the surface.
    Surface& GetSurface() { return surface_; }
    /// Returns the glyph atlas the draw list refers to.
    text::GlyphAtlas& Atlas() { return atlas_; }
    /// Returns the font, or null when `Valid()` is false.
    ///
    /// - Ownership: borrowed; the owner keeps it and it stays valid while the owner lives.
    /// - Fails: returns null when there is none.
    text::Font* GetFont() { return font_ok_ ? &font_ : nullptr; }
    /// Returns the draw list of the last frame that rebuilt it.
    const DrawList& List() const { return list_; }
    /// Serves the harness font for every family.
    /// - Fails: returns null when the font did not open.
    /// - Ownership: borrowed; the owner keeps it and it stays valid while the owner lives.
    text::Font* Resolve(u32 family) override;

    /// Runs a frame of `dt` seconds, rebuilding the draw list when needed; returns whether it was rebuilt.
    /// - Fails: never.
    bool Advance(f32 dt);
    /// Moves the pointer to (`x`, `y`) in surface coordinates.
    void PointerMove(f32 x, f32 y);
    /// Presses `button` (an [[os::MouseButtonBit]]) at (`x`, `y`).
    void PointerDown(f32 x, f32 y, u32 button = 1);
    /// Releases `button` at (`x`, `y`).
    void PointerUp(f32 x, f32 y, u32 button = 1);
    /// Presses and releases the left button at (`x`, `y`).
    void Click(f32 x, f32 y);
    /// Turns the wheel by (`dx`, `dy`) notches at (`x`, `y`).
    void Wheel(f32 x, f32 y, f32 dx, f32 dy);
    /// Sends a key press and, when `release` is set, the release.
    void Key(ui::Key key, bool release = true, u32 modifiers = 0);
    /// Types the characters of `utf8` one scalar at a time.
    void TypeText(StringView utf8);

    /// Finds the widget called `name` in the root or the popup layer, or null.
    ///
    /// - Ownership: borrowed; the owner keeps it and it stays valid while the owner lives.
    /// - Fails: returns null when there is none.
    Widget* Find(StringView name);
    /// Returns the bounds of the widget called `name`.
    /// - Aborts: no widget has that name.
    Rect RectOf(StringView name);
    /// Renders the last draw list to `out` on `clear_abgr` (size: the surface in pixels).
    /// - Ownership: borrowed; the caller keeps ownership and it must outlive its use here.
    void Render(RasterImage* out, u32 clear_abgr = 0xFF000000u);

private:
    text::FontLibrary library_;
    text::Font font_;
    bool font_ok_ = false;
    text::GlyphAtlas atlas_;
    DrawList list_;
    Surface surface_;
};

} // namespace engine_placeholder::ui
