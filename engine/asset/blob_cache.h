#pragma once

//! [[asset::BlobCache]] (Phase 22 Day 332): a directory of blobs named by their content hash, with an index of which
//! resource id had which hash last, bounded in bytes with least-recently-used eviction. The network backend keeps the
//! small blobs it fetched in it, so they are not fetched twice and are served when the server cannot be reached.
//!
//! A file is written to a temporary and renamed over its name ([[io::WriteFileAtomic]]), so a crash leaves a whole file
//! or none. Opening the cache hashes every file and drops the ones that do not match their name (a damaged or foreign
//! file), and files that no index entry names. A read of a cached blob does not hash it again: the cache is local and
//! was verified when it was opened and when the blob was put.

#include "asset/blob.h"
#include "asset/content_hash.h"
#include "asset/resource_id.h"
#include "asset/status.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "io/path.h"
#include "sync/mutex.h"
#include "types.h"

namespace engine_placeholder::asset
{

/// What the cache counted.
///
/// - Thread-safety: immutable value (a copy).
struct BlobCacheStats
{
    u32 entries = 0;          ///< Blobs held.
    u64 bytes = 0;            ///< Their bytes.
    u64 hits = 0;             ///< Blobs found by hash.
    u64 misses = 0;           ///< Blobs asked for and not held.
    u64 evictions = 0;        ///< Blobs dropped to stay within the limit.
    u64 dropped_corrupt = 0;  ///< Files dropped because they did not match their hash when the cache was opened.
};

/// The cache.
///
/// - Thread-safety: thread-safe; every member may be called from any thread.
/// - Ownership: owns its files under the directory it was opened on.
class BlobCache
{
public:
    /// Opens (and creates) the cache in `directory`, reading its index and checking every file.
    ///
    /// - Param directory: the directory; created when it does not exist.
    /// - Param max_bytes: the most bytes of blobs held (0 for none held).
    /// - Param out: receives the cache.
    /// - Fails: `Io` when the directory cannot be made or read.
    /// - Ownership: `*out` receives the cache.
    static Status Open(const io::Path& directory, u64 max_bytes, UniquePtr<BlobCache>* out);
    /// Used by `Open` (public so that `MakeUnique` can reach it): an empty cache over `directory`.
    BlobCache(const io::Path& directory, u64 max_bytes);
    /// Writes the index.
    ~BlobCache();
    BlobCache(const BlobCache&) = delete;
    BlobCache& operator=(const BlobCache&) = delete;

    /// What the cache last learned about `id`: the content hash of its blob and its size, even when the blob itself is
    /// not held (a name known from the last time the server was reached).
    ///
    /// - Returns: false when nothing is known.
    /// - Ownership: the outputs are written on success.
    /// - Thread-safety: thread-safe.
    bool Find(ResourceId id, ContentHash* hash, u64* size) const;
    /// Remembers that `id` has `hash` and `size` now.
    ///
    /// - Thread-safety: thread-safe.
    /// - Fails: never.
    void Note(ResourceId id, const ContentHash& hash, u64 size);
    /// True when the blob with this hash is held.
    ///
    /// - Thread-safety: thread-safe.
    /// - Fails: never.
    bool Has(const ContentHash& hash) const;
    /// Reads the blob with `hash` and makes it the most recently used.
    ///
    /// - Param out: receives the bytes.
    /// - Fails: `NotFound` when it is not held (a file that vanished is forgotten); `Io`.
    /// - Ownership: `*out` is replaced.
    /// - Thread-safety: thread-safe.
    Status Read(const ContentHash& hash, Vector<byte>* out);
    /// Stores a blob under its hash, remembers that `id` has it and evicts the least recently used blobs beyond the
    /// limit. A blob larger than the limit is not held.
    ///
    /// - Param hash: the hash the blob records.
    /// - Param bytes: the whole blob.
    /// - Fails: `Corrupt` when the bytes do not hash to `hash` (nothing is stored); `Io`.
    /// - Ownership: `bytes` is read during the call only.
    /// - Thread-safety: thread-safe.
    Status Put(ResourceId id, const ContentHash& hash, ByteSpan bytes);
    /// Writes the index now.
    ///
    /// - Fails: `Io`.
    /// - Thread-safety: thread-safe.
    Status Flush();
    /// A copy of the counters.
    ///
    /// - Thread-safety: thread-safe.
    /// - Fails: never.
    BlobCacheStats Stats() const;

private:
    struct Name  // what the cache knows of an id
    {
        ResourceId id;
        ContentHash hash;
        u64 size = 0;
    };
    struct Blob  // a file
    {
        ContentHash hash;
        u64 size = 0;
        u64 used = 0;
    };

    io::Path FileOf(const ContentHash& hash) const;
    Blob* FindBlob(const ContentHash& hash);
    void NoteLocked(ResourceId id, const ContentHash& hash, u64 size);
    Status WriteIndexLocked();
    void EvictLocked();
    void RemoveBlobLocked(usize index);

    io::Path directory_;
    u64 max_bytes_;
    mutable Mutex mutex_;
    Vector<Name> names_;
    Vector<Blob> blobs_;
    u64 clock_ = 0;
    u64 bytes_ = 0;
    BlobCacheStats stats_;
};

}  // namespace engine_placeholder::asset
