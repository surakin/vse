#pragma once

//! The text source form of a height map (`.hgt.src`) and the height map minter. An offline tool's code: never linked
//! into the engine; its tests compile it directly.
//!
//! Line-oriented text; `#` starts a comment; fields are separated by spaces or tabs:
//!
//!     id 0123456789abcdef
//!     source terrain.r16      (a raw file of width * height 16-bit little-endian samples, rows from z = 0, no header)
//!     width 1025              (samples across, 2 to 8192)
//!     height 1025             (samples down, 2 to 8192)
//!     range_min 0             (the height in metres of sample value 0)
//!     range_max 300           (the height in metres of sample value 65535; not below range_min)
//!     cell_size 2             (metres between samples; above 0)
//!
//! `id` comes first (written by `asset_mint adopt`); every other record is required once.

#include "asset/height_map.h"
#include "asset/resource_id.h"
#include "asset/status.h"
#include "containers/array.h"
#include "containers/vector.h"
#include "string_view.h"
#include "types.h"

namespace vse::asset
{

/// A parsed height map source.
///
/// - Thread-safety: thread-compatible.
struct HeightMapSource
{
    ResourceId id;                 ///< From the `id` line.
    Array<char, 256> source = {};  ///< The raw sample file name, NUL-terminated, relative to the source file.
    u32 source_line = 0;           ///< The line of the `source` record.
    HeightMapHeader header;        ///< The size, the range and the cell size.
};

/// Where and why a height map source was refused.
///
/// - Thread-safety: immutable value.
struct HeightMapSourceError
{
    u32 line = 0;                  ///< 1-based line number; 0 when the error is about the file as a whole.
    Array<char, 96> message = {};  ///< A sentence.
};

/// Parses a height map source.
///
/// - Fails: `Invalid` with `error` filled in: a missing id (the message names `asset_mint adopt`), a missing or
///   repeated record, an unknown record, a wrong field count, a number that does not parse, a source name longer than
///   255 bytes, a size, range or cell size the blob refuses.
/// - Ownership: borrows `text` for the call; `*out` and `*error` are written.
Status ParseHeightMapSource(StringView text, HeightMapSource* out, HeightMapSourceError* error);

/// Mints a height map: parses the source and builds the blob.
///
/// - Param samples: the bytes of the `source` file.
/// - Fails: as [[asset::ParseHeightMapSource]], plus `Invalid` for a sample file that is not `width * height * 2`
/// bytes.
/// - Ownership: borrows its arguments for the call; `*out` is replaced.
Status MintHeightMap(StringView text, ByteSpan samples, const TypeRegistry& types, Vector<byte>* out,
                     HeightMapSourceError* error, u32 target = 0);

}  // namespace vse::asset
