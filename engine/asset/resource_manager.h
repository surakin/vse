#pragma once

//! [[asset::ResourceManager]]: ask for a resource by id, get a handle, never know which backend served it.
//!
//! The manager owns the mount table ([[asset::MountTable]]), the pool the blobs are read into
//! ([[asset::LoadBufferPool]]) and a cache of loaded resources. `Load<T>` reads the blob into one aligned buffer,
//! checks its container and version, loads its **whole closure first** (only the dependencies the blob declares, each
//! of the type and version the declaration says) and then calls the loader registered for its type; the handle is
//! published only when all of that succeeded, so a resource is never half loaded and a dependency can never be broken.
//! A failure returns the status and no handle (there is no silent fallback; see
//! [[asset::ResourceManager::RegisterFallback]]).
//!
//! A handle is an owning, reference-counted reference with a generation: a copy adds one, the last one frees the
//! resource (unless it is pinned), and a handle whose resource was unloaded reads as invalid, never as another
//! resource. A loaded resource holds a reference on each of its dependencies, so a dependency cannot go while a
//! dependent is loaded.

#include "asset/asset_settings.h"
#include "asset/backend.h"
#include "asset/blob.h"
#include "asset/load_buffer_pool.h"
#include "asset/memory_backend.h"
#include "asset/mount_table.h"
#include "asset/resource_id.h"
#include "asset/resource_type.h"
#include "asset/status.h"
#include "containers/array.h"
#include "containers/hash_map.h"
#include "containers/shared_bytes.h"
#include "containers/shared_ptr.h"
#include "containers/span.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "io/path.h"
#include "lang/status_try.h"
#include "sync/mutex.h"
#include "types.h"

namespace vse::asset
{

class ResourceManager;
class LoadContext;

/// Maps a C++ type to the resource type of the blobs it is loaded from (`kType`) and to a code of its own (`kKind`,
/// built with `ResourceType::FromChars(...).Tag()`; two C++ types made from one resource type, a CPU view and a GPU
/// object, have one `kType` and two `kKind`s). Specialized for the types the loaders produce
/// ([[asset::RegisterCpuLoaders]] declares the CPU ones).
///
/// - Thread-safety: thread-safe, a compile-time constant.
template <class T> struct ResourceTraits;

/// The key that tells C++ types apart without run-time type information: the `kKind` constant of the type's
/// `ResourceTraits` (a four-character code, unique per C++ type). A constant, not an address, so the key
/// is the same in every module of a build that is split into libraries.
///
/// - Fails: never.
template <class T> constexpr u32 ObjectKey()
{
    return ResourceTraits<T>::kKind;
}

/// A loaded resource, type-erased. Every object a loader produces is held by one of these.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owned by the manager's slot.
class LoadedObject
{
public:
    virtual ~LoadedObject() = default;
    /// The key of the C++ type of the value ([[asset::ObjectKey]]).
    ///
    /// - Fails: never.
    u32 Key() const { return key_; }

private:
    template <class T> friend class LoadedValue;
    u32 key_ = 0;
};

/// The holder of a loaded value of type `T`.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns `value`.
template <class T> class LoadedValue final : public LoadedObject
{
public:
    /// Makes a value-initialized `T`, keyed by its type.
    LoadedValue() { key_ = ObjectKey<T>(); }
    /// The loaded value.
    T value;
};

/// The bytes of blob buffers that are alive, counted where the buffers are made and unmade: the manager and every
/// [[asset::BlobBuffer]] share one, so a buffer that a GPU object keeps after the manager let go of the resource still
/// counts as resident until the last holder returns it.
///
/// - Thread-safety: thread-safe.
/// - Ownership: shared by the manager and the buffers.
class ResidentBytes
{
public:
    /// Adds `n` bytes.
    void Add(u64 n) { bytes_.FetchAdd(n, MemoryOrder::Relaxed); }
    /// Removes `n` bytes.
    void Sub(u64 n) { bytes_.FetchSub(n, MemoryOrder::Relaxed); }
    /// The bytes now.
    ///
    /// - Fails: never.
    u64 Get() const { return bytes_.Load(MemoryOrder::Relaxed); }

private:
    Atomic<u64> bytes_{0};
};

/// One blob read into a pooled buffer, shared by everything that views it: the manager's slot, the loaded object, and
/// the GPU objects uploaded from it (a mesh that is uploaded again after a device reset reads these bytes). It holds
/// the pool too, so the buffer goes back to a pool that is still there however the holders die.
///
/// - Thread-safety: thread-safe to read; the bytes never change.
/// - Ownership: owns the buffer and shares the pool.
class BlobBuffer final : public SharedBytes
{
public:
    /// Wraps `buffer`, of which the first `size` bytes are the blob.
    ///
    /// - Ownership: takes `buffer` and shares `pool` and `resident`.
    BlobBuffer(SharedPtr<LoadBufferPool> pool, SharedPtr<ResidentBytes> resident, LoadBuffer&& buffer, usize size)
        : pool_(static_cast<SharedPtr<LoadBufferPool>&&>(pool)),
          resident_(static_cast<SharedPtr<ResidentBytes>&&>(resident)), buffer_(static_cast<LoadBuffer&&>(buffer)),
          size_(size)
    {
        resident_->Add(size_);
    }
    /// Gives the bytes back to the count, then the buffer to the pool.
    ~BlobBuffer() override { resident_->Sub(size_); }

    /// The blob's bytes.
    ///
    /// - Fails: never.
    ByteSpan Bytes() const override { return ByteSpan(buffer_.Data(), size_); }

private:
    SharedPtr<LoadBufferPool> pool_;  // declared first: destroyed after the buffer goes back to it
    SharedPtr<ResidentBytes> resident_;
    LoadBuffer buffer_;
    usize size_;
};

/// How a [[asset::BlobBuffer]] is held: shared.
using BlobBufferPtr = SharedPtr<BlobBuffer>;

/// What a loader is given: the blob, its bytes and the objects of its dependencies.
///
/// - Thread-safety: thread-compatible; valid during the loader call only.
/// - Ownership: borrows everything; the bytes stay valid for as long as the loaded object lives (the manager keeps the
///   buffer), so a loader may keep views into them.
class LoadContext
{
public:
    /// The resource's own id.
    ResourceId Id() const { return id_; }
    /// The whole blob.
    ByteSpan Bytes() const { return buffer_->Bytes(); }
    /// The shared region the blob's bytes are in: what an object that outlives the load (a GPU mesh uploaded again
    /// after a device reset) holds instead of a pointer into the bytes.
    ///
    /// - Fails: never.
    /// - Ownership: the caller may keep the share.
    const SharedBytesPtr& Buffer() const { return buffer_; }
    /// The opened blob (sections, dependencies).
    const BlobReader& Blob() const { return *blob_; }
    /// The registry of resource types.
    const TypeRegistry& Types() const { return *types_; }
    /// The object of a declared dependency, or null when `id` is not one of this blob's dependencies or its object is
    /// not a `T`.
    ///
    /// - Fails: returns null as described.
    /// - Ownership: the object belongs to the manager and stays valid while the loaded resource lives.
    template <class T> const T* Dependency(ResourceId id) const
    {
        const LoadedObject* object = Find(id);
        if (object == nullptr || object->Key() != ObjectKey<T>())
        {
            return nullptr;
        }
        return &static_cast<const LoadedValue<T>*>(object)->value;
    }
    /// Counts a conversion: a copy or transformation of bulk data beyond the one read into the aligned buffer. A
    /// loader of minted data calls it never; [[asset::ResourceStats::load_conversions]] is what a test asserts on.
    void CountConversion() const { *conversions_ += 1; }

private:
    friend class ResourceManager;
    const LoadedObject* Find(ResourceId id) const { return find_(find_user_, id); }

    ResourceId id_;
    SharedBytesPtr buffer_;
    const BlobReader* blob_ = nullptr;
    const TypeRegistry* types_ = nullptr;
    const LoadedObject* (*find_)(void* user, ResourceId id) = nullptr;
    void* find_user_ = nullptr;
    u64* conversions_ = nullptr;
};

/// A function that makes the object of a resource of type `T` from its blob.
template <class T> using LoaderFn = Status (*)(const LoadContext& context, T* out, void* user);

/// The base of a registered loader (an implementation detail of [[asset::ResourceManager::RegisterLoader]]).
///
/// - Thread-safety: thread-compatible; called with the manager's lock held.
/// - Ownership: owned by the manager.
class LoaderBase
{
public:
    virtual ~LoaderBase() = default;
    /// The C++ type the loader produces ([[asset::ObjectKey]]).
    ///
    /// - Fails: never.
    virtual u32 Key() const = 0;
    /// Makes the object.
    ///
    /// - Fails: the loader's status (a bad blob, a dependency that does not fit).
    /// - Ownership: borrows the context for the call; `*out` receives the object.
    virtual Status Load(const LoadContext& context, UniquePtr<LoadedObject>* out) = 0;
};

/// The loader of a `T`, made by [[asset::ResourceManager::RegisterLoader]].
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns nothing; the function and the user pointer are the caller's.
template <class T> class TypedLoader final : public LoaderBase
{
public:
    /// Makes a loader that calls `function` with `user`.
    ///
    /// - Ownership: stores both without owning them.
    TypedLoader(LoaderFn<T> function, void* user) : function_(function), user_(user) {}
    u32 Key() const override { return ObjectKey<T>(); }
    Status Load(const LoadContext& context, UniquePtr<LoadedObject>* out) override
    {
        UniquePtr<LoadedValue<T>> holder = MakeUnique<LoadedValue<T>>();
        ENGINE_TRY(function_(context, &holder->value, user_));
        *out = UniquePtr<LoadedObject>(holder.Release());
        return Status::Success();
    }

private:
    LoaderFn<T> function_;
    void* user_;
};

/// The knobs of a manager. [[asset::ManagerConfigFromSettings]] reads them from the settings.
///
/// - Thread-safety: thread-compatible plain data.
struct ManagerConfig
{
    u64 buffer_pool_bytes = u64{256} << 20;  ///< `assets.buffer_pool_bytes`.
    u64 cache_bytes = 0;                     ///< `assets.cache_bytes`: the most resident bytes; 0 for no limit.
    bool allow_fallbacks = false;            ///< `assets.allow_fallbacks`.
    bool verify_on_load = false;             ///< `assets.verify_on_load`.
    u32 target = 0;                          ///< The target tag of the running program (for paths mounted by it).
    u32 max_inflight = 4;                    ///< `assets.max_inflight`: the most asynchronous loads reading at once
                                             ///< ([[asset::AsyncLoader]]).
    u32 max_queued = 256;  ///< `assets.max_queued`: the most asynchronous loads waiting; one more is `LimitExceeded`.
};

/// Reads a [[asset::ManagerConfig]] from the settings.
///
/// - Param registry: the settings; `RegisterAssetSettings` and `RegisterManagerSettings` must have been called.
/// - Aborts: a setting was not declared.
/// - Thread-safety: thread-safe.
ManagerConfig ManagerConfigFromSettings(const settings::Registry& registry);

/// Reads from one mount.
///
/// - Thread-safety: immutable value.
struct MountReads
{
    MountId mount = 0;  ///< The mount.
    u64 reads = 0;      ///< Blobs read from it.
    u64 bytes = 0;      ///< Bytes read from it.
};

/// Blobs read ahead of a load: what an asynchronous load reads on a worker (the blob and the blobs of its closure) so
/// that the load itself, on the owner thread, only builds objects.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: shares the buffers.
class PrefetchedBlobs
{
public:
    /// An empty set.
    PrefetchedBlobs() : index_(HeapAllocator::Default()) {}
    PrefetchedBlobs(const PrefetchedBlobs&) = delete;
    PrefetchedBlobs& operator=(const PrefetchedBlobs&) = delete;

    /// Adds the blob of `id`.
    void Add(ResourceId id, const BlobBufferPtr& buffer)
    {
        Entry entry;
        entry.id = id;
        entry.buffer = buffer;
        index_.Insert(id.Value(), static_cast<u32>(entries_.Size()));
        entries_.Push(static_cast<Entry&&>(entry));
    }
    /// The blob of `id`, or an empty pointer.
    ///
    /// - Fails: returns an empty pointer when it was not added.
    BlobBufferPtr Find(ResourceId id) const
    {
        const u32* at = const_cast<HashMap<u64, u32, HeapAllocator>&>(index_).Find(id.Value());
        return at == nullptr ? BlobBufferPtr() : entries_[*at].buffer;
    }
    /// How many blobs were added.
    usize Count() const { return entries_.Size(); }
    /// Drops every buffer.
    void Clear()
    {
        for (usize i = 0; i < entries_.Size(); ++i)
        {
            index_.Remove(entries_[i].id.Value());
        }
        entries_.Clear();
    }
    /// Bytes of the blobs added.
    u64 Bytes() const
    {
        u64 total = 0;
        for (usize i = 0; i < entries_.Size(); ++i)
        {
            total += entries_[i].buffer->Bytes().Size();
        }
        return total;
    }

private:
    struct Entry
    {
        ResourceId id;
        BlobBufferPtr buffer;
    };
    Vector<Entry> entries_;
    HashMap<u64, u32, HeapAllocator> index_;  // id -> position in `entries_`
};

/// What the manager counted.
///
/// - Thread-safety: immutable value (a copy).
struct ResourceStats
{
    u64 loads = 0;                 ///< Resources loaded (a closure counts each member).
    u64 cache_hits = 0;            ///< Requests answered by a resource that was loaded already.
    u64 bytes_resident = 0;        ///< Bytes of the buffers of the loaded resources.
    u64 resident = 0;              ///< Loaded resources.
    u64 fallbacks_used = 0;        ///< Times a fallback stood in for a resource (always 0 for minted data).
    u64 load_conversions = 0;      ///< Conversions loaders reported (always 0 for minted data).
    u64 rejected_old_version = 0;  ///< Blobs refused because they were not the current version.
    u64 bytes_read = 0;            ///< Bytes read from the mounts.
    u64 read_nanos = 0;            ///< Time spent reading and loading, in nanoseconds.
    Vector<MountReads> mounts;     ///< Reads per mount.
};

/// Where a failed load stopped: the chain from the resource asked for down to the one that failed, as text
/// (`/levels/a -> /materials/m -> /textures/t`, paths where the mounts know them, ids otherwise).
///
/// - Thread-safety: thread-compatible plain data.
struct LoadFailure
{
    ResourceId resource;          ///< The resource that failed.
    Array<char, 512> chain = {};  ///< The chain, empty when the failure was not in a dependency.
};

/// The result of reloading one resource after its backend reported a change.
///
/// - Thread-safety: immutable value.
struct ReloadResult
{
    ResourceId id;     ///< The resource that changed.
    Status status;     ///< Success, or why the old version stayed loaded.
    u32 reloaded = 0;  ///< Objects rebuilt (the resource and its dependents).
};

/// An owning reference to a loaded resource, without the type.
///
/// - Thread-safety: thread-safe to copy and destroy from any thread; the manager must outlive every handle.
/// - Ownership: owns one reference to the resource.
class HandleBase
{
public:
    HandleBase() = default;
    /// Copies the handle, adding a reference.
    HandleBase(const HandleBase& other);
    /// Copies the handle, adding a reference and giving up the old one.
    HandleBase& operator=(const HandleBase& other);
    /// Takes the reference of `other`.
    HandleBase(HandleBase&& other) noexcept;
    /// Gives up the old reference and takes the one of `other`.
    HandleBase& operator=(HandleBase&& other) noexcept;
    /// Gives up the reference.
    ~HandleBase() { Reset(); }

    /// Gives up the reference now.
    void Reset();
    /// True while the resource is loaded (false for an empty handle and for one whose resource was unloaded).
    ///
    /// - Fails: never.
    bool IsValid() const;
    /// The resource's id (null for an empty handle).
    ///
    /// - Fails: never.
    ResourceId Id() const { return id_; }

protected:
    friend class ResourceManager;
    ResourceManager* manager_ = nullptr;
    u32 index_ = 0;
    u32 generation_ = 0;
    ResourceId id_;
};

/// An owning reference to a loaded `T`.
///
/// - Thread-safety: thread-safe to copy and destroy from any thread, as [[asset::HandleBase]]; the manager must outlive
/// it.
/// - Ownership: owns one reference to the resource, as [[asset::HandleBase]].
template <class T> class Handle : public HandleBase
{
public:
    /// The loaded value, or null when the handle is empty or stale. The pointer is valid while a handle to the resource
    /// exists and until the manager reloads or force-unloads it.
    ///
    /// - Fails: returns null as described.
    /// - Ownership: the value belongs to the manager.
    const T* Get() const;
};

/// The manager. See the file comment.
///
/// - Thread-safety: thread-safe. One lock guards the loaded set; a load holds it while it reads, so loads are
/// serialized
///   (asynchronous loading is separate). A loader must not call back into the manager.
/// - Ownership: borrows the type registry and the backends given to `Mount`; owns the backends given to `MountPath` and
///   `MountOwned`, the pool, the loaded resources and the loaders. Handles must be gone before the manager.
class ResourceManager
{
public:
    /// Makes a manager.
    ///
    /// - Param types: the registry of resource types; it must outlive the manager.
    /// - Param config: the knobs.
    ResourceManager(const TypeRegistry& types, const ManagerConfig& config);
    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;
    /// Frees every loaded resource. Handles must have been destroyed.
    ~ResourceManager();

    /// Adds a mount over a backend the caller owns.
    ///
    /// - Param prefix: the path prefix (`/` for none).
    /// - Param backend: the backend; it must outlive the manager.
    /// - Param priority: higher wins.
    /// - Param mount: receives the mount's id when not null.
    /// - Fails: those of [[asset::MountTable::Mount]].
    /// - Ownership: borrows `backend`, which must outlive the manager; `*mount` is written.
    Status Mount(StringView prefix, ResourceBackend* backend, i32 priority, MountId* mount = nullptr);
    /// Adds a mount over a backend the manager takes.
    ///
    /// - Fails: those of [[asset::MountTable::Mount]]; the backend is destroyed with the manager either way.
    /// - Ownership: takes `backend`; `*mount` is written.
    Status MountOwned(StringView prefix, UniquePtr<ResourceBackend> backend, i32 priority, MountId* mount = nullptr);
    /// Opens `path` as a directory of loose blobs or as a pak file and mounts it.
    ///
    /// - Fails: `NotFound` when `path` is neither; the failures of the backends' opening (`UnsupportedVersion` for a
    /// pak
    ///   of an old version: it is refused when mounted); those of [[asset::MountTable::Mount]].
    /// - Ownership: the manager owns the backend it opens; `path` is read during the call only.
    Status MountPath(StringView prefix, const io::Path& path, i32 priority, MountId* mount = nullptr);
    /// Mounts every entry of `assets.mounts`.
    ///
    /// - Fails: the failures of `MountSpecsFromSettings` and `MountPath`; entries before the failing one stay mounted.
    Status MountFromSettings(const settings::Registry& registry);
    /// The mount table (path questions, tables).
    MountTable& Mounts() { return mounts_; }

    /// Registers the loader of `T`; a later registration for the same resource type replaces the earlier one.
    ///
    /// - Param function: makes a `T` from a blob.
    /// - Param user: passed to `function`.
    /// - Ownership: stores `function` and `user`; `user` must stay valid until the manager is destroyed.
    template <class T> void RegisterLoader(LoaderFn<T> function, void* user = nullptr)
    {
        UniquePtr<TypedLoader<T>> typed = MakeUnique<TypedLoader<T>>(function, user);
        AddLoader(ResourceTraits<T>::kType, UniquePtr<LoaderBase>(typed.Release()));
    }

    /// Registers the fallback of a resource type: a blob (copied) that stands in for a resource of that type that is
    /// missing or invalid, when `assets.allow_fallbacks` is on. Each use is logged and counted; a fallback never stands
    /// in for a dependency (a closure with a missing member fails as a whole).
    ///
    /// - Param type: the type.
    /// - Param blob: a valid blob of the type.
    /// - Fails: `Invalid` for a blob that does not open as a blob of `type`.
    Status RegisterFallback(ResourceType type, ByteSpan blob);

    /// Loads the resource `id` and its closure, or finds it loaded.
    ///
    /// - Param id: the resource.
    /// - Param out: receives the handle on success; reset on failure.
    /// - Param failure: receives where a failure stopped when not null.
    /// - Fails: `NotFound` (no mount has it); `MissingDependency` (a dependency is nowhere; the chain is in `failure`);
    ///   `DependencyTypeMismatch` (a dependency is not of the declared type or version, or a loader found the closure
    ///   inconsistent); `DependencyCycle`; `UnsupportedVersion` (counted in `rejected_old_version`); `Corrupt`;
    ///   `TypeMismatch` (the blob is not of the type of `T`, or `T` is not what the type's loader makes);
    ///   `LimitExceeded` (`assets.cache_bytes`, or the buffer pool's budget `assets.buffer_pool_bytes`, which the
    ///   resident blobs occupy); `TypeMismatch` also when no loader is registered for the type. With
    ///   `assets.allow_fallbacks` and a registered fallback, `NotFound`, `MissingDependency`, `Corrupt`,
    ///   `UnsupportedVersion` and `DependencyTypeMismatch` return the fallback instead; the others never do.
    /// - Ownership: `*out` receives the handle, which owns one reference; `*failure` is written when not null.
    template <class T> Status Load(ResourceId id, Handle<T>* out, LoadFailure* failure = nullptr)
    {
        return LoadRaw(id, ResourceTraits<T>::kType, ObjectKey<T>(), out, failure);
    }
    /// Loads the resource a path names (through the mounts' tables).
    ///
    /// - Fails: `NotReady` (a table has not arrived), `NotFound`, `Invalid` (the path), and those of `Load`.
    /// - Ownership: as `Load`; `path` is read during the call only.
    template <class T> Status LoadPath(StringView path, Handle<T>* out, LoadFailure* failure = nullptr)
    {
        ResourceId id;
        ENGINE_TRY(ResolveForLoad(path, &id));
        return Load<T>(id, out, failure);
    }

    /// Reads the blob of `id` into a pool buffer without loading it: the reading half of an asynchronous load, callable
    /// from any thread and without the manager's lock held during the read.
    ///
    /// - Param out: receives the buffer.
    /// - Fails: `NotFound`, `Corrupt` (the size is not that of a blob), `LimitExceeded` (`assets.cache_bytes` or the
    ///   pool's budget), `Io`.
    /// - Thread-safety: thread-safe.
    /// - Ownership: `*out` receives a share of the buffer.
    Status ReadBlob(ResourceId id, BlobBufferPtr* out);
    /// True when `id` is loaded.
    ///
    /// - Fails: never.
    /// - Thread-safety: thread-safe.
    bool IsLoaded(ResourceId id) const;
    /// A cheap guess at [[asset::ResourceManager::IsLoaded]] for a thread that must not wait for a build in progress:
    /// false when the manager is busy.
    ///
    /// - Fails: never.
    /// - Thread-safety: thread-safe.
    bool MayBeLoaded(ResourceId id) const;
    /// The C++ type key of the loader registered for `type`, or 0 when there is none.
    ///
    /// - Thread-safety: thread-safe.
    u32 LoaderKeyOf(ResourceType type) const;
    /// As `Load`, taking the blobs of the closure from `prefetched` before it reads any from the mounts (a blob that is
    /// not in it is read as usual). The objects are built on the calling thread.
    ///
    /// - Param prefetched: blobs read ahead; read during the call only.
    /// - Fails: those of `Load`.
    /// - Ownership: `*out` receives the handle; `prefetched` is borrowed for the call.
    template <class T>
    Status LoadPrefetched(ResourceId id, const PrefetchedBlobs& prefetched, Handle<T>* out,
                          LoadFailure* failure = nullptr)
    {
        return LoadPrefetchedRaw(id, ResourceTraits<T>::kType, ObjectKey<T>(), prefetched, out, failure);
    }
    /// As `LoadPrefetched`, with the type and the C++ type key given instead of a `T` (what the loader registered for
    /// the type makes: [[asset::ResourceManager::LoaderKeyOf]]).
    ///
    /// - Fails: those of `Load`.
    /// - Ownership: as `LoadPrefetched`.
    Status LoadPrefetchedRaw(ResourceId id, ResourceType type, u32 key, const PrefetchedBlobs& prefetched,
                             HandleBase* out, LoadFailure* failure);
    /// Resolves a path through the mounts' tables.
    ///
    /// - Fails: `NotReady` (a table has not arrived), `NotFound`, `Invalid`.
    /// - Ownership: borrows `path` and `id` for the call only.
    /// - Thread-safety: thread-safe.
    Status Resolve(StringView path, ResourceId* id) const { return mounts_.Resolve(path, id); }
    /// True once every mount's table has arrived.
    ///
    /// - Fails: never.
    /// - Thread-safety: thread-safe.
    bool TablesReady() const { return mounts_.TablesReady(); }

    /// Frees the resource `id` and the loaded resources that depend on it, when `force` is true (their handles go
    /// stale), or only when nothing loaded depends on it and no handle holds it.
    ///
    /// - Param dependents: receives the ids that stopped the call (`InUse`) or were unloaded (`force`); may be null.
    /// - Fails: `NotFound` (not loaded); `InUse` (a dependent is loaded, or a handle holds it, and `force` is false).
    /// - Ownership: `*dependents` is appended to.
    Status Unload(ResourceId id, bool force = false, Vector<ResourceId>* dependents = nullptr);

    /// Keeps `id` loaded when its last handle goes (a pinned resource is freed by `Unpin` and `Unload`).
    ///
    /// - Fails: `NotFound` (not loaded).
    Status Pin(ResourceId id);
    /// Undoes `Pin`; a resource with no handle and no dependent is freed.
    ///
    /// - Fails: `NotFound` (not loaded).
    Status Unpin(ResourceId id);

    /// Tells whether a loaded resource depends on `id`, and which.
    ///
    /// - Param dependents: receives the ids of the loaded resources that list `id` as a dependency; may be null.
    /// - Returns: true when there is at least one.
    /// - Ownership: `*dependents` is appended to.
    /// - Fails: never.
    bool ResourceInUse(ResourceId id, Vector<ResourceId>* dependents) const;
    /// The query to give `HostMemoryBackend::SetInUseQuery`, so the backend refuses to remove or replace a resource
    /// that something loaded depends on and a forced change unloads the dependents first. The manager must outlive the
    /// backend's use of it.
    ///
    /// - Fails: never.
    ResourceInUseQuery InUseQuery();

    /// Polls the mounts for changes and reloads the loaded resources that changed, each with its dependents,
    /// atomically: the new objects are made first (the closure rules are checked again against the new blob) and
    /// replace the old ones only when all succeeded; otherwise the old version stays loaded and the result says why.
    /// Raw pointers got from `Get` are valid until this call.
    ///
    /// - Param results: receives one entry per loaded resource that changed.
    /// - Fails: the failure of the mounts' `Watch`.
    /// - Ownership: `*results` is replaced.
    Status PollChanges(Vector<ReloadResult>* results);

    /// A copy of the counters.
    ///
    /// - Thread-safety: thread-safe.
    ResourceStats Stats() const;
    /// The loaded object of `id` when it is loaded as a `T`, without taking a reference: for a reader that knows the
    /// resource is held by something else (an animation system reading the skeletons a level holds).
    ///
    /// - Returns: null when the resource is not loaded or is not a `T`.
    /// - Fails: returns null as described.
    /// - Thread-safety: thread-safe; the pointer is valid while something holds the resource.
    /// - Ownership: the object belongs to the manager.
    template <class T> const T* Find(ResourceId id) const
    {
        const LoadedObject* object = FindObject(id);
        if (object == nullptr || object->Key() != ObjectKey<T>())
        {
            return nullptr;
        }
        return &static_cast<const LoadedValue<T>*>(object)->value;
    }
    /// The ids of the loaded resources, in id order (for tests and tools).
    ///
    /// - Ownership: `*out` is appended to.
    /// - Fails: never.
    void LoadedIds(Vector<ResourceId>* out) const;
    /// The pool the blobs are read into.
    LoadBufferPool& Pool() { return *pool_; }
    /// The registry of resource types.
    const TypeRegistry& Types() const { return types_; }
    /// The configuration the manager was made with.
    const ManagerConfig& Config() const { return config_; }

private:
    friend class HandleBase;
    template <class T> friend class Handle;
    struct Slot;
    struct Expect;
    struct Staged;
    struct FindState;

    const LoadedObject* FindObject(ResourceId id) const;
    void AddLoader(ResourceType type, UniquePtr<LoaderBase> loader);
    Status ResolveForLoad(StringView path, ResourceId* id);
    Status LoadRaw(ResourceId id, ResourceType type, u32 key, HandleBase* out, LoadFailure* failure);
    Status LoadFallback(ResourceType type, u32 key, HandleBase* out);
    Status AcquireLocked(ResourceId id, const Expect& expect, Vector<ResourceId>* chain, u32* slot,
                         LoadFailure* failure);
    Status ReadBlobLocked(ResourceId id, u64 reclaim, BlobBufferPtr* buffer, usize* size);
    Status ReadBlobUnlocked(ResourceId id, u64 reclaim, BlobBufferPtr* buffer, usize* size, MountId* mount, u64* nanos);
    void NoteReadLocked(MountId mount, u64 bytes, u64 nanos);
    Status BuildLocked(ResourceId id, const Expect& expect, BlobBufferPtr buffer, usize size, Vector<ResourceId>* chain,
                       u32* slot, LoadFailure* failure);
    void NoteFailure(LoadFailure* failure, const Vector<ResourceId>& chain, ResourceId failing);
    void AddRefLocked(u32 index);
    void ReleaseLocked(u32 index);
    void FreeLocked(u32 index);
    void AddRef(u32 index, u32 generation);
    void Release(u32 index, u32 generation);
    bool IsLive(u32 index, u32 generation) const;
    const LoadedObject* ObjectOf(u32 index, u32 generation) const;
    void DependentsLocked(ResourceId id, Vector<ResourceId>* out) const;
    Status UnloadLocked(ResourceId id, bool force, Vector<ResourceId>* dependents);
    Status ReloadLocked(ResourceId id, u32* reloaded);
    static bool QueryDependents(void* user, ResourceId id, Vector<ResourceId>* out);
    static void QueryUnload(void* user, ResourceId dependent);
    static const LoadedObject* FindInContext(void* user, ResourceId id);
    void DescribeLocked(ResourceId id, String<HeapAllocator>* out) const;

    const TypeRegistry& types_;
    ManagerConfig config_;
    Vector<UniquePtr<ResourceBackend>>
        owned_backends_;  // before the mounts: destroyed after them (table reads use them)
    MountTable mounts_;
    SharedPtr<LoadBufferPool> pool_;
    SharedPtr<ResidentBytes> resident_;
    mutable RecursiveMutex mutex_;
    mutable Mutex read_stats_mutex_;  // the read counters of `stats_`: reads on workers do not wait for a build
    struct LoaderEntry
    {
        ResourceType type;
        UniquePtr<LoaderBase> loader;
    };
    Vector<LoaderEntry> loaders_;
    struct FallbackEntry
    {
        ResourceType type;
        Vector<byte> blob;
    };
    Vector<FallbackEntry> fallbacks_;
    Vector<UniquePtr<Slot>> slots_;
    Vector<u32> free_slots_;
    HashMap<u64, u32, HeapAllocator> by_id_;
    ResourceStats stats_;
    const PrefetchedBlobs* prefetched_ = nullptr;  // set while LoadPrefetchedRaw runs, under the lock
};

template <class T> const T* Handle<T>::Get() const
{
    if (manager_ == nullptr)
    {
        return nullptr;
    }
    const LoadedObject* object = manager_->ObjectOf(index_, generation_);
    if (object == nullptr || object->Key() != ObjectKey<T>())
    {
        return nullptr;
    }
    return &static_cast<const LoadedValue<T>*>(object)->value;
}

}  // namespace vse::asset
