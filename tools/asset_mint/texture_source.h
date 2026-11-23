#pragma once

//! The text source form of a texture (`.tex.src`), the raw image container and the texture minter. An
//! offline tool's code: never linked into the engine; its tests compile it directly.
//!
//! Line-oriented text; `#` starts a comment; fields are separated by spaces or tabs:
//!
//!     id 0123456789abcdef
//!     kind 2d                 (or `cube` or `array`; optional, default 2d)
//!     source rock.raw         (one file for 2d, six for a cube: +X -X +Y -Y +Z -Z, one per layer for an array)
//!     format rgba8            (r8, rg8, rgba8, r16f, rgba16f, bc1, bc4, bc5, bc7 or bc7-alpha; default rgba8)
//!     srgb true               (rgba8: store sRGB-encoded; r16f/rgba16f: the source colors are sRGB; default false)
//!     mips full               (or a count; default full)
//!     filter box              (or `kaiser`; default box)
//!     alpha straight          (or `premultiply` or `ignore`; default straight for rgba8, ignore otherwise)
//!     input rgba8             (or `rgba16f`: the raw files hold half-float RGBA, a high-dynamic-range image; needs
//!                              `format rgba16f`; default rgba8)
//!     projection faces        (or `equirect`, for a cube with `input rgba16f`: one latitude-longitude source file is
//!                              converted into the six faces, `face_size` texels across; default faces)
//!     face_size 512           (1 to 4096; with `projection equirect` only)
//!
//! `id` comes first (written by `asset_mint adopt`). The images are raw files: a 24-byte header (`RAW1`,
//! width, height, the id as a `u64`, a reserved `u32`) and `width * height * 4` bytes of RGBA8 rows, or, with the magic
//! `RAWH`, `width * height * 8` bytes of half-float RGBA rows. No PNG.

#include "asset/block_compress.h"
#include "asset/resource_id.h"
#include "asset/resource_type.h"
#include "asset/status.h"
#include "asset/texture.h"
#include "containers/array.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "string_view.h"
#include "types.h"

namespace vse::asset
{

/// One `source` record.
///
/// - Thread-safety: thread-compatible (a plain value).
struct TextureSourceFile
{
    Array<char, 256> name = {};  ///< The file name, NUL-terminated, as written (relative to the source file).
    u32 line = 0;                ///< The line of the record.
};

/// A parsed texture source.
///
/// - Thread-safety: thread-compatible.
struct TextureSource
{
    ResourceId id;                                  ///< From the `id` line.
    TextureKind kind = TextureKind::Tex2D;          ///< The `kind` record.
    Vector<TextureSourceFile> sources;              ///< The `source` records, in order.
    rhi::Format format = rhi::Format::RGBA8_UNorm;  ///< The storage format (`srgb` already applied).
    bool drop_alpha = false;                        ///< `format bc7`: alpha is encoded as 255 (`bc7-alpha` keeps it).
    bool srgb = false;                              ///< The `srgb` record.
    u32 mips = 0;                                   ///< The `mips` record (0: the full chain).
    MipFilter filter = MipFilter::Box;              ///< The `filter` record.
    MipAlpha alpha = MipAlpha::Straight;            ///< The `alpha` record.
    bool half_input = false;                        ///< `input rgba16f`: the raw files are half-float images.
    bool equirect = false;                          ///< `projection equirect`: one latitude-longitude source.
    bool environment = false;  ///< `environment yes`: the cube map's levels from 1 are the GGX specular chain.
    u32 face_size = 0;         ///< The `face_size` record (0 when not given).
};

/// Where and why a texture source or image was refused.
///
/// - Thread-safety: immutable value.
struct TextureSourceError
{
    u32 line = 0;                  ///< 1-based line number; 0 when the error is about the file as a whole.
    Array<char, 96> message = {};  ///< A sentence.
};

/// An RGBA8 or half-float RGBA image read from a raw file.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: borrows the file bytes.
struct RawImage
{
    u32 width = 0;      ///< Texels across.
    u32 height = 0;     ///< Texels down.
    u64 id = 0;         ///< The id field of the header (0 when none).
    bool half = false;  ///< The magic was `RAWH`: half-float texels.
    ByteSpan pixels;    ///< `width * height * 4` bytes, or `* 8` when `half`.
};

/// Parses a texture source.
///
/// - Fails: `Invalid` with `error` filled in: a missing id (the message names `asset_mint adopt`), a repeated record,
///   an unknown record, a wrong field count, an unknown value, no `source` or a count that does not fit the kind
///   (1, 6 or 1 to 2048), a source name longer than 255 bytes, `srgb` on `r8` or `rg8`, `straight` alpha on a half
///   float format, a mip count of 0 or above 15.
/// - Ownership: borrows `text` for the call; `*out` and `*error` are written.
Status ParseTextureSource(StringView text, TextureSource* out, TextureSourceError* error);

/// Reads the header of a raw image.
///
/// - Fails: `Corrupt` for a bad magic, a size outside 1 to 16384 or pixel bytes that are not `width * height * 4`
/// (`RAWH`:
///   `* 8`).
/// - Ownership: borrows `bytes`; the image keeps a span into it.
Status ParseRawImage(ByteSpan bytes, RawImage* out);

/// Writes a raw image (the test generators' and the importer's output form).
///
/// - Param id: the id field of the header (0 for none).
/// - Fails: `Invalid` for a size outside 1 to 16384 or pixels of the wrong size.
/// - Ownership: borrows `rgba`; `*out` is replaced.
Status BuildRawImage(u32 width, u32 height, ByteSpan rgba, u64 id, Vector<byte>* out);

/// Writes a half-float raw image (`RAWH`: `width * height * 8` bytes of half-float RGBA).
///
/// - Fails: `Invalid` for a size outside 1 to 16384 or pixels of the wrong size.
/// - Ownership: borrows `rgba16f`; `*out` is replaced.
Status BuildRawHalfImage(u32 width, u32 height, ByteSpan rgba16f, u64 id, Vector<byte>* out);

/// Mints a texture: parses the source, premultiplies and converts each image to the stored format, makes the mip
/// chains, and builds the blob laid out for `target`.
///
/// - Param images: the raw file bytes, one per `source` record, in order.
/// - Fails: `Invalid` with `error` filled in as for [[asset::ParseTextureSource]], plus: a number of files that is
///   not the number of `source` records, a raw file that is malformed, images of different sizes, a cube whose faces
///   are not square, a requested mip count above the full chain; and as [[asset::BuildTextureBlob]].
/// - Param scheduler: where the block compression of `bc1`, `bc4`, `bc5` and `bc7` textures fans out, or null for the
/// calling
///   thread; the blob is the same either way (the caller must be the scheduler's participant 0 outside any job).
/// - Ownership: borrows its arguments for the call; `*out` is replaced.
Status MintTexture(StringView text, Span<const ByteSpan> images, const TypeRegistry& types, Vector<byte>* out,
                   TextureSourceError* error, u32 target = 0, jobs::Scheduler* scheduler = nullptr);

}  // namespace vse::asset
