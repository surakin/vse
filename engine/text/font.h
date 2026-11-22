#pragma once

#include "containers/span.h"
#include "containers/vector.h"
#include "io/error.h"
#include "io/path.h"
#include "text/library.h"
#include "types.h"

//! Font faces: loading, sizing, glyph lookup, metrics, kerning and outlines.
//!
//! A [[text::FontLibrary]] owns one FreeType library (and the counting allocator behind it); each [[text::Font]] is one
//! face opened from it at one pixel size. FreeType is named only in `font_freetype.cpp`. Errors are values
//! ([[text::Status]]), never exceptions. There is **no shaping**: one code point is one glyph through the font's cmap
//! (`docs/superpowers/specs/2026-11-08-text-design.md`, section 3).
namespace vse::text
{

/// A glyph index inside one font; 0 is `.notdef`, the "missing glyph" box.
using GlyphId = u32;

/// A Unicode scalar value (U+0000..U+10FFFF, no surrogates).
using char32 = u32;

/// The largest pixel size the atlas rasterizes (`Font::Render` and the glyph atlas refuse more with `Error::TooLarge`).
/// Larger text is built from outline meshes.
constexpr u32 kMaxAtlasPixelSize = 128;

/// The largest pixel size a [[text::Font]] accepts at all (metrics, layout and outlines work up to it).
constexpr u32 kMaxPixelSize = 2048;

/// What went wrong while loading or using a font.
enum class Error : u8
{
    None,      ///< Success.
    NotFound,  ///< The file does not exist (or could not be opened for another reason: see [[text::Status::io_error]]).
    Empty,     ///< The data has zero bytes.
    NotAFont,  ///< The data is not in a font format FreeType knows (a text file, an image).
    Corrupt,   ///< The data looks like a font but is damaged or truncated.
    Unsupported,      ///< A valid font the engine cannot use (no scalable outlines, a face index out of range).
    OutOfMemory,      ///< An allocation failed.
    InvalidArgument,  ///< A size of 0 or above [[text::kMaxPixelSize]], or an operation on a font that is not open.
    TooLarge,         ///< The pixel size is above [[text::kMaxAtlasPixelSize]] and the operation rasterizes.
    AtlasFull,  ///< The glyph atlas has no room for the glyph (every page is full, or the glyph is larger than a page).
};

/// The result of a text operation: an [[text::Error]] and a short message that names the cause.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: `message` points at a string literal; owns nothing.
struct Status
{
    Error error = Error::None;             ///< What went wrong; [[text::Error::None]] means success.
    const char* message = "";              ///< The cause in words, for logs.
    io::Error io_error = io::Error::None;  ///< The file layer's error when the font file could not be read.

    /// Returns true on success.
    ///
    /// - Fails: never.
    bool Ok() const { return error == Error::None; }
    /// Returns a failure with `e` and `message`.
    ///
    /// - Param e: the error.
    /// - Param message: a string literal (or other string that outlives the status) naming the cause.
    /// - Fails: never.
    static Status Of(Error e, const char* message)
    {
        Status status;
        status.error = e;
        status.message = message;
        return status;
    }
    /// Returns true when the status is exactly `e`.
    ///
    /// - Fails: never.
    bool Is(Error e) const { return error == e; }
};

/// How strongly the font's outlines are fitted to the pixel grid.
enum class Hinting : u8
{
    None,      ///< No hinting: shapes are exact, stems blur across pixels.
    Light,     ///< FreeType's light auto-hinting: vertical fitting only, advances stay unrounded. The default.
    Normal,    ///< FreeType's normal mode (the font's bytecode where it has some, otherwise the auto-hinter).
    Bytecode,  ///< The font's own bytecode only, no auto-hinter fallback.
};

/// Options of [[text::Font::Open]].
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns nothing.
struct FontOptions
{
    u32 face_index = 0;                ///< Which face of a collection to open (0 for a plain font file).
    Hinting hinting = Hinting::Light;  ///< The hinting mode of every size of this font.
};

/// The vertical metrics of a font at its current pixel size, in pixels.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns nothing.
struct FontMetrics
{
    f32 ascent = 0;        ///< Distance from the baseline up to the top of the line (positive).
    f32 descent = 0;       ///< Distance from the baseline down to the bottom of the line (positive).
    f32 line_height = 0;   ///< Distance between two baselines.
    u32 units_per_em = 0;  ///< The font's design grid size; outlines are in these units.
};

/// The metrics of one glyph at the font's current pixel size, in pixels (y grows up).
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns nothing.
struct GlyphMetrics
{
    f32 advance = 0;    ///< Horizontal distance to the next pen position (vertical layout is out of scope).
    f32 bearing_x = 0;  ///< Distance from the pen to the left edge of the glyph's box.
    f32 bearing_y = 0;  ///< Distance from the baseline up to the top edge of the glyph's box.
    u32 width = 0;   ///< Width in whole pixels of the pixel box around the outline; 0 for a glyph with no outline, such
                     ///< as a space.
    u32 height = 0;  ///< Height of that box (floor of the left/bottom edge to ceiling of the right/top edge, as the
                     ///< rasterized bitmap).
};

/// One point of a glyph outline, in font units.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns nothing.
struct OutlinePoint
{
    i32 x = 0;             ///< Horizontal position in font units.
    i32 y = 0;             ///< Vertical position in font units (y grows up).
    bool on_curve = true;  ///< True for a point on the outline; false for a quadratic or cubic control point.
    bool cubic = false;    ///< True when an off-curve point is a cubic control point (CFF fonts); false for a quadratic
                           ///< (TrueType) one.
};

/// A rasterized glyph: 8-bit coverage, one byte per pixel, rows top to bottom with no padding (the pitch is `width`).
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns its pixels.
struct GlyphBitmap
{
    u32 width = 0;    ///< Columns; 0 for a glyph with no outline (a space).
    u32 height = 0;   ///< Rows; 0 for a glyph with no outline.
    i32 left = 0;     ///< Distance from the pen to the left column of the bitmap (negative when the glyph reaches back
                      ///< past the pen).
    i32 top = 0;      ///< Distance from the baseline up to the top row of the bitmap.
    f32 advance = 0;  ///< The glyph's horizontal advance in pixels, the same as [[text::GlyphMetrics::advance]].
    Vector<byte> pixels;  ///< `width * height` coverage values, 0 (empty) to 255 (full).
};

/// A glyph outline: closed contours of points, unscaled (the mesh builder's input).
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns its vectors.
struct Outline
{
    Vector<OutlinePoint> points;  ///< All points, contour after contour.
    Vector<u32> contour_ends;     ///< Index of the last point of each contour.
    i32 x_min = 0;                ///< Bounding box of the points, in font units.
    i32 y_min = 0;                ///< Bounding box of the points, in font units.
    i32 x_max = 0;                ///< Bounding box of the points, in font units.
    i32 y_max = 0;                ///< Bounding box of the points, in font units.
};

/// One FreeType library with the engine's counting allocator behind it; the owner of every [[text::Font]] opened from
/// it.
///
/// A program normally has one. FreeType's library and faces are not thread-safe, so two threads that work in parallel
/// use two libraries. The library must outlive its fonts; the destructor aborts when a font is still open.
///
/// - Thread-safety: thread-compatible; `Stats` and `FailAllocationsAfter` are safe to call while a font of this library
/// is used on another thread.
/// - Ownership: owns the FreeType library; not copyable and not movable (fonts point at it).
class FontLibrary
{
public:
    /// Creates a library that is not initialized; call [[text::FontLibrary::Initialize]].
    FontLibrary();
    /// Destroys the library.
    ///
    /// - Aborts: a [[text::Font]] opened from this library is still alive.
    ~FontLibrary();
    FontLibrary(const FontLibrary&) = delete;
    FontLibrary& operator=(const FontLibrary&) = delete;

    /// Starts FreeType with the engine's allocator and module list.
    ///
    /// - Fails: `OutOfMemory` when FreeType could not start; the library stays unusable.
    Status Initialize();
    /// Returns true after a successful [[text::FontLibrary::Initialize]].
    ///
    /// - Fails: never.
    bool IsInitialized() const;
    /// Returns the version of the FreeType library.
    LibraryVersion Version() const;
    /// Returns the allocation counts so far (a snapshot; `live_bytes_at_end` is the bytes live now).
    LibraryAllocationStats Stats() const;
    /// A test aid: the allocation number `count` from now (0 = the next one) and every later one fail, until reset with
    /// `~u64{0}`.
    ///
    /// - Param count: allocations that still succeed; `~u64{0}` turns the injection off.
    void FailAllocationsAfter(u64 count);

private:
    friend class Font;
    friend bool ProbeLibrary(LibraryVersion* version, LibraryAllocationStats* stats);
    struct Impl;
    Impl* impl_ = nullptr;
};

/// One font face at one pixel size.
///
/// A `Font` owns a copy of the font file's bytes (FreeType reads them for the face's whole life). It is movable, not
/// copyable. Using one `Font` from two threads at once is not supported (`FT_Face` is not thread-safe), **including the
/// const calls**: `Metrics`, `FindGlyph`, `Kerning` and `LoadOutline` load into the face's shared glyph slot. Use one
/// `Font` per thread or an external lock.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns the face and the font bytes; borrows the [[text::FontLibrary]], which must outlive it.
class Font
{
public:
    /// Creates a font that is not open.
    Font() = default;
    /// Closes the face.
    ~Font();
    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;
    /// Takes over the face of `other`, which becomes not open.
    Font(Font&& other) noexcept;
    /// Closes this face and takes over the face of `other`.
    Font& operator=(Font&& other) noexcept;

    /// Opens the face in `data` (a TrueType, OpenType or other FreeType-readable font file); the bytes are copied.
    ///
    /// The pixel size starts at 16.
    /// - Param library: an initialized library that outlives the font.
    /// - Param data: the whole font file.
    /// - Param options: face index and hinting.
    /// - Param out: receives the font; must not be null.
    /// - Ownership: `out` owns a copy of `data`; the library is borrowed and must outlive it.
    /// - Fails: `Empty` for zero bytes, `NotAFont` for data that is no font, `Corrupt` for a damaged or truncated font,
    /// `Unsupported` for a face index out of range or a font without
    ///   scalable outlines, `OutOfMemory`, `InvalidArgument` for an uninitialized library; `out` is left not open.
    static Status Open(FontLibrary& library, ByteSpan data, const FontOptions& options, Font* out);
    /// Reads the file at `path` through the file layer and opens it as [[text::Font::Open]] does.
    ///
    /// - Ownership: as [[text::Font::Open]]; the file is read whole and closed before the call returns.
    /// - Fails: `NotFound` (with the file layer's error in `io_error`) when the file cannot be read, plus the errors of
    /// [[text::Font::Open]].
    static Status OpenFile(FontLibrary& library, const io::Path& path, const FontOptions& options, Font* out);

    /// Returns true when a face is open.
    ///
    /// - Fails: never.
    bool IsOpen() const;
    /// Closes the face; harmless on a font that is not open.
    void Close();

    /// Sets the pixel size (the size of the em square in pixels). Metrics, advances and outlines use it from now on.
    ///
    /// Sizes above [[text::kMaxAtlasPixelSize]] are accepted (only rasterization refuses them).
    /// - Fails: `InvalidArgument` for 0, above [[text::kMaxPixelSize]], or on a font that is not open; the size is
    /// unchanged.
    Status SetPixelSize(u32 pixels);
    /// Returns the current pixel size (0 when not open).
    u32 PixelSize() const;
    /// Returns the font's vertical metrics at the current size.
    FontMetrics Metrics() const;
    /// Returns the options the font was opened with.
    FontOptions Options() const;
    /// Returns the number of glyphs in the font.
    u32 GlyphCount() const;
    /// Returns an identifier that is unique among the open fonts of the process (0 when not open); an atlas keys glyphs
    /// by it.
    u64 Id() const;

    /// Returns the glyph for `codepoint` through the character map.
    ///
    /// - Returns: `.notdef` (0) when the font has no glyph, and for invalid code points (surrogates, above U+10FFFF).
    GlyphId FindGlyph(char32 codepoint) const;
    /// Returns true when the font has a real glyph for `codepoint`.
    ///
    /// - Fails: never; an invalid code point has no glyph.
    bool HasGlyph(char32 codepoint) const;
    /// Returns the metrics of `glyph` at the current size (all zero for a glyph id out of range or a font that is not
    /// open).
    GlyphMetrics Metrics(GlyphId glyph) const;
    /// Returns the kerning adjustment in pixels between two glyphs.
    ///
    /// - Returns: the amount to add to the advance of `left` when `right` follows (negative pulls them together); 0
    /// when the font has no such pair.
    ///
    /// Only the font's `kern` table is read (`FT_Get_Kerning`); GPOS pair adjustment is not (the guide lists the
    /// limit).
    f32 Kerning(GlyphId left, GlyphId right) const;
    /// Rasterizes `glyph` at the current size with the font's hinting mode into 8-bit coverage.
    ///
    /// - Ownership: `out` owns its pixels.
    /// - Fails: `TooLarge` when the pixel size is above [[text::kMaxAtlasPixelSize]] (nothing is rendered),
    /// `InvalidArgument` for a glyph id out of range or a font that is not
    ///   open, `OutOfMemory`; `out` is cleared first.
    Status Render(GlyphId glyph, GlyphBitmap* out) const;
    /// Loads the unscaled outline of `glyph` in font units.
    ///
    /// - Ownership: `out` owns the points and contours; they stay valid after the font closes.
    /// - Fails: `InvalidArgument` for a glyph id out of range, `Unsupported` for a glyph with no outline data (a
    /// bitmap-only glyph), `OutOfMemory`; `out` is cleared.
    Status LoadOutline(GlyphId glyph, Outline* out) const;

private:
    static Status OpenOwned(FontLibrary& library, Vector<byte>&& bytes, const FontOptions& options, Font* out);
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace vse::text
