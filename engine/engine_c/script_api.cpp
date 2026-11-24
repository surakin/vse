// The script half of the C API (Phase 27 Day 471).
#include "engine_c/script_api.h"

#include "engine_c/engine_internal.h"
#include "engine_c/handles.h"
#include "engine_c/runtime.h"

#if ENGINE_C_WITH_SCRIPT
#include "containers/shared_bytes.h"
#include "containers/vector.h"
#include "cstr/format.h"
#include "cstr/cstr.h"
#include "cstr/mem_ops.h"
#include "ecs/world.h"
#include "script/backend.h"
#include "script/bindings.h"
#include "script/luajit/luajit_backend.h"
#include "script/script_runtime.h"
#include "script/event_bus.h"
#include "script/script_settings.h"
#include "script/task_pool.h"
#include "script/value.h"
#include "script/service_slots.h"
#include "engine_c/script_scene.h"
#include "script_physics/physics_bindings.h"
#include "script_physics/physics_events.h"
#include "settings/setting.h"
#include "string_view.h"
#include "sync/atomic.h"
#endif

namespace vse::engine_c
{

#if ENGINE_C_WITH_SCRIPT

namespace detail  // named, not anonymous: GCC refuses a class member whose type has internal linkage
{

constexpr usize kMaxScripts = 1024;
constexpr usize kMaxScriptBytes = usize{16} << 20;
constexpr u32 kMaxArity = 16;
constexpr usize kMaxFunctionName = 128;
constexpr usize kHostResultItems = 64;
constexpr usize kHostResultBytes = 4096;
constexpr u32 kInputDepth = 32;

struct HostFunction
{
    engine_script_fn fn = nullptr;
    void* user = nullptr;
    Atomic<u64>* counter = nullptr;
};

// ---- values: the pool on the C side, script::Value on the engine's

using script::Value;
using script::ValueKind;

struct Reader
{
    const engine_value_pool* pool;
    u32 nodes = 0;
    usize bytes_copied = 0;
};

bool ReadValue(Reader& r, const engine_value& v, u32 depth, Value* out, const char** why)
{
    if (depth > kInputDepth)
    {
        *why = "a value is nested deeper than 32 levels";
        return false;
    }
    if (++r.nodes > (1u << 16))
    {
        *why = "more than 65536 values";
        return false;
    }
    const engine_value_pool& pool = *r.pool;
    switch (v.tag)
    {
    case ENGINE_VALUE_NIL: *out = Value(); return true;
    case ENGINE_VALUE_BOOL: *out = Value::FromBool(v.i != 0); return true;
    case ENGINE_VALUE_INT: *out = Value::FromInt(v.i); return true;
    case ENGINE_VALUE_NUMBER: *out = Value::FromFloat(v.f); return true;
    case ENGINE_VALUE_ENTITY:
        if (v.i == 0)
        {
            *why = "an entity value has the key 0";
            return false;
        }
        *out = Value::FromEntity(ecs::Entity::FromU64(static_cast<u64>(v.i)));
        return true;
    case ENGINE_VALUE_STRING:
    case ENGINE_VALUE_BYTES:
    {
        const usize limit = v.tag == ENGINE_VALUE_STRING ? (usize{1} << 20) : kMaxScriptBytes;
        if (v.i < 0 || static_cast<u64>(v.i) > pool.byte_count || v.count > pool.byte_count - static_cast<usize>(v.i) ||
            v.count > limit)
        {
            *why = "a string or blob lies outside the bytes of the pool, or is over its limit";
            return false;
        }
        r.bytes_copied += v.count;
        if (r.bytes_copied > kMaxScriptBytes)
        {
            *why = "the strings and blobs of the pool add up to more than 16 MiB";
            return false;
        }
        const u8* data = pool.bytes + static_cast<usize>(v.i);
        *out = v.tag == ENGINE_VALUE_STRING ? Value::FromText(StringView(reinterpret_cast<const char*>(data), v.count))
                                            : Value::FromBytes(Span<const u8>(data, v.count));
        return true;
    }
    case ENGINE_VALUE_ARRAY:
    {
        if (v.i < 0 || static_cast<u64>(v.i) > pool.item_count || v.count > pool.item_count - static_cast<usize>(v.i))
        {
            *why = "an array lies outside the items of the pool";
            return false;
        }
        Value array = Value::MakeArray();
        for (u32 k = 0; k < v.count; ++k)
        {
            Value item;
            if (!ReadValue(r, pool.items[static_cast<usize>(v.i) + k], depth + 1, &item, why))
            {
                return false;
            }
            if (!array.Push(Move(item)))
            {
                *why = "an array is too long or nests too deep";
                return false;
            }
        }
        *out = Move(array);
        return true;
    }
    case ENGINE_VALUE_MAP:
    {
        if (v.i < 0 || static_cast<u64>(v.i) > pool.item_count ||
            usize{v.count} * 2 > pool.item_count - static_cast<usize>(v.i))
        {
            *why = "a map lies outside the items of the pool";
            return false;
        }
        Value map = Value::MakeMap();
        for (u32 k = 0; k < v.count; ++k)
        {
            const engine_value& key = pool.items[static_cast<usize>(v.i) + usize{k} * 2];
            if (key.tag != ENGINE_VALUE_STRING || key.i < 0 || static_cast<u64>(key.i) > pool.byte_count ||
                key.count > pool.byte_count - static_cast<usize>(key.i) || key.count > 1024)
            {
                *why = "a map key is not a string of the pool (at most 1024 bytes)";
                return false;
            }
            Value item;
            if (!ReadValue(r, pool.items[static_cast<usize>(v.i) + usize{k} * 2 + 1], depth + 1, &item, why))
            {
                return false;
            }
            const StringView name(reinterpret_cast<const char*>(pool.bytes + static_cast<usize>(key.i)), key.count);
            if (!map.Set(name, Move(item)))
            {
                *why = "a map is too long or nests too deep";
                return false;
            }
        }
        *out = Move(map);
        return true;
    }
    default: *why = "a value has an unknown tag"; return false;
    }
}

// Checks the shape of an input pool (the arrays and the counts), then reads its roots.
engine_status ReadPool(const engine_value_pool* pool, Vector<Value>* out)
{
    out->Clear();
    if (pool == nullptr)
    {
        return ENGINE_OK;
    }
    if (StructSizeOf(pool) < sizeof(engine_value_pool))
    {
        return Fail(ENGINE_ERROR_STRUCT_SIZE, "engine_value_pool.struct_size is smaller than the struct");
    }
    if (pool->root_count > pool->item_count || pool->item_count > (usize{1} << 16) ||
        (pool->item_count != 0 && pool->items == nullptr) || (pool->byte_count != 0 && pool->bytes == nullptr) ||
        pool->byte_count > kMaxScriptBytes)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the value pool's counts are out of range or its arrays are NULL");
    }
    Reader reader{pool};
    for (u32 i = 0; i < pool->root_count; ++i)
    {
        Value value;
        const char* why = "";
        if (!ReadValue(reader, pool->items[i], 1, &value, &why))
        {
            SetLastError("invalid_argument: argument %u of the value pool: %s", i, why);
            return ENGINE_ERROR_INVALID_ARGUMENT;
        }
        out->Push(Move(value));
    }
    return ENGINE_OK;
}

// Writes values as a pool into growable storage; containers get their children right after the roots.
struct Writer
{
    Vector<engine_value>* items;
    Vector<u8>* bytes;

    usize Bytes(const u8* data, usize length)
    {
        const usize at = bytes->Size();
        for (usize i = 0; i < length; ++i)
        {
            bytes->Push(data[i]);
        }
        return at;
    }

    usize Slots(usize count)
    {
        const usize at = items->Size();
        for (usize i = 0; i < count; ++i)
        {
            items->Push(engine_value{});
        }
        return at;
    }

    void Number(usize slot, f64 f)
    {
        engine_value v = {};
        v.tag = ENGINE_VALUE_NUMBER;
        v.f = f;
        (*items)[slot] = v;
    }

    void Write(const Value& value, usize slot)
    {
        engine_value v = {};
        switch (value.Kind())
        {
        case ValueKind::Nil: v.tag = ENGINE_VALUE_NIL; break;
        case ValueKind::Bool:
            v.tag = ENGINE_VALUE_BOOL;
            v.i = value.AsBool() ? 1 : 0;
            break;
        case ValueKind::Int:
            v.tag = ENGINE_VALUE_INT;
            v.i = value.AsInt();
            break;
        case ValueKind::Float:
            v.tag = ENGINE_VALUE_NUMBER;
            v.f = value.AsFloat();
            break;
        case ValueKind::Text:
        {
            const StringView text = value.Text();
            v.tag = ENGINE_VALUE_STRING;
            v.i = static_cast<i64>(Bytes(reinterpret_cast<const u8*>(text.Data()), text.Length()));
            v.count = static_cast<u32>(text.Length());
            break;
        }
        case ValueKind::Bytes:
        {
            const Span<const u8> blob = value.Bytes();
            v.tag = ENGINE_VALUE_BYTES;
            v.i = static_cast<i64>(Bytes(blob.Data(), blob.Size()));
            v.count = static_cast<u32>(blob.Size());
            break;
        }
        case ValueKind::Entity:
            v.tag = ENGINE_VALUE_ENTITY;
            v.i = static_cast<i64>(value.AsEntity().ToU64());
            break;
        case ValueKind::Vec2:
        case ValueKind::Vec3:
        case ValueKind::Vec4:
        case ValueKind::Quat:
        {
            f64 lanes[4] = {0, 0, 0, 0};
            u32 n = 0;
            if (value.Kind() == ValueKind::Vec2)
            {
                const Vec2 a = value.AsVec2();
                lanes[0] = a.x;
                lanes[1] = a.y;
                n = 2;
            }
            else if (value.Kind() == ValueKind::Vec3)
            {
                const Vec3d a = value.AsVec3d();
                lanes[0] = a.x;
                lanes[1] = a.y;
                lanes[2] = a.z;
                n = 3;
            }
            else if (value.Kind() == ValueKind::Vec4)
            {
                const Vec4 a = value.AsVec4();
                lanes[0] = a.x;
                lanes[1] = a.y;
                lanes[2] = a.z;
                lanes[3] = a.w;
                n = 4;
            }
            else
            {
                const Quaternion a = value.AsQuat();
                lanes[0] = a.x;
                lanes[1] = a.y;
                lanes[2] = a.z;
                lanes[3] = a.w;
                n = 4;
            }
            v.tag = ENGINE_VALUE_ARRAY;
            v.count = n;
            v.i = static_cast<i64>(Slots(n));
            for (u32 k = 0; k < n; ++k)
            {
                Number(static_cast<usize>(v.i) + k, lanes[k]);
            }
            break;
        }
        case ValueKind::Array:
        {
            const usize n = value.Size();
            v.tag = ENGINE_VALUE_ARRAY;
            v.count = static_cast<u32>(n);
            const usize first = Slots(n);
            v.i = static_cast<i64>(first);
            (*items)[slot] = v;  // the children may grow the vector
            for (usize k = 0; k < n; ++k)
            {
                Write(value.At(k), first + k);
            }
            return;
        }
        case ValueKind::Map:
        {
            const usize n = value.Size();
            v.tag = ENGINE_VALUE_MAP;
            v.count = static_cast<u32>(n);
            const usize first = Slots(n * 2);
            v.i = static_cast<i64>(first);
            (*items)[slot] = v;
            for (usize k = 0; k < n; ++k)
            {
                const StringView key = value.KeyAt(k);
                engine_value kv = {};
                kv.tag = ENGINE_VALUE_STRING;
                kv.i = static_cast<i64>(Bytes(reinterpret_cast<const u8*>(key.Data()), key.Length()));
                kv.count = static_cast<u32>(key.Length());
                (*items)[first + k * 2] = kv;
                Write(value.At(k), first + k * 2 + 1);
            }
            return;
        }
        }
        (*items)[slot] = v;
    }
};

// Writes `values` as roots plus children into the vectors.
void WriteRoots(Span<const Value> values, Vector<engine_value>* items, Vector<u8>* bytes)
{
    Writer w{items, bytes};
    const usize first = w.Slots(values.Size());
    for (usize i = 0; i < values.Size(); ++i)
    {
        w.Write(values[i], first + i);
    }
}

// Copies the written vectors into the caller's output pool when they fit; otherwise reports what is needed.
engine_status StorePool(const Vector<engine_value>& items, const Vector<u8>& bytes, u32 roots, engine_value_pool* out)
{
    if (out == nullptr)
    {
        return ENGINE_OK;
    }
    if (StructSizeOf(out) < sizeof(engine_value_pool))
    {
        return Fail(ENGINE_ERROR_STRUCT_SIZE, "engine_value_pool.struct_size is smaller than the struct");
    }
    out->root_count = roots;
    if (items.Size() > out->item_capacity || bytes.Size() > out->byte_capacity ||
        (items.Size() != 0 && out->items == nullptr) || (bytes.Size() != 0 && out->bytes == nullptr))
    {
        out->item_count = items.Size();
        out->byte_count = bytes.Size();
        SetLastError("too_large: the results need %zu items and %zu bytes", static_cast<size_t>(items.Size()),
                     static_cast<size_t>(bytes.Size()));
        return ENGINE_ERROR_TOO_LARGE;
    }
    for (usize i = 0; i < items.Size(); ++i)
    {
        out->items[i] = items[i];
    }
    for (usize i = 0; i < bytes.Size(); ++i)
    {
        out->bytes[i] = bytes[i];
    }
    out->item_count = items.Size();
    out->byte_count = bytes.Size();
    return ENGINE_OK;
}

engine_status StatusOfOutcome(const script::Outcome& outcome)
{
    switch (outcome.status)
    {
    case script::Status::Ok: return ENGINE_OK;
    case script::Status::Invalid: return Fail(ENGINE_ERROR_INVALID_ARGUMENT, outcome.message.CStr());
    case script::Status::Unsupported: return Fail(ENGINE_ERROR_UNSUPPORTED, outcome.message.CStr());
    case script::Status::BudgetExceeded:
    case script::Status::OutOfMemory: return Fail(ENGINE_ERROR_LIMIT_EXCEEDED, outcome.message.CStr());
    case script::Status::SyntaxError:
    case script::Status::RuntimeError: break;
    }
    SetLastError("internal: the script failed: %s", outcome.message.CStr());
    return ENGINE_ERROR_INTERNAL;
}

// How deep the calling thread is inside a host callback of a world's scripts (a host function or an event callback).
// The frame that calls them holds the world mutex, so a script call that would take it there is refused instead of
// deadlocking on the thread's own lock.
thread_local u32 g_callback_depth = 0;

struct CallbackScope
{
    CallbackScope() { ++g_callback_depth; }
    ~CallbackScope() { --g_callback_depth; }
    CallbackScope(const CallbackScope&) = delete;
    CallbackScope& operator=(const CallbackScope&) = delete;
};

// The refusal of a world call made from inside a host callback (see g_callback_depth).
engine_status RefuseFromCallback()
{
    return Fail(ENGINE_ERROR_NOT_READY,
                "called from inside an event callback or a host function: the frame holds the world (call it between "
                "frames)");
}

// A host function as scripts call it.
script::Outcome InvokeHost(void* user, Span<const Value> args, Vector<Value>* results)
{
    HostFunction* f = static_cast<HostFunction*>(user);
    f->counter->FetchAdd(1, MemoryOrder::Relaxed);
    Vector<engine_value> in_items;
    Vector<u8> in_bytes;
    WriteRoots(args, &in_items, &in_bytes);
    engine_value_pool in = {};
    in.struct_size = sizeof(in);
    in.root_count = static_cast<u32>(args.Size());
    in.item_count = in_items.Size();
    in.item_capacity = in_items.Size();
    in.items = in_items.Data();
    in.byte_count = in_bytes.Size();
    in.byte_capacity = in_bytes.Size();
    in.bytes = in_bytes.Data();
    engine_value out_items[kHostResultItems] = {};
    u8 out_bytes[kHostResultBytes] = {};
    engine_value_pool out = {};
    out.struct_size = sizeof(out);
    out.item_capacity = kHostResultItems;
    out.items = out_items;
    out.byte_capacity = kHostResultBytes;
    out.bytes = out_bytes;
    engine_status status = ENGINE_OK;
    {
        CallbackScope scope;
        status = f->fn(f->user, &in, &out);
    }
    if (status != ENGINE_OK)
    {
        char text[64];
        FormatTo(text, sizeof(text), "the host function returned status %d", static_cast<int>(status));
        return script::Outcome(script::Status::RuntimeError, StringView(text, CStrLength(text)));
    }
    if (out.root_count > kHostResultItems || out.item_count > kHostResultItems || out.byte_count > kHostResultBytes ||
        out.root_count > out.item_count)
    {
        return script::Outcome(script::Status::RuntimeError, "the host function returned a pool with bad counts");
    }
    Reader reader{&out};
    for (u32 i = 0; i < out.root_count; ++i)
    {
        Value value;
        const char* why = "";
        if (!ReadValue(reader, out_items[i], 1, &value, &why))
        {
            return script::Outcome(script::Status::RuntimeError, why);
        }
        results->Push(Move(value));
    }
    return script::Outcome();
}

constexpr usize kMaxEventSubscriptions = 256;

// One host subscription to script events.
struct EventSub
{
    u32 id = 0;
    String<HeapAllocator> name{HeapAllocator::Default()};
    engine_script_event_fn fn = nullptr;
    void* user = nullptr;
    u32 bus_id = 0;  // the event bus's id; 0 until the runtime exists
};

// The bus calls this on the thread that runs the script tick.
void DeliverEvent(void* user, const script::EventRecord& event)
{
    EventSub* sub = static_cast<EventSub*>(user);
    Vector<engine_value> items;
    Vector<u8> bytes;
    WriteRoots(Span<const Value>(&event.payload, 1), &items, &bytes);
    engine_value_pool pool = {};
    pool.struct_size = sizeof(pool);
    pool.root_count = 1;
    pool.item_count = items.Size();
    pool.item_capacity = items.Size();
    pool.items = items.Data();
    pool.byte_count = bytes.Size();
    pool.byte_capacity = bytes.Size();
    pool.bytes = bytes.Data();
    engine_string name;
    name.data = event.name.CStr();
    name.length = event.name.Length();
    CallbackScope scope;
    sub->fn(sub->user, name, event.sender_key, &pool);
}

bool GoodName(StringView name)
{
    if (name.Length() == 0 || name.Length() > kMaxFunctionName)
    {
        return false;
    }
    for (usize i = 0; i < name.Length(); ++i)
    {
        const char c = name[i];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.';
        if (!ok)
        {
            return false;
        }
    }
    return true;
}

// Takes the world mutex when it is free; a call that finds a frame running does not wait.
class TryLocked
{
public:
    explicit TryLocked(Mutex& mutex) : mutex_(mutex), held_(mutex.TryLock()) {}
    ~TryLocked()
    {
        if (held_)
        {
            mutex_.Unlock();
        }
    }
    TryLocked(const TryLocked&) = delete;
    TryLocked& operator=(const TryLocked&) = delete;
    bool Held() const { return held_; }

private:
    Mutex& mutex_;
    bool held_;
};

}  // namespace detail

using namespace detail;

struct ScriptHost::Impl
{
    explicit Impl(const ScriptSettingValues& values) : settings(values) {}
    ScriptSettingValues settings;
    UniquePtr<script::Backend> backend;
    script::BindingRegistry bindings;
    bool standard_bindings = false;
    Vector<UniquePtr<HostFunction>> functions;
    UniquePtr<script::ScriptRuntime> runtime;
    script::Budget
        call_budget = script::ScriptRuntimeConfig::DefaultBudget();  // 0 would mean unlimited: never the default
    script::Budget callback_budget = script::ScriptRuntimeConfig::DefaultBudget();
    u32 loaded = 0;
    u64 calls = 0;
    Atomic<u64> host_calls{0};
    Vector<UniquePtr<EventSub>> subs;
    u32 next_sub = 1;
    UniquePtr<script::TaskPool> tasks;
    // The physics bindings' services (C API script physics, 2026-11-20): the world's physics world and character set.
    script_physics::PhysicsLayers layers;
    script_physics::PhysicsScriptServices physics;
    // The scene bindings' services: the world's environment and lights (`environment.*`, `light.*`).
    SceneScriptServices scene;
    SceneNames scene_names;  // `entity.name` and `entity.find`

    // The bindings every world's scripts get: the standard set and the physics and character set.
    bool RegisterBindings()
    {
        return script::RegisterStandardBindings(&bindings) && script_physics::RegisterPhysicsBindings(&bindings) &&
               RegisterSceneBindings(&bindings);
    }

    // Makes the backend and the standard bindings (what a task and the runtime both need).
    engine_status EnsureBackend()
    {
        if (backend.Get() == nullptr)
        {
            script::luajit::LuaJitOptions options;
            options.budgeted = settings.budgeted;
            options.jit = settings.jit;
            options.jit_code_kb = settings.jit_code_kb;
            options.random_seed = settings.random_seed;
            backend = script::luajit::CreateLuaJitBackend(options);
        }
        if (!standard_bindings)
        {
            if (!RegisterBindings())
            {
                return Fail(ENGINE_ERROR_INTERNAL, "the standard script bindings did not register");
            }
            standard_bindings = true;
        }
        return ENGINE_OK;
    }

    // Makes the backend, the standard bindings and the runtime on first use.
    engine_status Ensure()
    {
        if (runtime.Get() != nullptr)
        {
            return ENGINE_OK;
        }
        const engine_status backend_status = EnsureBackend();
        if (backend_status != ENGINE_OK)
        {
            return backend_status;
        }
        script::ScriptRuntimeConfig config;
        config.backend = backend.Get();
        config.bindings = &bindings;
        config.partitions = settings.partitions;
        config.partition_memory = settings.partition_memory;
        config.random_seed = settings.random_seed;
        config.budget = callback_budget;
        config.scene = &scene_names.services;
        runtime = MakeUnique<script::ScriptRuntime>(config);
        runtime->Services().extensions[script::kServicePhysics] = &physics;
        runtime->Services().extensions[script::kServiceScene] = &scene;
        if (!script_physics::RegisterPhysicsCommandKinds(&runtime->Kinds()))
        {
            runtime = nullptr;
            return Fail(ENGINE_ERROR_INTERNAL, "the physics command kinds did not register");
        }
        if (!RegisterSceneCommandKinds(&runtime->Kinds()))
        {
            runtime = nullptr;
            return Fail(ENGINE_ERROR_INTERNAL, "the scene command kinds did not register");
        }
        for (usize i = 0; i < subs.Size(); ++i)
        {
            subs[i]->bus_id = runtime->Events().Subscribe(subs[i]->name.View(), &DeliverEvent, subs[i].Get());
        }
        return ENGINE_OK;
    }
};

ScriptHost::ScriptHost() = default;
ScriptHost::~ScriptHost() = default;

namespace
{

ScriptHost::Impl* ImplOf(WorldObject& object, const ScriptSettingValues* make_with)
{
    if (object.scripts.Get() == nullptr)
    {
        if (make_with == nullptr)
        {
            return nullptr;
        }
        object.scripts = MakeUnique<ScriptHost>();
        object.scripts->impl = MakeUnique<ScriptHost::Impl>(*make_with);
        ScriptHost::Impl& impl = *object.scripts->impl;
        String<HeapAllocator> why(HeapAllocator::Default());
        // The layer names of `script.physics_layers` (the `default` layer alone when the text is refused).
        if (!impl.layers.Parse(StringView(make_with->physics_layers.Data(), make_with->physics_layers_length), &why))
        {
            impl.layers.Parse(StringView("default"), &why);
        }
        impl.physics.world = &object.world.Resource<physics::PhysicsWorld>();
        impl.physics.layers = &impl.layers;
        impl.physics.characters = object.characters.Get();
        impl.scene.object = &object;
        impl.scene.runtime = CurrentRuntime();
    }
    return object.scripts->impl.Get();
}

}  // namespace

bool ScriptBuilt()
{
    return true;
}

void RegisterScriptSettingsInEngine(settings::Registry* registry)
{
    script::RegisterScriptSettings(registry);
    script_physics::RegisterPhysicsScriptSettings(registry);
}

ScriptSettingValues ReadScriptSettings(const settings::Registry& registry)
{
    ScriptSettingValues values;
    values.partitions = script::ScriptPartitions(registry);
    values.budgeted = script::ScriptBudgeted(registry);
    values.jit = script::ScriptJit(registry);
    values.jit_code_kb = script::ScriptJitCodeKb(registry);
    values.random_seed = script::ScriptRandomSeed(registry);
    values.partition_memory = script::ScriptPartitionMemory(registry);
    const settings::SettingId layers = registry.Find(StringView("script.physics_layers"));
    if (layers.IsValid())
    {
        const usize length = settings::TextSetting(registry, layers)
                                 .Get(values.physics_layers.Data(), values.physics_layers.Size());
        values.physics_layers_length = length < values.physics_layers.Size() ? length : 0;  // too long: `default`
    }
    return values;
}

void ScriptTickWorld(WorldObject& object, f64 dt)
{
    ScriptHost::Impl* impl = ImplOf(object, nullptr);
    if (impl != nullptr && impl->runtime.Get() != nullptr)
    {
        impl->runtime->Tick(&object.world, dt);
    }
}

void ScriptPhysicsEvents(WorldObject& object)
{
    ScriptHost::Impl* impl = ImplOf(object, nullptr);
    if (impl != nullptr && impl->runtime.Get() != nullptr)
    {
        script_physics::PhysicsEventOptions options;
        options.characters = object.characters.Get();
        script_physics::EmitPhysicsEvents(object.world.Resource<physics::PhysicsWorld>(), impl->runtime.Get(), options);
    }
}

ecs::ComponentId ScriptComponentId()
{
    return ecs::ComponentOf<script::ScriptComponent>();
}

engine_status ScriptComponentSet(Runtime& runtime, WorldObject& object, ecs::Entity entity, const void* value)
{
    const engine_script_component* in = static_cast<const engine_script_component*>(value);
    usize length = 0;
    while (length < sizeof(in->behavior) && in->behavior[length] != '\0')
    {
        ++length;
    }
    if (length == 0 || length == sizeof(in->behavior))
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the behavior name is empty or has no NUL in 64 bytes");
    }
    Value params;
    if (StructSizeOf(in) >= sizeof(engine_script_component) && in->params != nullptr)
    {
        Vector<Value> read;
        if (StructSizeOf(in->params) < sizeof(engine_value_pool))
        {
            return Fail(ENGINE_ERROR_STRUCT_SIZE, "engine_value_pool.struct_size is smaller than the struct");
        }
        if (in->params->root_count != 1)
        {
            return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the params pool has one root: a map");
        }
        const engine_status status = ReadPool(in->params, &read);
        if (status != ENGINE_OK)
        {
            return status;
        }
        if (read[0].Kind() != ValueKind::Map && read[0].Kind() != ValueKind::Nil)
        {
            return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the params are a map or nil");
        }
        params = Move(read[0]);
    }
    ScriptHost::Impl* impl = ImplOf(object, &runtime.script_settings);
    const engine_status ensured = impl->Ensure();
    if (ensured != ENGINE_OK)
    {
        return ensured;
    }
    script::ScriptComponent component(StringView(in->behavior, length), Move(params), in->enabled != 0);
    const ecs::Status status = object.world.Has<script::ScriptComponent>(entity)
                                   ? object.world.Set<script::ScriptComponent>(entity, Move(component))
                                   : object.world.Add<script::ScriptComponent>(entity, Move(component));
    return status == ecs::Status::Ok ? ENGINE_OK : Fail(ENGINE_ERROR_LIMIT_EXCEEDED, "the world refused the component");
}

engine_status ScriptComponentGet(WorldObject& object, ecs::Entity entity, void* out)
{
    const script::ScriptComponent* component = static_cast<const ecs::World&>(object.world)
                                                   .Peek<script::ScriptComponent>(entity);
    if (component == nullptr)
    {
        return Fail(ENGINE_ERROR_NOT_FOUND, "the entity has no script component");
    }
    engine_script_component result = {};
    result.struct_size = sizeof(result);
    result.enabled = component->enabled ? 1u : 0u;
    const StringView name = component->behavior.View();
    const usize length = name.Length() < sizeof(result.behavior) - 1 ? name.Length() : sizeof(result.behavior) - 1;
    MemCopy(result.behavior, name.Data(), length);
    MemCopy(out, &result, sizeof(result));
    return ENGINE_OK;
}

engine_status ScriptComponentRemove(WorldObject& object, ecs::Entity entity)
{
    if (object.world.Remove<script::ScriptComponent>(entity) != ecs::Status::Ok)
    {
        return Fail(ENGINE_ERROR_NOT_FOUND, "the entity has no script component");
    }
    return ENGINE_OK;
}

engine_status ScriptLoadFn(engine_handle world, const engine_script_load* info, u64* out_script)
{
    if (out_script != nullptr)
    {
        *out_script = 0;
    }
    if (info == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`info` is NULL");
    }
    if (StructSizeOf(info) < sizeof(engine_script_load))
    {
        return Fail(ENGINE_ERROR_STRUCT_SIZE, "engine_script_load.struct_size is smaller than the struct");
    }
    if (info->flags != 0 || info->source.length == 0 || info->source.data == nullptr ||
        (info->name.length != 0 && info->name.data == nullptr))
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the flags are not 0, or the source is empty or NULL");
    }
    if (info->source.length > kMaxScriptBytes)
    {
        return Fail(ENGINE_ERROR_TOO_LARGE, "the script is over 16 MiB");
    }
    Runtime* runtime = CurrentRuntime();
    if (runtime == nullptr)
    {
        return Fail(world.value == 0 ? ENGINE_ERROR_INVALID_ARGUMENT : ENGINE_ERROR_STALE_HANDLE, "no engine exists");
    }
    if (g_callback_depth != 0)
    {
        return RefuseFromCallback();
    }
    LockGuard<Mutex> lock(runtime->world_mutex);
    engine_status status = ENGINE_OK;
    WorldObject* object = FindWorld(*runtime, world, &status);
    if (object == nullptr)
    {
        return status;
    }
    ScriptHost::Impl* impl = ImplOf(*object, &runtime->script_settings);
    if (impl->loaded >= kMaxScripts)
    {
        return Fail(ENGINE_ERROR_LIMIT_EXCEEDED, "a world holds at most 1024 scripts");
    }
    status = impl->Ensure();
    if (status != ENGINE_OK)
    {
        return status;
    }
    SharedBytesPtr bytes = CopyShared(ByteSpan(reinterpret_cast<const byte*>(info->source.data), info->source.length));
    const StringView name = info->name.length == 0 ? StringView("host_script")
                                                   : StringView(info->name.data, info->name.length);
    impl->runtime->AddScript(name, Move(bytes));
    ++impl->loaded;
    if (out_script != nullptr)
    {
        *out_script = impl->loaded;
    }
    return ENGINE_OK;
}

engine_status ScriptCallFn(engine_handle world, engine_string function, const engine_value_pool* args,
                           engine_value_pool* results)
{
    if (function.data == nullptr || function.length == 0 || function.length > kMaxFunctionName)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the function name is empty, NULL or over 128 bytes");
    }
    Vector<Value> values;
    const engine_status read = ReadPool(args, &values);
    if (read != ENGINE_OK)
    {
        return read;
    }
    if (results != nullptr && StructSizeOf(results) < sizeof(engine_value_pool))
    {
        return Fail(ENGINE_ERROR_STRUCT_SIZE, "engine_value_pool.struct_size is smaller than the struct");
    }
    Runtime* runtime = CurrentRuntime();
    if (runtime == nullptr)
    {
        return Fail(world.value == 0 ? ENGINE_ERROR_INVALID_ARGUMENT : ENGINE_ERROR_STALE_HANDLE, "no engine exists");
    }
    if (g_callback_depth != 0)
    {
        return RefuseFromCallback();
    }
    TryLocked lock(runtime->world_mutex);
    if (!lock.Held())
    {
        return Fail(ENGINE_ERROR_NOT_READY, "a frame or another world call is running on another thread");
    }
    engine_status status = ENGINE_OK;
    WorldObject* object = FindWorld(*runtime, world, &status);
    if (object == nullptr)
    {
        return status;
    }
    ScriptHost::Impl* impl = ImplOf(*object, nullptr);
    if (impl == nullptr || impl->runtime.Get() == nullptr)
    {
        return Fail(ENGINE_ERROR_NOT_READY, "no script was loaded into the world");
    }
    script::CallOutput output;
    ++impl->calls;
    script::PartitionLease lease;
    script::Outcome checkout_error;
    const script::CheckoutStatus checkout = impl->runtime->Scripts().TryCheckout(0, &lease, &checkout_error);
    if (checkout != script::CheckoutStatus::Ok)
    {
        if (checkout == script::CheckoutStatus::Busy)
        {
            return Fail(ENGINE_ERROR_NOT_READY, "the script state is in use");
        }
        return checkout_error.IsOk() ? Fail(ENGINE_ERROR_INTERNAL, "the script state could not be made")
                                     : StatusOfOutcome(checkout_error);
    }
    const script::Outcome outcome = lease.Call(StringView(function.data, function.length),
                                               Span<const Value>(values.Data(), values.Size()), impl->call_budget,
                                               &output);
    lease.Release();
    if (!outcome.IsOk())
    {
        return StatusOfOutcome(outcome);
    }
    if (output.Suspended())
    {
        return Fail(ENGINE_ERROR_INTERNAL, "the function waits: script_call cannot wait for it");
    }
    Vector<engine_value> items;
    Vector<u8> bytes;
    WriteRoots(Span<const Value>(output.results.Data(), output.results.Size()), &items, &bytes);
    return StorePool(items, bytes, static_cast<u32>(output.results.Size()), results);
}

engine_status ScriptRegisterFunctionFn(engine_handle world, engine_string name, u32 arity, u32 flags,
                                       engine_script_fn function, void* user)
{
    if (function == nullptr || name.data == nullptr || !GoodName(StringView(name.data, name.length)) ||
        arity > kMaxArity || (flags & ~ENGINE_SCRIPT_FUNCTION_PURE) != 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT,
                    "the function or name is NULL or malformed, or the arity or flags are out of range");
    }
    const StringView full(name.data, name.length);
    usize dot = full.Length();
    for (usize i = 0; i < full.Length(); ++i)
    {
        if (full[i] == '.')
        {
            dot = i;
            break;
        }
    }
    if (dot == 0 || dot + 1 >= full.Length())
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the name is `namespace.function`");
    }
    Runtime* runtime = CurrentRuntime();
    if (runtime == nullptr)
    {
        return Fail(world.value == 0 ? ENGINE_ERROR_INVALID_ARGUMENT : ENGINE_ERROR_STALE_HANDLE, "no engine exists");
    }
    if (g_callback_depth != 0)
    {
        return RefuseFromCallback();
    }
    LockGuard<Mutex> lock(runtime->world_mutex);
    engine_status status = ENGINE_OK;
    WorldObject* object = FindWorld(*runtime, world, &status);
    if (object == nullptr)
    {
        return status;
    }
    ScriptHost::Impl* impl = ImplOf(*object, &runtime->script_settings);
    if (impl->runtime.Get() != nullptr || impl->tasks.Get() != nullptr)
    {
        return Fail(ENGINE_ERROR_NOT_READY, "register host functions before the first script or task of the world");
    }
    if (!impl->standard_bindings)
    {
        if (!impl->RegisterBindings())
        {
            return Fail(ENGINE_ERROR_INTERNAL, "the standard script bindings did not register");
        }
        impl->standard_bindings = true;
    }
    auto record = MakeUnique<HostFunction>();
    record->fn = function;
    record->user = user;
    record->counter = &impl->host_calls;
    // One placeholder parameter name per argument: the host's own names are not part of the signature.
    String<HeapAllocator> params(HeapAllocator::Default());
    for (u32 i = 0; i < arity; ++i)
    {
        if (i != 0)
        {
            params.Append(',');
        }
        params.Append(StringView("a"));
        char digit[4];
        FormatTo(digit, sizeof(digit), "%u", i);
        params.Append(StringView(digit, CStrLength(digit)));
    }
    script::Outcome error;
    const script::ThreadRule rule = (flags & ENGINE_SCRIPT_FUNCTION_PURE) != 0 ? script::ThreadRule::Pure
                                                                               : script::ThreadRule::WorldRead;
    if (!impl->bindings.AddDynamic(full.Substring(0, dot), full.Substring(dot + 1), rule, params.View(),
                                   "a host function", &InvokeHost, record.Get(), &error))
    {
        SetLastError("already_exists: %s", error.message.CStr());
        return ENGINE_ERROR_ALREADY_EXISTS;
    }
    impl->functions.Push(Move(record));
    return ENGINE_OK;
}

engine_status ScriptSetBudgetFn(engine_handle world, const engine_script_budget* budget)
{
    if (budget == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`budget` is NULL");
    }
    if (StructSizeOf(budget) < sizeof(engine_script_budget))
    {
        return Fail(ENGINE_ERROR_STRUCT_SIZE, "engine_script_budget.struct_size is smaller than the struct");
    }
    Runtime* runtime = CurrentRuntime();
    if (runtime == nullptr)
    {
        return Fail(world.value == 0 ? ENGINE_ERROR_INVALID_ARGUMENT : ENGINE_ERROR_STALE_HANDLE, "no engine exists");
    }
    if (g_callback_depth != 0)
    {
        return RefuseFromCallback();
    }
    LockGuard<Mutex> lock(runtime->world_mutex);
    engine_status status = ENGINE_OK;
    WorldObject* object = FindWorld(*runtime, world, &status);
    if (object == nullptr)
    {
        return status;
    }
    ScriptHost::Impl* impl = ImplOf(*object, &runtime->script_settings);
    script::Budget set;
    set.instructions = budget->instructions;
    set.nanoseconds = budget->nanoseconds;
    set.memory_bytes = budget->memory_bytes;
    impl->call_budget = set;
    impl->tasks = UniquePtr<script::TaskPool>();  // the pool took the old budget when it was made: the next task
                                                  // remakes it
    if (impl->runtime.Get() == nullptr)
    {
        impl->callback_budget = set;
    }
    return ENGINE_OK;
}

engine_status ScriptStatsFn(engine_handle world, engine_script_stats* out)
{
    if (out == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`out` is NULL");
    }
    const u32 size = StructSizeOf(out);
    if (size < sizeof(engine_script_stats))
    {
        return Fail(ENGINE_ERROR_STRUCT_SIZE, "engine_script_stats.struct_size is smaller than the struct");
    }
    Runtime* runtime = CurrentRuntime();
    if (runtime == nullptr)
    {
        return Fail(world.value == 0 ? ENGINE_ERROR_INVALID_ARGUMENT : ENGINE_ERROR_STALE_HANDLE, "no engine exists");
    }
    if (g_callback_depth != 0)
    {
        return RefuseFromCallback();
    }
    LockGuard<Mutex> lock(runtime->world_mutex);
    engine_status status = ENGINE_OK;
    WorldObject* object = FindWorld(*runtime, world, &status);
    if (object == nullptr)
    {
        return status;
    }
    engine_script_stats result = {};
    result.struct_size = sizeof(result);
    ScriptHost::Impl* impl = ImplOf(*object, nullptr);
    if (impl != nullptr)
    {
        result.scripts_loaded = impl->loaded;
        result.host_functions = static_cast<u32>(impl->functions.Size());
        result.calls = impl->calls;
        result.host_calls = impl->host_calls.Load(MemoryOrder::Relaxed);
        if (impl->runtime.Get() != nullptr)
        {
            const script::ScriptRuntimeStats& stats = impl->runtime->Stats();
            result.partitions = impl->runtime->Scripts().PartitionCount();
            result.ticks = stats.ticks;
            result.instances = stats.instances;
            result.updates = stats.updates;
            result.errors = stats.errors;
        }
    }
    MemCopy(out, &result, sizeof(result));
    return ENGINE_OK;
}

engine_status ScriptEventEmitFn(engine_handle world, engine_string name, const engine_value_pool* payload, u64 target)
{
    if (name.data == nullptr || name.length == 0 || name.length > script::kMaxEventNameBytes)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the event name is empty, NULL or over 64 bytes");
    }
    Vector<Value> values;
    const engine_status read = ReadPool(payload, &values);
    if (read != ENGINE_OK)
    {
        return read;
    }
    Runtime* runtime = CurrentRuntime();
    if (runtime == nullptr)
    {
        return Fail(world.value == 0 ? ENGINE_ERROR_INVALID_ARGUMENT : ENGINE_ERROR_STALE_HANDLE, "no engine exists");
    }
    if (g_callback_depth != 0)
    {
        return RefuseFromCallback();
    }
    TryLocked lock(runtime->world_mutex);
    if (!lock.Held())
    {
        return Fail(ENGINE_ERROR_NOT_READY, "a frame or another world call is running on another thread");
    }
    engine_status status = ENGINE_OK;
    WorldObject* object = FindWorld(*runtime, world, &status);
    if (object == nullptr)
    {
        return status;
    }
    ScriptHost::Impl* impl = ImplOf(*object, nullptr);
    if (impl == nullptr || impl->runtime.Get() == nullptr)
    {
        return Fail(ENGINE_ERROR_NOT_READY, "no script was loaded into the world");
    }
    Value value;
    if (values.Size() != 0)
    {
        value = Move(values[0]);
    }
    if (!impl->runtime->Events().Emit(StringView(name.data, name.length), Move(value), target))
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the event name was refused");
    }
    return ENGINE_OK;
}

engine_status ScriptEventSubscribeFn(engine_handle world, engine_string name, engine_script_event_fn callback,
                                     void* user, u32* out_id)
{
    if (callback == nullptr || out_id == nullptr || (name.data == nullptr && name.length != 0) ||
        name.length > script::kMaxEventNameBytes)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`callback` or `out_id` is NULL, or the name is malformed");
    }
    *out_id = 0;
    Runtime* runtime = CurrentRuntime();
    if (runtime == nullptr)
    {
        return Fail(world.value == 0 ? ENGINE_ERROR_INVALID_ARGUMENT : ENGINE_ERROR_STALE_HANDLE, "no engine exists");
    }
    if (g_callback_depth != 0)
    {
        return RefuseFromCallback();
    }
    LockGuard<Mutex> lock(runtime->world_mutex);
    engine_status status = ENGINE_OK;
    WorldObject* object = FindWorld(*runtime, world, &status);
    if (object == nullptr)
    {
        return status;
    }
    ScriptHost::Impl* impl = ImplOf(*object, &runtime->script_settings);
    if (impl->subs.Size() >= kMaxEventSubscriptions)
    {
        return Fail(ENGINE_ERROR_LIMIT_EXCEEDED, "a world holds at most 256 event subscriptions");
    }
    auto sub = MakeUnique<EventSub>();
    sub->id = impl->next_sub++;
    sub->name = String<HeapAllocator>(HeapAllocator::Default(), StringView(name.data, name.length));
    sub->fn = callback;
    sub->user = user;
    if (impl->runtime.Get() != nullptr)
    {
        sub->bus_id = impl->runtime->Events().Subscribe(sub->name.View(), &DeliverEvent, sub.Get());
    }
    *out_id = sub->id;
    impl->subs.Push(Move(sub));
    return ENGINE_OK;
}

engine_status ScriptEventUnsubscribeFn(engine_handle world, u32 id)
{
    Runtime* runtime = CurrentRuntime();
    if (runtime == nullptr)
    {
        return Fail(world.value == 0 ? ENGINE_ERROR_INVALID_ARGUMENT : ENGINE_ERROR_STALE_HANDLE, "no engine exists");
    }
    if (g_callback_depth != 0)
    {
        return RefuseFromCallback();
    }
    LockGuard<Mutex> lock(runtime->world_mutex);
    engine_status status = ENGINE_OK;
    WorldObject* object = FindWorld(*runtime, world, &status);
    if (object == nullptr)
    {
        return status;
    }
    ScriptHost::Impl* impl = ImplOf(*object, nullptr);
    if (impl != nullptr)
    {
        for (usize i = 0; i < impl->subs.Size(); ++i)
        {
            if (impl->subs[i]->id == id)
            {
                if (impl->subs[i]->bus_id != 0 && impl->runtime.Get() != nullptr)
                {
                    impl->runtime->Events().Unsubscribe(impl->subs[i]->bus_id);
                }
                impl->subs.Remove(i);
                return ENGINE_OK;
            }
        }
    }
    return Fail(ENGINE_ERROR_NOT_FOUND, "no such event subscription");
}

engine_status ScriptTaskRunFn(engine_handle world, const engine_script_task* task, engine_value_pool* results)
{
    if (task == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`task` is NULL");
    }
    if (StructSizeOf(task) < sizeof(engine_script_task))
    {
        return Fail(ENGINE_ERROR_STRUCT_SIZE, "engine_script_task.struct_size is smaller than the struct");
    }
    if (task->source.data == nullptr || task->source.length == 0 || task->source.length > kMaxScriptBytes)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the task's source is empty or too long");
    }
    if (task->mode < ENGINE_SCRIPT_TASK_RUN || task->mode > ENGINE_SCRIPT_TASK_FOR || task->reserved != 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the task's mode is unknown or `reserved` is not zero");
    }
    if (results != nullptr && StructSizeOf(results) < sizeof(engine_value_pool))
    {
        return Fail(ENGINE_ERROR_STRUCT_SIZE, "engine_value_pool.struct_size is smaller than the struct");
    }
    Vector<Value> values;
    const engine_status read = ReadPool(task->data, &values);
    if (read != ENGINE_OK)
    {
        return read;
    }
    Runtime* runtime = CurrentRuntime();
    if (runtime == nullptr)
    {
        return Fail(world.value == 0 ? ENGINE_ERROR_INVALID_ARGUMENT : ENGINE_ERROR_STALE_HANDLE, "no engine exists");
    }
    if (g_callback_depth != 0)
    {
        return RefuseFromCallback();
    }
    TryLocked lock(runtime->world_mutex);
    if (!lock.Held())
    {
        return Fail(ENGINE_ERROR_NOT_READY, "a frame or another world call is running on another thread");
    }
    engine_status status = ENGINE_OK;
    WorldObject* object = FindWorld(*runtime, world, &status);
    if (object == nullptr)
    {
        return status;
    }
    ScriptHost::Impl* impl = ImplOf(*object, &runtime->script_settings);
    const engine_status backend_status = impl->EnsureBackend();
    if (backend_status != ENGINE_OK)
    {
        return backend_status;
    }
    if (impl->tasks.Get() == nullptr)
    {
        script::TaskPoolConfig config;
        config.backend = impl->backend.Get();
        config.bindings = &impl->bindings;
        config.states = impl->settings.partitions < 1 ? 1u : impl->settings.partitions;
        config.state_memory = impl->settings.partition_memory;
        config.budget = impl->call_budget;
        impl->tasks = MakeUnique<script::TaskPool>(config);
    }
    script::TaskRequest request;
    request.id = 1;
    request.mode = static_cast<script::TaskMode>(task->mode);
    request.source = String<HeapAllocator>(HeapAllocator::Default(),
                                           StringView(task->source.data, task->source.length));
    if (values.Size() != 0)
    {
        request.data = Move(values[0]);
    }
    request.seed = task->seed;
    request.count = task->count;
    request.chunk = task->chunk;
    Vector<script::TaskRequest> requests;
    requests.Push(Move(request));
    Vector<script::TaskOutcome> outcomes;
    impl->tasks->Execute(Span<const script::TaskRequest>(requests.Data(), requests.Size()), &outcomes, nullptr);
    if (outcomes.Size() != 1)
    {
        return Fail(ENGINE_ERROR_INTERNAL, "the task pool returned no outcome");
    }
    if (outcomes[0].failed)
    {
        SetLastError("internal: the task failed: %s", outcomes[0].error.CStr());
        return ENGINE_ERROR_INTERNAL;
    }
    Vector<engine_value> items;
    Vector<u8> bytes;
    WriteRoots(Span<const Value>(&outcomes[0].value, 1), &items, &bytes);
    return StorePool(items, bytes, 1, results);
}

#else  // no script module in this build

// Nothing to hold; defined so that the destructor below deletes a complete type (cl refuses an incomplete one).
struct ScriptHost::Impl
{
};

ScriptHost::ScriptHost() = default;
ScriptHost::~ScriptHost() = default;

bool ScriptBuilt()
{
    return false;
}

void RegisterScriptSettingsInEngine(settings::Registry*) {}

ScriptSettingValues ReadScriptSettings(const settings::Registry&)
{
    return ScriptSettingValues();
}

void ScriptTickWorld(WorldObject&, f64) {}

void ScriptPhysicsEvents(WorldObject&) {}

ecs::ComponentId ScriptComponentId()
{
    return ecs::kInvalidComponent;
}

engine_status ScriptComponentSet(Runtime&, WorldObject&, ecs::Entity, const void*)
{
    return Fail(ENGINE_ERROR_UNSUPPORTED, "this build of the engine has no scripts");
}

engine_status ScriptComponentGet(WorldObject&, ecs::Entity, void*)
{
    return Fail(ENGINE_ERROR_UNSUPPORTED, "this build of the engine has no scripts");
}

engine_status ScriptComponentRemove(WorldObject&, ecs::Entity)
{
    return Fail(ENGINE_ERROR_UNSUPPORTED, "this build of the engine has no scripts");
}

engine_status ScriptLoadFn(engine_handle, const engine_script_load*, u64*)
{
    return Fail(ENGINE_ERROR_UNSUPPORTED, "this build of the engine has no scripts");
}

engine_status ScriptCallFn(engine_handle, engine_string, const engine_value_pool*, engine_value_pool*)
{
    return Fail(ENGINE_ERROR_UNSUPPORTED, "this build of the engine has no scripts");
}

engine_status ScriptRegisterFunctionFn(engine_handle, engine_string, u32, u32, engine_script_fn, void*)
{
    return Fail(ENGINE_ERROR_UNSUPPORTED, "this build of the engine has no scripts");
}

engine_status ScriptSetBudgetFn(engine_handle, const engine_script_budget*)
{
    return Fail(ENGINE_ERROR_UNSUPPORTED, "this build of the engine has no scripts");
}

engine_status ScriptStatsFn(engine_handle, engine_script_stats*)
{
    return Fail(ENGINE_ERROR_UNSUPPORTED, "this build of the engine has no scripts");
}

engine_status ScriptEventEmitFn(engine_handle, engine_string, const engine_value_pool*, u64)
{
    return Fail(ENGINE_ERROR_UNSUPPORTED, "this build of the engine has no scripts");
}

engine_status ScriptEventSubscribeFn(engine_handle, engine_string, engine_script_event_fn, void*, u32*)
{
    return Fail(ENGINE_ERROR_UNSUPPORTED, "this build of the engine has no scripts");
}

engine_status ScriptEventUnsubscribeFn(engine_handle, u32)
{
    return Fail(ENGINE_ERROR_UNSUPPORTED, "this build of the engine has no scripts");
}

engine_status ScriptTaskRunFn(engine_handle, const engine_script_task*, engine_value_pool*)
{
    return Fail(ENGINE_ERROR_UNSUPPORTED, "this build of the engine has no scripts");
}

#endif

}  // namespace vse::engine_c
