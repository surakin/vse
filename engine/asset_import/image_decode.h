#pragma once

//! Decoding the images of a glTF: PNG and JPEG to RGBA8.
//!
//! Phase 22 Day 338. libpng (over zlib) and stb_image's JPEG decoder, vendored under `third_party/`, reached only
//! through this wrapper. The bytes may be hostile (an image fetched from a room): a decoder fails by return value and
//! never aborts or exits, the dimensions and the pixel count are checked against the limits before any pixel is
//! decoded, and the size limits of libpng are set before it reads. Palette, gray and alpha forms expand to RGBA, 16-bit
//! samples reduce to 8 bits, an interlaced PNG is read whole.

#include "asset_import/gltf_error.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "types.h"

namespace engine_placeholder::asset
{

/// The image formats the importer decodes.
enum class ImageFormat : u8
{
    Unknown,  ///< Neither a PNG nor a JPEG signature.
    Png,      ///< The 8-byte PNG signature.
    Jpeg,     ///< The JPEG start-of-image marker.
};

/// A decoded image.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns its pixels.
struct DecodedImage
{
    u32 width = 0;           ///< Texels across.
    u32 height = 0;          ///< Texels down.
    Vector<byte> rgba;       ///< `width * height * 4` bytes, rows from the top, straight (not premultiplied) alpha.
    bool has_alpha = false;  ///< The file has an alpha channel (or a transparency chunk): `rgba` alpha is not all 255.
    bool reduced_depth = false;  ///< The file had 16 bits per channel; they were reduced to 8.
};

/// Tells which format `bytes` start like.
///
/// - Param bytes: the file's bytes.
/// - Fails: reported by the return value.
ImageFormat SniffImageFormat(ByteSpan bytes);

/// Decodes a PNG or a JPEG.
///
/// - Param bytes: the file's bytes.
/// - Param max_pixels: the most pixels it may have (`width * height`), checked before decoding; also at most 16384 on
/// either side.
/// - Param out: replaced by the image.
/// - Param error: receives the sentence (the caller prefixes the glTF object).
/// - Fails: `Invalid` (not a PNG or JPEG, a corrupt or unsupported file: progressive JPEGs are supported, 12-bit and
/// arithmetic-coded ones are not),
///   `TooLarge` (above `max_pixels` or 16384 on a side, or more than 2 GiB of bytes), `LimitExceeded` (memory).
/// - Thread-safety: thread-safe for separate calls.
/// - Ownership: borrows `bytes` for the call only; `*out` owns its pixels.
Status DecodePixels(ByteSpan bytes, u64 max_pixels, DecodedImage* out, GltfError* error);

}  // namespace engine_placeholder::asset
