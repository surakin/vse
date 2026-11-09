#pragma once

#include "lang/scalar.h"

#include "containers/vector.h"
#include "text/font.h"
#include "text/shelf_packer.h"
#include "types.h"

//! The glyph atlas (Phase 14): rasterized glyphs packed into 8-bit coverage pages held in CPU memory.
//!
//! The CPU pages are the source of truth; the renderer copies them into GPU textures and can rebuild those textures from them after a device reset.
//! There is no eviction in this version: a full page adds a page, up to a maximum, after which glyphs are refused and counted.
namespace engine_placeholder::text {

/// Where one glyph lives in the atlas and how to place it.
///
/// Thread-safety: thread-compatible plain data.
/// Ownership: owns nothing.
struct GlyphEntry {
    bool valid = false;       ///< False when the glyph could not be added (see the status of [[text::GlyphAtlas::GetOrAdd]]).
    bool has_pixels = false;  ///< False for a glyph with no outline (a space): nothing to draw, only the advance.
    u32 page = 0;             ///< The atlas page.
    Rect rect;                ///< The glyph's pixels in the page.
    f32 u0 = 0;               ///< Left texture coordinate (0..1) of `rect`.
    f32 v0 = 0;               ///< Top texture coordinate of `rect`.
    f32 u1 = 0;               ///< Right texture coordinate of `rect`.
    f32 v1 = 0;               ///< Bottom texture coordinate of `rect`.
    i32 left = 0;             ///< Pen to the left column, as [[text::GlyphBitmap::left]].
    i32 top = 0;              ///< Baseline up to the top row, as [[text::GlyphBitmap::top]].
    f32 advance = 0;          ///< Horizontal advance in pixels.
};

/// Counters of an atlas, for tests and the benchmark.
///
/// Thread-safety: thread-compatible plain data.
/// Ownership: owns nothing.
struct AtlasStats {
    u64 glyphs = 0;    ///< Glyphs stored (including blank ones).
    u64 pages = 0;     ///< Pages allocated.
    u64 hits = 0;      ///< Lookups answered from the atlas.
    u64 misses = 0;    ///< Lookups that rasterized a glyph.
    u64 bytes = 0;     ///< Bytes of page memory.
    u64 too_large = 0; ///< Lookups refused for a font size above [[text::kMaxAtlasPixelSize]].
    u64 failures = 0;  ///< Lookups that could not add the glyph (atlas full, glyph larger than a page, out of memory).
};

/// A set of 8-bit coverage pages keyed by (font id, pixel size, glyph id).
///
/// One atlas serves several fonts and sizes; asking twice for the same key returns the same entry and stores no second copy. Each page tracks the one rectangle that
/// covers what changed since the last [[text::GlyphAtlas::TakeDirty]], so a frame uploads only that.
///
/// Thread-safety: thread-compatible.
/// Ownership: owns its pages; the fonts passed to `GetOrAdd` are only used during the call.
class GlyphAtlas {
public:
    /// Creates an empty atlas.
    ///
    /// Param page_size: width and height of each square page in pixels.
    /// Param max_pages: how many pages the atlas may allocate.
    explicit GlyphAtlas(u32 page_size = 1024, u32 max_pages = 8);

    /// Returns the entry for `glyph` of `font` at the font's current pixel size, rasterizing and packing it on first use.
    ///
    /// Param font: the font (its pixel size is part of the key).
    /// Param glyph: the glyph id.
    /// Param out: receives the entry; `valid` is false on failure.
    /// Ownership: nothing is retained.
    /// Fails: `TooLarge` when the font's size is above [[text::kMaxAtlasPixelSize]] (nothing changes), `AtlasFull` when the glyph does not fit in any page and no page may be added,
    ///   `OutOfMemory`, plus the errors of [[text::Font::Render]]; the atlas keeps no entry for the glyph and its pages are unchanged; only the counters move.
    Status GetOrAdd(Font& font, GlyphId glyph, GlyphEntry* out);

    /// Returns the number of pages.
    ///
    /// Fails: never.
    u32 PageCount() const { return static_cast<u32>(pages_.Size()); }
    /// Returns the side of a page in pixels.
    ///
    /// Fails: never.
    u32 PageSize() const { return page_size_; }
    /// Returns the pixels of page `page`: `PageSize() * PageSize()` bytes, rows top to bottom.
    ///
    /// Aborts: `page` is not below [[text::GlyphAtlas::PageCount]].
    /// Ownership: borrowed from the atlas; invalidated by the next `GetOrAdd`.
    ByteSpan PagePixels(u32 page) const;
    /// Takes the rectangle of page `page` that changed since the last call (the union of the glyphs added), and marks the page clean.
    ///
    /// Param page: the page.
    /// Param rect: receives the rectangle.
    /// Returns: true when something changed; false (leaving `rect` alone) when the page is clean.
    /// Aborts: `page` is not below [[text::GlyphAtlas::PageCount]].
    /// Ownership: nothing is retained.
    bool TakeDirty(u32 page, Rect* rect);
    /// Returns the counters.
    ///
    /// Fails: never.
    AtlasStats Stats() const { return stats_; }

private:
    struct Key {
        u64 font = 0;
        u32 size = 0;
        u32 glyph = 0;
    };
    struct Slot {
        Key key;
        GlyphEntry entry;
        bool used = false;
    };
    struct Page {
        Page(u32 size, u32 padding) : packer(size, size, padding) {}
        Vector<byte> pixels;
        ShelfPacker packer;
        bool dirty = false;
        Rect dirty_rect;
    };

    static usize HashKey(const Key& key);
    const GlyphEntry* Find(const Key& key) const;
    bool EnsureSlot();
    void Insert(const Key& key, const GlyphEntry& entry);
    bool AddPage();
    void MarkDirty(Page& page, const Rect& rect);

    u32 page_size_;
    u32 max_pages_;
    Vector<Page> pages_;
    Vector<Slot> slots_; // open addressing, power-of-two size
    usize slot_count_ = 0;
    AtlasStats stats_;
};

/// The pixel rectangle one glyph is drawn into.
///
/// Thread-safety: thread-compatible plain data.
/// Ownership: owns nothing.
struct GlyphQuad {
    f32 left = 0;   ///< Left edge, a whole pixel.
    f32 top = 0;    ///< Top edge, a whole pixel.
    f32 right = 0;  ///< Right edge, a whole pixel.
    f32 bottom = 0; ///< Bottom edge, a whole pixel.
};

/// Places one laid-out glyph on whole pixels, so its atlas bitmap is shown texel for texel: the one rule every glyph drawer (the text renderer, the UI canvas) follows.
///
/// Param pen_x: pixel the layout's left edge (x = 0) lands on.
/// Param pen_y: pixel the layout's top edge lands on.
/// Param glyph_x: the glyph's pen x within the layout ([[text::PositionedGlyph::x]]).
/// Param glyph_y: the glyph's baseline y within the layout ([[text::PositionedGlyph::y]]).
/// Param entry: the glyph's atlas entry.
/// Param scale: 1, or the requested size over [[text::kMaxAtlasPixelSize]] for text above the atlas maximum.
/// Returns: the quad.
///
/// Fails: never.
inline GlyphQuad PlaceGlyph(f32 pen_x, f32 pen_y, f32 glyph_x, f32 glyph_y, const GlyphEntry& entry, f32 scale) {
    GlyphQuad q;
    q.left = Floor(pen_x + glyph_x + static_cast<f32>(entry.left) * scale + 0.5f);
    q.top = Floor(pen_y + glyph_y - static_cast<f32>(entry.top) * scale + 0.5f);
    q.right = q.left + Floor(static_cast<f32>(entry.rect.w) * scale + 0.5f);
    q.bottom = q.top + Floor(static_cast<f32>(entry.rect.h) * scale + 0.5f);
    return q;
}

} // namespace engine_placeholder::text
