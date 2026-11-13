#pragma once

//! [[asset::HostMemoryBackend]]: a real backend for resources whose bytes the host application provides at run time
//! (the content a host that embeds the engine registers through Phase 23's C API), with no file, pak or server behind
//! it.

#include "asset/backend.h"
#include "asset/resource_type.h"
#include "containers/hash_map.h"
#include "containers/shared_ptr.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "mem/heap_allocator.h"
#include "string/string.h"
#include "sync/mutex.h"

namespace engine_placeholder::asset
{

/// Called exactly once when a borrowed resource's bytes are no longer used (replaced, removed or the backend gone, and
/// no reader of them open): the host may free or reuse `data` from then on.
using ReleaseBytes = void (*)(void* user, const void* data, usize size);

/// What the manager reports about the resources that depend on another one (Day 328 supplies it); the backend refuses
/// to remove or replace a resource that has dependents unless the host forces it.
///
/// - Thread-safety: thread-compatible; the functions are called from the thread that changes the backend, one at a
/// time.
struct ResourceInUseQuery
{
    /// Appends the ids of the loaded resources that depend on `id`; returns false (or appends nothing) when none do.
    bool (*dependents)(void* user, ResourceId id, Vector<ResourceId>* out) = nullptr;
    /// Unloads `dependent` (called by a forced change before the bytes change).
    void (*unload)(void* user, ResourceId dependent) = nullptr;
    void* user = nullptr;  ///< Passed to both functions.
};

/// Options of [[asset::HostMemoryBackend::Add]] and its siblings.
///
/// - Thread-safety: immutable value.
struct MemoryAddOptions
{
    Span<const StringView> paths;  ///< Paths the backend's table reports for the resource (none: reachable by id only).
    const ContentHash* expected_hash = nullptr;  ///< A hash the host promises; a blob that differs is refused.
};

/// Resources provided by the host.
///
/// The bytes of every resource are a complete blob (Day 314): `Add` checks the container, that the blob's own id is the
/// id the host passed, that the content hash verifies and, when the host passes one, that it equals the host's hash.
/// Bytes are *copied* into memory the backend owns, or *borrowed* (the host keeps them alive and gives a release
/// function, called exactly once, after the resource was replaced or removed and its last open reader closed). A
/// replace never tears a read: an open reader keeps the bytes it opened. Every add, replace and remove leaves a
/// `Watch` record. The bytes of all resources count against the budget (`assets.memory_backend_bytes`); an add past it
/// returns `LimitExceeded` (back-pressure, never eviction: the host owns the content).
/// - Thread-safety: thread-safe; adds, replaces and removes may run concurrently with reads.
/// - Ownership: owns copied bytes; borrowed bytes belong to the host until their release function ran. Readers must not
///   outlive the backend object, but the bytes they read outlive a replacement.
class HostMemoryBackend : public ResourceBackend
{
public:
    /// Makes a backend that checks blobs against `types` (which must outlive it) and holds at most `budget_bytes`.
    HostMemoryBackend(const TypeRegistry& types, u64 budget_bytes);
    /// Releases every borrowed resource whose last reader is closed (the others when their reader closes).
    ~HostMemoryBackend() override;

    /// A resource's bytes and its bookkeeping (an implementation detail, public so readers can hold one).
    struct Entry;
    /// A shared entry.
    using EntryPtr = SharedPtr<Entry>;

    /// Installs the dependents query (or clears it with a default-constructed one).
    void SetInUseQuery(const ResourceInUseQuery& query);

    /// Registers `bytes`, copied.
    ///
    /// - Fails: `Invalid` (null id, a malformed path); `DuplicateId` (the id is live, or a path belongs to another id);
    ///   `Corrupt` (not a blob, the blob's id is not `id`, the hash does not verify or differs from the host's);
    ///   `UnsupportedVersion`, `TypeMismatch` (as `BlobReader::Open`); `LimitExceeded` (the budget); `Other` (memory).
    Status Add(ResourceId id, ByteSpan bytes, const MemoryAddOptions& options = MemoryAddOptions());

    /// Registers `bytes` without copying them: the host keeps them valid until `release` ran.
    ///
    /// - Fails: as `Add`; the release function is not called for a refused add (the host still owns the bytes).
    /// - Ownership: borrows `bytes` until `release` has run; `release_user` stays the host's.
    Status AddBorrowed(ResourceId id, ByteSpan bytes, ReleaseBytes release, void* release_user,
                       const MemoryAddOptions& options = MemoryAddOptions());

    /// Replaces the bytes of a live resource with `bytes`, copied. Its paths stay.
    ///
    /// - Fails: `NotFound`; `InUse` (dependents named in `*dependents` when it is not null; `force` unloads them
    /// first);
    ///   the failures of `Add` for the new bytes.
    /// - Ownership: borrows its pointer arguments for the call only; nothing is retained.
    Status Replace(ResourceId id, ByteSpan bytes, bool force = false, Vector<ResourceId>* dependents = nullptr);

    /// As `Replace`, borrowing the new bytes.
    ///
    /// - Ownership: borrows `bytes` until `release` has run; `release_user` stays the host's.
    Status ReplaceBorrowed(ResourceId id, ByteSpan bytes, ReleaseBytes release, void* release_user, bool force = false,
                           Vector<ResourceId>* dependents = nullptr);

    /// Removes a resource and its paths.
    ///
    /// - Fails: `NotFound`; `InUse` (dependents named in `*dependents` when it is not null; `force` unloads them
    /// first).
    /// - Ownership: borrows its pointer arguments for the call only; nothing is retained.
    Status Remove(ResourceId id, bool force = false, Vector<ResourceId>* dependents = nullptr);

    /// Bytes held now (copied and borrowed).
    u64 BytesHeld() const;

    const char* Name() const override { return "memory"; }
    u32 Capabilities() const override { return kBackendRanged | kBackendWatch | kBackendWritable; }
    Status Stat(ResourceId id, ResourceInfo* out) override;
    Status Open(ResourceId id, UniquePtr<ResourceReader>* out) override;
    Status ReadTable(ResourceTable* out) override;
    Status Watch(Vector<ChangeRecord>* out) override;

private:
    Status Install(ResourceId id, ByteSpan bytes, bool borrowed, ReleaseBytes release, void* release_user,
                   const MemoryAddOptions& options, bool replacing, bool force, Vector<ResourceId>* dependents,
                   EntryPtr* replaced);
    Status CheckDependents(ResourceId id, bool force, Vector<ResourceId>* named, Vector<ResourceId>* to_unload,
                           ResourceInUseQuery* query);
    static void UnloadDependents(const ResourceInUseQuery& query, const Vector<ResourceId>& to_unload);

    const TypeRegistry* types_;
    u64 budget_;
    Mutex change_mutex_;   // serializes adds, replaces and removes (the in-use hooks run under it, not under mutex_)
    mutable Mutex mutex_;  // guards the members below
    ResourceInUseQuery query_;
    HashMap<u64, EntryPtr, HeapAllocator> entries_;
    ResourceTable table_;
    u64 held_ = 0;
    i64 stamp_counter_ = 0;
    Vector<ChangeRecord> changes_;
};

}  // namespace engine_placeholder::asset
