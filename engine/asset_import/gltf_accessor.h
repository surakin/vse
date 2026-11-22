#pragma once

//! Reading accessors as floats or unsigned integers.
//!
//! The elements of an accessor as floats or as unsigned integers, with the stride,
//! the matrix column padding, `normalized` and sparse overrides applied. The ranges were checked when the document was
//! parsed
//! ([[asset::ParseGltf]]); a sparse index is checked here, against the accessor's count.

#include "asset_import/gltf_container.h"
#include "containers/vector.h"

namespace vse::asset
{

/// Reads an accessor as floats.
///
/// `count * components` of them in element order. An integer component is its value as a float, or, when `normalized`,
/// mapped to [0, 1] (unsigned) or [-1, 1] (signed, the most negative value clamped).
///
/// - Param document: a parsed glTF.
/// - Param index: the accessor.
/// - Param out: replaced by the values.
/// - Param error: receives the sentence naming the accessor.
/// - Fails: `Invalid` (an index past the accessors, a float that is not finite, a sparse index out of range or not
/// strictly
///   increasing), `LimitExceeded` (memory).
/// - Ownership: borrows `document`; `*out` is replaced.
Status ReadAccessorFloats(const GltfDocument& document, u32 index, Vector<f32>* out, GltfError* error);

/// Reads an accessor of an unsigned integer type as `u32`s.
///
/// `UNSIGNED_BYTE`, `UNSIGNED_SHORT` or `UNSIGNED_INT`, not `normalized`; `count * components` of them: indices, joint
/// numbers.
///
/// - Fails: `Invalid` (a signed or float accessor, a normalized one, a sparse index out of range), `LimitExceeded`.
/// - Ownership: borrows its arguments for the call only; nothing is retained.
Status ReadAccessorUnsigned(const GltfDocument& document, u32 index, Vector<u32>* out, GltfError* error);

}  // namespace vse::asset
