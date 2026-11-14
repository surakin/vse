#pragma once

//! The glTF container: reading a file into a checked document.
//!
//! Phase 22 Day 335. `.glb` (the 12-byte header and its chunks) and `.gltf` (JSON with external
//! files and base64 `data:` URIs) read into a [[asset::GltfDocument]] whose buffers, buffer views and accessors have
//! been checked against each other and against [[asset::ImportLimits]]. Everything after this point may index the
//! buffers through the document without checking a range again.

#include "asset_import/gltf_import.h"
#include "asset_import/json_view.h"
#include "containers/vector.h"

namespace engine_placeholder::asset
{

/// A `bufferViews` entry, range-checked against its buffer.
///
/// - Thread-safety: thread-compatible plain data.
struct GltfBufferView
{
    u32 buffer = 0;  ///< The index into `buffers`.
    u64 offset = 0;  ///< Where the view starts in the buffer.
    u64 length = 0;  ///< Its length (above zero).
    u32 stride = 0;  ///< The distance between elements, 0 for tightly packed.
};

/// The number of components of an accessor's `type`.
enum class GltfAccessorType : u8
{
    Scalar,  ///< `SCALAR`: 1.
    Vec2,    ///< `VEC2`: 2.
    Vec3,    ///< `VEC3`: 3.
    Vec4,    ///< `VEC4`: 4.
    Mat2,    ///< `MAT2`: 4, column-major, columns padded to 4 bytes.
    Mat3,    ///< `MAT3`: 9.
    Mat4,    ///< `MAT4`: 16.
};

/// The sparse part of an accessor.
///
/// - Thread-safety: thread-compatible plain data.
struct GltfSparse
{
    bool present = false;  ///< The accessor has a `sparse` object.
    u32 count = 0;         ///< How many elements it overrides.
    u32 index_view = 0;    ///< The buffer view of the indices.
    u64 index_offset = 0;  ///< Their byte offset.
    u32 index_type = 0;    ///< Their component type (5121, 5123 or 5125).
    u32 value_view = 0;    ///< The buffer view of the new values.
    u64 value_offset = 0;  ///< Their byte offset.
};

/// An `accessors` entry, range-checked against its buffer view.
///
/// - Thread-safety: thread-compatible plain data.
struct GltfAccessor
{
    u32 component_type = 0;  ///< 5120 BYTE, 5121 UBYTE, 5122 SHORT, 5123 USHORT, 5125 UINT or 5126 FLOAT.
    GltfAccessorType type = GltfAccessorType::Scalar;  ///< The element shape.
    u32 components = 1;                                ///< Components per element (1, 2, 3, 4, 4, 9 or 16).
    u64 count = 0;                                     ///< Elements (at least 1).
    bool normalized = false;                           ///< Integers map to [0, 1] or [-1, 1].
    bool has_view = false;                             ///< There is a buffer view (else the base data is all zeros).
    u32 view = 0;                                      ///< Its index.
    u64 offset = 0;                                    ///< Where the first element is in the view.
    u32 stride = 0;         ///< The bytes between elements (the view's stride or the element size).
    u32 element_bytes = 0;  ///< The bytes of one element (matrix columns padded).
    GltfSparse sparse;      ///< The sparse override.
};

/// A parsed glTF file: the JSON, the bytes of every buffer, and the checked views and accessors.
///
/// - Thread-safety: thread-compatible; read-only after `ParseGltf`.
/// - Ownership: owns the document and the buffers.
struct GltfDocument
{
    JsonDocument json;               ///< The parsed JSON text.
    JsonValue root;                  ///< Its root object.
    bool is_glb = false;             ///< The file was a `.glb`.
    Vector<Vector<byte>> buffers;    ///< Each buffer's bytes (exactly its `byteLength`).
    Vector<GltfBufferView> views;    ///< `bufferViews`, checked.
    Vector<GltfAccessor> accessors;  ///< `accessors`, checked.
};

/// The `extensionsRequired` entries the importer supports (the others are refused naming them).
///
/// - Param count: receives the number of names.
/// - Ownership: the array is static.
/// - Fails: never.
const char* const* SupportedRequiredExtensions(usize* count);

/// Reads the container of a glTF file and checks it.
///
/// The version, the extensions, every array's count against the limits,
/// the buffers (read through `source` when external), the buffer views against their buffers and the accessors against
/// their views, all with checked arithmetic.
///
/// - Param file: the `.gltf` or `.glb` bytes.
/// - Param source: reads the external files by their cleaned relative names.
/// - Param limits: the bounds.
/// - Param out: replaced by the document.
/// - Param error: receives the sentence naming the object.
/// - Fails: `Invalid` (not a glTF 2.0, a broken container, a range outside its buffer, a URI that leaves the glTF's
///   directory), `TooLarge` (a limit), `Unsupported` (a required extension), `NotFound` (a file the source lacks),
///   `LimitExceeded` (memory).
/// - Ownership: borrows `file` for the call only.
Status ParseGltf(ByteSpan file, GltfSource& source, const ImportLimits& limits, GltfDocument* out, GltfError* error);

/// Turns a URI of a glTF into the clean relative name a [[asset::GltfSource]] receives: percent-escapes decoded,
/// segments joined by `/`.
///
/// - Param uri: the URI as written (not a `data:` URI).
/// - Param out: receives the name.
/// - Param error: receives the sentence (the caller sets the object).
/// - Fails: `Invalid` for an empty URI, an absolute path, a scheme (`file:`, `http:`), a backslash, a control
/// character, a
///   `..` segment, an empty segment or a bad percent-escape, or a name over 512 bytes. A `.` segment is dropped.
/// - Ownership: borrows its arguments for the call only; nothing is retained.
Status CleanRelativeUri(StringView uri, String<HeapAllocator>* out, GltfError* error);

/// Decodes standard base64 (with padding, no whitespace).
///
/// - Param text: the characters.
/// - Param max_bytes: the most bytes it may decode to (checked before decoding).
/// - Param out: replaced by the bytes.
/// - Fails: `Invalid` (a character outside the alphabet, bad padding, a length that is not a multiple of four),
///   `TooLarge` (it would decode to more than `max_bytes`).
/// - Ownership: borrows its arguments for the call only; nothing is retained.
Status DecodeBase64(StringView text, u64 max_bytes, Vector<byte>* out, GltfError* error);

}  // namespace engine_placeholder::asset
