#include "engine_c/resources.h"

#include "asset/cpu_loaders.h"
#include "asset/content_hash.h"
#include "engine_c/engine_internal.h"
#include "engine_c/engine_objects.h"
#include "engine_c/handles.h"
#include "engine_c/runtime.h"
#include "engine_c/status_map.h"
#include "jobs/job.h"
#include "jobs/scheduler.h"
#include "log/assert.h"
#include "os/os.h"
#include "sync/thread.h"

namespace engine_placeholder::engine_c
{

struct ResourceHost::ReleaseRecord
{
    ResourceHost* host = nullptr;
    engine_resource_release_fn function = nullptr;
    void* user = nullptr;
    const void* data = nullptr;
    usize size = 0;
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

engine_status ResourceHost::PinClip(u64 id, u64 skeleton_id)
{
    LockGuard<Mutex> lock(pin_mutex_);
    const asset::SkeletonView* skeleton = manager_->Find<asset::SkeletonView>(
        asset::ResourceId::FromValue(skeleton_id));
    if (skeleton == nullptr)
    {
        return Fail(ENGINE_ERROR_NOT_FOUND, "the skeleton of the entity is not loaded");
    }
    asset::Handle<asset::ClipView> handle;
    const asset::Status loaded = manager_->Load<asset::ClipView>(asset::ResourceId::FromValue(id), &handle);
    if (!loaded.Ok())
    {
        return Fail(FromAssetError(loaded.Code()), loaded.Message());
    }
    if (!handle.Get()->Bind(*skeleton).Ok())
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the clip was not made for the entity's skeleton");
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

}  // namespace engine_placeholder::engine_c
