#include "engine_c/resources.h"
#include "containers/hash.h"

#include "asset/asset_module.h"
#include "asset/cpu_loaders.h"
#include "asset/content_hash.h"
#include "asset/level.h"
#include "asset/mesh.h"
#include "cstr/cstr.h"
#include "engine_c/engine_internal.h"
#include "engine_c/engine_objects.h"
#include "engine_c/handles.h"
#include "engine_c/runtime.h"
#include "engine_c/status_map.h"
#include "jobs/job.h"
#include "jobs/scheduler.h"
#include "log/assert.h"
#include "mem/heap_allocator.h"
#include "os/os.h"
#include "string/string.h"
#include "sync/thread.h"

namespace vse::engine_c
{

struct ResourceHost::ReleaseRecord
{
    ResourceHost* host = nullptr;
    engine_resource_release_fn function = nullptr;
    void* user = nullptr;
    const void* data = nullptr;
    usize size = 0;
};

// One resource of an adding of many (resource_add_many and its asynchronous form): the checked arguments, owned except
// for the bytes, which the host keeps valid until the adding is over.
struct AddManyItem
{
    asset::ResourceId id;
    String<HeapAllocator> path{HeapAllocator::Default()};
    StringView path_view;  // `path`, in a place that does not move
    ByteSpan bytes;
    asset::ContentHash hash;
    bool check_hash = false;
};

// An adding of many: what the job reads, and what it leaves for the host.
struct AddManyObject
{
    Vector<UniquePtr<AddManyItem>> items;
    Atomic<bool> done{false};  // the job's last access to the object
    u32 waiters = 0;           // calls using the ticket (guarded by the host's table mutex)
    engine_status code = ENGINE_OK;
    String<HeapAllocator> message{HeapAllocator::Default()};
    usize failed_index = 0;
};

// Returns from the function when a call of the C API's own kind fails.
#define TRY_C(expression)                                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        const engine_status try_c_status = (expression);                                                               \
        if (try_c_status != ENGINE_OK)                                                                                 \
        {                                                                                                              \
            return try_c_status;                                                                                       \
        }                                                                                                              \
    } while (0)

namespace
{

constexpr usize kMaxHostSlots = 1u << 16;  // loaded resources, and tickets, each
constexpr usize kMaxAddManyItems = 1u << 16;
constexpr usize kMaxAddManyTickets = 256;
constexpr u32 kAddManyFlags = ENGINE_RESOURCE_CHECK_HASH;

constexpr u32 kAddFlags = ENGINE_RESOURCE_BORROW | ENGINE_RESOURCE_CHECK_HASH;
constexpr u32 kReplaceFlags = kAddFlags | ENGINE_RESOURCE_FORCE;

engine_status FromStatus(const asset::Status& status)
{
    return Fail(FromAssetError(status.Code()), status.Message());
}

StringView TextOf(engine_string s)
{
    return s.data == nullptr ? StringView() : StringView(s.data, s.length);
}

jobs::Job RunReleaseJob(ResourceHost* host)
{
    host->RunReleases();
    co_return;
}

jobs::Job RunAddManyJob(ResourceHost* host, AddManyObject* object)
{
    host->RunAddMany(object);
    co_return;
}

// A free slot of a table, or a new one. The caller holds the table mutex.
template <class Slot> bool FindFreeSlot(Vector<Slot>& slots, usize* out)
{
    for (usize i = 0; i < slots.Size(); ++i)
    {
        if (slots[i].object.Get() == nullptr)
        {
            *out = i;
            return true;
        }
    }
    if (slots.Size() >= kMaxHostSlots)
    {
        return false;
    }
    slots.Push(Slot());
    *out = slots.Size() - 1;
    return true;
}

// The arguments of an add or a replace, checked and turned into what the backend takes.
struct AddArgs
{
    asset::ResourceId id;
    StringView path;
    asset::ContentHash hash;
    asset::MemoryAddOptions options;
    ByteSpan bytes;
    UniquePtr<ResourceHost::ReleaseRecord> record;  // freed unless the backend takes the bytes
};

}  // namespace

ResourceHost::ResourceHost(Runtime& runtime) : runtime_(&runtime)
{
    const ResourceConfig& config = runtime.resource_config;
    memory_ = MakeUnique<asset::HostMemoryBackend>(types_, config.memory_bytes);
    asset::ManagerConfig manager_config;
    manager_config.buffer_pool_bytes = config.buffer_pool_bytes;
    manager_config.cache_bytes = config.cache_bytes;
    manager_config.allow_fallbacks = config.allow_fallbacks;
    manager_config.verify_on_load = config.verify_on_load;
    manager_config.max_inflight = config.max_inflight;
    manager_config.max_queued = config.max_queued;
    manager_ = MakeUnique<asset::ResourceManager>(types_, manager_config);
    asset::RegisterCpuLoaders(manager_.Get());
    const asset::Status mounted = manager_->Mount(StringView("/"), memory_.Get(), config.priority);
    ENGINE_VERIFY(mounted.Ok(), "mounting the memory backend cannot fail");
    memory_->SetInUseQuery(manager_->InUseQuery());
    imports_ = MakeUnique<ImportHost>(runtime, *memory_, &tables_dirty_);
    animation_ = MakeUnique<animation::ManagerResolver>(manager_.Get());
}

ResourceHost::~ResourceHost()
{
    {
        LockGuard<Mutex> lock(loader_mutex_);
        loader_ = nullptr;  // cancels the loads, waits for the reading jobs
    }
    skeleton_pins_.Clear();
    clip_pins_.Clear();
    mesh_collider_pins_.Clear();  // handles release into the manager: before it goes (glTF colliders)
    height_map_pins_.Clear();
    animation_ = nullptr;
    // The tickets and handles are gone (the engine refuses to be destroyed while any is out). The backend's entries
    // queue the hand-back of their lent bytes as they are destroyed.
    manager_ = nullptr;
    memory_ = nullptr;
    UniqueLock<Mutex> lock(release_mutex_);
    release_changed_.Wait(lock, [this] { return release_pending_ == 0 && !release_pump_active_; });
}

engine_status ResourceHost::PinSkeleton(u64 id)
{
    LockGuard<Mutex> lock(pin_mutex_);
    for (usize i = 0; i < skeleton_pins_.Size(); ++i)
    {
        if (skeleton_pins_[i].IsValid() && skeleton_pins_[i].Id().Value() == id)
        {
            return ENGINE_OK;
        }
    }
    asset::Handle<asset::SkeletonView> handle;
    const asset::Status loaded = manager_->Load<asset::SkeletonView>(asset::ResourceId::FromValue(id), &handle);
    if (!loaded.Ok())
    {
        return Fail(FromAssetError(loaded.Code()), loaded.Message());
    }
    skeleton_pins_.Push(static_cast<asset::Handle<asset::SkeletonView>&&>(handle));
    return ENGINE_OK;
}

engine_status ResourceHost::PinMeshCollider(u64 id, const asset::MeshColliderView** view, asset::ContentHash* hash)
{
    LockGuard<Mutex> lock(pin_mutex_);
    asset::Handle<asset::MeshColliderView> handle;
    const asset::Status loaded = manager_->Load<asset::MeshColliderView>(asset::ResourceId::FromValue(id), &handle);
    if (!loaded.Ok())
    {
        return Fail(FromAssetError(loaded.Code()), loaded.Message());
    }
    for (usize i = 0; i < mesh_collider_pins_.Size(); ++i)
    {
        if (mesh_collider_pins_[i].handle.Get() == handle.Get())
        {
            *view = handle.Get();
            *hash = mesh_collider_pins_[i].hash;
            return ENGINE_OK;
        }
    }
    MeshColliderPin pin;
    pin.hash = asset::HashBytes(handle.Get()->Bytes());
    *view = handle.Get();
    *hash = pin.hash;
    pin.handle = static_cast<asset::Handle<asset::MeshColliderView>&&>(handle);
    mesh_collider_pins_.Push(static_cast<MeshColliderPin&&>(pin));
    return ENGINE_OK;
}

engine_status ResourceHost::PinHeightMap(u64 id, const asset::HeightMapView** view, asset::ContentHash* hash)
{
    LockGuard<Mutex> lock(pin_mutex_);
    asset::Handle<asset::HeightMapView> handle;
    const asset::Status loaded = manager_->Load<asset::HeightMapView>(asset::ResourceId::FromValue(id), &handle);
    if (!loaded.Ok())
    {
        return Fail(FromAssetError(loaded.Code()), loaded.Message());
    }
    for (usize i = 0; i < height_map_pins_.Size(); ++i)
    {
        HeightMapPin& known = height_map_pins_[i];
        if (known.handle.Get() == handle.Get())
        {
            if (known.bytes.Get() != handle.Get()->Bytes().Get())
            {  // the same view object holds new bytes (a replaced map): the remembered hash is of the old ones
                known.bytes = handle.Get()->Bytes();
                known.hashed = false;
            }
            if (hash != nullptr && !known.hashed)
            {
                known.hash = asset::HashBytes(known.bytes->Bytes());
                known.hashed = true;
            }
            if (view != nullptr)
            {
                *view = handle.Get();
            }
            if (hash != nullptr)
            {
                *hash = known.hash;
            }
            return ENGINE_OK;
        }
    }
    HeightMapPin pin;
    pin.bytes = handle.Get()->Bytes();
    if (hash != nullptr)
    {  // hashing reads every byte: a caller that does not need the hash does not pay for it (it is made on demand)
        pin.hash = asset::HashBytes(pin.bytes->Bytes());
        pin.hashed = true;
    }
    if (view != nullptr)
    {
        *view = handle.Get();
    }
    if (hash != nullptr)
    {
        *hash = pin.hash;
    }
    pin.handle = static_cast<asset::Handle<asset::HeightMapView>&&>(handle);
    // A replaced map keeps its predecessor one generation (a reader that took the old view a moment ago still holds
    // valid bytes); older generations of the same id are released, so ten replacements leave two pins, not eleven.
    u32 same = 0;
    for (usize i = height_map_pins_.Size(); i > 0; --i)
    {
        if (height_map_pins_[i - 1].handle.Get()->Id().Value() == id && ++same >= 2)
        {
            height_map_pins_.Remove(i - 1);
        }
    }
    height_map_pins_.Push(static_cast<HeightMapPin&&>(pin));
    return ENGINE_OK;
}

usize ResourceHost::HeightMapPinCount() const
{
    LockGuard<Mutex> lock(pin_mutex_);
    return height_map_pins_.Size();
}

engine_status ResourceHost::AddHeightMap(u64 id, const u16* samples, u32 width, u32 height, f32 range_min,
                                         f32 range_max, f32 cell_size)
{
    if (id == 0 || samples == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "a height map needs an id and samples");
    }
    asset::HeightMapHeader header;
    header.width = width;
    header.height = height;
    header.range_min = range_min;
    header.range_max = range_max;
    header.cell_size = cell_size;
    if (width < asset::kMinHeightMapSide || height < asset::kMinHeightMapSide || width > asset::kMaxHeightMapSide ||
        height > asset::kMaxHeightMapSide)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "a height map is 2 to 8192 samples a side");
    }
    Vector<byte> little;  // the blob's samples are little-endian
    if (!little.TryResize(usize{width} * height * 2))
    {
        return Fail(ENGINE_ERROR_LIMIT_EXCEEDED, "the height map is too large to hold in memory");
    }
    for (usize i = 0; i < usize{width} * height; ++i)
    {
        little[2 * i] = static_cast<byte>(samples[i] & 0xFFu);
        little[2 * i + 1] = static_cast<byte>(samples[i] >> 8);
    }
    Vector<byte> blob;
    const asset::Status built = asset::BuildHeightMapBlob(types_, asset::ResourceId::FromValue(id), header,
                                                          ByteSpan(little.Data(), little.Size()), &blob);
    if (built.Failed())
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, built.Message());
    }
    engine_resource_add_info info = {};
    info.struct_size = sizeof(info);
    info.id = id;
    info.bytes = blob.Data();
    info.size = blob.Size();
    return Add(&info);  // copies the bytes (no BORROW)
}

engine_status ResourceHost::ReadLevel(u64 id, Vector<byte>* bytes)
{
    bytes->Clear();
    asset::ResourceInfo info;
    const asset::Status found = memory_->Stat(asset::ResourceId::FromValue(id), &info);
    if (found.Failed())
    {
        return Fail(FromAssetError(found.Code()), "the level is not mounted");
    }
    UniquePtr<asset::ResourceReader> reader;
    const asset::Status opened = memory_->Open(asset::ResourceId::FromValue(id), &reader);
    if (opened.Failed())
    {
        return Fail(FromAssetError(opened.Code()), opened.Message());
    }
    bytes->Resize(static_cast<usize>(reader->Size()));
    if (bytes->Size() != 0)
    {
        const asset::Status read = reader->ReadAt(0, bytes->Data(), bytes->Size());
        if (read.Failed())
        {
            return Fail(FromAssetError(read.Code()), read.Message());
        }
    }
    asset::LevelSummary summary;
    const asset::Status summarized = asset::ReadLevelSummary(ByteSpan(bytes->Data(), bytes->Size()), types_, &summary);
    if (summarized.Failed())
    {
        return Fail(FromAssetError(summarized.Code()), summarized.Message());
    }
    for (usize i = 0; i < summary.resources.Size(); ++i)
    {
        if ((summary.resources[i].roles & asset::kLevelRoleInclude) != 0)
        {
            return Fail(ENGINE_ERROR_UNSUPPORTED, "a level that includes other levels cannot be spawned");
        }
        asset::ResourceInfo named;
        if (memory_->Stat(summary.resources[i].id, &named).Failed())
        {
            SetLastError("not_found: the level names a resource that is not mounted (%016llx)",
                         static_cast<unsigned long long>(summary.resources[i].id.Value()));
            return ENGINE_ERROR_NOT_FOUND;
        }
    }
    return ENGINE_OK;
}

engine_status ResourceHost::FindMeshSlot(u64 id, StringView name, u32* index)
{
    UniquePtr<asset::ResourceReader> reader;
    const asset::Status opened = memory_->Open(asset::ResourceId::FromValue(id), &reader);
    if (opened.Failed())
    {
        return Fail(FromAssetError(opened.Code()), "the mesh is not mounted");
    }
    Vector<byte> bytes;
    bytes.Resize(static_cast<usize>(reader->Size()));
    if (bytes.Size() != 0)
    {
        const asset::Status read = reader->ReadAt(0, bytes.Data(), bytes.Size());
        if (read.Failed())
        {
            return Fail(FromAssetError(read.Code()), read.Message());
        }
    }
    asset::MeshView view;
    const asset::Status parsed = view.Open(ByteSpan(bytes.Data(), bytes.Size()), types_);
    if (parsed.Failed())
    {
        return Fail(ENGINE_ERROR_TYPE_MISMATCH, "the resource is not a mesh");
    }
    for (u32 i = 0; i < view.SlotCount(); ++i)
    {
        const asset::MeshSlot slot = view.Slot(i);
        if (StringView(slot.name.Data(), CStrLength(slot.name.Data())) == name)
        {
            *index = i;
            return ENGINE_OK;
        }
    }
    return Fail(ENGINE_ERROR_NOT_FOUND, "the mesh has no slot of that name");
}

engine_status ResourceHost::PinClip(u64 id, u64 skeleton_id, bool* has_weights)
{
    LockGuard<Mutex> lock(pin_mutex_);
    asset::Handle<asset::ClipView> handle;
    const asset::Status loaded = manager_->Load<asset::ClipView>(asset::ResourceId::FromValue(id), &handle);
    if (!loaded.Ok())
    {
        return Fail(FromAssetError(loaded.Code()), loaded.Message());
    }
    if (has_weights != nullptr)
    {
        *has_weights = handle.Get()->WeightTargetCount() != 0;
    }
    if (!handle.Get()->Skeleton().IsNone())
    {
        if (skeleton_id == 0)
        {
            return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the clip moves joints and the entity has no skeleton instance");
        }
        const asset::SkeletonView* skeleton = manager_->Find<asset::SkeletonView>(
            asset::ResourceId::FromValue(skeleton_id));
        if (skeleton == nullptr)
        {
            return Fail(ENGINE_ERROR_NOT_FOUND, "the skeleton of the entity is not loaded");
        }
        if (!handle.Get()->Bind(*skeleton).Ok())
        {
            return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the clip was not made for the entity's skeleton");
        }
    }
    for (usize i = 0; i < clip_pins_.Size(); ++i)
    {
        if (clip_pins_[i].IsValid() && clip_pins_[i].Id().Value() == id)
        {
            return ENGINE_OK;
        }
    }
    clip_pins_.Push(static_cast<asset::Handle<asset::ClipView>&&>(handle));
    return ENGINE_OK;
}

// ---- lent bytes

void ResourceHost::OnRelease(void* user, ByteSpan bytes)
{
    ReleaseRecord* record = static_cast<ReleaseRecord*>(user);
    record->data = bytes.Data();
    record->size = bytes.Size();
    record->host->QueueRelease(record);
}

void ResourceHost::QueueRelease(ReleaseRecord* record)
{
    bool start = false;
    {
        LockGuard<Mutex> lock(release_mutex_);
        release_queue_.Push(record);
        ++release_pending_;
        if (!release_pump_active_)
        {
            release_pump_active_ = true;
            start = true;
        }
    }
    if (start)
    {
        runtime_->EnsureJobs().Submit(RunReleaseJob(this));
    }
}

void ResourceHost::RunReleases()
{
    for (;;)
    {
        Vector<ReleaseRecord*> batch;
        {
            LockGuard<Mutex> lock(release_mutex_);
            if (release_queue_.Size() == 0)
            {
                release_pump_active_ = false;
                release_changed_.NotifyAll();  // under the lock: after it is released nothing of the host is touched
                return;
            }
            batch = static_cast<Vector<ReleaseRecord*>&&>(release_queue_);
            release_queue_.Clear();
        }
        for (usize i = 0; i < batch.Size(); ++i)
        {
            ReleaseRecord* record = batch[i];
            record->function(record->user, record->data, record->size);
            UniquePtr<ReleaseRecord> done(record);
        }
        {
            LockGuard<Mutex> lock(release_mutex_);
            release_pending_ -= static_cast<u32>(batch.Size());
        }
    }
}

// ---- the content

namespace
{

engine_status BuildArgs(ResourceHost* host, const engine_resource_add_info* info, u32 allowed, AddArgs* args)
{
    if (info == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`info` is NULL");
    }
    if (StructSizeOf(info) < sizeof(engine_resource_add_info))
    {
        return Fail(ENGINE_ERROR_STRUCT_SIZE, "engine_resource_add_info.struct_size is smaller than the struct");
    }
    if ((info->flags & ~allowed) != 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "engine_resource_add_info.flags has a bit this call does not take");
    }
    if (info->id == 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the resource id is zero");
    }
    if (info->bytes == nullptr || info->size == 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the bytes are NULL or empty");
    }
    const bool borrow = (info->flags & ENGINE_RESOURCE_BORROW) != 0;
    if (borrow != (info->release != nullptr))
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`release` is required with ENGINE_RESOURCE_BORROW and only then");
    }
    args->id = asset::ResourceId::FromValue(info->id);
    args->path = TextOf(info->path);
    args->bytes = ByteSpan(static_cast<const byte*>(info->bytes), info->size);
    if ((info->flags & ENGINE_RESOURCE_CHECK_HASH) != 0)
    {
        MemCopy(args->hash.bytes, info->expected_hash, sizeof(args->hash.bytes));
        args->options.expected_hash = &args->hash;
    }
    if (args->path.Length() != 0)
    {
        args->options.paths = Span<const StringView>(&args->path, 1);
    }
    if (borrow)
    {
        args->record = MakeUnique<ResourceHost::ReleaseRecord>();
        args->record->host = host;
        args->record->function = info->release;
        args->record->user = info->release_user;
    }
    return ENGINE_OK;
}

// Writes ids as an array of uint64_t values into a buffer the host releases.
void WriteIds(const Vector<asset::ResourceId>& ids, engine_buffer* out)
{
    if (out == nullptr)
    {
        return;
    }
    Vector<u64> values;
    for (usize i = 0; i < ids.Size(); ++i)
    {
        values.Push(ids[i].Value());
    }
    MakeTextBuffer(StringView(reinterpret_cast<const char*>(values.Data()), values.Size() * sizeof(u64)), out);
}

}  // namespace

engine_status ResourceHost::Add(const engine_resource_add_info* info)
{
    AddArgs args;
    TRY_C(BuildArgs(this, info, kAddFlags, &args));
    if (args.record)
    {
        args.options.release = &ResourceHost::OnRelease;
        args.options.release_user = args.record.Get();
    }
    const asset::Status status = memory_->Add(args.id, args.bytes, args.options);
    if (status.Failed())
    {
        return FromStatus(status);  // the record is freed with `args`: the host keeps its bytes
    }
    args.record.Release();  // the backend's entry hands the bytes back, once
    TablesChanged();
    return ENGINE_OK;
}

engine_status ResourceHost::Replace(const engine_resource_add_info* info, engine_buffer* dependents)
{
    if (dependents != nullptr)
    {
        *dependents = engine_buffer{};
    }
    AddArgs args;
    TRY_C(BuildArgs(this, info, kReplaceFlags, &args));
    if (args.record)
    {
        args.options.release = &ResourceHost::OnRelease;
        args.options.release_user = args.record.Get();
    }
    const bool force = (info->flags & ENGINE_RESOURCE_FORCE) != 0;
    Vector<asset::ResourceId> unloaded;
    if (force)
    {
        manager_->ResourceInUse(args.id, &unloaded);  // what a forced replace unloads, for the host
    }
    Vector<asset::ResourceId> named;
    const asset::Status status = memory_->Replace(args.id, args.bytes, force, &named, args.options);
    if (status.Failed())
    {
        if (status.Code() == asset::ErrorCode::InUse)
        {
            WriteIds(named, dependents);
        }
        return FromStatus(status);
    }
    args.record.Release();
    TablesChanged();
    WriteIds(unloaded, dependents);
    Vector<asset::ReloadResult> results;
    const asset::Status polled = manager_->PollChanges(&results);  // loaded copies are rebuilt from the new bytes now
    return polled.Failed() ? FromStatus(polled) : ENGINE_OK;
}

engine_status ResourceHost::Remove(u64 id, u32 flags, engine_buffer* dependents)
{
    if (dependents != nullptr)
    {
        *dependents = engine_buffer{};
    }
    if ((flags & ~static_cast<u32>(ENGINE_RESOURCE_FORCE)) != 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "flags has a bit this call does not take");
    }
    if (id == 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the resource id is zero");
    }
    const asset::ResourceId rid = asset::ResourceId::FromValue(id);
    const bool force = (flags & ENGINE_RESOURCE_FORCE) != 0;
    Vector<asset::ResourceId> unloaded;
    if (force)
    {
        manager_->ResourceInUse(rid, &unloaded);
    }
    Vector<asset::ResourceId> named;
    const asset::Status status = memory_->Remove(rid, force, &named);
    if (status.Failed())
    {
        if (status.Code() == asset::ErrorCode::InUse)
        {
            WriteIds(named, dependents);
        }
        return FromStatus(status);
    }
    TablesChanged();
    WriteIds(unloaded, dependents);
    return ENGINE_OK;
}

engine_status ResourceHost::Read(u64 id, u64 max_size, engine_buffer* out)
{
    if (out == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`out` is NULL");
    }
    *out = engine_buffer{};
    if (id == 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the resource id is zero");
    }
    const asset::ResourceId rid = asset::ResourceId::FromValue(id);
    UniquePtr<asset::ResourceReader> reader;
    const asset::Status opened = memory_->Open(rid, &reader);  // a resource of a pak or a file is not here
    if (opened.Failed())
    {
        return FromStatus(opened);
    }
    const u64 size = reader->Size();
    if (max_size != 0 && size > max_size)
    {
        SetLastError("too_large: the blob is %llu bytes, above the limit of %llu",
                     static_cast<unsigned long long>(size), static_cast<unsigned long long>(max_size));
        return ENGINE_ERROR_TOO_LARGE;
    }
    char* block = MakeBlankBuffer(static_cast<usize>(size), out);
    if (block == nullptr)
    {
        return Fail(ENGINE_ERROR_INTERNAL, "memory for the blob ran out");
    }
    if (size != 0)
    {
        const asset::Status read = reader->ReadAt(0, block, static_cast<usize>(size));
        if (read.Failed())
        {
            out->release(out);
            return FromStatus(read);
        }
    }
    return ENGINE_OK;
}

void ResourceHost::RunAddMany(AddManyObject* object)
{
    Vector<asset::MemoryAddItem> list;
    for (usize i = 0; i < object->items.Size(); ++i)
    {
        AddManyItem* item = object->items[i].Get();
        asset::MemoryAddItem one;
        one.id = item->id;
        one.data = item->bytes.Data();
        one.size = item->bytes.Size();
        if (item->path_view.Length() != 0)
        {
            one.options.paths = Span<const StringView>(&item->path_view, 1);
        }
        one.options.expected_hash = item->check_hash ? &item->hash : nullptr;
        list.Push(one);
    }
    usize failed = 0;
    const asset::Status status = memory_->AddMany(Span<const asset::MemoryAddItem>(list.Data(), list.Size()), &failed);
    if (status.Failed())
    {
        object->code = FromAssetError(status.Code());
        object->message = String<HeapAllocator>(HeapAllocator::Default(), StringView(status.Message()));
        object->failed_index = failed;
    }
    else
    {
        TablesChanged();
    }
    object->done.Store(true, MemoryOrder::Release);
}

namespace
{

// Checks the host's array and makes the items of an adding (the paths copied, the bytes borrowed).
engine_status MakeAddManyItems(ResourceHost* host, const engine_resource_add_info* infos, usize count,
                               usize* failed_index, AddManyObject* object)
{
    if (failed_index != nullptr)
    {
        *failed_index = 0;
    }
    if (count > kMaxAddManyItems)
    {
        if (failed_index != nullptr)
        {
            *failed_index = count;
        }
        return Fail(ENGINE_ERROR_LIMIT_EXCEEDED, "an adding takes at most 65536 resources");
    }
    if (count != 0 && infos == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`infos` is NULL");
    }
    for (usize i = 0; i < count; ++i)
    {
        AddArgs args;
        const engine_status checked = BuildArgs(host, &infos[i], kAddManyFlags, &args);
        if (checked != ENGINE_OK)
        {
            if (failed_index != nullptr)
            {
                *failed_index = i;
            }
            return checked;
        }
        UniquePtr<AddManyItem> item = MakeUnique<AddManyItem>();
        item->id = args.id;
        item->path = String<HeapAllocator>(HeapAllocator::Default(), args.path);
        item->path_view = item->path.View();
        item->bytes = args.bytes;
        item->check_hash = (infos[i].flags & ENGINE_RESOURCE_CHECK_HASH) != 0;
        item->hash = args.hash;
        object->items.Push(Move(item));
    }
    return ENGINE_OK;
}

}  // namespace

engine_status ResourceHost::AddMany(const engine_resource_add_info* infos, usize count, usize* failed_index)
{
    AddManyObject object;
    TRY_C(MakeAddManyItems(this, infos, count, failed_index, &object));
    if (count == 0)
    {
        return ENGINE_OK;
    }
    RunAddMany(&object);
    if (object.code != ENGINE_OK)
    {
        if (failed_index != nullptr)
        {
            *failed_index = object.failed_index;
        }
        return Fail(object.code, object.message.CStr());
    }
    return ENGINE_OK;
}

engine_status ResourceHost::AddManyAsync(const engine_resource_add_info* infos, usize count, engine_handle* ticket)
{
    if (ticket == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`ticket` is NULL");
    }
    *ticket = engine_handle{};
    UniquePtr<AddManyObject> object = MakeUnique<AddManyObject>();
    TRY_C(MakeAddManyItems(this, infos, count, nullptr, object.Get()));
    if (!EngineObjectAcquire())
    {
        return Fail(ENGINE_ERROR_STALE_HANDLE, "no engine exists");
    }
    AddManyObject* raw = object.Get();
    {
        LockGuard<Mutex> lock(table_mutex_);
        usize slot = 0;
        for (; slot < adds_.Size(); ++slot)
        {
            if (adds_[slot].object.Get() == nullptr)
            {
                break;
            }
        }
        if (slot == adds_.Size())
        {
            if (adds_.Size() >= kMaxAddManyTickets)
            {
                EngineObjectRelease();
                return Fail(ENGINE_ERROR_LIMIT_EXCEEDED, "too many adding tickets exist (at most 256)");
            }
            adds_.Push(AddSlot());
        }
        adds_[slot].object = Move(object);
        adds_[slot].generation = NextGeneration();
        *ticket = MakeObjectHandle(kKindAddMany, adds_[slot].generation, slot);
    }
    runtime_->EnsureJobs().Submit(RunAddManyJob(this, raw));
    return ENGINE_OK;
}

namespace
{

// Keeps a ticket from being destroyed while a call waits on it.
class AddPin
{
public:
    AddPin(Mutex* mutex, AddManyObject* object) : mutex_(mutex), object_(object) {}
    AddPin(const AddPin&) = delete;
    AddPin& operator=(const AddPin&) = delete;
    ~AddPin()
    {
        LockGuard<Mutex> lock(*mutex_);
        --object_->waiters;
    }
    AddManyObject* Get() const { return object_; }

private:
    Mutex* mutex_;
    AddManyObject* object_;
};

}  // namespace

engine_status ResourceHost::AddWait(engine_handle ticket, u64 timeout_ms)
{
    if (jobs::Scheduler::InsideJob())
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "resource_add_wait is not allowed inside a job");
    }
    AddManyObject* found = nullptr;
    {
        LockGuard<Mutex> lock(table_mutex_);
        AddSlot* slot = nullptr;
        TRY_C(ResolveSlot(adds_, ticket, kKindAddMany, "add ticket", &slot));
        found = slot->object.Get();
        ++found->waiters;
    }
    AddPin pin(&table_mutex_, found);
    const u64 start = os::MonotonicTimeNanos();
    const bool forever = timeout_ms == 0xFFFFFFFFFFFFFFFFull;
    while (!pin.Get()->done.Load(MemoryOrder::Acquire))
    {
        if (!forever && (os::MonotonicTimeNanos() - start) / 1000000u >= timeout_ms)
        {
            return Fail(ENGINE_ERROR_NOT_READY, "the adding has not finished");
        }
        ThisThread::SleepForMicroseconds(100);
    }
    return ENGINE_OK;
}

engine_status ResourceHost::AddFinish(engine_handle ticket, usize* failed_index)
{
    UniquePtr<AddManyObject> taken;
    {
        LockGuard<Mutex> lock(table_mutex_);
        AddSlot* slot = nullptr;
        TRY_C(ResolveSlot(adds_, ticket, kKindAddMany, "add ticket", &slot));
        if (!slot->object->done.Load(MemoryOrder::Acquire))
        {
            return Fail(ENGINE_ERROR_NOT_READY, "the adding has not finished");
        }
        if (slot->object->waiters != 0)
        {
            return Fail(ENGINE_ERROR_IN_USE, "another call is waiting on the ticket");
        }
        taken = Move(slot->object);
    }
    EngineObjectRelease();
    if (failed_index != nullptr)
    {
        *failed_index = taken->failed_index;
    }
    return taken->code == ENGINE_OK ? ENGINE_OK : Fail(taken->code, taken->message.CStr());
}

engine_status ResourceHost::AddDestroy(engine_handle ticket)
{
    UniquePtr<AddManyObject> dying;
    {
        LockGuard<Mutex> lock(table_mutex_);
        AddSlot* slot = nullptr;
        TRY_C(ResolveSlot(adds_, ticket, kKindAddMany, "add ticket", &slot));
        if (slot->object->waiters != 0)
        {
            return Fail(ENGINE_ERROR_IN_USE, "another call is waiting on the ticket");
        }
        dying = Move(slot->object);
    }
    while (!dying->done.Load(MemoryOrder::Acquire))  // the job still reads the object
    {
        ThisThread::SleepForMicroseconds(100);
    }
    dying = nullptr;
    EngineObjectRelease();
    return ENGINE_OK;
}

engine_status ResourceHost::Mounted(u64 id, engine_resource_info* out)
{
    if (out == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`out` is NULL");
    }
    if (StructSizeOf(out) < sizeof(engine_resource_info))
    {
        return Fail(ENGINE_ERROR_STRUCT_SIZE, "engine_resource_info.struct_size is smaller than the struct");
    }
    if (id == 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the resource id is zero");
    }
    const asset::ResourceId rid = asset::ResourceId::FromValue(id);
    asset::ResourceInfo info;
    const asset::Status status = memory_->Stat(rid, &info);
    if (status.Failed())
    {
        return FromStatus(status);
    }
    *out = engine_resource_info{};
    out->struct_size = sizeof(engine_resource_info);
    out->type = info.type.Tag();
    out->id = id;
    out->size = info.size;
    MemCopy(out->content_hash, info.hash.bytes, sizeof(out->content_hash));
    out->loaded = manager_->IsLoaded(rid) ? 1 : 0;
    return ENGINE_OK;
}

// ---- loading

void ResourceHost::FreshTables()
{
    if (tables_dirty_.Exchange(false, MemoryOrder::AcqRel))
    {
        manager_->Mounts().RefreshTablesAsync();  // a path load waits for the table; a resolve below waits itself
    }
}

engine_status ResourceHost::ResolveId(u64 id, engine_string path, asset::ResourceId* out)
{
    if (path.length != 0)
    {
        FreshTables();
        asset::Status status = manager_->Resolve(TextOf(path), out);
        if (status.Is(asset::ErrorCode::NotReady))
        {  // a table is being read (a change started it): wait for it
            const asset::Status refreshed = manager_->Mounts().RefreshTables();
            status = refreshed.Failed() ? refreshed : manager_->Resolve(TextOf(path), out);
        }
        return status.Failed() ? FromStatus(status) : ENGINE_OK;
    }
    if (id == 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the resource id is zero and no path is given");
    }
    *out = asset::ResourceId::FromValue(id);
    return ENGINE_OK;
}

engine_status ResourceHost::MakeResource(asset::HandleBase&& handle, u32 type, engine_handle* out)
{
    if (!EngineObjectAcquire())
    {
        return Fail(ENGINE_ERROR_STALE_HANDLE, "no engine exists");
    }
    UniquePtr<ResourceObject> object = MakeUnique<ResourceObject>();
    object->handle = static_cast<asset::HandleBase&&>(handle);
    object->type = type;
    LockGuard<Mutex> lock(table_mutex_);
    usize slot = 0;
    if (!FindFreeSlot(resources_, &slot))
    {
        EngineObjectRelease();
        return Fail(ENGINE_ERROR_LIMIT_EXCEEDED, "too many resources are loaded");
    }
    resources_[slot].object = static_cast<UniquePtr<ResourceObject>&&>(object);
    resources_[slot].generation = NextGeneration();
    *out = MakeObjectHandle(kKindResource, resources_[slot].generation, slot);
    return ENGINE_OK;
}

engine_status ResourceHost::Load(u64 id, engine_string path, u32 type, engine_handle* out)
{
    if (out == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`out` is NULL");
    }
    *out = engine_handle{};
    const asset::ResourceType resource_type = asset::ResourceType::FromTag(type);
    const u32 key = manager_->LoaderKeyOf(resource_type);
    if (key == 0)
    {
        return Fail(ENGINE_ERROR_UNSUPPORTED, "no loader is registered for the resource type");
    }
    asset::ResourceId rid;
    TRY_C(ResolveId(id, path, &rid));
    asset::HandleBase handle;
    asset::LoadFailure failure;
    asset::PrefetchedBlobs none;
    const asset::Status status = manager_->LoadPrefetchedRaw(rid, resource_type, key, none, &handle, &failure);
    if (status.Failed())
    {
        return FromStatus(status);
    }
    return MakeResource(static_cast<asset::HandleBase&&>(handle), type, out);
}

engine_status ResourceHost::Release(engine_handle resource)
{
    UniquePtr<ResourceObject> dying;
    {
        LockGuard<Mutex> lock(table_mutex_);
        ResourceSlot* slot = nullptr;
        TRY_C(ResolveSlot(resources_, resource, kKindResource, "resource", &slot));
        dying = static_cast<UniquePtr<ResourceObject>&&>(slot->object);
    }
    dying = nullptr;  // gives the manager its reference back
    EngineObjectRelease();
    return ENGINE_OK;
}

engine_status ResourceHost::StatusOf(engine_handle resource, engine_resource_info* out)
{
    if (out == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`out` is NULL");
    }
    if (StructSizeOf(out) < sizeof(engine_resource_info))
    {
        return Fail(ENGINE_ERROR_STRUCT_SIZE, "engine_resource_info.struct_size is smaller than the struct");
    }
    asset::ResourceId rid;
    u32 type = 0;
    bool loaded = false;
    {
        LockGuard<Mutex> lock(table_mutex_);
        ResourceSlot* slot = nullptr;
        TRY_C(ResolveSlot(resources_, resource, kKindResource, "resource", &slot));
        rid = slot->object->handle.Id();
        type = slot->object->type;
        loaded = slot->object->handle.IsValid();
    }
    *out = engine_resource_info{};
    out->struct_size = sizeof(engine_resource_info);
    out->type = type;
    out->id = rid.Value();
    out->loaded = loaded ? 1 : 0;
    asset::ResourceInfo info;
    if (memory_->Stat(rid, &info).Ok())
    {
        out->size = info.size;
        MemCopy(out->content_hash, info.hash.bytes, sizeof(out->content_hash));
    }
    return ENGINE_OK;
}

// ---- asynchronous loading

asset::AsyncLoader* ResourceHost::LoaderLocked()
{
    if (loader_.Get() == nullptr)
    {
        loader_ = MakeUnique<asset::AsyncLoader>(*manager_, *runtime_->EnsureJobs().GetScheduler());
    }
    return loader_.Get();
}

void ResourceHost::PumpLoads()
{
    LockGuard<Mutex> lock(loader_mutex_);
    if (loader_.Get() != nullptr)
    {
        loader_->Update(500, 0);
    }
}

engine_status ResourceHost::LoadAsync(u64 id, engine_string path, u32 type, u32 priority, engine_handle* ticket)
{
    if (ticket == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`ticket` is NULL");
    }
    *ticket = engine_handle{};
    if (priority > ENGINE_LOAD_BACKGROUND)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the priority is not an ENGINE_LOAD_* value");
    }
    const asset::ResourceType resource_type = asset::ResourceType::FromTag(type);
    const u32 key = manager_->LoaderKeyOf(resource_type);
    if (key == 0)
    {
        return Fail(ENGINE_ERROR_UNSUPPORTED, "no loader is registered for the resource type");
    }
    if (path.length == 0 && id == 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the resource id is zero and no path is given");
    }
    if (!EngineObjectAcquire())
    {
        return Fail(ENGINE_ERROR_STALE_HANDLE, "no engine exists");
    }
    UniquePtr<TicketObject> object = MakeUnique<TicketObject>();
    object->type = type;
    FreshTables();
    {
        LockGuard<Mutex> lock(loader_mutex_);
        asset::AsyncLoader* loader = LoaderLocked();
        const asset::LoadPriority load_priority = static_cast<asset::LoadPriority>(priority);
        object->ticket = path.length != 0
                             ? loader->LoadPathAsyncRaw(TextOf(path), resource_type, key, load_priority, nullptr)
                             : loader->LoadAsyncRaw(asset::ResourceId::FromValue(id), resource_type, key, load_priority,
                                                    nullptr);
    }
    LockGuard<Mutex> lock(table_mutex_);
    usize slot = 0;
    if (!FindFreeSlot(tickets_, &slot))
    {
        {  // a started request leaves the loader under its mutex (TicketDestroy)
            LockGuard<Mutex> loader_lock(loader_mutex_);
            object.Reset();
        }
        EngineObjectRelease();
        return Fail(ENGINE_ERROR_LIMIT_EXCEEDED, "too many tickets exist");
    }
    tickets_[slot].object = static_cast<UniquePtr<TicketObject>&&>(object);
    tickets_[slot].generation = NextGeneration();
    *ticket = MakeObjectHandle(kKindTicket, tickets_[slot].generation, slot);
    return ENGINE_OK;
}

namespace
{

// Keeps a ticket from being destroyed while a call uses it.
class TicketPin
{
public:
    TicketPin(Mutex* table_mutex, TicketObject* object) : table_mutex_(table_mutex), object_(object) {}
    TicketPin(const TicketPin&) = delete;
    TicketPin& operator=(const TicketPin&) = delete;
    ~TicketPin()
    {
        LockGuard<Mutex> lock(*table_mutex_);
        --object_->waiters;
    }
    TicketObject* Get() const { return object_; }

private:
    Mutex* table_mutex_;
    TicketObject* object_;
};

}  // namespace

// The calls below find the ticket, count themselves on it and leave the table mutex; ticket_destroy refuses while any
// is counted.
#define ENGINE_PIN_TICKET(handle, name)                                                                                \
    TicketObject* name##_object = nullptr;                                                                             \
    {                                                                                                                  \
        LockGuard<Mutex> table_lock(table_mutex_);                                                                     \
        TicketSlot* slot = nullptr;                                                                                    \
        TRY_C(ResolveSlot(tickets_, handle, kKindTicket, "ticket", &slot));                                            \
        name##_object = slot->object.Get();                                                                            \
        ++name##_object->waiters;                                                                                      \
    }                                                                                                                  \
    TicketPin name(&table_mutex_, name##_object)

engine_status ResourceHost::TicketPoll(engine_handle ticket, engine_ticket_info* out)
{
    if (out == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`out` is NULL");
    }
    if (StructSizeOf(out) < sizeof(engine_ticket_info))
    {
        return Fail(ENGINE_ERROR_STRUCT_SIZE, "engine_ticket_info.struct_size is smaller than the struct");
    }
    ENGINE_PIN_TICKET(ticket, pin);
    u32 read = 0;
    u32 total = 0;
    bool done = false;
    {
        LockGuard<Mutex> lock(loader_mutex_);
        LoaderLocked()->Update(0, 0);
        done = pin.Get()->ticket.IsDone();
        pin.Get()->ticket.Progress(&read, &total);
    }
    *out = engine_ticket_info{};
    out->struct_size = sizeof(engine_ticket_info);
    out->done = done ? 1 : 0;
    out->blobs_read = read;
    out->blobs_total = total;
    return ENGINE_OK;
}

engine_status ResourceHost::TicketWait(engine_handle ticket, u64 timeout_ms)
{
    if (jobs::Scheduler::InsideJob())
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "ticket_wait is not allowed inside a job or a release callback");
    }
    ENGINE_PIN_TICKET(ticket, pin);
    const u64 start = os::MonotonicTimeNanos();
    const bool forever = timeout_ms == 0xFFFFFFFFFFFFFFFFull;
    for (;;)
    {
        bool done = false;
        {
            LockGuard<Mutex> lock(loader_mutex_);
            LoaderLocked()->Update(0, 0);
            done = pin.Get()->ticket.IsDone();
        }
        if (done)
        {
            return ENGINE_OK;
        }
        if (!forever && (os::MonotonicTimeNanos() - start) / 1000000u >= timeout_ms)
        {
            return Fail(ENGINE_ERROR_NOT_READY, "the load has not finished");
        }
        ThisThread::SleepForMicroseconds(200);
    }
}

engine_status ResourceHost::TicketFinish(engine_handle ticket, engine_handle* resource)
{
    if (resource == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`resource` is NULL");
    }
    *resource = engine_handle{};
    ENGINE_PIN_TICKET(ticket, pin);
    asset::HandleBase handle;
    asset::Status status;
    {
        LockGuard<Mutex> lock(loader_mutex_);
        LoaderLocked()->Update(0, 0);
        status = pin.Get()->ticket.Finish(&handle);
    }
    if (status.Failed())
    {
        return FromStatus(status);
    }
    return MakeResource(static_cast<asset::HandleBase&&>(handle), pin.Get()->type, resource);
}

engine_status ResourceHost::TicketCancel(engine_handle ticket)
{
    ENGINE_PIN_TICKET(ticket, pin);
    pin.Get()->ticket.Cancel();
    return ENGINE_OK;
}

engine_status ResourceHost::TicketDestroy(engine_handle ticket)
{
    UniquePtr<TicketObject> dying;
    {
        LockGuard<Mutex> lock(table_mutex_);
        TicketSlot* slot = nullptr;
        TRY_C(ResolveSlot(tickets_, ticket, kKindTicket, "ticket", &slot));
        if (slot->object->waiters != 0)
        {
            SetLastError("in_use: %u call(s) are using the ticket", static_cast<unsigned>(slot->object->waiters));
            return ENGINE_ERROR_IN_USE;
        }
        dying = static_cast<UniquePtr<TicketObject>&&>(slot->object);
    }
    {
        LockGuard<Mutex> lock(loader_mutex_);  // a ticket leaves the loader's requests under the loader's owner rule
        dying = nullptr;
    }
    EngineObjectRelease();
    return ENGINE_OK;
}

// ---- the functions of the table

// The host of the engine a handle names, made on first use.
engine_status HostOfEngine(engine_handle engine, ResourceHost** out)
{
    Runtime* runtime = nullptr;
    TRY_C(ResolveRuntime(engine, &runtime));
    *out = runtime->Resources();
    return ENGINE_OK;
}

// The host of the engine that exists, for the calls that have only a handle of an object.
engine_status HostOfCurrent(engine_handle handle, ResourceHost** out)
{
    *out = nullptr;
    Runtime* runtime = CurrentRuntime();
    if (runtime == nullptr)
    {
        return Fail(handle.value == 0 ? ENGINE_ERROR_INVALID_ARGUMENT : ENGINE_ERROR_STALE_HANDLE, "no engine exists");
    }
    *out = runtime->ResourcesIfCreated();
    if (*out == nullptr)
    {  // no resource or ticket was ever made on this engine, so no handle of theirs can be valid
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the handle was never issued by this engine");
    }
    return ENGINE_OK;
}

engine_status ResourceAddFn(engine_handle engine, const engine_resource_add_info* info)
{
    ResourceHost* host = nullptr;
    TRY_C(HostOfEngine(engine, &host));
    return host->Add(info);
}

engine_status HeightMapCreateFn(engine_handle engine, u64 id, const u16* samples, u32 width, u32 height, f32 range_min,
                                f32 range_max, f32 cell_size)
{
    ResourceHost* host = nullptr;
    TRY_C(HostOfEngine(engine, &host));
    return host->AddHeightMap(id, samples, width, height, range_min, range_max, cell_size);
}

engine_status ResourceReplaceFn(engine_handle engine, const engine_resource_add_info* info, engine_buffer* dependents)
{
    ResourceHost* host = nullptr;
    TRY_C(HostOfEngine(engine, &host));
    return host->Replace(info, dependents);
}

engine_status ResourceRemoveFn(engine_handle engine, u64 id, u32 flags, engine_buffer* dependents)
{
    ResourceHost* host = nullptr;
    TRY_C(HostOfEngine(engine, &host));
    return host->Remove(id, flags, dependents);
}

engine_status ResourceMountedFn(engine_handle engine, u64 id, engine_resource_info* out)
{
    ResourceHost* host = nullptr;
    TRY_C(HostOfEngine(engine, &host));
    return host->Mounted(id, out);
}

engine_status ResourceReadFn(engine_handle engine, u64 id, u64 max_size, engine_buffer* out)
{
    ResourceHost* host = nullptr;
    TRY_C(HostOfEngine(engine, &host));
    return host->Read(id, max_size, out);
}

engine_status ResourceAddManyFn(engine_handle engine, const engine_resource_add_info* infos, usize count,
                                usize* failed_index)
{
    ResourceHost* host = nullptr;
    TRY_C(HostOfEngine(engine, &host));
    return host->AddMany(infos, count, failed_index);
}

engine_status ResourceAddManyAsyncFn(engine_handle engine, const engine_resource_add_info* infos, usize count,
                                     engine_handle* ticket)
{
    ResourceHost* host = nullptr;
    TRY_C(HostOfEngine(engine, &host));
    return host->AddManyAsync(infos, count, ticket);
}

engine_status ResourceAddWaitFn(engine_handle ticket, u64 timeout_ms)
{
    ResourceHost* host = nullptr;
    TRY_C(HostOfCurrent(ticket, &host));
    return host->AddWait(ticket, timeout_ms);
}

engine_status ResourceAddFinishFn(engine_handle ticket, usize* failed_index)
{
    ResourceHost* host = nullptr;
    TRY_C(HostOfCurrent(ticket, &host));
    return host->AddFinish(ticket, failed_index);
}

engine_status ResourceAddDestroyFn(engine_handle ticket)
{
    ResourceHost* host = nullptr;
    TRY_C(HostOfCurrent(ticket, &host));
    return host->AddDestroy(ticket);
}

namespace
{

u64 Mix(u64 hash, u64 value)
{
    return Fnv1a64Builder(hash).AddU64Bytes(value).Value();
}

}  // namespace

u64 ImportFormatVersionFn()
{
    u64 hash = kFnv1a64Basis;
    hash = Mix(hash, 1);  // the layout of this fingerprint
    hash = Mix(hash, ImporterOutputVersion());
    hash = Mix(hash, asset::kContainerVersion);
    const asset::TypeRegistry types;
    for (usize i = 0; i < types.Size(); ++i)
    {
        hash = Mix(hash, types.At(i).type.Tag());
        hash = Mix(hash, types.At(i).current_version);
    }
    return hash;
}

engine_status ResourceLoadFn(engine_handle engine, u64 id, engine_string path, u32 type, engine_handle* out)
{
    ResourceHost* host = nullptr;
    TRY_C(HostOfEngine(engine, &host));
    return host->Load(id, path, type, out);
}

engine_status ResourceReleaseFn(engine_handle resource)
{
    ResourceHost* host = nullptr;
    TRY_C(HostOfCurrent(resource, &host));
    return host->Release(resource);
}

engine_status ResourceStatusFn(engine_handle resource, engine_resource_info* out)
{
    ResourceHost* host = nullptr;
    TRY_C(HostOfCurrent(resource, &host));
    return host->StatusOf(resource, out);
}

engine_status ResourceLoadAsyncFn(engine_handle engine, u64 id, engine_string path, u32 type, u32 priority,
                                  engine_handle* ticket)
{
    ResourceHost* host = nullptr;
    TRY_C(HostOfEngine(engine, &host));
    return host->LoadAsync(id, path, type, priority, ticket);
}

engine_status TicketPollFn(engine_handle ticket, engine_ticket_info* out)
{
    ResourceHost* host = nullptr;
    TRY_C(HostOfCurrent(ticket, &host));
    return host->TicketPoll(ticket, out);
}

engine_status TicketWaitFn(engine_handle ticket, u64 timeout_ms)
{
    ResourceHost* host = nullptr;
    TRY_C(HostOfCurrent(ticket, &host));
    return host->TicketWait(ticket, timeout_ms);
}

engine_status TicketFinishFn(engine_handle ticket, engine_handle* resource)
{
    ResourceHost* host = nullptr;
    TRY_C(HostOfCurrent(ticket, &host));
    return host->TicketFinish(ticket, resource);
}

engine_status TicketCancelFn(engine_handle ticket)
{
    ResourceHost* host = nullptr;
    TRY_C(HostOfCurrent(ticket, &host));
    return host->TicketCancel(ticket);
}

engine_status TicketDestroyFn(engine_handle ticket)
{
    ResourceHost* host = nullptr;
    TRY_C(HostOfCurrent(ticket, &host));
    return host->TicketDestroy(ticket);
}

}  // namespace vse::engine_c
