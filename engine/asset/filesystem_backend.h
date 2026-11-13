#pragma once

//! [[asset::FilesystemBackend]]: a directory of loose blobs served through [[asset::ResourceBackend]], the backend of
//! development builds.

#include "asset/backend.h"
#include "asset/blob.h"
#include "asset/resource_type.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "io/directory.h"
#include "io/path.h"
#include "mem/heap_allocator.h"
#include "string/string.h"
#include "sync/mutex.h"

namespace engine_placeholder::asset
{

/// The name of the stored table a directory may hold at its root (a minted `NAMES` blob).
inline constexpr const char* kStoredTableFileName = "resources.table";

/// Serves the blobs under a root directory.
///
/// The index (id, type, virtual path, file) is built once, on first use, either from the stored table
/// `resources.table` at the root (a minted `NAMES` blob) or, when there is none, by one walk of the directory that
/// reads each file's 64-byte header (the path is the file's location under the root, the id the blob's own, never an id
/// from a file name). A stored table is a cache that is never trusted over the files: `Stat` and `Open` check the
/// header of the file they find against the id they were asked for. Files that do not start with the blob magic are
/// ignored by the walk; two files that claim one id fail the build of the index. `Watch` rescans and reports what
/// changed (modification stamps and sizes decide, so an unchanged file is not read again).
///
/// The walk goes 64 directories deep; a blob below that is not indexed.
///
/// A path never leaves the root: the index only holds files found under it, and a symbolic link is followed only when
/// its target is inside the root too (loops fail to resolve and are skipped).
/// - Thread-safety: thread-safe (one lock around the index).
/// - Ownership: owns its index; `types` must outlive the backend.
class FilesystemBackend : public ResourceBackend
{
public:
    /// Makes a backend over `root`.
    ///
    /// - Param target: the target tag of the running program; a blob laid out for another target is refused with
    ///   `WrongTarget`.
    /// - Fails: `NotFound` when `root` is not a directory; `Io` when it cannot be examined.
    /// - Ownership: `*out` receives the backend on success; the caller owns it.
    static Status Create(const io::Path& root, const TypeRegistry& types, UniquePtr<FilesystemBackend>* out,
                         u32 target = 0);

    /// Used by `Create`; call that instead.
    FilesystemBackend(const io::Path& root, io::Path canonical_root, const TypeRegistry& types, bool stored_table,
                      u32 target = 0);

    const char* Name() const override { return "filesystem"; }
    u32 Capabilities() const override
    {
        return kBackendRanged | kBackendAsync | kBackendWatch | (stored_table_ ? u32(kBackendStoredTable) : 0u);
    }

    /// Describes, opens and lists the files under the root as [[asset::ResourceBackend]] says.
    ///
    /// - Fails: `NotFound`, `Corrupt` (the file's own id differs from `id`: a stale table), the index's failures
    ///   (`DuplicateId`, `UnsupportedVersion`, `Corrupt`, `Io`).
    /// - Ownership: borrows its pointer arguments for the call only; nothing is retained.
    Status Stat(ResourceId id, ResourceInfo* out) override;
    Status Open(ResourceId id, UniquePtr<ResourceReader>* out) override;
    Status ReadTable(ResourceTable* out) override;
    Status Watch(Vector<ChangeRecord>* out) override;

private:
    struct Item
    {
        Item() : path(HeapAllocator::Default()) {}
        ResourceId id;
        ResourceType type;
        String<HeapAllocator> path;  // the virtual path, normalized
        io::Path file;
        u64 size = 0;
        i64 modified_ns = 0;
    };

    struct ScanState;

    static io::WalkAction VisitEntry(void* user, const io::WalkEntry& entry);

    Status EnsureIndexLocked();
    Status ScanLocked(Vector<Item>* out) const;
    Status Locate(ResourceId id, io::Path* file);
    Status OpenChecked(ResourceId id, const io::Path& file, io::FilePtr* out, BlobHeaderInfo* header, u64* size);
    const Item* FindLocked(ResourceId id) const;

    io::Path root_;
    io::Path canonical_root_;
    const TypeRegistry* types_;
    bool stored_table_;
    u32 target_;
    mutable Mutex mutex_;
    bool indexed_ = false;
    Vector<Item> items_;  // sorted by id
};

}  // namespace engine_placeholder::asset
