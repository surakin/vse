#include "asset/resource_manager.h"

#include "asset/filesystem_backend.h"
#include "asset/pak_backend.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"
#include "io/file_system.h"
#include "lang/status_try.h"
#include "log/log.h"
#include "os/os.h"
#include "settings/setting.h"

namespace engine_placeholder::asset
{

// ---------------------------------------------------------------------------------------------------------------------
// Internals

struct ResourceManager::Slot
{
    u32 generation = 1;
    bool live = false;
    bool pinned = false;
    ResourceId id;
    ResourceType type;
    u32 type_version = 0;
    u32 refs = 0;
    BlobBufferPtr buffer;
    usize size = 0;
    UniquePtr<LoadedObject> object;
    Vector<ResourceId> deps;  // the ids of the dependencies this slot holds a reference on
    Vector<u32> dep_slots;    // their slots, in the same order
};

// What a load expects of the blob it reads.
struct ResourceManager::Expect
{
    bool dependency = false;  // asked for by another blob: type mismatches are `DependencyTypeMismatch`
    bool check_type = false;
    ResourceType type;
    bool check_version = false;
    u32 version = 0;
    u32 key = 0;  // the C++ type asked for at the top level (0: any)
};

// The objects a loader may see: its declared dependencies, with staged replacements first.
struct ResourceManager::FindState
{
    const ResourceManager* manager = nullptr;
    const Vector<ResourceId>* ids = nullptr;
    const Vector<u32>* slots = nullptr;
    const Vector<ResourceId>* staged_ids = nullptr;
    const Vector<const LoadedObject*>* staged_objects = nullptr;
};

namespace
{

const char* kPathSeparator = " -> ";

Status Fail(ErrorCode code, const char* message)
{
    return Status::Make(code, message);
}

}  // namespace

ManagerConfig ManagerConfigFromSettings(const settings::Registry& registry)
{
    ManagerConfig config;
    config.buffer_pool_bytes = BufferPoolBytesFromSettings(registry);
    config.cache_bytes = static_cast<u64>(
        settings::Setting<i64>::Find(registry, StringView("assets.cache_bytes")).Get());
    config.allow_fallbacks = settings::Setting<bool>::Find(registry, StringView("assets.allow_fallbacks")).Get();
    config.verify_on_load = settings::Setting<bool>::Find(registry, StringView("assets.verify_on_load")).Get();
    return config;
}

// ---------------------------------------------------------------------------------------------------------------------
// Handles

HandleBase::HandleBase(const HandleBase& other)
    : manager_(other.manager_), index_(other.index_), generation_(other.generation_), id_(other.id_)
{
    if (manager_ != nullptr)
    {
        manager_->AddRef(index_, generation_);
    }
}

HandleBase& HandleBase::operator=(const HandleBase& other)
{
    if (this != &other)
    {
        HandleBase copy(other);
        *this = static_cast<HandleBase&&>(copy);
    }
    return *this;
}

HandleBase::HandleBase(HandleBase&& other) noexcept
    : manager_(other.manager_), index_(other.index_), generation_(other.generation_), id_(other.id_)
{
    other.manager_ = nullptr;
    other.id_ = ResourceId();
}

HandleBase& HandleBase::operator=(HandleBase&& other) noexcept
{
    if (this != &other)
    {
        Reset();
        manager_ = other.manager_;
        index_ = other.index_;
        generation_ = other.generation_;
        id_ = other.id_;
        other.manager_ = nullptr;
        other.id_ = ResourceId();
    }
    return *this;
}

void HandleBase::Reset()
{
    if (manager_ != nullptr)
    {
        ResourceManager* manager = manager_;
        manager_ = nullptr;
        manager->Release(index_, generation_);
    }
    id_ = ResourceId();
}

bool HandleBase::IsValid() const
{
    return manager_ != nullptr && manager_->IsLive(index_, generation_);
}

// ---------------------------------------------------------------------------------------------------------------------
// The manager

ResourceManager::ResourceManager(const TypeRegistry& types, const ManagerConfig& config)
    : types_(types), config_(config), pool_(MakeShared<LoadBufferPool>(config.buffer_pool_bytes)),
      resident_(MakeShared<ResidentBytes>()), by_id_(HeapAllocator::Default())
{
}

ResourceManager::~ResourceManager()
{
    LockGuard<RecursiveMutex> lock(mutex_);
    for (usize i = 0; i < slots_.Size(); ++i)
    {
        if (slots_[i]->live)
        {
            slots_[i]->live = false;
            slots_[i]->object.Reset();
        }
    }
}

Status ResourceManager::Mount(StringView prefix, ResourceBackend* backend, i32 priority, MountId* mount)
{
    MountId id = 0;
    ENGINE_TRY(mounts_.Mount(prefix, backend, priority, &id));
    if (mount != nullptr)
    {
        *mount = id;
    }
    return Status::Success();
}

Status ResourceManager::MountOwned(StringView prefix, UniquePtr<ResourceBackend> backend, i32 priority, MountId* mount)
{
    ResourceBackend* raw = backend.Get();
    {
        LockGuard<RecursiveMutex> lock(mutex_);
        owned_backends_.Push(static_cast<UniquePtr<ResourceBackend>&&>(backend));
    }
    return Mount(prefix, raw, priority, mount);
}

Status ResourceManager::MountPath(StringView prefix, const io::Path& path, i32 priority, MountId* mount)
{
    if (io::IsDirectory(path))
    {
        UniquePtr<FilesystemBackend> fs;
        ENGINE_TRY(FilesystemBackend::Create(path, types_, &fs, config_.target));
        return MountOwned(prefix, UniquePtr<ResourceBackend>(fs.Release()), priority, mount);
    }
    if (io::IsFile(path))
    {
        UniquePtr<PakBackend> pak;
        ENGINE_TRY(PakBackend::Open(path, types_, config_.target, &pak));
        return MountOwned(prefix, UniquePtr<ResourceBackend>(pak.Release()), priority, mount);
    }
    return Fail(ErrorCode::NotFound, "the mount path is neither a directory nor a pak file");
}

Status ResourceManager::MountFromSettings(const settings::Registry& registry)
{
    Vector<MountSpec> specs;
    ENGINE_TRY(MountSpecsFromSettings(registry, &specs));
    for (usize i = 0; i < specs.Size(); ++i)
    {
        io::Path path;
        if (!io::Path::Parse(specs[i].path.View(), io::NativePathStyle(), &path))
        {
            return Fail(ErrorCode::Invalid, "an assets.mounts path is not a valid path");
        }
        ENGINE_TRY(MountPath(specs[i].prefix.View(), path, specs[i].priority));
    }
    return Status::Success();
}

const LoadedObject* ResourceManager::FindObject(ResourceId id) const
{
    LockGuard<RecursiveMutex> lock(mutex_);
    const u32* found = by_id_.Find(id.Value());
    return found == nullptr ? nullptr : slots_[*found]->object.Get();
}

void ResourceManager::AddLoader(ResourceType type, UniquePtr<LoaderBase> loader)
{
    LockGuard<RecursiveMutex> lock(mutex_);
    for (usize i = 0; i < loaders_.Size(); ++i)
    {
        if (loaders_[i].type == type)
        {
            loaders_[i].loader = static_cast<UniquePtr<LoaderBase>&&>(loader);
            return;
        }
    }
    LoaderEntry entry;
    entry.type = type;
    entry.loader = static_cast<UniquePtr<LoaderBase>&&>(loader);
    loaders_.Push(static_cast<LoaderEntry&&>(entry));
}

Status ResourceManager::RegisterFallback(ResourceType type, ByteSpan blob)
{
    BlobReader reader;
    ENGINE_TRY(reader.Open(blob, types_));
    if (reader.Type() != type)
    {
        return Fail(ErrorCode::Invalid, "the fallback blob is not of the type it is registered for");
    }
    LockGuard<RecursiveMutex> lock(mutex_);
    FallbackEntry entry;
    entry.type = type;
    entry.blob.Resize(blob.Size());
    MemCopy(entry.blob.Data(), blob.Data(), blob.Size());
    for (usize i = 0; i < fallbacks_.Size(); ++i)
    {
        if (fallbacks_[i].type == type)
        {
            fallbacks_[i] = static_cast<FallbackEntry&&>(entry);
            return Status::Success();
        }
    }
    fallbacks_.Push(static_cast<FallbackEntry&&>(entry));
    return Status::Success();
}

Status ResourceManager::ResolveForLoad(StringView path, ResourceId* id)
{
    return mounts_.Resolve(path, id);
}

// ---------------------------------------------------------------------------------------------------------------------
// Counting

void ResourceManager::AddRefLocked(u32 index)
{
    slots_[index]->refs += 1;
}

void ResourceManager::AddRef(u32 index, u32 generation)
{
    LockGuard<RecursiveMutex> lock(mutex_);
    if (index < slots_.Size() && slots_[index]->live && slots_[index]->generation == generation)
    {
        AddRefLocked(index);
    }
}

void ResourceManager::ReleaseLocked(u32 index)
{
    Slot& slot = *slots_[index];
    if (slot.refs != 0)
    {
        slot.refs -= 1;
    }
    if (slot.refs == 0 && !slot.pinned)
    {
        FreeLocked(index);
    }
}

void ResourceManager::Release(u32 index, u32 generation)
{
    LockGuard<RecursiveMutex> lock(mutex_);
    if (index < slots_.Size() && slots_[index]->live && slots_[index]->generation == generation)
    {
        ReleaseLocked(index);
    }
}

bool ResourceManager::IsLive(u32 index, u32 generation) const
{
    LockGuard<RecursiveMutex> lock(mutex_);
    return index < slots_.Size() && slots_[index]->live && slots_[index]->generation == generation;
}

const LoadedObject* ResourceManager::ObjectOf(u32 index, u32 generation) const
{
    LockGuard<RecursiveMutex> lock(mutex_);
    if (index < slots_.Size() && slots_[index]->live && slots_[index]->generation == generation)
    {
        return slots_[index]->object.Get();
    }
    return nullptr;
}

void ResourceManager::FreeLocked(u32 index)
{
    Slot& slot = *slots_[index];
    if (!slot.live)
    {
        return;
    }
    slot.live = false;
    slot.pinned = false;
    by_id_.Remove(slot.id.Value());
    slot.object.Reset();
    slot.buffer.Reset();
    slot.size = 0;
    slot.refs = 0;
    slot.generation += 1;
    if (slot.generation == 0)
    {
        slot.generation = 1;
    }
    Vector<u32> deps = static_cast<Vector<u32>&&>(slot.dep_slots);
    slot.deps.Clear();
    slot.dep_slots.Clear();
    free_slots_.Push(index);
    for (usize i = 0; i < deps.Size(); ++i)
    {
        ReleaseLocked(deps[i]);
    }
}

void ResourceManager::DependentsLocked(ResourceId id, Vector<ResourceId>* out) const
{
    for (usize i = 0; i < slots_.Size(); ++i)
    {
        const Slot& slot = *slots_[i];
        if (!slot.live)
        {
            continue;
        }
        for (usize d = 0; d < slot.deps.Size(); ++d)
        {
            if (slot.deps[d] == id)
            {
                out->Push(slot.id);
                break;
            }
        }
    }
}

void ResourceManager::DescribeLocked(ResourceId id, String<HeapAllocator>* out) const
{
    Vector<String<HeapAllocator>> paths;
    if (mounts_.PathsOf(id, &paths).Ok() && paths.Size() != 0)
    {
        out->Append(paths[0].View());
        return;
    }
    char hex[17];
    FormatResourceId(id, hex);
    out->Append(StringView(hex, 16));
}

void ResourceManager::NoteFailure(LoadFailure* failure, const Vector<ResourceId>& chain, ResourceId failing)
{
    if (failure == nullptr || !failure->resource.IsNone())
    {
        return;
    }
    failure->resource = failing;
    String<HeapAllocator> text(HeapAllocator::Default());
    for (usize i = 0; i < chain.Size(); ++i)
    {
        DescribeLocked(chain[i], &text);
        text.Append(StringView(kPathSeparator));
    }
    DescribeLocked(failing, &text);
    FormatTo(failure->chain.Data(), failure->chain.Size(), "%.*s", static_cast<int>(text.Length()), text.View().Data());
}

const LoadedObject* ResourceManager::FindInContext(void* user, ResourceId id)
{
    const FindState* state = static_cast<const FindState*>(user);
    if (state->staged_ids != nullptr)
    {
        for (usize i = 0; i < state->staged_ids->Size(); ++i)
        {
            if ((*state->staged_ids)[i] == id)
            {
                return (*state->staged_objects)[i];
            }
        }
    }
    for (usize i = 0; i < state->ids->Size(); ++i)
    {
        if ((*state->ids)[i] == id)
        {
            return state->manager->slots_[(*state->slots)[i]]->object.Get();
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------------------------------------------------
// Loading

Status ResourceManager::ReadBlobLocked(ResourceId id, u64 reclaim, BlobBufferPtr* buffer, usize* size)
{
    const u64 started = os::MonotonicTimeNanos();
    UniquePtr<ResourceReader> reader;
    MountId mount = 0;
    ENGINE_TRY(mounts_.Open(id, &reader, &mount));
    const u64 total = reader->Size();
    if (total < kBlobHeaderBytes || total >= kMaxBlobBytes)
    {
        return Fail(ErrorCode::Corrupt, "a resource's size is not that of a blob");
    }
    if (config_.cache_bytes != 0 && resident_->Get() - reclaim + total > config_.cache_bytes)
    {
        return Fail(ErrorCode::LimitExceeded, "loading the resource would pass assets.cache_bytes");
    }
    LoadBuffer raw;
    ENGINE_TRY(pool_->Acquire(static_cast<usize>(total), &raw));
    ENGINE_TRY(reader->ReadAt(0, raw.Data(), static_cast<usize>(total)));
    *buffer = MakeShared<BlobBuffer>(pool_, resident_, static_cast<LoadBuffer&&>(raw), static_cast<usize>(total));
    *size = static_cast<usize>(total);
    stats_.bytes_read += total;
    stats_.read_nanos += os::MonotonicTimeNanos() - started;
    usize at = stats_.mounts.Size();
    for (usize i = 0; i < stats_.mounts.Size(); ++i)
    {
        if (stats_.mounts[i].mount == mount)
        {
            at = i;
        }
    }
    if (at == stats_.mounts.Size())
    {
        MountReads reads;
        reads.mount = mount;
        stats_.mounts.Push(reads);
    }
    stats_.mounts[at].reads += 1;
    stats_.mounts[at].bytes += total;
    return Status::Success();
}

Status ResourceManager::AcquireLocked(ResourceId id, const Expect& expect, Vector<ResourceId>* chain, u32* slot,
                                      LoadFailure* failure)
{
    // Loaded already: the declared type and version must be what is loaded.
    if (const u32* found = by_id_.Find(id.Value()))
    {
        const Slot& s = *slots_[*found];
        if (expect.check_type && s.type != expect.type)
        {
            NoteFailure(failure, *chain, id);
            return Fail(expect.dependency ? ErrorCode::DependencyTypeMismatch : ErrorCode::TypeMismatch,
                        "a loaded resource is not of the type that was asked for");
        }
        if (expect.check_version && s.type_version != expect.version)
        {
            NoteFailure(failure, *chain, id);
            return Fail(ErrorCode::DependencyTypeMismatch,
                        "a loaded resource is not of the version the reference declared");
        }
        if (expect.key != 0 && s.object->Key() != expect.key)
        {
            NoteFailure(failure, *chain, id);
            return Fail(ErrorCode::TypeMismatch, "the loaded resource is not the C++ type that was asked for");
        }
        AddRefLocked(*found);
        stats_.cache_hits += 1;
        *slot = *found;
        return Status::Success();
    }
    for (usize i = 0; i < chain->Size(); ++i)
    {
        if ((*chain)[i] == id)
        {
            NoteFailure(failure, *chain, id);
            return Fail(ErrorCode::DependencyCycle, "the resources depend on each other in a cycle");
        }
    }
    BlobBufferPtr buffer;
    usize size = 0;
    const Status read = ReadBlobLocked(id, 0, &buffer, &size);
    if (read.Failed())
    {
        NoteFailure(failure, *chain, id);
        if (read.Is(ErrorCode::NotFound) && expect.dependency)
        {
            return Fail(ErrorCode::MissingDependency, "a dependency is in none of the mounts");
        }
        return read;
    }
    return BuildLocked(id, expect, static_cast<BlobBufferPtr&&>(buffer), size, chain, slot, failure);
}

Status ResourceManager::BuildLocked(ResourceId id, const Expect& expect, BlobBufferPtr buffer, usize size,
                                    Vector<ResourceId>* chain, u32* slot_out, LoadFailure* failure)
{
    BlobReader reader;
    const ByteSpan bytes = buffer->Bytes();
    const Status opened = reader.Open(bytes, types_);
    if (opened.Failed())
    {
        if (opened.Is(ErrorCode::UnsupportedVersion))
        {
            stats_.rejected_old_version += 1;
        }
        NoteFailure(failure, *chain, id);
        return opened;
    }
    if (reader.Id() != id)
    {
        NoteFailure(failure, *chain, id);
        return Fail(ErrorCode::Corrupt, "the blob's own id is not the id it was read under");
    }
    if (config_.verify_on_load)
    {
        const Status verified = reader.Verify();
        if (verified.Failed())
        {
            NoteFailure(failure, *chain, id);
            return verified;
        }
    }
    if (expect.check_type && reader.Type() != expect.type)
    {
        NoteFailure(failure, *chain, id);
        return Fail(expect.dependency ? ErrorCode::DependencyTypeMismatch : ErrorCode::TypeMismatch,
                    "the resource is not of the type that was asked for");
    }
    if (expect.check_version && reader.TypeVersion() != expect.version)
    {
        NoteFailure(failure, *chain, id);
        return Fail(ErrorCode::DependencyTypeMismatch, "the resource is not of the version the reference declared");
    }
    LoaderBase* loader = nullptr;
    for (usize i = 0; i < loaders_.Size(); ++i)
    {
        if (loaders_[i].type == reader.Type())
        {
            loader = loaders_[i].loader.Get();
        }
    }
    if (loader == nullptr)
    {
        NoteFailure(failure, *chain, id);
        return Fail(ErrorCode::TypeMismatch, "no loader is registered for the resource's type");
    }
    if (expect.key != 0 && loader->Key() != expect.key)
    {
        NoteFailure(failure, *chain, id);
        return Fail(ErrorCode::TypeMismatch, "the type's loader does not make the C++ type that was asked for");
    }

    // The closure first: every declared dependency, of the declared type and version.
    Vector<ResourceId> dep_ids;
    Vector<u32> dep_slots;
    chain->Push(id);
    Status status = Status::Success();
    for (usize i = 0; i < reader.DependencyCount() && status.Ok(); ++i)
    {
        const Dependency d = reader.DependencyAt(i);
        Expect want;
        want.dependency = true;
        want.check_type = true;
        want.type = d.type;
        want.check_version = true;
        want.version = d.type_version;
        u32 dep_slot = 0;
        status = AcquireLocked(d.id, want, chain, &dep_slot, failure);
        if (status.Ok())
        {
            dep_ids.Push(d.id);
            dep_slots.Push(dep_slot);
        }
    }
    chain->Pop();
    if (status.Failed())
    {
        for (usize i = 0; i < dep_slots.Size(); ++i)
        {
            ReleaseLocked(dep_slots[i]);
        }
        return status;
    }

    LoadContext context;
    context.id_ = id;
    context.bytes_ = bytes;
    context.buffer_ = buffer;
    context.blob_ = &reader;
    context.types_ = &types_;
    FindState state;
    state.manager = this;
    state.ids = &dep_ids;
    state.slots = &dep_slots;
    context.find_ = &ResourceManager::FindInContext;
    context.find_user_ = &state;
    context.conversions_ = &stats_.load_conversions;
    UniquePtr<LoadedObject> object;
    const u64 loading = os::MonotonicTimeNanos();
    status = loader->Load(context, &object);
    stats_.read_nanos += os::MonotonicTimeNanos() - loading;
    if (status.Failed())
    {
        for (usize i = 0; i < dep_slots.Size(); ++i)
        {
            ReleaseLocked(dep_slots[i]);
        }
        NoteFailure(failure, *chain, id);
        return status;
    }

    u32 index = 0;
    if (free_slots_.Size() != 0)
    {
        index = free_slots_[free_slots_.Size() - 1];
        free_slots_.Pop();
    }
    else
    {
        index = static_cast<u32>(slots_.Size());
        slots_.Push(MakeUnique<Slot>());
    }
    Slot& slot = *slots_[index];
    slot.live = true;
    slot.pinned = false;
    slot.id = id;
    slot.type = reader.Type();
    slot.type_version = reader.TypeVersion();
    slot.refs = 1;
    slot.buffer = static_cast<BlobBufferPtr&&>(buffer);
    slot.size = size;
    slot.object = static_cast<UniquePtr<LoadedObject>&&>(object);
    slot.deps = static_cast<Vector<ResourceId>&&>(dep_ids);
    slot.dep_slots = static_cast<Vector<u32>&&>(dep_slots);
    by_id_.Insert(id.Value(), index);
    stats_.loads += 1;
    *slot_out = index;
    return Status::Success();
}

Status ResourceManager::LoadRaw(ResourceId id, ResourceType type, u32 key, HandleBase* out, LoadFailure* failure)
{
    out->Reset();
    if (failure != nullptr)
    {
        *failure = LoadFailure();
    }
    LockGuard<RecursiveMutex> lock(mutex_);
    Expect expect;
    expect.check_type = true;
    expect.type = type;
    expect.key = key;
    Vector<ResourceId> chain;
    u32 slot = 0;
    const Status status = AcquireLocked(id, expect, &chain, &slot, failure);
    if (status.Ok())
    {
        out->manager_ = this;
        out->index_ = slot;
        out->generation_ = slots_[slot]->generation;
        out->id_ = id;
        return status;
    }
    const bool usable = status.Is(ErrorCode::NotFound) || status.Is(ErrorCode::MissingDependency) ||
                        status.Is(ErrorCode::Corrupt) || status.Is(ErrorCode::UnsupportedVersion) ||
                        status.Is(ErrorCode::DependencyTypeMismatch);
    if (config_.allow_fallbacks && usable)
    {
        const Status fell = LoadFallback(type, key, out);
        if (fell.Ok())
        {
            char hex[17];
            FormatResourceId(id, hex);
            char message[160];
            FormatTo(message, sizeof(message), "resource %s did not load (%s); a fallback stands in", hex,
                     status.Message());
            Log(LogCore, LogVerbosity::Warn, StringView(message, CStrLength(message)));
            return Status::Success();
        }
    }
    return status;
}

Status ResourceManager::LoadFallback(ResourceType type, u32 key, HandleBase* out)
{
    const FallbackEntry* entry = nullptr;
    for (usize i = 0; i < fallbacks_.Size(); ++i)
    {
        if (fallbacks_[i].type == type)
        {
            entry = &fallbacks_[i];
        }
    }
    if (entry == nullptr)
    {
        return Fail(ErrorCode::NotFound, "no fallback is registered for the resource's type");
    }
    BlobReader reader;
    const ByteSpan blob(entry->blob.Data(), entry->blob.Size());
    ENGINE_TRY(reader.Open(blob, types_));
    u32 slot = 0;
    if (const u32* found = by_id_.Find(reader.Id().Value()))
    {
        if (slots_[*found]->object->Key() != key)
        {
            return Fail(ErrorCode::TypeMismatch, "the fallback is not the C++ type that was asked for");
        }
        AddRefLocked(*found);
        slot = *found;
    }
    else
    {
        LoadBuffer raw;
        ENGINE_TRY(pool_->Acquire(blob.Size(), &raw));
        MemCopy(raw.Data(), blob.Data(), blob.Size());
        BlobBufferPtr buffer = MakeShared<BlobBuffer>(pool_, resident_, static_cast<LoadBuffer&&>(raw), blob.Size());
        Expect expect;
        expect.check_type = true;
        expect.type = type;
        expect.key = key;
        Vector<ResourceId> chain;
        ENGINE_TRY(BuildLocked(reader.Id(), expect, static_cast<BlobBufferPtr&&>(buffer), blob.Size(), &chain, &slot,
                               nullptr));
    }
    stats_.fallbacks_used += 1;
    out->manager_ = this;
    out->index_ = slot;
    out->generation_ = slots_[slot]->generation;
    out->id_ = reader.Id();
    return Status::Success();
}

// ---------------------------------------------------------------------------------------------------------------------
// Unloading and queries

Status ResourceManager::UnloadLocked(ResourceId id, bool force, Vector<ResourceId>* dependents)
{
    const u32* found = by_id_.Find(id.Value());
    if (found == nullptr)
    {
        return Fail(ErrorCode::NotFound, "the resource is not loaded");
    }
    const u32 index = *found;
    Vector<ResourceId> users;
    DependentsLocked(id, &users);
    if (!force)
    {
        const u32 external = slots_[index]->refs - static_cast<u32>(users.Size());
        if (users.Size() != 0 || external != 0)
        {
            if (dependents != nullptr)
            {
                for (usize i = 0; i < users.Size(); ++i)
                {
                    dependents->Push(users[i]);
                }
            }
            return Fail(ErrorCode::InUse, "a loaded resource depends on it or a handle holds it");
        }
        FreeLocked(index);
        return Status::Success();
    }
    for (usize i = 0; i < users.Size(); ++i)
    {
        if (dependents != nullptr)
        {
            dependents->Push(users[i]);
        }
        static_cast<void>(UnloadLocked(users[i], true, dependents));
    }
    FreeLocked(index);
    return Status::Success();
}

Status ResourceManager::Unload(ResourceId id, bool force, Vector<ResourceId>* dependents)
{
    LockGuard<RecursiveMutex> lock(mutex_);
    return UnloadLocked(id, force, dependents);
}

Status ResourceManager::Pin(ResourceId id)
{
    LockGuard<RecursiveMutex> lock(mutex_);
    const u32* found = by_id_.Find(id.Value());
    if (found == nullptr)
    {
        return Fail(ErrorCode::NotFound, "the resource is not loaded");
    }
    slots_[*found]->pinned = true;
    return Status::Success();
}

Status ResourceManager::Unpin(ResourceId id)
{
    LockGuard<RecursiveMutex> lock(mutex_);
    const u32* found = by_id_.Find(id.Value());
    if (found == nullptr)
    {
        return Fail(ErrorCode::NotFound, "the resource is not loaded");
    }
    const u32 index = *found;
    slots_[index]->pinned = false;
    if (slots_[index]->refs == 0)
    {
        FreeLocked(index);
    }
    return Status::Success();
}

bool ResourceManager::ResourceInUse(ResourceId id, Vector<ResourceId>* dependents) const
{
    LockGuard<RecursiveMutex> lock(mutex_);
    Vector<ResourceId> users;
    DependentsLocked(id, &users);
    if (dependents != nullptr)
    {
        for (usize i = 0; i < users.Size(); ++i)
        {
            dependents->Push(users[i]);
        }
    }
    return users.Size() != 0;
}

bool ResourceManager::QueryDependents(void* user, ResourceId id, Vector<ResourceId>* out)
{
    return static_cast<ResourceManager*>(user)->ResourceInUse(id, out);
}

void ResourceManager::QueryUnload(void* user, ResourceId dependent)
{
    static_cast<void>(static_cast<ResourceManager*>(user)->Unload(dependent, true));
}

ResourceInUseQuery ResourceManager::InUseQuery()
{
    ResourceInUseQuery query;
    query.dependents = &ResourceManager::QueryDependents;
    query.unload = &ResourceManager::QueryUnload;
    query.user = this;
    return query;
}

ResourceStats ResourceManager::Stats() const
{
    LockGuard<RecursiveMutex> lock(mutex_);
    ResourceStats copy;
    copy.loads = stats_.loads;
    copy.cache_hits = stats_.cache_hits;
    copy.bytes_resident = resident_->Get();
    copy.fallbacks_used = stats_.fallbacks_used;
    copy.load_conversions = stats_.load_conversions;
    copy.rejected_old_version = stats_.rejected_old_version;
    copy.bytes_read = stats_.bytes_read;
    copy.read_nanos = stats_.read_nanos;
    for (usize i = 0; i < stats_.mounts.Size(); ++i)
    {
        copy.mounts.Push(stats_.mounts[i]);
    }
    for (usize i = 0; i < slots_.Size(); ++i)
    {
        copy.resident += slots_[i]->live ? 1u : 0u;
    }
    return copy;
}

void ResourceManager::LoadedIds(Vector<ResourceId>* out) const
{
    LockGuard<RecursiveMutex> lock(mutex_);
    for (usize i = 0; i < slots_.Size(); ++i)
    {
        if (slots_[i]->live)
        {
            out->Push(slots_[i]->id);
        }
    }
    for (usize i = 1; i < out->Size(); ++i)
    {
        for (usize j = i; j > 0 && (*out)[j] < (*out)[j - 1]; --j)
        {
            const ResourceId t = (*out)[j];
            (*out)[j] = (*out)[j - 1];
            (*out)[j - 1] = t;
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Reloading

Status ResourceManager::ReloadLocked(ResourceId id, u32* reloaded)
{
    const u32 index = *by_id_.Find(id.Value());
    // The replacement blob, read afresh.
    BlobBufferPtr buffer;
    usize size = 0;
    ENGINE_TRY(ReadBlobLocked(id, slots_[index]->size, &buffer, &size));
    BlobReader reader;
    const ByteSpan bytes = buffer->Bytes();
    const Status opened = reader.Open(bytes, types_);
    if (opened.Failed())
    {
        if (opened.Is(ErrorCode::UnsupportedVersion))
        {
            stats_.rejected_old_version += 1;
        }
        return opened;
    }
    if (reader.Id() != id)
    {
        return Fail(ErrorCode::Corrupt, "the blob's own id is not the id it was read under");
    }
    if (reader.Type() != slots_[index]->type)
    {
        return Fail(ErrorCode::TypeMismatch, "the replacement is of another resource type");
    }
    if (config_.verify_on_load)
    {
        ENGINE_TRY(reader.Verify());
    }
    LoaderBase* loader = nullptr;
    for (usize i = 0; i < loaders_.Size(); ++i)
    {
        if (loaders_[i].type == reader.Type())
        {
            loader = loaders_[i].loader.Get();
        }
    }
    if (loader == nullptr || loader->Key() != slots_[index]->object->Key())
    {
        return Fail(ErrorCode::TypeMismatch, "the type's loader no longer makes the loaded C++ type");
    }

    // The new object, with its dependencies (acquired like a load's).
    Vector<ResourceId> chain;
    chain.Push(id);
    Vector<ResourceId> dep_ids;
    Vector<u32> dep_slots;
    Status status = Status::Success();
    // A new dependency may not be (or depend on) a resource that already depends on this one: that is a cycle, which
    // the cache hit of a loaded dependency would not notice.
    Vector<ResourceId> above;
    {
        Vector<ResourceId> frontier;
        frontier.Push(id);
        while (frontier.Size() != 0)
        {
            const ResourceId next = frontier[frontier.Size() - 1];
            frontier.Pop();
            Vector<ResourceId> users;
            DependentsLocked(next, &users);
            for (usize u = 0; u < users.Size(); ++u)
            {
                bool known = false;
                for (usize k = 0; k < above.Size(); ++k)
                {
                    known = known || above[k] == users[u];
                }
                if (!known)
                {
                    above.Push(users[u]);
                    frontier.Push(users[u]);
                }
            }
        }
    }
    for (usize i = 0; i < reader.DependencyCount() && status.Ok(); ++i)
    {
        const Dependency d = reader.DependencyAt(i);
        bool cyclic = d.id == id;
        for (usize k = 0; k < above.Size(); ++k)
        {
            cyclic = cyclic || above[k] == d.id;
        }
        if (cyclic)
        {
            status = Fail(ErrorCode::DependencyCycle,
                          "the replacement would make the resources depend on each other in a cycle");
            break;
        }
        Expect want;
        want.dependency = true;
        want.check_type = true;
        want.type = d.type;
        want.check_version = true;
        want.version = d.type_version;
        u32 dep_slot = 0;
        status = AcquireLocked(d.id, want, &chain, &dep_slot, nullptr);
        if (status.Ok())
        {
            dep_ids.Push(d.id);
            dep_slots.Push(dep_slot);
        }
    }
    auto drop_new_deps = [&]()
    {
        for (usize i = 0; i < dep_slots.Size(); ++i)
        {
            ReleaseLocked(dep_slots[i]);
        }
        dep_slots.Clear();
    };
    if (status.Failed())
    {
        drop_new_deps();
        return status;
    }
    UniquePtr<LoadedObject> fresh;
    {
        LoadContext context;
        context.id_ = id;
        context.bytes_ = bytes;
        context.buffer_ = buffer;
        context.blob_ = &reader;
        context.types_ = &types_;
        FindState state;
        state.manager = this;
        state.ids = &dep_ids;
        state.slots = &dep_slots;
        context.find_ = &ResourceManager::FindInContext;
        context.find_user_ = &state;
        context.conversions_ = &stats_.load_conversions;
        status = loader->Load(context, &fresh);
    }
    if (status.Failed())
    {
        drop_new_deps();
        return status;
    }

    // The dependents, transitively, in dependency order, each rebuilt against the new object.
    Vector<u32> order;
    {
        Vector<ResourceId> frontier;
        frontier.Push(id);
        Vector<ResourceId> seen;
        seen.Push(id);
        while (frontier.Size() != 0)
        {
            const ResourceId next = frontier[frontier.Size() - 1];
            frontier.Pop();
            Vector<ResourceId> users;
            DependentsLocked(next, &users);
            for (usize u = 0; u < users.Size(); ++u)
            {
                bool known = false;
                for (usize s = 0; s < seen.Size(); ++s)
                {
                    known = known || seen[s] == users[u];
                }
                if (!known)
                {
                    seen.Push(users[u]);
                    frontier.Push(users[u]);
                    order.Push(*by_id_.Find(users[u].Value()));
                }
            }
        }
        // Sort so that a dependent comes after the dependencies it has in the set (repeated passes; the set is small).
        for (usize pass = 0; pass < order.Size(); ++pass)
        {
            for (usize i = 0; i < order.Size(); ++i)
            {
                for (usize j = i + 1; j < order.Size(); ++j)
                {
                    bool depends = false;
                    for (usize d = 0; d < slots_[order[i]]->deps.Size(); ++d)
                    {
                        depends = depends || slots_[order[i]]->deps[d] == slots_[order[j]]->id;
                    }
                    if (depends)
                    {
                        const u32 t = order[i];
                        order[i] = order[j];
                        order[j] = t;
                    }
                }
            }
        }
    }
    Vector<ResourceId> staged_ids;
    Vector<const LoadedObject*> staged_objects;
    Vector<UniquePtr<LoadedObject>> staged_owned;
    staged_ids.Push(id);
    staged_objects.Push(fresh.Get());
    for (usize o = 0; o < order.Size() && status.Ok(); ++o)
    {
        Slot& dependent = *slots_[order[o]];
        BlobReader dependent_reader;
        const ByteSpan dependent_bytes = dependent.buffer->Bytes();
        status = dependent_reader.Open(dependent_bytes, types_);
        if (status.Failed())
        {
            break;
        }
        for (usize d = 0; d < dependent_reader.DependencyCount() && status.Ok(); ++d)
        {
            const Dependency declared = dependent_reader.DependencyAt(d);
            if (declared.id == id && (declared.type != reader.Type() || declared.type_version != reader.TypeVersion()))
            {
                status = Fail(ErrorCode::DependencyTypeMismatch,
                              "a loaded resource declared another type or version of the replaced one");
            }
        }
        if (status.Failed())
        {
            break;
        }
        LoaderBase* dependent_loader = nullptr;
        for (usize i = 0; i < loaders_.Size(); ++i)
        {
            if (loaders_[i].type == dependent.type)
            {
                dependent_loader = loaders_[i].loader.Get();
            }
        }
        if (dependent_loader == nullptr)
        {
            status = Fail(ErrorCode::TypeMismatch, "no loader is registered for a dependent's type");
            break;
        }
        LoadContext context;
        context.id_ = dependent.id;
        context.bytes_ = dependent_bytes;
        context.buffer_ = dependent.buffer;
        context.blob_ = &dependent_reader;
        context.types_ = &types_;
        FindState state;
        state.manager = this;
        state.ids = &dependent.deps;
        state.slots = &dependent.dep_slots;
        state.staged_ids = &staged_ids;
        state.staged_objects = &staged_objects;
        context.find_ = &ResourceManager::FindInContext;
        context.find_user_ = &state;
        context.conversions_ = &stats_.load_conversions;
        UniquePtr<LoadedObject> rebuilt;
        status = dependent_loader->Load(context, &rebuilt);
        if (status.Ok())
        {
            staged_ids.Push(dependent.id);
            staged_objects.Push(rebuilt.Get());
            staged_owned.Push(static_cast<UniquePtr<LoadedObject>&&>(rebuilt));
        }
    }
    if (status.Failed())
    {
        drop_new_deps();
        return status;
    }

    // Commit: nothing below can fail.
    Slot& target = *slots_[index];
    Vector<u32> old_deps;
    for (usize i = 0; i < target.dep_slots.Size(); ++i)
    {
        old_deps.Push(target.dep_slots[i]);
    }
    target.object = static_cast<UniquePtr<LoadedObject>&&>(fresh);
    target.buffer = static_cast<BlobBufferPtr&&>(buffer);
    target.size = size;
    target.type_version = reader.TypeVersion();
    target.deps = static_cast<Vector<ResourceId>&&>(dep_ids);
    target.dep_slots = static_cast<Vector<u32>&&>(dep_slots);
    for (usize i = 0; i < order.Size(); ++i)
    {
        slots_[order[i]]->object = static_cast<UniquePtr<LoadedObject>&&>(staged_owned[i]);
    }
    for (usize i = 0; i < old_deps.Size(); ++i)
    {
        ReleaseLocked(old_deps[i]);
    }
    stats_.loads += 1;
    *reloaded = static_cast<u32>(1 + order.Size());
    return Status::Success();
}

Status ResourceManager::PollChanges(Vector<ReloadResult>* results)
{
    results->Clear();
    Vector<ChangeRecord> changes;
    ENGINE_TRY(mounts_.PollChanges(&changes));
    LockGuard<RecursiveMutex> lock(mutex_);
    for (usize i = 0; i < changes.Size(); ++i)
    {
        if (changes[i].kind != ChangeKind::Replaced || by_id_.Find(changes[i].id.Value()) == nullptr)
        {
            continue;
        }
        ReloadResult result;
        result.id = changes[i].id;
        result.status = ReloadLocked(changes[i].id, &result.reloaded);
        results->Push(result);
    }
    return Status::Success();
}

}  // namespace engine_placeholder::asset
