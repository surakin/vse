#pragma once

//! Splat maps for the terrain: the fixed weight normalisation applied to the pixels of a raw source, so a splat map is
//! minted the same on every machine. The result goes to the texture minter ([[asset::MintTextureFromPixels]], format
//! `RGBA8_UNorm`, one mip level or a box chain) like any other image.

#include "asset/status.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "types.h"

namespace vse::asset
{

/// Rewrites every pixel of an RGBA8 image so its four weights sum to exactly 255
/// ([[renderer::TerrainNormalizeWeights]]).
///
/// - Param width: texels across, 1 to 16384.
/// - Param height: texels down, 1 to 16384.
/// - Param rgba: `width * height * 4` bytes.
/// - Param out: replaced by the normalised image.
/// - Fails: `Invalid` for a size out of range or bytes of the wrong count; `LimitExceeded` when memory runs out.
/// - Thread-safety: thread-safe.
/// - Ownership: borrows `rgba`; `*out` is replaced.
Status NormalizeSplatImage(u32 width, u32 height, ByteSpan rgba, Vector<byte>* out);

}  // namespace vse::asset
