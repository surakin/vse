#pragma once

#include "containers/vector.h"
#include "types.h"

//! The Direct3D 12 pipeline cache file's rules (Phase 13 Day 167), as pure functions so they are tested without a GPU: its name, its
//! header, and the names pipelines are stored under in the `ID3D12PipelineLibrary` it holds.

namespace engine_placeholder::rhi::dx12 {

/// FNV-1a over bytes, continuing from `seed` (the offset basis to start).
///
/// - Param data: the bytes.
/// - Param size: how many.
/// - Param seed: the running hash.
/// - Returns: the hash.
/// - Thread-safety: thread-safe. A pure function.
u64 Fnv1a(const void* data, usize size, u64 seed = 0xcbf29ce484222325ull);

/// The size of the header in front of the library's serialized bytes.
inline constexpr usize kPipelineLibraryHeaderBytes = 40;

/// Hashes the adapter id and driver version a cache file is for (the header records it, so a renamed file is caught too).
///
/// - Param adapter_id: [[rhi::AdapterInfo]]'s `id`.
/// - Param driver: its `driver` version text.
/// - Returns: the hash.
/// - Thread-safety: thread-safe. A pure function.
u64 PipelineLibraryIdentity(const char* adapter_id, const char* driver);

/// Writes the header: the magic `ENGPLIB1`, a format version, the key count, the identity, the payload's size and its FNV-1a hash.
/// The payload is the keys (8 bytes each, little-endian), then the serialized library.
///
/// - Param out: receives [[rhi::dx12::kPipelineLibraryHeaderBytes]] bytes.
/// - Param identity: from [[rhi::dx12::PipelineLibraryIdentity]].
/// - Param key_count: how many keys the payload starts with.
/// - Param payload: the keys and the library.
/// - Param payload_size: their size.
/// - Thread-safety: thread-safe. A pure function.
void WritePipelineLibraryHeader(byte* out, u64 identity, u32 key_count, const void* payload, usize payload_size);

/// The parts of a checked cache file.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: the pointers borrow the file's bytes.
struct PipelineLibraryParts {
    /// The keys of the pipelines the library holds (unaligned, little-endian; read with [[rhi::dx12::PipelineLibraryKey]]).
    const byte* keys = nullptr;
    /// How many.
    u32 key_count = 0;
    /// The serialized library, for `CreatePipelineLibrary`.
    const byte* library = nullptr;
    /// Its size.
    usize library_size = 0;
};

/// Checks a cache file before D3D12 sees any of it: the header, the identity, the payload's size and hash, and the key section.
///
/// - Param data: the whole file.
/// - Param size: its size.
/// - Param identity: the device's, from [[rhi::dx12::PipelineLibraryIdentity]].
/// - Param parts: receives the parts when the file is usable; may be null.
/// - Returns: null if the file is usable, else why not.
/// - Thread-safety: thread-safe. A pure function.
const char* CheckPipelineLibraryFile(const void* data, usize size, u64 identity, PipelineLibraryParts* parts = nullptr);

/// Reads key `index` of a checked file's key section.
///
/// - Param parts: from [[rhi::dx12::CheckPipelineLibraryFile]].
/// - Param index: below `key_count`.
/// - Returns: the key.
/// - Thread-safety: thread-safe. A pure function.
u64 PipelineLibraryKey(const PipelineLibraryParts& parts, u32 index);

/// Adds a key to a sorted list unless it is there.
///
/// - Param keys: sorted ascending.
/// - Param key: the key.
/// - Returns: true if it was added.
/// - Thread-safety: not-thread-safe (the caller's list).
bool InsertSortedKey(Vector<u64>* keys, u64 key);

/// Returns whether a sorted list holds a key.
///
/// - Param keys: sorted ascending.
/// - Param key: the key.
/// - Returns: true if found.
/// - Thread-safety: thread-safe for a list nobody changes meanwhile.
bool HasSortedKey(const Vector<u64>& keys, u64 key);

/// Builds the cache file path `<dir>/pipelines_<adapter id>_<driver>.bin`, the driver's dots made underscores.
///
/// - Param dir: the directory.
/// - Param adapter_id: the adapter's id.
/// - Param driver: its driver version text (empty: `nodriver`).
/// - Param out: receives the nul-terminated path.
/// - Param capacity: size of `out`.
/// - Returns: true if it fit.
/// - Fails: false, leaving `out` empty, if it does not fit.
/// - Thread-safety: thread-safe. A pure function.
bool BuildPipelineLibraryPath(const char* dir, const char* adapter_id, const char* driver, char* out, usize capacity);

/// The name a pipeline is stored under in the library: `p` and the key's 16 hex digits.
///
/// - Param key: the pipeline's cache key.
/// - Param out: receives 18 wide characters, nul included.
/// - Thread-safety: thread-safe. A pure function.
void PipelineLibraryName(u64 key, wchar_t (&out)[18]);

} // namespace engine_placeholder::rhi::dx12
