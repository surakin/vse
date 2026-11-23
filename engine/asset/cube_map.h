#pragma once

//! Cube map geometry for the minters: face directions and the equirectangular-to-cube conversion.
//!
//! The conversion uses only `+ - * /` and the square root, with the angle functions replaced by fixed polynomials,
//! so the bytes are the same on every machine.

#include "asset/status.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "math/vector.h"
#include "types.h"

namespace vse::asset
{

/// The most texels across a cube face the equirectangular conversion makes.
inline constexpr u32 kMaxCubeFaceSize = 4096;

/// The exact value of a half float given as its 16 bits (an integer-only conversion).
///
/// - Param half: the bits.
/// - Param out: set to the value on success.
/// - Returns: false for an infinity or a NaN.
/// - Ownership: `*out` is written.
/// - Thread-safety: thread-safe. A pure function of its arguments.
bool HalfBitsToFloat(u32 half, f32* out);

/// The half float nearest to `value` (ties to even) as its 16 bits, clamped to the largest finite half (65504).
///
/// - Param value: finite.
/// - Thread-safety: thread-safe. A pure function of its arguments.
u32 FloatToHalfBits(f32 value);

/// The direction a cube face texel looks along (not normalized), in the convention of the three graphics APIs.
///
/// The face order is +X, -X, +Y, -Y, +Z, -Z; `u` runs along the face's rows from the left edge (-1) to the right edge
/// (+1) and `v` down its columns from the top edge (-1) to the bottom edge (+1), both at texel centers `2 * (i + 0.5) /
/// n - 1`.
///
/// - Param face: 0 to 5.
/// - Param u: the horizontal face coordinate, -1 to 1.
/// - Param v: the vertical face coordinate, -1 to 1.
/// - Aborts: `face` is 6 or more.
/// - Thread-safety: thread-safe. A pure function of its arguments.
Vec3 CubeFaceDirection(u32 face, f32 u, f32 v);

/// The angle in radians of the point `(x, y)` from the positive x axis, -pi to pi, by a fixed polynomial (error under
/// 1e-5 radians); `(0, 0)` gives 0.
///
/// - Thread-safety: thread-safe. A pure function of its arguments.
f32 DeterministicAtan2(f32 y, f32 x);

/// Converts an equirectangular image of half-float RGBA texels into the six faces of a cube map, bilinearly sampled.
///
/// The image's center is the direction -Z, its columns run toward +X (east, to the right when looking along -Z), its
/// top row is straight up (+Y). Texels must be finite.
///
/// - Param rgba16f: `width * height * 8` bytes of half-float RGBA, rows tightly packed.
/// - Param width: 2 to 16384 texels across.
/// - Param height: 1 to 16384 texels down.
/// - Param face_size: texels across a face, 1 to [[asset::kMaxCubeFaceSize]].
/// - Param faces: replaced by six faces of `face_size * face_size * 8` bytes each, in the order +X -X +Y -Y +Z -Z.
/// - Fails: `Invalid` for a size out of range or bytes of the wrong count; `Corrupt` for an infinite or NaN texel;
///   `LimitExceeded` when memory runs out.
/// - Thread-safety: thread-safe.
/// - Ownership: borrows `rgba16f`; `*faces` is replaced.
Status EquirectangularToCubeFaces(ByteSpan rgba16f, u32 width, u32 height, u32 face_size, Vector<Vector<byte>>* faces);

}  // namespace vse::asset
