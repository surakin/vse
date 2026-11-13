# Fonts and text

Games show text everywhere: menus, names above heads, debug overlays. This page shows how the engine turns a font file and a string into pixels on the screen. It does so in
three steps, and each step has its own types:

1. **Open a font and look up glyphs** (`text::Font`). A *glyph* is the drawn shape of one character.
2. **Lay the string out** (`text::Layout`): decide where each glyph goes, where the lines break and where the caret can stand.
3. **Draw it**: rasterize the glyphs once into a texture atlas (`text::GlyphAtlas`), then draw quads from that atlas (`renderer::TextRenderer`). *Rasterizing* means turning a
   shape into a grid of pixel coverage values; an *atlas* is one big texture that holds many small images, so one draw can use all of them.

**Audience:** anyone who puts text on screen or edits text. Steps 1 and 2 run on the CPU and need no window or GPU, which is why the samples below run anywhere. Most game
code never calls these directly: the UI toolkit ([The UI toolkit](ui.md)) draws its labels and text fields through them.

The text layer lives in `engine/text/` (namespace `engine_placeholder::text`). Design and reasons: `docs/superpowers/specs/2026-11-08-text-design.md`.
FreeType 2.15.0 (vendored, `third_party/freetype/`) reads and rasterizes the fonts; it is named in one file, `engine/text/font_freetype.cpp`, and every
block it allocates comes from the engine's heap. The demo (`renderer_demo --text`, F11) shows the overlay; the benchmark numbers are at the end.

## At a glance

| I need | Use | Notes |
|---|---|---|
| to open a font | `FontLibrary`, then `Font::Open` / `Font::OpenFile` | the library must outlive its fonts; check the returned `text::Status` |
| a glyph and its size | `Font::FindGlyph`, `Font::Metrics` | pixels; a missing character gives glyph 0 (the box), not an error |
| positions for a string | `text::Layout(utf8, style, options, &layout)` | wraps, handles tabs and newlines; no shaping |
| a caret, a click position, a selection | `text/cursor.h` (`PositionOfOffset`, `OffsetAtPoint`, `SelectionRects`) | offsets are grapheme boundaries |
| glyph bitmaps in a texture | `GlyphAtlas::GetOrAdd` | CPU pages; no eviction; sizes up to `kMaxAtlasPixelSize` (128) |
| text on the screen | `renderer::TextRenderer` | `BeginFrame`, `Draw`, `AddToGraph` once per frame |

## The model

- A **`FontLibrary`** is one FreeType library. A program usually has one. FreeType is not thread-safe, so threads that work in parallel each use their own
  library (and their own fonts). The library must outlive its fonts.
- A **`Font`** is one face at one pixel size, opened from bytes (`Font::Open`) or a path through the file layer (`Font::OpenFile`). It owns a copy of the
  font file. It is movable, not copyable, and not safe to use from two threads at once.
- A **glyph** is one code point's shape: `FindGlyph(codepoint)` looks it up in the font's character map. There is **no shaping**: a code point is a glyph, a
  missing one is glyph 0 (`.notdef`, the box), never an error.
- Sizes are **pixels** (`SetPixelSize`): the caller multiplies by its display scale, there is no hidden HiDPI factor. 0 and sizes above 2048 are refused.
  Sizes above `kMaxAtlasPixelSize` (128) work for metrics, layout and outlines; only rasterization refuses them (Phase 26 draws such text as meshes).
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

## Hello, world

The whole CPU path, from a file to positioned glyphs and a rasterized bitmap (the sample runs with the shipped font):

```cpp compile run standalone
#include <cstdio>
#include <cstring>

#include "io/path.h"
#include "string_view.h"
#include "text/font.h"
#include "text/layout.h"

using namespace engine_placeholder;

int main() {
    text::FontLibrary library;
    if (!library.Initialize().Ok()) return 1;
    text::Font font;
    const char* file = DOCS_FONT_FILE;
    if (!text::Font::OpenFile(library, io::Path(StringView(file, std::strlen(file))), text::FontOptions{}, &font).Ok()) return 1;

    text::TextStyle style;
    style.font = &font;
    style.pixel_size = 32;
    text::TextLayout layout;
    if (!text::Layout("Hello, world!", style, text::LayoutOptions{}, &layout).Ok()) return 1;
    std::printf("%zu glyphs on %zu line\n", static_cast<size_t>(layout.glyphs.Size()), static_cast<size_t>(layout.lines.Size()));

    text::GlyphBitmap bitmap;
    if (!font.Render(layout.glyphs[0].glyph, &bitmap).Ok()) return 1; // the "H": coverage 0..255, one byte per pixel
    std::printf("the first glyph is %s\n", bitmap.width > 0 && bitmap.height > 0 ? "inked" : "empty");
    return 0;
}
```

```output
13 glyphs on 1 line
the first glyph is inked
```

On screen the same layout goes through `renderer::TextRenderer` ("Drawing text", below). Sizes are **device pixels**: on a 2x display the caller asks for 2 times the size it
thinks in, and positions in pixels too.

## Rasterizing and the glyph atlas

`Font::Render(glyph, &bitmap)` gives 8-bit coverage (`GlyphBitmap`: width, height, the left and top bearings in pixels, the advance, tightly packed rows). A glyph with
no outline (a space) gives a 0x0 bitmap that still carries the advance. Sizes above `kMaxAtlasPixelSize` (128) are refused with `Error::TooLarge`.

`GlyphAtlas` packs rasterized glyphs into square 8-bit **pages** (1024 pixels by default) in CPU memory with a shelf packer (`ShelfPacker`, one pixel of empty
padding around every glyph and the page edge, so bilinear sampling does not bleed). `GetOrAdd(font, glyph, &entry)` keys by (font id, pixel size, glyph id): a
second request returns the same entry. The entry has the page, the pixel rectangle, texture coordinates, the bearings and the advance. A full page adds a page up
to the maximum given to the constructor; after that a glyph is refused with `Error::AtlasFull` and counted in `Stats().failures` (never a crash; there is **no
eviction** in this version). `TakeDirty(page, &rect)` returns the rectangle that changed since the last call, so a frame uploads only that. The CPU pages are the
source of truth: a renderer's GPU copy can be rebuilt from `PagePixels(page)` after a device reset.

## Layout

`Layout(utf8, style, options, &layout)` turns UTF-8 into a `TextLayout`: positioned glyphs (`glyph`, pen `x`, baseline `y`, `advance`, the byte offset and the
grapheme index of its source), grapheme **clusters** (byte range, line, left edge, width: what the cursor moves by) and **lines**. Coordinates are pixels, origin at
the top-left of the first line, y down. `TextStyle` names the font, the pixel size and a color the drawing pass uses; `LayoutOptions` the soft-wrap width
(0 = none), the line-height scale, the tab width in spaces and the alignment of each line.

- A cluster of several code points yields one glyph per code point; combining marks, joiners and variation selectors have zero advance and are drawn at the pen
  after their base glyph (a font gives such marks a negative bearing). That is the whole of "complex text": **no shaping**, so scripts that need it (Arabic,
  Indic) draw their isolated forms, and there is no bidirectional or vertical text.
- Kerning from the `kern` table applies between neighbouring clusters on a line, never across a break or a tab.
- `\n`, `\r\n` and `\r` end a line (the break is a cluster with no glyph); a text ending in a break has an empty last line; the empty string is one empty line.
  A tab goes to the next multiple of `tab_width_in_spaces` space widths.
- With a wrap width, a line breaks after the last space that fits, or between clusters when a word is wider than the line; never inside a cluster. Trailing
  spaces never trigger a wrap and do not count towards a line's width.
- Malformed UTF-8 draws U+FFFD and is counted in `invalid_sequences`. The same input always gives the same layout.

## Cursor, hit-testing and selection

`text/cursor.h` works on a `TextLayout`. The only valid cursor offsets are **grapheme boundaries**: the start of a cluster, or the text length (an offset inside a cluster
snaps to the cluster's start). `PositionOfOffset` gives the caret's x, the top and height of its line and the line index; `OffsetAtPoint(x, y)` is the inverse, choosing the
nearest boundary (the half of a cluster the point is in decides) and clamping above, below, left and right of the text. `NextBoundary` and `PreviousBoundary` move by
cluster (a combining mark, flag, ZWJ sequence or Hangul syllable is one step); `LineStart`, `LineEnd`, `LineAbove(offset, preferred_x)` and `LineBelow` move by line and
keep the column through lines of different length when the caller keeps `preferred_x`. `SelectionRects(begin, end)` returns one rectangle per line covered.

Limits: there is no caret affinity, so the offset where a soft-wrapped line ends is the start of the next line; the end of a wrapped line is before its trailing spaces.
Word movement (Ctrl+arrow) is not in `text/cursor.h`; the UI toolkit adds it (`ui/text_words.h`).

## Drawing text

`renderer::TextRenderer` (`renderer/text_renderer.h`) puts a laid-out text on a color target. It owns a `renderer::GlyphAtlasTexture` (one R8 texture per atlas page), its own pipeline
(shaders `text.vert` and `text.frag`), a sampler and a vertex buffer with one region per frame in flight, and registers with the `DeviceResources` registry, so a device reset or loss
rebuilds all of it from the CPU atlas (the pages are the source of truth; no glyph is lost and no handle is kept past a frame). It is meant to draw on every backend,
Metal included (Phase 21 Day 308: the text GPU tests run once per backend, so through Metal on macOS, and are first run there on Day 309; on a Retina
window text is rasterized at the backing scale the window reports).

```cpp
renderer::TextRenderer text;
text.Create(device, &atlas, renderer::TextRendererDesc{rhi::Format::RGBA8_SRGB, 8192}, &registry);

// every frame, before the graph is built:
text.BeginFrame();
text.Draw("Hello, world!", style, layout_options, /*x=*/16.0f, /*y=*/16.0f);   // style.color is packed RGBA (red in the lowest byte)
// ... after the passes that wrote the display target (the tonemap):
text.AddToGraph(&graph, display_target, display_format, width, height);
```

`AddToGraph` declares a pass named `text` that reads and writes the target as a color attachment (it blends over what the earlier passes drew and never clears), so the graph
orders it after them. A frame in which nothing was drawn declares no pass at all: no barrier, no cost. A target in another format than the renderer was made for (a settings
change) rebuilds the pipeline inside `AddToGraph`. `Draw` lays the text out into a layout the renderer keeps and reuses, so a steady frame allocates nothing per glyph (a
frame has a few fixed temporaries inside `Layout`; the test compares a short and a five times longer text). Programs that lay out themselves call `Add(layout, style, x, y)`
and, outside the graph, `Record(list, width, height)` inside their own rendering pass.

- Coordinates are pixels of the target, origin at its top-left corner, X right, Y down, as the layout gives them. Each glyph quad is placed on whole pixels, so the atlas bitmap is shown
  texel for texel (the test compares every pixel with the atlas page).
- The result is premultiplied alpha: the fragment shader outputs `color * coverage * alpha`, and the pipeline blends with source `One`, destination `OneMinusSrcAlpha`. There is no depth test.
- Colors are written as the 8-bit values a person picks. For an sRGB target the vertex shader decodes them to linear (the hardware encodes on write), so a gray of 0x80 shows as 0x80; for a
  UNORM target they are used as they are.
- `Add` looks each glyph up in the atlas (rasterizing it on first use), so the first frame that uses a glyph pays for it; `Record` sends the atlas pages that changed since the last frame. A
  changed page is sent **whole** (the RHI uploads a full mip level at a time), one megabyte for a 1024-pixel page.
- Text above `kMaxAtlasPixelSize` (128 px) is laid out at the requested size but rasterized at 128 px and drawn with scaled quads (blurry, correctly placed); `Stats().scaled_large_glyphs`
  counts such glyphs and the first use of each size is logged. Phase 26 draws large text as outline meshes.
- One frame draws at most `max_glyphs` glyphs (8192 by default); the rest are dropped, counted in `DroppedGlyphs()` and logged once per frame, and a full atlas drops glyphs the same way.

## Common mistakes

- **Destroying the `FontLibrary` before its fonts.** The library must outlive every `Font` opened from it.
- **Sharing a library or a font between threads.** FreeType is not thread-safe: give each thread its own library and fonts.
- **Ignoring the returned `text::Status`.** Opening or rendering can fail (`NotAFont`, `Corrupt`, `TooLarge`, `AtlasFull`); nothing is thrown.
- **Passing logical sizes.** Sizes and positions are device pixels. On a 2x display ask for twice the size.
- **Expecting complex scripts to shape.** There is no shaping, bidirectional or vertical text (see "Layout").
- **Rasterizing above 128 px.** `Font::Render` and the atlas refuse sizes above `kMaxAtlasPixelSize`; `TextRenderer` scales such text up from 128 px (it looks soft).
- **Drawing more than `max_glyphs` per frame, or filling the atlas.** The surplus glyphs are dropped and counted (`DroppedGlyphs()`, `Stats().failures`), not drawn.

## Hinting

`FontOptions::hinting` is `Light` by default (FreeType's light auto-hinter: vertical fitting only, advances unrounded, so line lengths do not jump with the
size). `None`, `Normal` and `Bytecode` are the others. The choice is made per font and applies to every size.

## Limits

- Kerning comes from the font's `kern` table only (`FT_Get_Kerning`); pair adjustment that lives only in GPOS is not read.
- Metrics are horizontal; vertical layout is out of scope.
- `LoadOutline` returns the unscaled contours with on/off-curve flags (quadratic for TrueType, cubic for CFF): the seam Phase 26's outline meshes use.
- FreeType's auto-hinter does not survive a failed allocation during the first glyph load (a FreeType 2.15.0 defect, `KNOWN-ISSUES.md`); opening a font and
  loading outlines do survive it, and the library's allocation counters stay exact.

## Cost

`renderer_bench --section text` (this machine, the default unoptimized configuration with debug info, so read the numbers as an upper bound; nothing asserts them):

| Measurement | Result |
|---|---|
| Layout of 1,000 characters (962 glyphs, one line) | 275-278 us at 16, 32 and 64 px; wrapping at 600 px adds about 1-4 us |
| Atlas miss (rasterize at 32 px and pack) | 18 us per glyph (94 ASCII glyphs) |
| Atlas hit | 0.06 us per glyph |
| Dirty rectangle after 94 new glyphs at 32 px | 85,848 bytes (913 bytes per glyph), but the RHI sends the whole page: 1,048,576 bytes |
| `TextRenderer::Add` of a laid-out text | 71 us for 962 glyphs (about 74 us per 1,000 glyphs) |
| `TextRenderer::Draw` (layout plus `Add`) | 347 us for the same text |

A steady frame therefore spends about 0.35 ms of CPU per thousand glyphs in an unoptimized build, and a frame that adds new glyphs pays about 18 us each plus one page upload.
