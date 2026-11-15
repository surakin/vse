// The engine's C API, version 1 (Phase 23 Day 346): the one exported function and the table it returns. The conventions
// are the comment at the top of engine_c.h. Nothing here lets a C++ type, an exception or an allocation cross the
// boundary: every function checks its arguments and its handle, turns the engine's own result types into an
// `engine_status` (status_map.h) and keeps the text of a failure for `last_error`.
#include "build_defs.h"
#include "asset/asset_settings.h"
#include "asset/resource_manager.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "jobs/frame_pool.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"
#include "cstr/varargs.h"
#include "lang/new.h"
#include "engine/engine_c.h"
#include "debug/crash_handler.h"
#include "debug/stack_trace.h"
#include "engine_c/engine_internal.h"
#include "engine_c/engine_objects.h"
#include "engine_c/import.h"
#include "engine_c/resources.h"
#include "engine_c/runtime.h"
#include "engine_c/status_map.h"
#include "engine_c/surface.h"
#include "log/diagnostics_hooks.h"
#include "log/log.h"
#include "mem/heap_allocator.h"
#include "settings/desc.h"
#include "settings/engine_settings.h"
#include "settings/env_source.h"
#include "settings/memory_store.h"
#include "settings/settings.h"
#include "string/string.h"
#include "sync/mutex.h"
#include "types.h"
#if ENGINE_TEST_EXTENSION
#include "engine_c/engine_test_extension.h"
#include "engine_c/test_state.h"
#endif

namespace engine_placeholder::engine_c
{

// The failure text of the calling thread. Plain bytes, so the thread-local has no destructor (nothing to run at thread
// exit, Phase 23 Day 345).
namespace
{
constexpr usize kErrorBytes = 256;
thread_local char t_last_error[kErrorBytes];
}  // namespace

void SetLastError(const char* format, ...)
{
    VarArgs args;
    ENGINE_VA_START(args, format);
    VFormatTo(t_last_error, kErrorBytes, format, args);
    ENGINE_VA_END(args);
}

engine_status Fail(engine_status status, const char* what)
{
    SetLastError("%s: %s", StatusName(status), what);
    return status;
}

namespace
{

void ReleaseTextBuffer(engine_buffer* buffer)
{
    if (buffer->owner != nullptr)
    {
        HeapAllocator::Default().Free(buffer->owner, buffer->size + 1, 1);
    }
    buffer->data = nullptr;
    buffer->size = 0;
    buffer->owner = nullptr;
    buffer->release = nullptr;
}

}  // namespace

// Fills `out` with a copy of `text` (and a NUL after it, which `size` does not count).
void MakeTextBuffer(StringView text, engine_buffer* out)
{
    char* block = static_cast<char*>(HeapAllocator::Default().Alloc(text.Length() + 1, 1));
    MemCopy(block, text.Data(), text.Length());
    block[text.Length()] = '\0';
    out->data = block;
    out->size = text.Length();
    out->owner = block;
    out->release = &ReleaseTextBuffer;
}

namespace
{

using Text = String<HeapAllocator>;

StringView View(engine_string s)
{
    return s.data == nullptr ? StringView() : StringView(s.data, s.length);
}

// What one engine holds. Created by engine_create, destroyed by engine_destroy; the registry and the layers of the
// settings service live exactly as long.
struct Engine
{
    settings::Registry registry;
    // The host's settings as the read-only layer wants them: NUL-terminated copies, kept for the layer's lifetime.
    Vector<Text> key_text;
    Vector<Text> value_text;
    Vector<settings::DefaultEntry> entries;
    Vector<Text> arg_text;
    Vector<const char*> argv;
    engine_log_fn log = nullptr;
    void* log_user = nullptr;
    bool suppress_console = false;
    // The log channels as they were before the engine applied its `log.*` settings; destroy puts them back.
    struct ChannelState
    {
        LogChannel* channel;
        LogVerbosity verbosity;
        bool enabled;
    };
    Vector<ChannelState> channels;
    // Objects the engine's C API has handed out and the host has not given back (worlds, loaded resources, imports;
    // Days 349 to 351 register theirs here).
    u32 outstanding = 0;
    bool crash_installed = false;            // the engine's crash handler is installed (destroy takes it away)
    UniquePtr<Runtime> runtime;              // worlds, counters and the job system (Day 349)
    UniquePtr<settings::Settings> settings;  // last: destroyed first, before the entries and the registry it reads
};

struct State
{
    RecursiveMutex mutex;  // recursive: a log callback may call back into the API on the thread that logged
    UniquePtr<Engine> engine;
    u32 generation = 0;       // of the engine that exists, or of the last one that did
    bool destroying = false;  // an engine is between leaving the slot and its log callback being gone: no new engine
                              // yet (it would install its own observer)
};

State& GetState()
{
    static State state;  // function-local: constructed on first use, no dependence on static initialization order
    return state;
}

constexpr u64 kSlot = 1;  // one engine per process: slot 1

engine_handle MakeHandle(u32 generation)
{
    engine_handle h;
    h.value = (static_cast<u64>(generation) << 32) | kSlot;
    return h;
}

// Finds the engine a handle names, or says why not. The caller holds the state's lock.
engine_status Resolve(State& state, engine_handle handle, Engine** out)
{
    *out = nullptr;
    if (handle.value == 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the engine handle is zero");
    }
    const u64 slot = handle.value & 0xFFFFFFFFull;
    const u32 generation = static_cast<u32>(handle.value >> 32);
    if (slot != kSlot || generation == 0 || generation > state.generation)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the engine handle was never issued");
    }
    if (state.engine.Get() == nullptr || generation != state.generation)
    {
        return Fail(ENGINE_ERROR_STALE_HANDLE, "the engine was destroyed");
    }
    *out = state.engine.Get();
    return ENGINE_OK;
}

// ---- the log
// -----------------------------------------------------------------------------------------------------------------------------------

bool ObserveLog(void* user, const char* channel, LogVerbosity level, StringView message)
{
    const Engine* engine = static_cast<const Engine*>(user);
    engine_string channel_text;
    channel_text.data = channel;
    channel_text.length = CStrLength(channel);
    engine_string message_text;
    message_text.data = message.Data();
    message_text.length = message.Length();
    engine->log(engine->log_user, static_cast<engine_log_level>(level), channel_text, message_text);
    return engine->suppress_console;
}

// ---- the table's functions
// -----------------------------------------------------------------------------------------------------------------------

engine_string StatusNameFn(engine_status status)
{
    engine_string s;
    s.data = StatusName(status);
    s.length = CStrLength(s.data);
    return s;
}

// The first layer problem as the failure text.
engine_status FailWithProblem(const settings::LayerProblem& p)
{
    SetLastError("settings: %s%s%s: %s", p.layer, p.key[0] != '\0' ? ": " : "", p.key, p.message);
    return ENGINE_ERROR_INVALID_ARGUMENT;
}

engine_status CreateFn(const engine_create_info* info, engine_handle* out)
{
    if (out == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`out` is NULL");
    }
    out->value = 0;
    if (info == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`info` is NULL");
    }
    // The first version of the struct ends after `log_user`.
    constexpr usize kFirstVersionSize = sizeof(engine_create_info);
    if (info->struct_size < kFirstVersionSize)
    {
        return Fail(ENGINE_ERROR_STRUCT_SIZE, "engine_create_info.struct_size is smaller than the struct");
    }
    if ((info->flags & ~ENGINE_CREATE_FLAGS_MASK) != 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "engine_create_info.flags has an unknown bit");
    }
    if ((info->settings == nullptr && info->setting_count != 0) || (info->args == nullptr && info->arg_count != 0))
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "a list is NULL with a count above zero");
    }
    for (usize i = 0; i < info->setting_count; ++i)
    {
        if ((info->settings[i].key.data == nullptr && info->settings[i].key.length != 0) ||
            (info->settings[i].value.data == nullptr && info->settings[i].value.length != 0))
        {
            return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "a setting string has a NULL pointer and a length");
        }
    }
    for (usize i = 0; i < info->arg_count; ++i)
    {
        if (info->args[i].data == nullptr && info->args[i].length != 0)
        {
            return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "an argument has a NULL pointer and a length");
        }
    }
    State& state = GetState();
    LockGuard<RecursiveMutex> lock(state.mutex);
    if (state.engine.Get() != nullptr || state.destroying)
    {
        return Fail(ENGINE_ERROR_LIMIT_EXCEEDED, "an engine already exists in this process");
    }
    UniquePtr<Engine> engine = MakeUnique<Engine>();
    engine->log = info->log;
    engine->log_user = info->log_user;
    engine->suppress_console = (info->flags & ENGINE_CREATE_LOG_SUPPRESS_CONSOLE) != 0;

    settings::RegisterEngineSettings(&engine->registry);
    settings::RegisterLogSettings(&engine->registry);
    asset::RegisterAssetSettings(&engine->registry);
    asset::RegisterManagerSettings(&engine->registry);
    engine_c::RegisterImporterSettings(&engine->registry);
    {
        settings::SettingDesc dump = settings::MakeText("crash.dump_directory", "");
        dump.description = "the directory a crash dump goes to (empty: the user's crash directory)";
        dump.group = "crash";
        dump.max_text = 255;
        engine->registry.Register(dump);
    }
    engine->runtime = MakeUnique<Runtime>();
    engine->settings = MakeUnique<settings::Settings>(engine->registry);

    // The host's entries as NUL-terminated text (the layer borrows them).
    for (usize i = 0; i < info->setting_count; ++i)
    {
        engine->key_text.Push(Text(HeapAllocator::Default(), View(info->settings[i].key)));
        engine->value_text.Push(Text(HeapAllocator::Default(), View(info->settings[i].value)));
    }
    for (usize i = 0; i < info->setting_count; ++i)
    {
        settings::DefaultEntry entry;
        entry.key = engine->key_text[i].CStr();
        entry.text = engine->value_text[i].CStr();
        engine->entries.Push(entry);
    }
    for (usize i = 0; i < info->setting_count; ++i)
    {  // a key the engine does not declare is NOT_FOUND, by the registry's own answer rather than by the text of a note
        if (!engine->registry.Find(View(info->settings[i].key)).IsValid())
        {
            SetLastError("not_found: no setting `%.*s`", static_cast<int>(info->settings[i].key.length),
                         info->settings[i].key.data == nullptr ? "" : info->settings[i].key.data);
            return ENGINE_ERROR_NOT_FOUND;
        }
    }
    for (usize i = 0; i < info->arg_count; ++i)
    {
        engine->arg_text.Push(Text(HeapAllocator::Default(), View(info->args[i])));
    }
    for (usize i = 0; i < engine->arg_text.Size(); ++i)
    {
        engine->argv.Push(engine->arg_text[i].CStr());
    }

    // The layers by hand, so that nothing outside the host's input is read: not the environment, not the user's
    // settings file, not a policy.
    settings::LayerStack& stack = engine->settings->Layers();
    stack.AddLayer(StringView("host settings"), settings::LayerRole::System,
                   MakeUnique<settings::ReadOnlyConfigStore>(engine->entries.Data(), engine->entries.Size()));
    stack.AddLayer(StringView("engine"), settings::LayerRole::User, MakeUnique<settings::MemoryConfigStore>());
    Vector<Text> command_line_errors;
    if (engine->argv.Size() > 0)
    {
        UniquePtr<settings::CommandLineSource> cli = MakeUnique<settings::CommandLineSource>(
            engine->registry, static_cast<int>(engine->argv.Size()), engine->argv.Data());
        for (usize i = 0; i < cli->Result().errors.Size(); ++i)
        {
            command_line_errors.Push(Text(HeapAllocator::Default(), cli->Result().errors[i].View()));
        }
        stack.AddLayer(StringView("command line"), settings::LayerRole::CommandLine,
                       settings::ConfigStorePtr(static_cast<UniquePtr<settings::CommandLineSource>&&>(cli)));
    }
    const settings::LoadReport report = engine->settings->LoadFromLayers();
    if (command_line_errors.Size() > 0)
    {
        SetLastError("command line: %s", command_line_errors[0].CStr());
        return ENGINE_ERROR_INVALID_ARGUMENT;
    }
    if (report.problems.Size() > 0)
    {
        return FailWithProblem(report.problems[0]);
    }
    if (report.notes.Size() > 0)
    {  // an unknown key is a note: a typo in a host's entry must not pass silently
        return FailWithProblem(report.notes[0]);
    }
    {  // what the resource host will be made from, read now (a later setting_set does not change a host that exists)
        ResourceConfig& config = engine->runtime->resource_config;
        config.memory_bytes = asset::MemoryBackendBytesFromSettings(engine->registry);
        config.priority = asset::MemoryMountPriorityFromSettings(engine->registry);
        const asset::ManagerConfig manager = asset::ManagerConfigFromSettings(engine->registry);
        config.buffer_pool_bytes = manager.buffer_pool_bytes;
        config.cache_bytes = manager.cache_bytes;
        config.allow_fallbacks = manager.allow_fallbacks;
        config.verify_on_load = manager.verify_on_load;
        config.max_inflight = manager.max_inflight;
        config.max_queued = manager.max_queued;
        engine_c::ReadImporterSettings(engine->registry, &config);
    }
    for (LogChannel* channel = LogChannel::First(); channel != nullptr; channel = channel->Next())
    {
        engine->channels.Push(Engine::ChannelState{channel, channel->Verbosity(), channel->Enabled()});
    }
    settings::AttachLogSettings(engine->settings.Get());

    ++state.generation;
    Engine* raw = engine.Get();
    state.engine = static_cast<UniquePtr<Engine>&&>(engine);
    if (raw->log != nullptr)
    {
        SetLogObserver(&ObserveLog, raw);
    }
    *out = MakeHandle(state.generation);
    return ENGINE_OK;
}

engine_status DestroyFn(engine_handle handle)
{
    State& state = GetState();
    UniquePtr<Engine> dying;
    {
        LockGuard<RecursiveMutex> lock(state.mutex);
        Engine* engine = nullptr;
        const engine_status status = Resolve(state, handle, &engine);
        if (status != ENGINE_OK)
        {
            return status;
        }
        if (engine->outstanding != 0)
        {  // refused, and nothing changed: the host gives its objects back and calls again
            SetLastError("in_use: %u object(s) of the engine are still outstanding",
                         static_cast<unsigned>(engine->outstanding));
            return ENGINE_ERROR_IN_USE;
        }
        dying = static_cast<UniquePtr<Engine>&&>(state.engine);
        state.destroying = true;
    }
    // Outside the lock: SetLogObserver waits for callbacks in flight, and a callback may be waiting for the lock to
    // call back into the API.
    if (dying->log != nullptr)
    {
        SetLogObserver(nullptr, nullptr);
    }
    for (usize i = 0; i < dying->channels.Size(); ++i)
    {  // the process's log channels are as the host had them before create
        dying->channels[i].channel->SetVerbosity(dying->channels[i].verbosity);
        dying->channels[i].channel->SetEnabled(dying->channels[i].enabled);
    }
    if (dying->crash_installed)
    {
        debug::UninstallCrashHandler();
    }
    dying = nullptr;
    {
        LockGuard<RecursiveMutex> lock(state.mutex);
        state.destroying = false;
    }
    return ENGINE_OK;
}

engine_status GetInfoFn(engine_handle handle, engine_info* out)
{
    if (out == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`out` is NULL");
    }
    constexpr usize kFirstVersionSize = 8;  // struct_size and setting_count; `caps` came with Day 351
    if (out->struct_size < kFirstVersionSize)
    {
        return Fail(ENGINE_ERROR_STRUCT_SIZE, "engine_info.struct_size is smaller than the struct");
    }
    State& state = GetState();
    LockGuard<RecursiveMutex> lock(state.mutex);
    Engine* engine = nullptr;
    const engine_status status = Resolve(state, handle, &engine);
    if (status != ENGINE_OK)
    {
        return status;
    }
    const u32 host_size = out->struct_size;
    out->setting_count = engine->registry.Count();
    out->struct_size = static_cast<u32>(kFirstVersionSize);
    if (host_size >= sizeof(engine_info))
    {
        out->caps = (engine_c::ImporterBuilt() ? ENGINE_CAP_IMPORT_GLTF : 0u) |
                    (engine_c::SurfaceKindSupported(ENGINE_SURFACE_X11) ? ENGINE_CAP_SURFACE_X11 : 0u) |
                    (engine_c::SurfaceKindSupported(ENGINE_SURFACE_WAYLAND) ? ENGINE_CAP_SURFACE_WAYLAND : 0u) |
                    (engine_c::SurfaceKindSupported(ENGINE_SURFACE_COCOA) ? ENGINE_CAP_SURFACE_COCOA : 0u) |
                    (engine_c::SurfaceKindSupported(ENGINE_SURFACE_WIN32) ? ENGINE_CAP_SURFACE_WIN32 : 0u) |
                    (engine_c::SurfaceKindSupported(ENGINE_SURFACE_X11) ||
                             engine_c::SurfaceKindSupported(ENGINE_SURFACE_COCOA) ||
                             engine_c::SurfaceKindSupported(ENGINE_SURFACE_WIN32)
                         ? (ENGINE_CAP_READBACK | ENGINE_CAP_WORLD_ON_SURFACE)
                         : 0u);
        out->reserved = 0;
        out->struct_size = static_cast<u32>(sizeof(engine_info));
    }
    return ENGINE_OK;
}

engine_status SettingSetFn(engine_handle handle, engine_string key, engine_string value)
{
    if ((key.data == nullptr && key.length != 0) || (value.data == nullptr && value.length != 0))
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "a string has a NULL pointer and a length");
    }
    State& state = GetState();
    LockGuard<RecursiveMutex> lock(state.mutex);
    Engine* engine = nullptr;
    const engine_status resolved = Resolve(state, handle, &engine);
    if (resolved != ENGINE_OK)
    {
        return resolved;
    }
    const settings::SetResult result = engine->settings->SetText(View(key), View(value));
    const engine_status status = FromSetError(result.error);
    if (status != ENGINE_OK)
    {
        SetLastError("%s: setting `%.*s` was not changed", StatusName(status), static_cast<int>(key.length),
                     key.data == nullptr ? "" : key.data);
    }
    return status;
}

engine_status SettingGetFn(engine_handle handle, engine_string key, engine_buffer* out)
{
    if (out == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`out` is NULL");
    }
    out->data = nullptr;
    out->size = 0;
    out->owner = nullptr;
    out->release = nullptr;
    if (key.data == nullptr && key.length != 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "a string has a NULL pointer and a length");
    }
    State& state = GetState();
    LockGuard<RecursiveMutex> lock(state.mutex);
    Engine* engine = nullptr;
    const engine_status resolved = Resolve(state, handle, &engine);
    if (resolved != ENGINE_OK)
    {
        return resolved;
    }
    const settings::SettingId id = engine->registry.Find(View(key));
    if (!id.IsValid())
    {
        return Fail(ENGINE_ERROR_NOT_FOUND, "no such setting");
    }
    char text[settings::kMaxValueTextBytes];
    const usize length = settings::FormatValue(*engine->registry.Desc(id), engine->registry.Current(id), text,
                                               sizeof(text));
    MakeTextBuffer(StringView(text, length), out);
    return ENGINE_OK;
}

engine_status LastErrorFn(engine_buffer* out)
{
    if (out == nullptr)
    {
        return ENGINE_ERROR_INVALID_ARGUMENT;
    }
    MakeTextBuffer(StringView(t_last_error, CStrLength(t_last_error)), out);
    return ENGINE_OK;
}

engine_status CrashHandlerInstallFn(engine_handle handle)
{
    State& state = GetState();
    LockGuard<RecursiveMutex> lock(state.mutex);
    Engine* engine = nullptr;
    const engine_status resolved = Resolve(state, handle, &engine);
    if (resolved != ENGINE_OK)
    {
        return resolved;
    }
    if (engine->crash_installed)
    {
        return Fail(ENGINE_ERROR_ALREADY_EXISTS, "the engine's crash handler is installed");
    }
    const settings::SettingId id = engine->registry.Find(StringView("crash.dump_directory"));
    const settings::Value current = engine->registry.Current(id);  // a copy: the text below views into it
    const StringView text = current.AsText();
    char directory[settings::kMaxValueTextBytes];
    const usize length = text.Length() < sizeof(directory) ? text.Length() : sizeof(directory) - 1;
    MemCopy(directory, text.Data(), length);
    directory[length] = '\0';
    debug::InitDebugFacilities();
    debug::CrashHandlerOptions options;
    options.dump_directory = length == 0 ? nullptr : directory;
    switch (debug::InstallCrashHandler(options))
    {
    case debug::CrashHandlerResult::Installed: engine->crash_installed = true; return ENGINE_OK;
    case debug::CrashHandlerResult::AlreadyInstalled:
        return Fail(ENGINE_ERROR_ALREADY_EXISTS, "a crash handler of this process is installed already");
    case debug::CrashHandlerResult::DirectoryFailed: return Fail(ENGINE_ERROR_IO, "the dump directory cannot be used");
    case debug::CrashHandlerResult::InitFailed:
        return Fail(ENGINE_ERROR_INTERNAL, "the crash handler library did not start");
    case debug::CrashHandlerResult::Disabled:
    case debug::CrashHandlerResult::Unsupported: break;
    }
    return Fail(ENGINE_ERROR_UNSUPPORTED, "this platform has no crash handler");
}

engine_status CrashHandlerUninstallFn(engine_handle handle)
{
    State& state = GetState();
    LockGuard<RecursiveMutex> lock(state.mutex);
    Engine* engine = nullptr;
    const engine_status resolved = Resolve(state, handle, &engine);
    if (resolved != ENGINE_OK)
    {
        return resolved;
    }
    if (!engine->crash_installed)
    {
        return Fail(ENGINE_ERROR_NOT_FOUND, "the engine's crash handler is not installed");
    }
    debug::UninstallCrashHandler();
    engine->crash_installed = false;
    return ENGINE_OK;
}

void ThreadDetachFn()
{
    NotifyThreadEnd();
}

constexpr u32 SanitizerOfLibrary()
{
#if ENGINE_SANITIZER_ADDRESS
    return ENGINE_LIB_SANITIZER_ADDRESS;
#elif ENGINE_SANITIZER_THREAD
    return ENGINE_LIB_SANITIZER_THREAD;
#else
    return ENGINE_LIB_SANITIZER_NONE;
#endif
}

const void* GetExtensionFn(engine_string name)
{
#if ENGINE_TEST_EXTENSION
    if (View(name) == StringView("engine.test_state"))
    {
        return TestStateExtension();
    }
#else
    (void)name;
#endif
    return nullptr;
}

}  // namespace

bool EngineObjectAcquire()
{
    State& state = GetState();
    LockGuard<RecursiveMutex> lock(state.mutex);
    if (state.engine.Get() == nullptr)
    {
        return false;
    }
    ++state.engine->outstanding;
    return true;
}

void EngineObjectRelease()
{
    State& state = GetState();
    LockGuard<RecursiveMutex> lock(state.mutex);
    if (state.engine.Get() != nullptr && state.engine->outstanding > 0)
    {
        --state.engine->outstanding;
    }
}

engine_status ResolveRuntime(engine_handle engine, Runtime** out)
{
    *out = nullptr;
    State& state = GetState();
    LockGuard<RecursiveMutex> lock(state.mutex);
    Engine* resolved = nullptr;
    const engine_status status = Resolve(state, engine, &resolved);
    if (status == ENGINE_OK)
    {
        *out = resolved->runtime.Get();
    }
    return status;
}

Runtime* CurrentRuntime()
{
    State& state = GetState();
    LockGuard<RecursiveMutex> lock(state.mutex);
    return state.engine.Get() == nullptr ? nullptr : state.engine->runtime.Get();
}

}  // namespace engine_placeholder::engine_c

using namespace engine_placeholder;

extern "C"
{
    ENGINE_API engine_status engine_get_api(u32 wanted_major, u32 wanted_minor, usize table_size,
                                            const engine_api** out)
    {
        if (out == nullptr)
        {
            return ENGINE_ERROR_INVALID_ARGUMENT;
        }
        *out = nullptr;
        // The table's first four fields (size, version, sanitizer) are what a host needs to even tell what it got.
        if (table_size < ENGINE_OFFSET_OF(engine_api, status_name))
        {
            return engine_c::Fail(ENGINE_ERROR_INVALID_ARGUMENT, "table_size is below the table's header");
        }
        if (wanted_major != ENGINE_ABI_VERSION_MAJOR || wanted_minor > ENGINE_ABI_VERSION_MINOR)
        {
            return engine_c::Fail(ENGINE_ERROR_UNSUPPORTED_VERSION, "the library does not serve that ABI version");
        }
        // Static, constant-initialized: the same table for every call, valid while the library is loaded.
        static const engine_api table = {
            static_cast<u32>(sizeof(engine_api)),
            ENGINE_ABI_VERSION_MAJOR,
            ENGINE_ABI_VERSION_MINOR,
            engine_c::SanitizerOfLibrary(),
            &engine_c::StatusNameFn,
            &engine_c::CreateFn,
            &engine_c::DestroyFn,
            &engine_c::GetInfoFn,
            &engine_c::SettingSetFn,
            &engine_c::SettingGetFn,
            &engine_c::LastErrorFn,
            &engine_c::GetExtensionFn,
            &engine_c::ThreadDetachFn,
            &engine_c::FrameFn,
            &engine_c::WorldCreateFn,
            &engine_c::WorldDestroyFn,
            &engine_c::EntityCreateFn,
            &engine_c::EntityDestroyFn,
            &engine_c::ComponentSetFn,
            &engine_c::ComponentGetFn,
            &engine_c::ComponentRemoveFn,
            &engine_c::ComponentHasFn,
            &engine_c::EntityNameSetFn,
            &engine_c::EntityNameGetFn,
            &engine_c::QueryFn,
            &engine_c::CounterCreateFn,
            &engine_c::CounterDestroyFn,
            &engine_c::JobsSubmitFn,
            &engine_c::CounterValueFn,
            &engine_c::CounterWaitFn,
            &engine_c::CrashHandlerInstallFn,
            &engine_c::CrashHandlerUninstallFn,
            &engine_c::ResourceAddFn,
            &engine_c::ResourceReplaceFn,
            &engine_c::ResourceRemoveFn,
            &engine_c::ResourceMountedFn,
            &engine_c::ResourceLoadFn,
            &engine_c::ResourceReleaseFn,
            &engine_c::ResourceStatusFn,
            &engine_c::ResourceLoadAsyncFn,
            &engine_c::TicketPollFn,
            &engine_c::TicketWaitFn,
            &engine_c::TicketFinishFn,
            &engine_c::TicketCancelFn,
            &engine_c::TicketDestroyFn,
            &engine_c::ImportGltfFn,
            &engine_c::ImportGltfAsyncFn,
            &engine_c::ImportPollFn,
            &engine_c::ImportWaitFn,
            &engine_c::ImportFinishFn,
            &engine_c::ImportCancelFn,
            &engine_c::ImportDestroyFn,
            &engine_c::SurfaceCreateFn,
            &engine_c::SurfaceDestroyFn,
            &engine_c::SurfaceInputFn,
            &engine_c::SurfaceResizedFn,
            &engine_c::SurfaceRebindFn,
            &engine_c::SurfaceSetBackgroundFn,
            &engine_c::SurfaceStateFn,
            &engine_c::SurfaceReadPixelsFn,
            &engine_c::PixelsFinishFn,
            &engine_c::PixelsDestroyFn,
            &engine_c::SurfaceSetWorldFn,
            &engine_c::SurfaceSetCameraFn,
            &engine_c::SurfacePixelRayFn,
            &engine_c::SurfaceProjectFn,
            &engine_c::WorldSetEnvironmentFn,
            &engine_c::EntitySetParentFn,
            &engine_c::EntityGetParentFn,
            &engine_c::EntityFindFn,
            &engine_c::EntityFindAllFn,
            &engine_c::EntitySetSlotOverrideFn,
            &engine_c::EntityClearSlotOverrideFn,
            &engine_c::AnimationPlayFn,
            &engine_c::AnimationSetSpeedFn,
            &engine_c::AnimationStopFn,
            &engine_c::AnimationTimeFn,
            &engine_c::SkeletonFindJointFn,
            &engine_c::SkeletonJointTransformFn,
            &engine_c::EntityAttachToJointFn,
        };
        *out = &table;
        return ENGINE_OK;
    }

}  // extern "C"

#if ENGINE_C_UNLOAD_HOOK && !ENGINE_PLATFORM_WINDOWS
// Runs when the shared library is unloaded (and at process exit, once the host's own static destructors are done with
// the engine): the one pool of the job system that is immortal in a program gives its memory back, so a host that loads
// and unloads the library does not accumulate it. By the contract of the C API every thread that used the engine has
// ended or called thread_detach, and the engine is destroyed.
__attribute__((destructor)) static void EngineLibraryUnloaded()
{
    engine_placeholder::jobs::FramePool::DestroyGlobalAtUnload();
}
#elif ENGINE_C_UNLOAD_HOOK
// Windows (Day 354): the same, as the destructor of a static object, which the C runtime of engine.dll runs when the
// DLL is detached (FreeLibrary, or process exit). engine.dll has no DllMain of its own: loading it runs the C runtime's
// start and the constructors of statics, none of which starts a thread or allocates from the engine (host_dllmain.c
// checks it), and the work of unloading is only this.
namespace
{
struct EngineLibraryUnloadHook
{
    EngineLibraryUnloadHook() = default;
    EngineLibraryUnloadHook(const EngineLibraryUnloadHook&) = delete;
    EngineLibraryUnloadHook& operator=(const EngineLibraryUnloadHook&) = delete;
    ~EngineLibraryUnloadHook() { engine_placeholder::jobs::FramePool::DestroyGlobalAtUnload(); }
};
const EngineLibraryUnloadHook g_engine_library_unload_hook;
}  // namespace
#endif
