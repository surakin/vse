#pragma once
//! The script resource: the source text of a script with the facts the minter found in it, so a
//! load never parses to learn them. [[asset::BuildScriptBlob]] writes an `SCRP` blob, [[asset::ScriptView]] reads one
//! in place (the source is a range of the loaded buffer, shared, never copied).
//!
//! Layout of the blob's sections (little-endian). The dependencies are the container's standard dependency section
//! (other scripts the source requires, by id, type `SCRP`).
//!
//! | Section | Content |
//! |---|---|
//! | 16 header | 32 bytes: source bytes `u32`, entry count `u32`, source hash `u64` (first 8 bytes of the content hash
//! of the source), language name bytes `u32`, language version bytes `u32`, reserved `u64` (zero) | | 17 source | UTF-8
//! text, line endings normalized to `\n`, as validated by the minter | | 18 language | the language name then its
//! version text (lengths in the header) | | 19 entry points | per entry: name length `u32`, the name; the behaviors the
//! validator found the source defines | | 20 bytecode | optional: key length `u32`, the key text, then the precompiled
//! chunk; used only when the loader's key is equal, never required |
//!
//! The asset module knows nothing about languages: the minter validates and names, the script module loads.

#include "asset/blob.h"
#include "asset/resource_id.h"
#include "asset/resource_type.h"
#include "asset/status.h"
#include "containers/shared_bytes.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "mem/heap_allocator.h"
#include "string/string.h"
#include "string_view.h"
#include "types.h"

namespace vse::asset
{

/// The size of the header section.
inline constexpr usize kScriptHeaderBytes = 32;
/// The most bytes of source one script may have.
inline constexpr u32 kMaxScriptSourceBytes = 1u << 20;
/// The most entry points one script may list.
inline constexpr u32 kMaxScriptEntries = 256;
/// The most bytes of one entry point name.
inline constexpr u32 kMaxScriptEntryBytes = 255;
/// The most bytes of the language name, its version and a bytecode key.
inline constexpr u32 kMaxScriptTextBytes = 128;
/// The most bytes of precompiled chunk.
inline constexpr u32 kMaxScriptBytecodeBytes = 1u << 24;

/// The section ids of a script blob.
enum ScriptSection : u32
{
    kScriptSectionHeader = 16,    ///< The description (32 bytes).
    kScriptSectionSource = 17,    ///< The source text.
    kScriptSectionLanguage = 18,  ///< The language name and version.
    kScriptSectionEntries = 19,   ///< The entry point names.
    kScriptSectionBytecode = 20,  ///< The optional precompiled chunk with its key.
};

/// What a script blob is made from.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns the strings; `bytecode` and `source` are copied into the blob.
struct ScriptBuild
{
    /// The language registry name (`luajit`).
    String<HeapAllocator> language{HeapAllocator::Default()};
    /// The language runtime's version text.
    String<HeapAllocator> language_version{HeapAllocator::Default()};
    /// The normalized source text.
    String<HeapAllocator> source{HeapAllocator::Default()};
    /// The entry points found, in the order the source defines them.
    Vector<String<HeapAllocator>> entries;
    /// The scripts this one requires, strictly increasing by id.
    Vector<ResourceId> dependencies;
    /// The key the bytecode is valid for (language, version, architecture); empty with no bytecode.
    String<HeapAllocator> bytecode_key{HeapAllocator::Default()};
    /// The precompiled chunk; empty with none.
    Vector<byte> bytecode;
};

/// Builds a script blob.
///
/// - Param types: the type registry; `SCRP` must be in it.
/// - Param id: the script's id; not none.
/// - Param build: the contents; the dependencies must not include `id` and must be strictly increasing.
/// - Param out: replaced with the blob.
/// - Param target: the target tag (0, development).
/// - Ownership: borrows the arguments for the call.
/// - Fails: `Invalid` for a source that is empty or above the limit, a name or key over its limit, too many entries, a
/// bytecode without a key (or a key without bytecode), or dependencies out of order.
Status BuildScriptBlob(const TypeRegistry& types, ResourceId id, const ScriptBuild& build, Vector<byte>* out,
                       u32 target = 0);

/// A script blob read in place.
///
/// - Thread-safety: thread-compatible; a view is immutable after `Open`.
/// - Ownership: shares the buffer it was opened over.
class ScriptView
{
public:
    /// Makes an empty view.
    ScriptView() = default;

    /// Validates the blob and reads its header.
    ///
    /// - Param bytes: the loaded buffer; kept (shared) by the view.
    /// - Param types: the type registry.
    /// - Returns: success, or why the blob is refused (truncated, another type or version, a header field out of range,
    /// a section of the wrong size, a malformed entry list).
    /// - Fails: a `Corrupt`, `TypeMismatch` or `Invalid` status, as described.
    Status Open(const SharedBytesPtr& bytes, const TypeRegistry& types);

    /// The script's id.
    ///
    /// - Fails: never.
    ResourceId Id() const { return id_; }

    /// The source text, a range of the shared buffer.
    ///
    /// - Ownership: borrowed; valid while the view lives.
    /// - Fails: never.
    ByteSpan Source() const;

    /// Where the source starts in `Bytes()` and how long it is.
    ///
    /// - Fails: never.
    usize SourceOffset() const { return source_offset_; }
    /// The length of the source.
    ///
    /// - Fails: never.
    usize SourceSize() const { return source_size_; }

    /// The first 8 bytes of the content hash of the source.
    ///
    /// - Fails: never.
    u64 SourceHash() const { return source_hash_; }

    /// The language name.
    ///
    /// - Fails: never.
    StringView Language() const;
    /// The language's version text.
    ///
    /// - Fails: never.
    StringView LanguageVersion() const;

    /// How many entry points the script lists.
    ///
    /// - Fails: never.
    usize EntryCount() const { return entry_count_; }
    /// The name of entry point `index`.
    ///
    /// - Param index: below `EntryCount()`.
    /// - Returns: the name, a view of the shared buffer; empty for an index out of range.
    /// - Fails: never.
    StringView Entry(usize index) const;

    /// The precompiled chunk when the blob has one for exactly `key`; empty otherwise (the caller loads the source).
    ///
    /// - Param key: the key of the running language, version and architecture.
    /// - Returns: the chunk, a range of the shared buffer, or an empty span.
    /// - Ownership: borrowed; valid while the view lives.
    /// - Fails: never.
    ByteSpan BytecodeFor(StringView key) const;

    /// How many scripts this one requires (the container's dependencies, strictly increasing by id).
    ///
    /// - Fails: never.
    usize DependencyCount() const { return dependencies_.Size(); }

    /// The id of required script `index`; none when `index` is out of range.
    ///
    /// - Fails: never.
    ResourceId DependencyAt(usize index) const
    {
        return index < dependencies_.Size() ? dependencies_[index] : ResourceId::None();
    }

    /// The buffer the view shares.
    ///
    /// - Ownership: borrowed.
    /// - Fails: never.
    const SharedBytesPtr& Bytes() const { return bytes_; }

private:
    ResourceId id_;
    Vector<ResourceId> dependencies_;
    SharedBytesPtr bytes_;
    usize source_offset_ = 0;
    usize source_size_ = 0;
    u64 source_hash_ = 0;
    usize language_offset_ = 0;
    u32 language_size_ = 0;
    u32 version_size_ = 0;
    usize entries_offset_ = 0;
    usize entries_size_ = 0;
    u32 entry_count_ = 0;
    usize bytecode_offset_ = 0;
    usize bytecode_size_ = 0;
    u32 key_size_ = 0;
};

}  // namespace vse::asset
