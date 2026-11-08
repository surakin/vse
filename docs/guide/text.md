# Fonts and text

Status: draft (Phase 14, Days 175-177 so far; layout, cursor and the drawing pass follow).

The text layer lives in `engine/text/` (namespace `engine_placeholder::text`). Design and reasons: `docs/superpowers/specs/2026-11-08-text-design.md`.
FreeType 2.15.0 (vendored, `third_party/freetype/`) reads and rasterizes the fonts; it is named in one file, `engine/text/font_freetype.cpp`, and every
block it allocates comes from the engine's heap.

## The model

- A **`FontLibrary`** is one FreeType library. A program usually has one. FreeType is not thread-safe, so threads that work in parallel each use their own
  library (and their own fonts). The library must outlive its fonts.
- A **`Font`** is one face at one pixel size, opened from bytes (`Font::Open`) or a path through the file layer (`Font::OpenFile`). It owns a copy of the
  font file. It is movable, not copyable, and not safe to use from two threads at once.
- A **glyph** is one code point's shape: `FindGlyph(codepoint)` looks it up in the font's character map. There is **no shaping**: a code point is a glyph, a
  missing one is glyph 0 (`.notdef`, the box), never an error.
- Sizes are **pixels** (`SetPixelSize`): the caller multiplies by its display scale, there is no hidden HiDPI factor. 0 and sizes above 2048 are refused.
  Sizes above `kMaxAtlasPixelSize` (128) work for metrics, layout and outlines; only rasterization refuses them (Phase 16 draws such text as meshes).
- **Errors are values.** `Font::Open` reports `Empty`, `NotAFont`, `Corrupt` (including a font cut short), `Unsupported` or `OutOfMemory` in a `text::Status`
  with a message.

```cpp
text::FontLibrary library;
library.Initialize();
text::Font font;
text::Status status = text::Font::OpenFile(library, path, text::FontOptions{}, &font);
font.SetPixelSize(24);
text::GlyphId h = font.FindGlyph(U'H');
text::GlyphMetrics m = font.Metrics(h);          // advance, bearing, box, in pixels
f32 pull = font.Kerning(font.FindGlyph(U'A'), font.FindGlyph(U'V'));
```

## Rasterizing and the glyph atlas

`Font::Render(glyph, &bitmap)` gives 8-bit coverage (`GlyphBitmap`: width, height, the left and top bearings in pixels, the advance, tightly packed rows). A glyph with
no outline (a space) gives a 0x0 bitmap that still carries the advance. Sizes above `kMaxAtlasPixelSize` (128) are refused with `Error::TooLarge`.

`GlyphAtlas` packs rasterized glyphs into square 8-bit **pages** (1024 pixels by default) in CPU memory with a shelf packer (`ShelfPacker`, one pixel of empty
padding around every glyph and the page edge, so bilinear sampling does not bleed). `GetOrAdd(font, glyph, &entry)` keys by (font id, pixel size, glyph id): a
second request returns the same entry. The entry has the page, the pixel rectangle, texture coordinates, the bearings and the advance. A full page adds a page up
to the maximum given to the constructor; after that a glyph is refused with `Error::AtlasFull` and counted in `Stats().failures` (never a crash; there is **no
eviction** in this version). `TakeDirty(page, &rect)` returns the rectangle that changed since the last call, so a frame uploads only that. The CPU pages are the
source of truth: a renderer's GPU copy can be rebuilt from `PagePixels(page)` after a device reset.

## Hinting

`FontOptions::hinting` is `Light` by default (FreeType's light auto-hinter: vertical fitting only, advances unrounded, so line lengths do not jump with the
size). `None`, `Normal` and `Bytecode` are the others. The choice is made per font and applies to every size.

## Limits

- Kerning comes from the font's `kern` table only (`FT_Get_Kerning`); pair adjustment that lives only in GPOS is not read.
- Metrics are horizontal; vertical layout is out of scope.
- `LoadOutline` returns the unscaled contours with on/off-curve flags (quadratic for TrueType, cubic for CFF): the seam Phase 16's outline meshes use.
- FreeType's auto-hinter does not survive a failed allocation during the first glyph load (a FreeType 2.15.0 defect, `KNOWN-ISSUES.md`); opening a font and
  loading outlines do survive it, and the library's allocation counters stay exact.
