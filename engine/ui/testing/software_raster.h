#pragma once

#include "containers/vector.h"
#include "text/glyph_atlas.h"
#include "types.h"
#include "ui/draw_list.h"

//! A small CPU rasteriser for a [[ui::DrawList]], so widget tests compare pixels without a GPU. For
//! tests only; the renderer is the real thing.
//!
//! It follows the UI shader (`shaders/ui.frag.slang`): solid, rounded-rectangle (anti-aliased, with ring and shadow)
//! and glyph modes, premultiplied blending, the scissor of each command, a target that is not sRGB (colours are written
//! as given). Image-mode commands are skipped (a software test has no image textures).

namespace vse::ui
{

/// A CPU image of RGBA8 pixels, rows top to bottom.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns its pixels.
struct RasterImage
{
    u32 width = 0;    ///< Width in pixels.
    u32 height = 0;   ///< Height in pixels.
    Vector<u8> rgba;  ///< `width * height * 4` bytes, red first.

    /// Returns the four bytes of pixel (`x`, `y`).
    /// - Aborts: the pixel is outside the image.
    /// - Ownership: borrowed; the owner keeps it and it stays valid while the owner lives.
    const u8* Pixel(u32 x, u32 y) const;
};

/// Draws `list` (already sorted by layer) on a new `width` x `height` image cleared to `clear_abgr`.
///
/// - Param list: the draw list in physical pixels.
/// - Param atlas: the glyph atlas whose pages the glyph commands sample, or null (glyphs are then skipped).
/// - Param width: image width in pixels.
/// - Param height: image height in pixels.
/// - Param clear_abgr: the background, packed with red in the lowest byte.
/// - Param out: receives the image.
/// - Aborts: `out` is null or `width` or `height` is 0.
/// - Ownership: borrows `list` and `atlas` for the call; `out` receives the image.
void RasterizeDrawList(const DrawList& list, const text::GlyphAtlas* atlas, u32 width, u32 height, u32 clear_abgr,
                       RasterImage* out);

}  // namespace vse::ui
