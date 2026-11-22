#pragma once

//! A typed, read-only view of a JSON document for the glTF importer.
//!
//! The parser is the vendored yyjson
//! (`third_party/yyjson`, PLAN.md assumption #19), reached only through this header and `json_view.cpp`: the input may
//! be hostile (a file fetched from a room), so nothing here throws, aborts or allocates beyond a bound set before
//! parsing. Every accessor that can fail returns a [[asset::Status]] and names the path of the value it refused
//! (`meshes[2].primitives[0].attributes`).

#include "asset/status.h"
#include "asset_import/gltf_error.h"
#include "containers/array.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "string_view.h"
#include "types.h"

namespace vse::asset
{

class JsonDocument;

/// One value of a parsed document (an object, array, string, number, boolean or null), or no value (a member that is
/// not there).
///
/// - Thread-safety: thread-compatible; a value reads its document and is valid while the document lives.
/// - Ownership: borrows the document.
class JsonValue
{
public:
    /// No value.
    JsonValue() = default;

    /// True when this is a value (a member that was absent gives a `JsonValue` for which this is false).
    /// - Fails: never.
    bool Exists() const { return node_ != nullptr; }
    /// The kind of value.
    /// - Fails: never.
    bool IsObject() const;
    /// The kind of value.
    /// - Fails: never.
    bool IsArray() const;
    /// The kind of value.
    ///
    /// - Fails: never.
    bool IsString() const;
    /// The kind of value.
    ///
    /// - Fails: never.
    bool IsNumber() const;
    /// The kind of value.
    ///
    /// - Fails: never.
    bool IsBool() const;
    /// The kind of value.
    ///
    /// - Fails: never.
    bool IsNull() const;

    /// The path of this value, for messages (`meshes[2].name`; `$` for the root).
    ///
    /// - Ownership: the view is borrowed from this value.
    StringView Path() const;

    /// The member of an object, or no value when this is not an object or has no such member.
    JsonValue Get(StringView key) const;
    /// The number of elements of an array or members of an object, 0 for anything else.
    usize Size() const;
    /// The element of an array, or no value when this is not an array or `index` is past its end.
    JsonValue At(usize index) const;

    /// Reads a string.
    ///
    /// - Fails: `Invalid` (it is not a string, or it holds a NUL).
    /// - Ownership: the view is borrowed from the document.
    Status AsString(StringView* out, GltfError* error) const;
    /// Reads a boolean.
    ///
    /// - Fails: `Invalid` (it is not a boolean).
    /// - Ownership: borrows its arguments for the call only; nothing is retained.
    Status AsBool(bool* out, GltfError* error) const;
    /// Reads a finite number.
    ///
    /// - Fails: `Invalid` (it is not a number).
    /// - Ownership: borrows its arguments for the call only; nothing is retained.
    Status AsDouble(f64* out, GltfError* error) const;
    /// Reads an integer in `[0, max]` (a number written `3` or `3.0`).
    ///
    /// - Fails: `Invalid` (it is not an integer, or is negative, or above `max`).
    /// - Ownership: borrows its arguments for the call only; nothing is retained.
    Status AsUnsigned(u64* out, u64 max, GltfError* error) const;
    /// Reads an index below `count` (a reference into another array of the glTF).
    ///
    /// - Fails: `Invalid` (it is not an integer, or is not below `count`).
    /// - Ownership: borrows its arguments for the call only; nothing is retained.
    Status AsIndex(u32* out, u64 count, GltfError* error) const;

    /// Reads the string member `key`; an absent member leaves `*out` as it was and sets `*present` false (when not
    /// null).
    ///
    /// - Fails: `Invalid` (it is there but not a string).
    /// - Ownership: borrows its arguments for the call only; nothing is retained.
    Status OptionalString(StringView key, StringView* out, bool* present, GltfError* error) const;
    /// Reads the unsigned member `key` as `AsUnsigned` does; an absent member leaves `*out` as it was.
    /// - Ownership: borrows its arguments for the call only; nothing is retained.
    Status OptionalUnsigned(StringView key, u64* out, u64 max, bool* present, GltfError* error) const;
    /// Reads the boolean member `key`; an absent member leaves `*out` as it was.
    /// - Ownership: borrows its arguments for the call only; nothing is retained.
    Status OptionalBool(StringView key, bool* out, GltfError* error) const;
    /// Reads the string member `key`, which must be there.
    ///
    /// - Fails: `Invalid` (absent, or not a string).
    /// - Ownership: borrows its arguments for the call only; nothing is retained.
    Status RequiredString(StringView key, StringView* out, GltfError* error) const;
    /// Reads the unsigned member `key`, which must be there.
    ///
    /// - Fails: `Invalid` (absent, or not an integer in range).
    /// - Ownership: borrows its arguments for the call only; nothing is retained.
    Status RequiredUnsigned(StringView key, u64* out, u64 max, GltfError* error) const;
    /// Reads the index member `key`, which must be there.
    ///
    /// - Fails: `Invalid` (absent, or not an integer below `count`).
    /// - Ownership: borrows its arguments for the call only; nothing is retained.
    Status RequiredIndex(StringView key, u32* out, u64 count, GltfError* error) const;

    /// Starts a walk over the members of an object.
    ///
    /// - Param iterator: receives the walk (empty for a value that is not an object).
    /// - Ownership: borrows its arguments for the call only; nothing is retained.
    void Members(class JsonMembers* iterator) const;

private:
    friend class JsonDocument;
    friend class JsonMembers;
    const void* node_ = nullptr;
    // The element `At` returned last, so that reading an array of objects from the first element to the last takes
    // linear time (finding element `i` of such an array otherwise walks `i` siblings).
    mutable const void* cursor_node_ = nullptr;
    mutable usize cursor_index_ = 0;
    Array<char, 96> path_ = {};
    void Extend(const JsonValue& parent, StringView key, bool index_form, usize index);
};

/// A walk over the members of an object, in document order.
///
/// - Thread-safety: thread-compatible.
class JsonMembers
{
public:
    /// Moves to the next member.
    ///
    /// - Returns: false at the end.
    /// - Ownership: borrows its arguments for the call only; nothing is retained.
    bool Next(StringView* key, JsonValue* value);

private:
    friend class JsonValue;
    usize state_[4] = {};
    JsonValue parent_;
};

/// A parsed JSON document and the memory it lives in.
///
/// - Thread-safety: thread-compatible; read-only after `Parse`, so any number of threads may read it.
/// - Ownership: owns the parsed tree (one buffer sized from the input's length before parsing).
class JsonDocument
{
public:
    /// An empty document.
    JsonDocument() = default;
    JsonDocument(const JsonDocument&) = delete;
    JsonDocument& operator=(const JsonDocument&) = delete;
    /// Moves a document (the values of the source stay valid in the destination).
    JsonDocument(JsonDocument&& other) noexcept;
    /// Moves a document.
    JsonDocument& operator=(JsonDocument&& other) noexcept;
    ~JsonDocument() = default;

    /// Parses strict RFC 8259 JSON.
    ///
    /// UTF-8 validated, no comments, no trailing commas, no BOM, nesting at most 128 deep. The memory the parse may use
    /// is a function of `bytes.Size()` alone, reserved before parsing; a document an object of which repeats a key is
    /// refused (the library accepts it, a glTF must not).
    ///
    /// - Param bytes: the JSON text.
    /// - Param max_bytes: the longest text taken.
    /// - Param error: receives the position and the reason.
    /// - Fails: `Invalid` (not JSON, or a repeated key), `TooLarge` (above `max_bytes`), `LimitExceeded` (the memory
    /// for a
    ///   document that size cannot be had).
    /// - Ownership: borrows `bytes` for the call only (strings are copied into the document).
    Status Parse(ByteSpan bytes, u64 max_bytes, GltfError* error);

    /// The root value (no value before a successful `Parse`).
    JsonValue Root() const;

private:
    Vector<byte> memory_;
    const void* root_ = nullptr;
};

}  // namespace vse::asset
