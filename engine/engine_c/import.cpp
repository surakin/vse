#include "engine_c/import.h"

#include "engine_c/engine_internal.h"
#include "engine_c/engine_objects.h"
#include "engine_c/handles.h"
#include "engine_c/resources.h"
#include "engine_c/runtime.h"
#include "engine_c/status_map.h"

#if ENGINE_C_WITH_IMPORTER
#include "asset_import/gltf_async.h"
#include "asset_import/gltf_import.h"
#include "asset_import/import_settings.h"
#include "cstr/mem_ops.h"
#include "jobs/scheduler.h"
#include "mem/heap_allocator.h"
#include "os/os.h"
#include "string/string.h"
#include "sync/thread.h"
#endif

// Returns from the function when a call of the C API's own kind fails.
#define TRY_IMPORT(expression)                                                                                         \
    do                                                                                                                 \
    {                                                                                                                  \
        const engine_status try_import_status = (expression);                                                          \
        if (try_import_status != ENGINE_OK)                                                                            \
        {                                                                                                              \
            return try_import_status;                                                                                  \
        }                                                                                                              \
    } while (0)

namespace engine_placeholder::engine_c
{

#if ENGINE_C_WITH_IMPORTER

namespace
{

constexpr usize kMaxImportTickets = 256;  // each is a job of the engine's scheduler, whose queues are bounded
constexpr u32 kKnownFlags = ENGINE_IMPORT_FLAT_NORMALS | ENGINE_IMPORT_DROP_MORPH_TARGETS | ENGINE_IMPORT_EMIT_LEVEL;
constexpr usize kMaxPrefixBytes = 200;

engine_status FromAsset(const asset::Status& status)
{
    return Fail(FromAssetError(status.Code()), status.Message());
}

// Reads an external file of the glTF through the host's callback.
class HostSource final : public asset::GltfSource
{
public:
    void Set(engine_import_read_file_fn function, void* user)
    {
        function_ = function;
        user_ = user;
    }

    asset::Status Read(StringView name, u64 max_bytes, Vector<byte>* out) override
    {
        if (function_ == nullptr)
        {
            return asset::Status::Make(asset::ErrorCode::NotFound,
                                       "the glTF names an external file and the host gave no read_file");
        }
        engine_import_file file = {};
        engine_string text;
        text.data = name.Data();
        text.length = name.Length();
        const engine_status status = function_(user_, text, max_bytes, &file);
        if (status != ENGINE_OK)
        {
            switch (status)
            {
            case ENGINE_ERROR_NOT_FOUND:
                return asset::Status::Make(asset::ErrorCode::NotFound, "the host has no such external file");
            case ENGINE_ERROR_IO:
                return asset::Status::Make(asset::ErrorCode::Io, "the host could not read an external file");
            case ENGINE_ERROR_TOO_LARGE:
                return asset::Status::Make(asset::ErrorCode::TooLarge, "an external file is too large");
            case ENGINE_ERROR_CANCELLED:
                return asset::Status::Make(asset::ErrorCode::Cancelled, "the host cancelled reading an external file");
            default: return asset::Status::Make(asset::ErrorCode::Other, "the host failed to read an external file");
            }
        }
        asset::Status result;
        if (file.data == nullptr && file.size != 0)
        {
            result = asset::Status::Make(asset::ErrorCode::Invalid, "an external file has a size but no data");
        }
        else if (file.size > max_bytes)
        {
            result = asset::Status::Make(asset::ErrorCode::TooLarge, "an external file is longer than the limit");
        }
        else
        {
            out->Resize(file.size);
            if (file.size != 0)
            {
                MemCopy(out->Data(), file.data, file.size);
            }
        }
        if (file.release != nullptr)
        {
            file.release(file.release_user, file.data, file.size);
        }
        return result;
    }

private:
    engine_import_read_file_fn function_ = nullptr;
    void* user_ = nullptr;
};

}  // namespace

// One import: what its options point to, and the ticket of an asynchronous one.
struct ImportObject
{
    explicit ImportObject(u64 seed) : seeded_ids(seed) {}

    HostSource source;
    asset::RandomIdSource random_ids;
    asset::SeededIdSource seeded_ids;
    asset::ImportManifest manifest;
    String<HeapAllocator> prefix{HeapAllocator::Default()};
    asset::ImportOptions options;
    UniquePtr<asset::ImportTicket> ticket;
    bool finished = false;  // the outcome was taken (guarded by the host's mutex)
    u32 waiters = 0;        // calls using the ticket (guarded by the host's mutex)
};

namespace
{

// The id seed of the host's options: read only when the host's struct is big enough to hold it (Prepare refuses a
// smaller one).
u64 SeedOf(const engine_import_options* given)
{
    if (given == nullptr || StructSizeOf(given) < sizeof(engine_import_options))
    {
        return 0;
    }
    return given->id_seed;
}

// Checks the host's options and makes the importer's from them (defaults, the engine's limits, the host's own).
engine_status Prepare(const engine_import_options* given, const ResourceConfig& config, ImportObject* object)
{
    engine_import_options o = {};
    if (given != nullptr)
    {
        if (StructSizeOf(given) < sizeof(engine_import_options))
        {
            return Fail(ENGINE_ERROR_STRUCT_SIZE, "engine_import_options.struct_size is smaller than the struct");
        }
        o = *given;
    }
    if ((o.flags & ~kKnownFlags) != 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the import flags have an unknown bit");
    }
    if (!(o.scale >= 0.0f) || !(o.scale <= 1.0e12f) || !(o.clip_rate >= 0.0f) || !(o.clip_rate <= 1.0e6f))
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "scale or clip_rate is negative, NaN or absurdly large");
    }
    if ((o.path_prefix.data == nullptr && o.path_prefix.length != 0) ||
        (o.manifest.data == nullptr && o.manifest.length != 0))
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "a string has a NULL pointer and a length");
    }
    if (o.path_prefix.length > kMaxPrefixBytes || (o.path_prefix.length != 0 && o.path_prefix.data[0] != '/'))
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "path_prefix must be empty or a path that starts with '/'");
    }
    asset::ImportOptions& out = object->options;
    out = asset::ImportOptions();
    const ImportLimitValues& c = config.import_limits;
    if (c.max_file_bytes != 0)
    {
        out.limits.max_file_bytes = c.max_file_bytes;
        out.limits.max_json_bytes = c.max_json_bytes;
        out.limits.max_buffer_bytes = c.max_buffer_bytes;
        out.limits.max_output_bytes = c.max_output_bytes;
        out.limits.max_pixels = c.max_pixels;
        out.limits.max_nodes = c.max_nodes;
        out.limits.max_accessors = c.max_accessors;
        out.limits.max_images = c.max_images;
    }
    if (o.max_file_bytes != 0)
    {
        out.limits.max_file_bytes = o.max_file_bytes;
    }
    if (o.max_json_bytes != 0)
    {
        out.limits.max_json_bytes = o.max_json_bytes;
    }
    if (o.max_buffer_bytes != 0)
    {
        out.limits.max_buffer_bytes = o.max_buffer_bytes;
    }
    if (o.max_output_bytes != 0)
    {
        out.limits.max_output_bytes = o.max_output_bytes;
    }
    if (o.max_pixels != 0)
    {
        out.limits.max_pixels = o.max_pixels;
    }
    if (o.max_nodes != 0)
    {
        out.limits.max_nodes = o.max_nodes;
    }
    if (o.max_accessors != 0)
    {
        out.limits.max_accessors = o.max_accessors;
    }
    if (o.max_images != 0)
    {
        out.limits.max_images = o.max_images;
    }
    out.flat_normals = (o.flags & ENGINE_IMPORT_FLAT_NORMALS) != 0;
    out.emit_level = (o.flags & ENGINE_IMPORT_EMIT_LEVEL) != 0;
    out.drop_morph_targets = (o.flags & ENGINE_IMPORT_DROP_MORPH_TARGETS) != 0;
    if (o.scale != 0.0f)
    {
        out.scale = o.scale;
    }
    if (o.clip_rate != 0.0f)
    {
        out.clip_rate = o.clip_rate;
    }
    if (o.path_prefix.length != 0)
    {
        object->prefix.Append(StringView(o.path_prefix.data, o.path_prefix.length));
        out.path_prefix = StringView(object->prefix.CStr(), object->prefix.Length());
    }
    out.ids = o.id_seed != 0 ? static_cast<asset::IdSource*>(&object->seeded_ids)
                             : static_cast<asset::IdSource*>(&object->random_ids);
    if (o.manifest.length != 0)
    {
        asset::GltfError error;
        const asset::Status parsed = object->manifest.Parse(StringView(o.manifest.data, o.manifest.length), &error);
        if (parsed.Failed())
        {
            return Fail(ENGINE_ERROR_INVALID_ARGUMENT, asset::GltfMessage(error, parsed));
        }
        out.manifest = &object->manifest;
    }
    object->source.Set(o.read_file, o.read_user);
    return ENGINE_OK;
}

// Fills the host's result from an import's.
void FillResult(const asset::ImportResult& made, engine_import_result* out)
{
    Vector<u64> ids;
    Vector<u32> types;
    String<HeapAllocator> paths{HeapAllocator::Default()};
    for (usize i = 0; i < made.resources.Size(); ++i)
    {
        ids.Push(made.resources[i].id.Value());
        types.Push(made.resources[i].type.Tag());
        paths.Append(StringView(made.resources[i].path.CStr(), made.resources[i].path.Length()));
        paths.Append('\n');
    }
    String<HeapAllocator> warnings{HeapAllocator::Default()};
    for (usize i = 0; i < made.report.notes.Size(); ++i)
    {
        warnings.Append(StringView(made.report.notes[i].CStr(), made.report.notes[i].Length()));
        warnings.Append('\n');
    }
    String<HeapAllocator> manifest{HeapAllocator::Default()};
    made.manifest.Format(&manifest);
    const bool has_summary = StructSizeOf(out) >=
                             sizeof(engine_import_result);  // `summary` came after the first version
    const u32 host_size = out->struct_size;
    // Clear only what the host's struct holds: an older host passes a smaller one.
    MemSet(out, 0, host_size < sizeof(engine_import_result) ? host_size : sizeof(engine_import_result));
    out->struct_size = host_size;
    out->resource_count = static_cast<u32>(made.resources.Size());
    out->warning_count = static_cast<u32>(made.report.notes.Size());
    MakeTextBuffer(StringView(reinterpret_cast<const char*>(ids.Data()), ids.Size() * sizeof(u64)), &out->ids);
    MakeTextBuffer(StringView(reinterpret_cast<const char*>(types.Data()), types.Size() * sizeof(u32)), &out->types);
    MakeTextBuffer(StringView(paths.CStr(), paths.Length()), &out->paths);
    MakeTextBuffer(StringView(warnings.CStr(), warnings.Length()), &out->warnings);
    MakeTextBuffer(StringView(manifest.CStr(), manifest.Length()), &out->manifest);
    if (has_summary)
    {
        MakeTextBuffer(StringView(made.summary.CStr(), made.summary.Length()), &out->summary);
    }
}

// The first version of engine_import_result ended before `summary`: four u32 and the five buffers.
constexpr usize kResultFirstVersionSize = 4 * sizeof(u32) + 5 * sizeof(engine_buffer);

engine_status CheckResultOut(engine_import_result* result)
{
    if (result == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`result` is NULL");
    }
    if (StructSizeOf(result) < kResultFirstVersionSize)
    {
        return Fail(ENGINE_ERROR_STRUCT_SIZE, "engine_import_result.struct_size is smaller than the struct");
    }
    const u32 host_size = result->struct_size;
    MemSet(result, 0, host_size < sizeof(engine_import_result) ? host_size : sizeof(engine_import_result));
    result->struct_size = host_size;
    return ENGINE_OK;
}

// Keeps a ticket from being destroyed while a call uses it.
class ImportPin
{
public:
    ImportPin(Mutex* mutex, ImportObject* object) : mutex_(mutex), object_(object) {}
    ImportPin(const ImportPin&) = delete;
    ImportPin& operator=(const ImportPin&) = delete;
    ~ImportPin()
    {
        LockGuard<Mutex> lock(*mutex_);
        --object_->waiters;
    }
    ImportObject* Get() const { return object_; }

private:
    Mutex* mutex_;
    ImportObject* object_;
};

}  // namespace

bool ImporterBuilt()
{
    return true;
}

u32 ImporterOutputVersion()
{
    return asset::kGltfImporterOutputVersion;
}

void RegisterImporterSettings(settings::Registry* registry)
{
    asset::RegisterImportSettings(registry);
}

void ReadImporterSettings(const settings::Registry& registry, ResourceConfig* config)
{
    const asset::ImportLimits limits = asset::ImportLimitsFromSettings(registry);
    ImportLimitValues& c = config->import_limits;
    c.max_file_bytes = limits.max_file_bytes;
    c.max_json_bytes = limits.max_json_bytes;
    c.max_buffer_bytes = limits.max_buffer_bytes;
    c.max_output_bytes = limits.max_output_bytes;
    c.max_pixels = limits.max_pixels;
    c.max_nodes = limits.max_nodes;
    c.max_accessors = limits.max_accessors;
    c.max_images = limits.max_images;
}

ImportHost::ImportHost(Runtime& runtime, asset::HostMemoryBackend& backend, Atomic<bool>* tables_dirty)
    : runtime_(&runtime), backend_(&backend), tables_dirty_(tables_dirty)
{
}

ImportHost::~ImportHost()
{
    // The tickets are gone (the engine refuses to be destroyed while any is out); a slot that is left would cancel its
    // job and wait for it as it is destroyed.
}

engine_status ImportHost::Import(const void* bytes, usize size, const engine_import_options* options,
                                 engine_import_result* result)
{
    TRY_IMPORT(CheckResultOut(result));
    if (bytes == nullptr || size == 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`bytes` is NULL or empty");
    }
    UniquePtr<ImportObject> object = MakeUnique<ImportObject>(SeedOf(options));
    TRY_IMPORT(Prepare(options, runtime_->resource_config, object.Get()));
    asset::ImportResult made;
    asset::GltfError error;
    const asset::Status imported = asset::ImportGltf(ByteSpan(static_cast<const byte*>(bytes), size), object->source,
                                                     object->options, &made, &error);
    if (imported.Failed())
    {
        return Fail(FromAssetError(imported.Code()), asset::GltfMessage(error, imported));
    }
    const asset::Status added = asset::ImportIntoBackend(*backend_, made);
    if (added.Failed())
    {
        return FromAsset(added);
    }
    tables_dirty_->Store(true, MemoryOrder::Release);
    FillResult(made, result);
    return ENGINE_OK;
}

engine_status ImportHost::ImportAsync(const void* bytes, usize size, const engine_import_options* options,
                                      engine_handle* ticket)
{
    if (ticket == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`ticket` is NULL");
    }
    *ticket = engine_handle{};
    if (bytes == nullptr || size == 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`bytes` is NULL or empty");
    }
    UniquePtr<ImportObject> object = MakeUnique<ImportObject>(SeedOf(options));
    TRY_IMPORT(Prepare(options, runtime_->resource_config, object.Get()));
    if (!EngineObjectAcquire())
    {
        return Fail(ENGINE_ERROR_STALE_HANDLE, "no engine exists");
    }
    Vector<byte> file;
    file.Resize(size);
    MemCopy(file.Data(), bytes, size);
    LockGuard<Mutex> lock(mutex_);
    usize slot = 0;
    for (; slot < slots_.Size(); ++slot)
    {
        if (slots_[slot].object.Get() == nullptr)
        {
            break;
        }
    }
    if (slot == slots_.Size())
    {
        if (slots_.Size() >= kMaxImportTickets)
        {
            EngineObjectRelease();
            return Fail(ENGINE_ERROR_LIMIT_EXCEEDED, "too many import tickets exist (at most 256)");
        }
        slots_.Push(Slot());
    }
    object->ticket = asset::ImportGltfAsync(*runtime_->EnsureJobs().GetScheduler(), static_cast<Vector<byte>&&>(file),
                                            object->source, object->options);
    if (object->ticket.Get() == nullptr)
    {
        EngineObjectRelease();
        return Fail(ENGINE_ERROR_INTERNAL, "the import could not be started");
    }
    slots_[slot].object = static_cast<UniquePtr<ImportObject>&&>(object);
    slots_[slot].generation = NextGeneration();
    *ticket = MakeObjectHandle(kKindImport, slots_[slot].generation, slot);
    return ENGINE_OK;
}

// The calls below find the ticket, count themselves on it and leave the mutex; destroy refuses while any is counted.
#define ENGINE_PIN_IMPORT(handle, name)                                                                                \
    ImportObject* name##_object = nullptr;                                                                             \
    {                                                                                                                  \
        LockGuard<Mutex> pin_lock(mutex_);                                                                             \
        Slot* slot = nullptr;                                                                                          \
        TRY_IMPORT(ResolveSlot(slots_, handle, kKindImport, "import ticket", &slot));                                  \
        name##_object = slot->object.Get();                                                                            \
        ++name##_object->waiters;                                                                                      \
    }                                                                                                                  \
    ImportPin name(&mutex_, name##_object)

engine_status ImportHost::Poll(engine_handle ticket, engine_import_info* out)
{
    if (out == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`out` is NULL");
    }
    if (StructSizeOf(out) < sizeof(engine_import_info))
    {
        return Fail(ENGINE_ERROR_STRUCT_SIZE, "engine_import_info.struct_size is smaller than the struct");
    }
    ENGINE_PIN_IMPORT(ticket, pin);
    const bool done = pin.Get()->ticket->IsDone();
    const f32 progress = pin.Get()->ticket->Progress();
    *out = engine_import_info{};
    out->struct_size = sizeof(engine_import_info);
    out->done = done ? 1 : 0;
    out->steps_total = asset::kImportSteps;
    out->steps_done = done && progress >= 1.0f
                          ? asset::kImportSteps
                          : static_cast<u32>(progress * static_cast<f32>(asset::kImportSteps) + 0.5f);
    return ENGINE_OK;
}

engine_status ImportHost::Wait(engine_handle ticket, u64 timeout_ms)
{
    if (jobs::Scheduler::InsideJob())
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "import_wait is not allowed inside a job");
    }
    ENGINE_PIN_IMPORT(ticket, pin);
    const u64 start = os::MonotonicTimeNanos();
    const bool forever = timeout_ms == 0xFFFFFFFFFFFFFFFFull;
    while (!pin.Get()->ticket->IsDone())
    {
        if (!forever && (os::MonotonicTimeNanos() - start) / 1000000u >= timeout_ms)
        {
            return Fail(ENGINE_ERROR_NOT_READY, "the import has not finished");
        }
        ThisThread::SleepForMicroseconds(200);
    }
    return ENGINE_OK;
}

engine_status ImportHost::Finish(engine_handle ticket, engine_import_result* result)
{
    TRY_IMPORT(CheckResultOut(result));
    ENGINE_PIN_IMPORT(ticket, pin);
    if (!pin.Get()->ticket->IsDone())
    {
        return Fail(ENGINE_ERROR_NOT_READY, "the import has not finished");
    }
    {
        LockGuard<Mutex> lock(mutex_);
        if (pin.Get()->finished)
        {
            return Fail(ENGINE_ERROR_NOT_FOUND, "the outcome of this import was already taken");
        }
        pin.Get()->finished = true;
    }
    asset::ImportResult made;
    asset::GltfError error;
    const asset::Status imported = pin.Get()->ticket->Finish(&made, &error);
    if (imported.Failed())
    {
        return Fail(FromAssetError(imported.Code()), asset::GltfMessage(error, imported));
    }
    const asset::Status added = asset::ImportIntoBackend(*backend_, made);
    if (added.Failed())
    {
        return FromAsset(added);
    }
    tables_dirty_->Store(true, MemoryOrder::Release);
    FillResult(made, result);
    return ENGINE_OK;
}

engine_status ImportHost::Cancel(engine_handle ticket)
{
    ENGINE_PIN_IMPORT(ticket, pin);
    pin.Get()->ticket->Cancel();
    return ENGINE_OK;
}

engine_status ImportHost::Destroy(engine_handle ticket)
{
    UniquePtr<ImportObject> dying;
    {
        LockGuard<Mutex> lock(mutex_);
        Slot* slot = nullptr;
        TRY_IMPORT(ResolveSlot(slots_, ticket, kKindImport, "import ticket", &slot));
        if (slot->object->waiters != 0)
        {
            SetLastError("in_use: %u call(s) are using the ticket", static_cast<unsigned>(slot->object->waiters));
            return ENGINE_ERROR_IN_USE;
        }
        dying = static_cast<UniquePtr<ImportObject>&&>(slot->object);
    }
    dying = nullptr;  // cancels the import and waits for its job
    EngineObjectRelease();
    return ENGINE_OK;
}

#else  // no importer in this build

bool ImporterBuilt()
{
    return false;
}

u32 ImporterOutputVersion()
{
    return 0;
}

void RegisterImporterSettings(settings::Registry*) {}

void ReadImporterSettings(const settings::Registry&, ResourceConfig*) {}

struct ImportObject
{
};

ImportHost::ImportHost(Runtime&, asset::HostMemoryBackend&, Atomic<bool>*) {}

ImportHost::~ImportHost() {}

namespace
{
engine_status NoImporter()
{
    return Fail(ENGINE_ERROR_UNSUPPORTED, "this build of the library has no glTF importer");
}
}  // namespace

engine_status ImportHost::Import(const void*, usize, const engine_import_options*, engine_import_result* result)
{
    if (result != nullptr && StructSizeOf(result) >= sizeof(engine_import_result))
    {
        *result = engine_import_result{};
        result->struct_size = sizeof(engine_import_result);
    }
    return NoImporter();
}

engine_status ImportHost::ImportAsync(const void*, usize, const engine_import_options*, engine_handle* ticket)
{
    if (ticket != nullptr)
    {
        *ticket = engine_handle{};
    }
    return NoImporter();
}

engine_status ImportHost::Poll(engine_handle, engine_import_info*)
{
    return NoImporter();
}

engine_status ImportHost::Wait(engine_handle, u64)
{
    return NoImporter();
}

engine_status ImportHost::Finish(engine_handle, engine_import_result*)
{
    return NoImporter();
}

engine_status ImportHost::Cancel(engine_handle)
{
    return NoImporter();
}

engine_status ImportHost::Destroy(engine_handle)
{
    return NoImporter();
}

#endif  // ENGINE_C_WITH_IMPORTER

// ---- the functions of the table

engine_status ImportGltfFn(engine_handle engine, const void* bytes, usize size, const engine_import_options* options,
                           engine_import_result* result)
{
    ResourceHost* host = nullptr;
    TRY_IMPORT(HostOfEngine(engine, &host));
    return host->Imports().Import(bytes, size, options, result);
}

engine_status ImportGltfAsyncFn(engine_handle engine, const void* bytes, usize size,
                                const engine_import_options* options, engine_handle* ticket)
{
    ResourceHost* host = nullptr;
    TRY_IMPORT(HostOfEngine(engine, &host));
    return host->Imports().ImportAsync(bytes, size, options, ticket);
}

engine_status ImportPollFn(engine_handle ticket, engine_import_info* out)
{
    ResourceHost* host = nullptr;
    TRY_IMPORT(HostOfCurrent(ticket, &host));
    return host->Imports().Poll(ticket, out);
}

engine_status ImportWaitFn(engine_handle ticket, u64 timeout_ms)
{
    ResourceHost* host = nullptr;
    TRY_IMPORT(HostOfCurrent(ticket, &host));
    return host->Imports().Wait(ticket, timeout_ms);
}

engine_status ImportFinishFn(engine_handle ticket, engine_import_result* result)
{
    ResourceHost* host = nullptr;
    TRY_IMPORT(HostOfCurrent(ticket, &host));
    return host->Imports().Finish(ticket, result);
}

engine_status ImportCancelFn(engine_handle ticket)
{
    ResourceHost* host = nullptr;
    TRY_IMPORT(HostOfCurrent(ticket, &host));
    return host->Imports().Cancel(ticket);
}

engine_status ImportDestroyFn(engine_handle ticket)
{
    ResourceHost* host = nullptr;
    TRY_IMPORT(HostOfCurrent(ticket, &host));
    return host->Imports().Destroy(ticket);
}

}  // namespace engine_placeholder::engine_c
