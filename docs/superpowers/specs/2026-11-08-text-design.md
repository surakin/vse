# Text design (Phase 14)

Status: decided 2026-11-08 (Day 175) from the experiments below. Scope: the 2D text layer — fonts, glyph rasterization, an atlas, layout, cursor and
hit-testing, and an overlay pass that draws it. **Not in this phase:** 3D text, large 2D outline meshes and colored glyphs (Phase 27), widgets (Phase 15),
complex-script shaping (`PLAN.md` assumption #17). The developer's bar for the phase: "a simple hello world is enough until the UI is implemented".

## 1. Modules

- **`engine/text`** (library `text`, layer 10 beside `jobs`; `ARCHITECTURE.md`): CPU only. `Font` (a face at a pixel size), glyph rasterization, the glyph
  atlas's CPU pages and shelf packer, layout, cursor and hit-testing. Depends on the foundation only. FreeType is named in one file,
  `font_freetype.cpp` (the `platform_neutral_check` boundary); the headers expose engine types.
- **`engine/renderer`** (existing layer 12): `GlyphAtlasTexture` (the RHI textures behind the CPU pages), the text material and shader, `TextRenderer`, and the
  overlay `TextPass` in the render graph. The renderer depends on `text`; `text` knows nothing of the RHI.
- **FreeType 2.15.0**, vendored under `third_party/freetype/` (`VENDORED.md`), built as the static library `freetype`; license FTL.

## 2. Allocation

Every block FreeType allocates comes from the engine's heap through a counting `FT_Memory` (`font_freetype.cpp`; `text::ProbeLibrary` is the first user and the
test of it): a size header per block (FreeType frees by pointer alone), atomic counters, so `allocations == frees` and `live_bytes_at_end == 0` are testable.
Day 176 adds an allocation-failure injection on this hook for the "a failed allocation leaks nothing" tests. One `FT_Library` per **`FontLibrary`** object (Day 176: a small owner of the library and its counting `FT_Memory`; a program normally has one, a `Font` borrows it), so no global
FreeType state exists, module setup is paid once per library and not once per face, and two threads that want to work in parallel use two `FontLibrary` objects (FreeType's
library and faces are not thread-safe).

## 3. What a glyph is

One Unicode code point maps to one glyph through the font's cmap. **No shaping.** A grapheme cluster of several code points draws as its component glyphs:
combining marks and ZWJ/variation selectors have zero advance and sit where the pen is; an emoji ZWJ sequence draws its parts or `.notdef` (Phase 27 handles
colored glyphs). The cursor still treats the cluster as one unit (Day 179). Kerning comes from the font's `kern` table through `FT_Get_Kerning`; GPOS-only
fonts (Noto Sans has no `kern` table) get none, and the guide says so. Layout produces glyph ids and pen positions independent of how a glyph is drawn.

## 4. Sizes (developer decision, 2026-11-08)

- A `Font` has a pixel size; the atlas key is (font id, pixel size, glyph id). HiDPI scale is the caller's: it passes the size in device pixels.
- **`kMaxAtlasPixelSize = 128`.** Measured with the shipped font, light hinting, ASCII 33-126: 16 px 0.01 MiB (101x101 px square if packed tight), 32 px 0.03 MiB,
  64 px 0.12 MiB, **128 px 0.44 MiB (680x680)**, 256 px 1.7 MiB (1343x1343), 512 px 6.8 MiB (2670x2670). At 128 px the whole ASCII set fits one 1024x1024 R8 page
  (1 MiB, with shelf-packing waste); 256 px would need a 2048x2048 page (4 MiB) for ASCII alone. Rasterization cost per glyph is 7 us at 16 px and 14 us at 128 px (36 us at
  512): rasterization time is not the constraint, atlas area is.
- Metrics, layout and outlines work at any size up to a sanity limit of 2048 px; only `Render` and the atlas refuse sizes above the maximum (a `too_large` status,
  counted). Above the maximum the text renderer draws the glyph from the maximum-size raster scaled up (blurry, counted in `Stats`, logged once) until Phase 27
  draws such text as filled 2D outline meshes. Pen positions do not depend on the path, so nothing moves when Phase 27 lands.
- No quantization of requested sizes in v1; a program that animates size should expect one raster set per distinct size (no eviction in v1, below).

## 5. The atlas

R8 coverage pages, 1024x1024, one shelf packer per page with 1 px padding, glyphs placed on demand. The CPU pages are the source of truth: the GPU texture is a
copy that can be rebuilt. A full page adds a page (one draw per page, as Phase 5 Day 84 says) up to a documented maximum; beyond it the glyph is not added and a
counter increments — never a crash. **No eviction in v1.** Dirty rectangles per page are tracked so a frame uploads only what changed. An RGBA page can be added later
(Phase 27) without changing the keys.

## 6. Reset and loss (CLAUDE.md, "The RHI can be reset at run time")

`GlyphAtlasTexture` keeps the CPU pages, registers with `renderer::DeviceResources` at its tier (a `ScopedRegistration` member), implements `Release`/`Recreate`
(re-upload every page from the CPU copy) and never caches a handle past a frame; the text renderer binds the atlas by owner, not by raw handle (Day 180 amendment: the planned text material in `MaterialSystem` became `renderer::TextRenderer` on the raw RHI; the shaders are `text.vert` / `text.frag`, SPIR-V only until the Windows PC mints DXIL). Its tests reset the
device through `tests/renderer/reset_rig.h` and compare `Device::Resources()` counts, and recover from a forced device loss.

## 7. Seams kept for Phase 27

The `Font` exposes outline access (`LoadOutline`: contours with on/off-curve points in font units) behind the FreeType boundary; layout yields glyph ids and pen
positions independent of drawing; the atlas can add an RGBA page; the font is the single owner of the face, so a mesh builder reads outlines from the same object
the layout read advances from.

## 8. Hinting (experiment c)

"Hello, world!" at 12 and 16 px in four modes, rendered by FreeType 2.15.0 from Liberation Sans Regular, 4x point-upscaled for the picture
(`2026-11-08-text-hinting-samples.png`; rows in each size: none, light, normal, auto-hinter):

| Mode | 12 px coverage sum | 16 px | 24 px | 48 px | Look |
|---|---|---|---|---|---|
| none | 159.8 | 287.5 | 643.0 | 2572.1 | faithful shapes, uneven stems at small sizes |
| **light** (`FT_LOAD_TARGET_LIGHT`) | 171.1 | 301.1 | 642.8 | 2583.4 | crisp horizontals, advance widths untouched, shapes faithful |
| normal (the font's bytecode) | 175.0 | 297.1 | 645.4 | 2577.8 | similar to light, depends on the font's own hints |
| auto-hinter | 171.5 | 302.6 | 649.9 | 2623.1 | crispest stems, widest deviation from the design |

**Decision: light hinting** is the default (`FontOptions::hinting`, the others selectable). It improves small text without changing advance widths, so layout stays
resolution-independent, and it does not depend on a font's bytecode quality. The coverage sums differ by under 5 percent between modes, which is the property
the Day 177 test checks (not a picture compare).

## 9. Settings

Nothing in this phase is user configuration: the font file, the size and the string are arguments of the API, and the demo's `--text` and font options are
launch-only options of the demo (listed in `config_access_check`'s allow list entry for it on Day 182). If a later phase needs a user-visible text scale, it is
declared then with `settings::Registry`.

## 10. The shipped font

**Liberation Sans Regular 2.1.5** (SIL OFL 1.1, `assets/fonts/LiberationSans-Regular.ttf`, license in `assets/fonts/LiberationSans-LICENSE.txt`, SHA-256
`baccc64becc3eb7d104b7c84d99f5314a0a1f896e2b3ea6c2f22fc08d2003bee`; 411 KB; Latin, Latin Extended, Greek, Cyrillic; has a `kern` table). Chosen against Noto Sans
(OFL, 622 KB, GPOS kerning only, which FreeType's `FT_Get_Kerning` does not read) and DejaVu Sans (Bitstream Vera license, 760 KB). It has no CJK or emoji; the
CJK and emoji strings of the tests draw `.notdef` and that behavior is tested, not worked around. A CJK test font is not added (the smallest candidates are
megabytes).

## 11. Experiments summary

| # | Question | Result |
|---|---|---|
| a | FreeType with the minimum modules compiles under our flags | yes: the engine's `ftmodule.h` (TrueType, CFF, psaux, psnames, pshinter, sfnt, smooth, auto-hinter); `ftoption.h` unmodified plus an engine options header that turns off zlib and SVG (the unmodified options pulled in gzip, which is not vendored); built with `-w` (third-party), no warnings of ours; Clang and GCC link |
| b | Cost | measured on this machine in the default (unoptimized, debug-info) configuration: `libfreetype.a` is 2.9 MB and builds in 1.4 s single-threaded; `libtext.a` is 46 KB. No budget is set for this phase; the optimized binary-size cost is not measured (it is for the first release build) |
| c | Hinting | section 8: light |
| d | Rasterization time and atlas area per size | section 4 |
