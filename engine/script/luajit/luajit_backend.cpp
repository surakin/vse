#include "script/luajit/luajit_backend.h"

#include "algorithm.h"
#include "cstr/cstr.h"
#include "cstr/mem_ops.h"
#include "lang/scalar.h"
#include "log/log.h"
#include "os/os.h"
#include "script/luajit/lua_patterns.h"
#include "script/backend_registry.h"
#include "script/log_buffer.h"

#include <lua.hpp>

// Rules for this file. LuaJIT reports an error by unwinding the C stack, which runs no C++ destructor in the frames it
// crosses (exceptions are off). So: every function that can raise (anything that allocates in Lua or indexes a table)
// is either a lua_CFunction run under lua_cpcall / lua_pcall / lua_resume, or is called only from one; such a function
// keeps no owning C++ object alive across a Lua call (owners live in the frame that made the protected call, and are
// reached through a pointer); and the engine's own checks (limits, bad types) are returned as values and never raised.
namespace engine_placeholder::script::luajit
{

namespace
{

using Text = String<HeapAllocator>;

// The default budget of a chunk's top level in a budgeted state.
constexpr u64 kLoadInstructions = 50'000'000;
constexpr u64 kLoadNanoseconds = 2'000'000'000;
constexpr u64 kDefaultMemoryCap = u64{256} << 20;
constexpr i64 kMaxExactInt = i64{1} << 53;
constexpr usize kMaxResults = 256;
// A script can raise an error with a huge message; the message and the trace are cut to these sizes.
constexpr usize kMaxMessageBytes = 4096;
constexpr usize kMaxTraceBytes = 16384;
// Headroom the allocator grants to cleanup (dropping a coroutine, collecting) at a full memory cap, where even a
// protected call needs bytes.
constexpr u64 kCleanupSlack = u64{1} << 20;
constexpr u32 kMaxNodes = 1u << 16;
// The most text, blob and key bytes one value may copy out of Lua: a table can hold many references to one big string.
constexpr usize kMaxMarshalBytes = usize{16} << 20;

Text MakeText(StringView sv)
{
    return Text(HeapAllocator::Default(), sv);
}

// ------------------------------------------------------------------ allocator

struct Accounting
{
    u64 used = 0;
    u64 cap = 0;
    // The budget of the call in progress, read by the count hook (it finds this struct through the allocator's user
    // data).
    bool instructions_limited = false;
    u64 instructions_left = 0;
    u64 deadline_ns = 0;  // 0: none
    bool tripped = false;
    const char* trip_message = "";
};

constexpr u64 kHookCount = 1000;

Accounting* AccountingOf(lua_State* L)
{
    void* ud = nullptr;
    lua_getallocf(L, &ud);
    return static_cast<Accounting*>(ud);
}

// The count hook: runs every kHookCount instructions while a budget is active. Once a budget has tripped it fires on
// every instruction, so an error caught by a script's pcall comes back at the next instruction outside that pcall.
void BudgetHook(lua_State* L, lua_Debug*)
{
    Accounting* a = AccountingOf(L);
    if (!a->tripped)
    {
        const bool out_of_instructions = a->instructions_limited && a->instructions_left <= kHookCount;
        if (a->instructions_limited && !out_of_instructions)
        {
            a->instructions_left -= kHookCount;
        }
        const bool out_of_time = a->deadline_ns != 0 && os::MonotonicTimeNanos() > a->deadline_ns;
        if (out_of_instructions || out_of_time)
        {
            a->tripped = true;
            a->trip_message = out_of_instructions ? "instruction budget exceeded" : "time budget exceeded";
            lua_sethook(L, &BudgetHook, LUA_MASKCOUNT, 1);  // from now on at every instruction
        }
    }
    if (a->tripped)
    {
        luaL_error(L, "%s", a->trip_message);
    }
}

void* CountingAlloc(void* ud, void* ptr, usize old_size, usize new_size)
{
    Accounting* a = static_cast<Accounting*>(ud);
    const usize old = ptr != nullptr ? old_size : 0;  // with a null block, old_size is a type tag
    if (new_size == 0)
    {
        if (ptr != nullptr)
        {
            a->used -= old;
            MemFree(ptr);
        }
        return nullptr;
    }
    if (new_size > old && a->used - old + new_size > a->cap)
    {
        return nullptr;
    }
    void* block = MemReallocate(ptr, new_size);
    if (block != nullptr)
    {
        a->used = a->used - old + new_size;
    }
    return block;
}

// ------------------------------------------------------------------ marshalling

// The tagged tables that carry the value kinds Lua has no type for. Each tag is a metatable kept in the registry.
enum Tag : int
{
    kTagVec2,
    kTagVec3,
    kTagVec4,
    kTagQuat,
    kTagEntity,
    kTagBytes,
    kTagArray,
    kTagMap,
    kTagCount,
    kTagNone = -1,
};

const char* const kTagNames[kTagCount] = {"engine.vec2",   "engine.vec3",  "engine.vec4",  "engine.quat",
                                          "engine.entity", "engine.bytes", "engine.array", "engine.map"};

// State shared by the conversions of one call. It lives in the frame that makes the protected call.
struct Marshal
{
    Vector<StringView> keys;  // a stack of map keys, sorted per table
    u32 nodes = 0;
    usize bytes = 0;
    const char* problem = nullptr;  // set (never raised) when a value cannot cross
};

bool Representable(const Value& v, const char** why)
{
    switch (v.Kind())
    {
    case ValueKind::Int:
        if (v.AsInt() > kMaxExactInt || v.AsInt() < -kMaxExactInt)
        {
            *why = "an integer outside +-2^53 cannot be represented as a Lua number";
            return false;
        }
        return true;
    case ValueKind::Array:
        for (usize i = 0; i < v.Size(); ++i)
        {
            if (!Representable(v.At(i), why))
            {
                return false;
            }
        }
        return true;
    case ValueKind::Map:
        for (usize i = 0; i < v.Size(); ++i)
        {
            if (!Representable(v.At(i), why))
            {
                return false;
            }
        }
        return true;
    default: return true;
    }
}

void SetNumberField(lua_State* L, const char* name, f64 number)
{
    lua_pushstring(L, name);
    lua_pushnumber(L, number);
    lua_rawset(L, -3);
}

void Tagged(lua_State* L, Tag tag, int narr, int nrec)
{
    lua_createtable(L, narr, nrec);
    lua_getfield(L, LUA_REGISTRYINDEX, kTagNames[tag]);
    lua_setmetatable(L, -2);
}

// Pushes `v` as a Lua value. May raise (memory); keeps no owner alive. The stack must have room (the caller checked).
void PushValue(lua_State* L, const Value& v)
{
    switch (v.Kind())
    {
    case ValueKind::Nil: lua_pushnil(L); return;
    case ValueKind::Bool: lua_pushboolean(L, v.AsBool() ? 1 : 0); return;
    case ValueKind::Int: lua_pushnumber(L, static_cast<f64>(v.AsInt())); return;
    case ValueKind::Float: lua_pushnumber(L, v.AsFloat()); return;
    case ValueKind::Text: lua_pushlstring(L, v.Text().Data(), v.Text().Length()); return;
    case ValueKind::Vec2:
        Tagged(L, kTagVec2, 0, 2);
        SetNumberField(L, "x", v.AsVec2().x);
        SetNumberField(L, "y", v.AsVec2().y);
        return;
    case ValueKind::Vec3:
        Tagged(L, kTagVec3, 0, 3);
        SetNumberField(L, "x", v.AsVec3().x);
        SetNumberField(L, "y", v.AsVec3().y);
        SetNumberField(L, "z", v.AsVec3().z);
        return;
    case ValueKind::Vec4:
        Tagged(L, kTagVec4, 0, 4);
        SetNumberField(L, "x", v.AsVec4().x);
        SetNumberField(L, "y", v.AsVec4().y);
        SetNumberField(L, "z", v.AsVec4().z);
        SetNumberField(L, "w", v.AsVec4().w);
        return;
    case ValueKind::Quat:
        Tagged(L, kTagQuat, 0, 4);
        SetNumberField(L, "x", v.AsQuat().x);
        SetNumberField(L, "y", v.AsQuat().y);
        SetNumberField(L, "z", v.AsQuat().z);
        SetNumberField(L, "w", v.AsQuat().w);
        return;
    case ValueKind::Entity:
        Tagged(L, kTagEntity, 0, 2);
        SetNumberField(L, "index", v.AsEntity().index);
        SetNumberField(L, "generation", v.AsEntity().generation);
        return;
    case ValueKind::Bytes:
        Tagged(L, kTagBytes, 0, 1);
        lua_pushstring(L, "data");
        lua_pushlstring(L, reinterpret_cast<const char*>(v.Bytes().Data()), v.Bytes().Size());
        lua_rawset(L, -3);
        return;
    case ValueKind::Array:
        Tagged(L, kTagArray, static_cast<int>(v.Size()), 0);
        for (usize i = 0; i < v.Size(); ++i)
        {
            PushValue(L, v.At(i));
            lua_rawseti(L, -2, static_cast<int>(i + 1));
        }
        return;
    case ValueKind::Map:
        Tagged(L, kTagMap, 0, static_cast<int>(v.Size()));
        for (usize i = 0; i < v.Size(); ++i)
        {
            lua_pushlstring(L, v.KeyAt(i).Data(), v.KeyAt(i).Length());
            PushValue(L, v.At(i));
            lua_rawset(L, -3);
        }
        return;
    }
}

int TagOf(lua_State* L, int index)
{
    if (lua_getmetatable(L, index) == 0)
    {
        return kTagNone;
    }
    int found = kTagNone;
    for (int t = 0; t < kTagCount && found == kTagNone; ++t)
    {
        lua_getfield(L, LUA_REGISTRYINDEX, kTagNames[t]);
        if (lua_rawequal(L, -1, -2) != 0)
        {
            found = t;
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return found;
}

// Reads a number field of the table at `table` (absolute); false when it is missing or not a number.
bool ReadField(lua_State* L, int table, const char* name, f64* out)
{
    lua_pushstring(L, name);
    lua_rawget(L, table);
    const bool ok = lua_type(L, -1) == LUA_TNUMBER;
    if (ok)
    {
        *out = lua_tonumber(L, -1);
    }
    lua_pop(L, 1);
    return ok;
}

bool Fail(Marshal* m, const char* problem)
{
    if (m->problem == nullptr)
    {
        m->problem = problem;
    }
    return false;
}

// Counts bytes copied out of Lua against the total allowed for one value.
bool Spend(Marshal* m, usize bytes)
{
    if (bytes > kMaxMarshalBytes - m->bytes)
    {
        return Fail(m, "a value copies too many bytes out of Lua");
    }
    m->bytes += bytes;
    return true;
}

bool KeyLess(const StringView& a, const StringView& b)
{
    const usize n = a.Length() < b.Length() ? a.Length() : b.Length();
    const int c = MemCompare(a.Data(), b.Data(), n);
    return c != 0 ? c < 0 : a.Length() < b.Length();
}

// Reads the Lua value at `index` into `*out`. Never raises on its own: it reports a problem by value. The tables it
// reads have no metamethods (raw access only).
bool ReadValue(lua_State* L, int index, Value* out, Marshal* m, u32 depth)
{
    if (index < 0)
    {
        index = lua_gettop(L) + index + 1;
    }
    if (depth > kMaxValueDepth)
    {
        return Fail(m, "a table is nested too deeply (or is cyclic)");
    }
    if (++m->nodes > kMaxNodes)
    {
        return Fail(m, "a value holds too many items");
    }
    if (lua_checkstack(L, 16) == 0)
    {
        return Fail(m, "the Lua stack is exhausted");
    }
    switch (lua_type(L, index))
    {
    case LUA_TNIL: *out = Value(); return true;
    case LUA_TBOOLEAN: *out = Value::FromBool(lua_toboolean(L, index) != 0); return true;
    case LUA_TNUMBER:
    {
        const f64 d = lua_tonumber(L, index);
        const bool whole = IsFinite(d) && Floor(d) == d && Abs(d) <= static_cast<f64>(kMaxExactInt);
        const bool negative_zero = d == 0.0 && (1.0 / d) < 0.0;
        *out = whole && !negative_zero ? Value::FromInt(static_cast<i64>(d)) : Value::FromFloat(d);
        return true;
    }
    case LUA_TSTRING:
    {
        usize len = 0;
        const char* s = lua_tolstring(L, index, &len);
        if (len > (usize{1} << 20))
        {
            return Fail(m, "a string is too long");
        }
        if (!Spend(m, len))
        {
            return false;
        }
        *out = Value::FromText(StringView(s, len));
        return true;
    }
    case LUA_TTABLE: break;
    default: return Fail(m, "a function, userdata or thread cannot cross to the engine");
    }
    const int tag = TagOf(L, index);
    f64 a = 0;
    f64 b = 0;
    f64 c = 0;
    f64 d = 0;
    switch (tag)
    {
    case kTagVec2:
        if (!ReadField(L, index, "x", &a) || !ReadField(L, index, "y", &b))
        {
            return Fail(m, "a vec2 needs numeric x and y");
        }
        *out = Value::FromVec2(Vec2{static_cast<f32>(a), static_cast<f32>(b)});
        return true;
    case kTagVec3:
        if (!ReadField(L, index, "x", &a) || !ReadField(L, index, "y", &b) || !ReadField(L, index, "z", &c))
        {
            return Fail(m, "a vec3 needs numeric x, y and z");
        }
        *out = Value::FromVec3(Vec3{static_cast<f32>(a), static_cast<f32>(b), static_cast<f32>(c)});
        return true;
    case kTagVec4:
    case kTagQuat:
        if (!ReadField(L, index, "x", &a) || !ReadField(L, index, "y", &b) || !ReadField(L, index, "z", &c) ||
            !ReadField(L, index, "w", &d))
        {
            return Fail(m, "a vec4 or quat needs numeric x, y, z and w");
        }
        if (tag == kTagVec4)
        {
            *out = Value::FromVec4(
                Vec4{static_cast<f32>(a), static_cast<f32>(b), static_cast<f32>(c), static_cast<f32>(d)});
        }
        else
        {
            Quaternion q;
            q.x = static_cast<f32>(a);
            q.y = static_cast<f32>(b);
            q.z = static_cast<f32>(c);
            q.w = static_cast<f32>(d);
            *out = Value::FromQuat(q);
        }
        return true;
    case kTagEntity:
    {
        if (!ReadField(L, index, "index", &a) || !ReadField(L, index, "generation", &b) || !(a >= 0.0) ||
            a > 4294967295.0 || !(b >= 0.0) || b > 4294967295.0 || Floor(a) != a || Floor(b) != b)
        {
            return Fail(m, "an entity needs integer index and generation within 32 bits");
        }
        ecs::Entity e;
        e.index = static_cast<u32>(a);
        e.generation = static_cast<u32>(b);
        *out = Value::FromEntity(e);
        return true;
    }
    case kTagBytes:
    {
        lua_pushstring(L, "data");
        lua_rawget(L, index);
        if (lua_type(L, -1) != LUA_TSTRING)
        {
            lua_pop(L, 1);
            return Fail(m, "a bytes value needs a string in its data field");
        }
        usize len = 0;
        const char* s = lua_tolstring(L, -1, &len);
        if (len > (usize{16} << 20) || !Spend(m, len))
        {
            lua_pop(L, 1);
            return Fail(m, "a byte blob is too large");
        }
        *out = Value::FromBytes(Span<const u8>(reinterpret_cast<const u8*>(s), len));
        lua_pop(L, 1);
        return true;
    }
    default: break;
    }

    // An array or a map, by the keys.
    const usize keys_base = m->keys.Size();
    usize numeric = 0;
    usize strings = 0;
    lua_pushnil(L);
    while (lua_next(L, index) != 0)
    {
        const int kt = lua_type(L, -2);
        if (kt == LUA_TNUMBER)
        {
            ++numeric;
        }
        else if (kt == LUA_TSTRING)
        {
            ++strings;
            usize len = 0;
            const char* s = lua_tolstring(L, -2, &len);
            if (len > (usize{1} << 20) || !Spend(m, len))
            {
                lua_pop(L, 2);
                m->keys.Resize(keys_base);
                return Fail(m, "a map key is too long, or the value is too large");
            }
            m->keys.Push(StringView(s, len));
        }
        else
        {
            lua_pop(L, 2);
            m->keys.Resize(keys_base);
            return Fail(m, "a table key must be a number or a string");
        }
        lua_pop(L, 1);
        if (numeric + strings > kMaxContainerItems)
        {
            lua_pop(L, 1);
            m->keys.Resize(keys_base);
            return Fail(m, "a table holds too many items");
        }
    }
    if (numeric > 0 && strings > 0)
    {
        m->keys.Resize(keys_base);
        return Fail(m, "a table mixes array and map keys");
    }
    if (strings == 0 && numeric == 0 && tag == kTagMap)
    {
        *out = Value::MakeMap();
        return true;
    }
    if (strings == 0)
    {
        const usize n = static_cast<usize>(lua_objlen(L, index));
        if (numeric != n)
        {
            return Fail(m, "an array table must have the keys 1..n and nothing else");
        }
        *out = Value::MakeArray();
        for (usize i = 1; i <= n; ++i)
        {
            lua_rawgeti(L, index, static_cast<int>(i));
            if (lua_type(L, -1) == LUA_TNIL)
            {
                lua_pop(L, 1);
                return Fail(m, "an array table must have the keys 1..n and nothing else");
            }
            Value child;
            const bool ok = ReadValue(L, -1, &child, m, depth + 1);
            lua_pop(L, 1);
            if (!ok)
            {
                return false;
            }
            if (!out->Push(Move(child)))
            {
                return Fail(m, "an array is too large or too deep");
            }
        }
        return true;
    }
    Sort(Span<StringView>(m->keys.Data() + keys_base, m->keys.Size() - keys_base), &KeyLess);
    *out = Value::MakeMap();
    const usize keys_end = m->keys.Size();
    for (usize k = keys_base; k < keys_end; ++k)
    {
        const StringView key = m->keys[k];
        lua_pushlstring(L, key.Data(), key.Length());
        lua_rawget(L, index);
        Value child;
        const bool ok = ReadValue(L, -1, &child, m, depth + 1);
        lua_pop(L, 1);
        if (!ok)
        {
            m->keys.Resize(keys_base);
            return false;
        }
        if (!out->Set(key, Move(child)))
        {
            m->keys.Resize(keys_base);
            return Fail(m, "a map is too large or too deep");
        }
    }
    m->keys.Resize(keys_base);
    return true;
}

// ------------------------------------------------------------------ natives and waits

// What a native call uses: kept in the state so the trampoline's frame holds no owner.
struct NativeScratch
{
    NativeScratch() : outcome() {}
    Vector<Value> args;
    Vector<Value> results;
    Marshal marshal;
    Outcome outcome;
};

struct NativeEntry
{
    NativeFunction function = nullptr;
    void* user = nullptr;
    NativeScratch* scratch = nullptr;
};

enum WaitKind : int
{
    kWaitSeconds,
    kWaitFrames,
    kWaitEvent,
    kWaitTask,
};

// wait(seconds), wait_frames(n), wait_event(name): yield (kind, argument) to whoever resumed the coroutine.
int WaitFunction(lua_State* L)
{
    const int kind = static_cast<int>(lua_tointeger(L, lua_upvalueindex(1)));
    if (kind == kWaitEvent)
    {
        if (lua_type(L, 1) != LUA_TSTRING)
        {
            return luaL_error(L, "wait_event needs an event name");
        }
    }
    else
    {
        if (lua_type(L, 1) != LUA_TNUMBER)
        {
            return luaL_error(L, "a wait needs a number");
        }
        const f64 d = lua_tonumber(L, 1);
        if (!(d >= 0.0) || d > (kind == kWaitFrames ? 4294967295.0 : (kind == kWaitTask ? 9007199254740991.0 : 1.0e12)))
        {
            return luaL_error(L, "a wait needs a finite number in range that is not negative");
        }
    }
    lua_pushinteger(L, kind);
    lua_pushvalue(L, 1);
    return lua_yield(L, 2);
}

// Converts the arguments, runs the native, checks its results. Returns null or a message (kept alive by the scratch).
// It calls no Lua function that raises except in a Lua out-of-memory, where the temporaries it holds are lost.
const char* RunNative(lua_State* L, NativeEntry* e)
{
    NativeScratch* s = e->scratch;
    s->marshal.nodes = 0;
    s->marshal.bytes = 0;
    s->marshal.problem = nullptr;
    s->marshal.keys.Clear();
    s->args.Clear();
    s->results.Clear();
    const int n = lua_gettop(L);
    for (int i = 1; i <= n; ++i)
    {
        Value v;
        if (!ReadValue(L, i, &v, &s->marshal, 0))
        {
            return s->marshal.problem;
        }
        s->args.Push(Move(v));
    }
    s->outcome = e->function(e->user, Span<const Value>(s->args.Data(), s->args.Size()), &s->results);
    if (!s->outcome.IsOk())
    {
        return s->outcome.message.Length() > 0 ? s->outcome.message.CStr() : "a native function failed";
    }
    if (s->results.Size() > kMaxResults)
    {
        return "a native function returned too many values";
    }
    const char* why = nullptr;
    for (usize i = 0; i < s->results.Size(); ++i)
    {
        if (!Representable(s->results[i], &why))
        {
            return why;
        }
    }
    return nullptr;
}

// Answers "where was this native called from": the Lua function one level up (level 0 is the native itself).
bool LuaCallerLocation(void* context, SourceLocation* out)
{
    lua_State* L = static_cast<lua_State*>(context);
    lua_Debug info;
    if (lua_getstack(L, 1, &info) == 0 || lua_getinfo(L, "Sl", &info) == 0)
    {
        return false;
    }
    const char* name = info.short_src;
    usize length = CStrLength(name);
    out->truncated = length > sizeof(out->chunk) - 1;
    length = length < sizeof(out->chunk) - 1 ? length : sizeof(out->chunk) - 1;
    MemCopy(out->chunk, name, length);
    out->chunk[length] = '\0';
    out->line = info.currentline > 0 ? static_cast<u32>(info.currentline) : 0u;
    return true;
}

// The Lua closure behind a registered native. Its frame holds nothing with a destructor.
int Trampoline(lua_State* L)
{
    NativeEntry* e = static_cast<NativeEntry*>(lua_touserdata(L, lua_upvalueindex(1)));
    SetCallerLocationProvider(&LuaCallerLocation, L);
    const char* problem = RunNative(L, e);
    SetCallerLocationProvider(nullptr, nullptr);
    if (problem != nullptr)
    {
        return luaL_error(L, "%s", problem);
    }
    const int count = static_cast<int>(e->scratch->results.Size());
    luaL_checkstack(L, count + 4, "too many results");
    for (int i = 0; i < count; ++i)
    {
        PushValue(L, e->scratch->results[static_cast<usize>(i)]);
    }
    return count;
}

// ------------------------------------------------------------------ protected operations

struct Suspended
{
    u64 id = 0;
    int ref = LUA_NOREF;
};

enum class Op : int
{
    OpenLibs,
    Load,
    Call,
    Unref,
    Register,
    Collect,
    Compile,
};

// The input and output of one protected operation; it lives in the frame that calls `lua_cpcall`.
struct Request
{
    Request()
        : event(HeapAllocator::Default()), error_text(HeapAllocator::Default()), trace_text(HeapAllocator::Default())
    {
    }

    Op op = Op::Collect;
    // Load
    const char* chunk = nullptr;  // "=name", null terminated
    StringView source;
    Vector<byte>* bytecode = nullptr;  // Compile: receives the dumped chunk, when not null
    // Call and Resume
    StringView path;
    Span<const Value> args;
    bool resume = false;
    int ref = LUA_NOREF;  // the coroutine to resume, or the ref to drop (Unref)
    CallOutput* out = nullptr;
    // OpenLibs
    bool budgeted = true;
    u32 seed = 1;
    // Register
    StringView name;
    NativeEntry* native = nullptr;
    // Results
    Marshal marshal;
    Value scratch_value;
    Status status = Status::Ok;
    const char* failure = nullptr;
    int new_ref = LUA_NOREF;  // the coroutine's anchor (the caller drops it unless the call suspended)
    bool suspended = false;
    int wait_kind = 0;
    f64 wait_number = 0;
    Text event;
    Text error_text;
    Text trace_text;
};

// Moves the error value on top of `from` to L and builds the message and trace into the request.
void TakeError(lua_State* L, lua_State* from, Request* r)
{
    if (from != L)
    {
        lua_xmove(from, L, 1);
    }
    if (lua_type(L, -1) != LUA_TSTRING)
    {
        lua_pushfstring(L, "(error object is a %s value)", luaL_typename(L, -1));
        lua_replace(L, -2);
    }
    usize len = 0;
    const char* msg = lua_tolstring(L, -1, &len);
    len = len < kMaxMessageBytes ? len : kMaxMessageBytes;
    lua_pushlstring(L, msg, len);  // a null-terminated copy of the cut message
    msg = lua_tostring(L, -1);
    r->error_text = MakeText(StringView(msg, len));
    luaL_traceback(L, from, msg, 0);
    usize tlen = 0;
    const char* tb = lua_tolstring(L, -1, &tlen);
    tlen = tlen < kMaxTraceBytes ? tlen : kMaxTraceBytes;
    r->trace_text = MakeText(StringView(tb, tlen));
    lua_pop(L, 3);
}

// A pcall message handler: the message and the traceback in one string.
int TracebackHandler(lua_State* L)
{
    if (lua_type(L, 1) != LUA_TSTRING)
    {
        lua_pushstring(L, "(the error value is not a string)");
        lua_replace(L, 1);
    }
    usize len = 0;
    const char* msg = lua_tolstring(L, 1, &len);
    len = len < kMaxMessageBytes ? len : kMaxMessageBytes;
    lua_pushlstring(L, msg, len);
    luaL_traceback(L, L, lua_tostring(L, -1), 1);
    return 1;
}

int ReadOnlyError(lua_State* L)
{
    return luaL_error(L, "attempt to modify a read-only table");
}

// rawset(t, k, v) except on the engine's read-only views (rawset would write into the proxy itself).
int GuardedRawSet(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    luaL_checkany(L, 2);
    luaL_checkany(L, 3);
    lua_getfield(L, LUA_REGISTRYINDEX, "engine.proxies");
    lua_pushvalue(L, 1);
    lua_rawget(L, -2);
    if (lua_toboolean(L, -1) != 0)
    {
        return luaL_error(L, "attempt to modify a read-only table");
    }
    lua_settop(L, 3);
    lua_rawset(L, 1);
    lua_settop(L, 1);
    return 1;
}

int TrippedFunction(lua_State* L)
{
    lua_pushboolean(L, AccountingOf(L)->tripped ? 1 : 0);
    return 1;
}

// print(...) goes to the engine log (channel LogScript, Info), arguments separated by tabs.
int PrintFunction(lua_State* L)
{
    char line[400];
    usize used = 0;
    const int n = lua_gettop(L);
    for (int i = 1; i <= n && used + 1 < sizeof line; ++i)
    {
        const char* text = nullptr;
        usize len = 0;
        if (lua_type(L, i) == LUA_TSTRING || lua_type(L, i) == LUA_TNUMBER)
        {
            text = lua_tolstring(L, i, &len);
        }
        else
        {
            text = lua_type(L, i) == LUA_TNIL
                       ? "nil"
                       : (lua_type(L, i) == LUA_TBOOLEAN ? (lua_toboolean(L, i) != 0 ? "true" : "false")
                                                         : luaL_typename(L, i));
            len = CStrLength(text);
        }
        if (i > 1)
        {
            line[used++] = '\t';
        }
        const usize room = sizeof line - 1 - used;
        len = len < room ? len : room;
        MemCopy(line + used, text, len);
        used += len;
    }
    Log(LogScript, LogVerbosity::Info, StringView(line, used));
    return 0;
}

// Turns the table on top of the stack into a read-only view: a proxy whose metatable reads through to the table and
// refuses writes, and hides itself from getmetatable. Replaces the table on the stack with the proxy.
void MakeReadOnlyProxy(lua_State* L)
{
    lua_newtable(L);  // proxy
    lua_getfield(L, LUA_REGISTRYINDEX, "engine.proxies");
    lua_pushvalue(L, -2);
    lua_pushboolean(L, 1);
    lua_rawset(L, -3);  // remembered, so rawset can refuse it
    lua_pop(L, 1);
    lua_newtable(L);  // metatable
    lua_pushvalue(L, -3);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, &ReadOnlyError);
    lua_setfield(L, -2, "__newindex");
    lua_pushboolean(L, 0);
    lua_setfield(L, -2, "__metatable");
    lua_setmetatable(L, -2);
    lua_remove(L, -2);
}

// The Lua half of the sandbox. It runs once with the C helpers as arguments: it wraps the functions that could swallow
// a budget error so they raise it again, and removes what scripts must not reach. In trusted states the check is never
// true.
const char kBootstrap[] = "local tripped = ...\n"
                          "local error0, pcall0, xpcall0 = error, pcall, xpcall\n"
                          "local co = coroutine\n"
                          "local create0, resume0 = co.create, co.resume\n"
                          "local function chk(...)\n"
                          "  if tripped() then error0('budget exceeded', 0) end\n"
                          "  return ...\n"
                          "end\n"
                          "function pcall(...) return chk(pcall0(...)) end\n"
                          "function xpcall(f, h) return chk(xpcall0(f, h)) end\n"
                          "function co.resume(c, ...) return chk(resume0(c, ...)) end\n"
                          "function co.wrap(f)\n"
                          "  local c = create0(f)\n"
                          "  return function(...)\n"
                          "    local r = { resume0(c, ...) }\n"
                          "    chk()\n"
                          "    if not r[1] then error0(r[2], 0) end\n"
                          "    return unpack(r, 2)\n"
                          "  end\n"
                          "end\n"
                          "co.running = nil\n";

// The operators of the value types (Day 450): the tagged tables behave like the math types in scripts. Run once with
// the five metatables as arguments, before the libraries become read-only views. Results of the operators carry the
// same metatable, so they cross back into the engine as the same kind of value.
const char kOperators[] =
    "local V2, V3, V4, Q, E = ...\n"
    "local setmt, type, error0, fmt, sqrt = setmetatable, type, error, string.format, math.sqrt\n"
    "local concat = table.concat\n"
    "local function bad(op, a, b)\n"
    "  error0('attempt to ' .. op .. ' ' .. (type(a) == 'table' and 'a value' or type(a)) .. ' and ' ..\n"
    "         (type(b) == 'table' and 'a value' or type(b)), 3)\n"
    "end\n"
    "local function common(mt, F, name)\n"
    "  local n = #F\n"
    "  mt.__eq = function(a, b)\n"
    "    for i = 1, n do local f = F[i] if a[f] ~= b[f] then return false end end\n"
    "    return true\n"
    "  end\n"
    "  mt.__tostring = function(a)\n"
    "    local parts = {}\n"
    "    for i = 1, n do parts[i] = fmt('%g', a[F[i]]) end\n"
    "    return name .. '(' .. concat(parts, ', ') .. ')'\n"
    "  end\n"
    "  mt.__metatable = false\n"
    "end\n"
    "local function vector(mt, F, name)\n"
    "  local n = #F\n"
    "  common(mt, F, name)\n"
    "  local function pair(op, fn)\n"
    "    return function(a, b)\n"
    "      if type(a) ~= 'table' or type(b) ~= 'table' then bad(op, a, b) end\n"
    "      local r = {}\n"
    "      for i = 1, n do local f = F[i] r[f] = fn(a[f], b[f]) end\n"
    "      return setmt(r, mt)\n"
    "    end\n"
    "  end\n"
    "  mt.__add = pair('add', function(x, y) return x + y end)\n"
    "  mt.__sub = pair('subtract', function(x, y) return x - y end)\n"
    "  local mul = pair('multiply', function(x, y) return x * y end)\n"
    "  mt.__mul = function(a, b)\n"
    "    local r = {}\n"
    "    if type(a) == 'number' and type(b) == 'table' then\n"
    "      for i = 1, n do local f = F[i] r[f] = a * b[f] end\n"
    "    elseif type(b) == 'number' and type(a) == 'table' then\n"
    "      for i = 1, n do local f = F[i] r[f] = a[f] * b end\n"
    "    else\n"
    "      return mul(a, b)\n"
    "    end\n"
    "    return setmt(r, mt)\n"
    "  end\n"
    "  mt.__div = function(a, b)\n"
    "    if type(a) ~= 'table' or type(b) ~= 'number' then bad('divide', a, b) end\n"
    "    local r = {}\n"
    "    for i = 1, n do local f = F[i] r[f] = a[f] / b end\n"
    "    return setmt(r, mt)\n"
    "  end\n"
    "  mt.__unm = function(a)\n"
    "    local r = {}\n"
    "    for i = 1, n do local f = F[i] r[f] = -a[f] end\n"
    "    return setmt(r, mt)\n"
    "  end\n"
    "end\n"
    "vector(V2, {'x', 'y'}, 'vec2')\n"
    "vector(V3, {'x', 'y', 'z'}, 'vec3')\n"
    "vector(V4, {'x', 'y', 'z', 'w'}, 'vec4')\n"
    "common(Q, {'x', 'y', 'z', 'w'}, 'quat')\n"
    "common(E, {'index', 'generation'}, 'entity')\n"
    "Q.__mul = function(a, b)\n"
    "  if type(a) ~= 'table' or type(b) ~= 'table' then bad('multiply', a, b) end\n"
    "  if b.w == nil then\n"
    "    -- rotate the vec3 b by the quaternion a: v + 2w(q x v) + 2 q x (q x v)\n"
    "    local qx, qy, qz, w = a.x, a.y, a.z, a.w\n"
    "    local cx, cy, cz = qy * b.z - qz * b.y, qz * b.x - qx * b.z, qx * b.y - qy * b.x\n"
    "    local dx, dy, dz = qy * cz - qz * cy, qz * cx - qx * cz, qx * cy - qy * cx\n"
    "    return setmt({ x = b.x + 2 * (w * cx + dx), y = b.y + 2 * (w * cy + dy), z = b.z + 2 * (w * cz + dz) }, V3)\n"
    "  end\n"
    "  return setmt({ x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,\n"
    "                 y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,\n"
    "                 z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,\n"
    "                 w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z }, Q)\n"
    "end\n";

void OpenLibs(lua_State* L, Request* r)
{
    lua_newtable(L);
    lua_setfield(L, LUA_REGISTRYINDEX, "engine.proxies");
    for (int t = 0; t < kTagCount; ++t)
    {
        lua_newtable(L);
        lua_setfield(L, LUA_REGISTRYINDEX, kTagNames[t]);
    }
    struct Lib
    {
        const char* name;
        lua_CFunction open;
    };
    static const Lib libs[] = {{"", luaopen_base},
                               {LUA_MATHLIBNAME, luaopen_math},
                               {LUA_STRLIBNAME, luaopen_string},
                               {LUA_TABLIBNAME, luaopen_table},
                               {LUA_BITLIBNAME, luaopen_bit}};
    for (const Lib& lib : libs)
    {
        lua_pushcfunction(L, lib.open);
        lua_pushstring(L, lib.name);
        lua_call(L, 1, 0);
    }
    if (r->budgeted)
    {
        luaJIT_setmode(L, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_OFF);
    }
    // The globals now are the engine environment E. Remove what scripts must not reach.
    static const char* const removed[] = {"dofile",  "loadfile", "load",           "loadstring", "newproxy", "getfenv",
                                          "setfenv", "gcinfo",   "collectgarbage", "module",     "require"};
    for (const char* name : removed)
    {
        lua_pushnil(L);
        lua_setglobal(L, name);
    }
    lua_getglobal(L, "string");
    lua_pushnil(L);
    lua_setfield(L, -2, "dump");  // it would let a script make bytecode
    InstallSafePatterns(L);
    lua_pop(L, 1);
    // A seed for math.random, so every run of a state repeats.
    lua_getglobal(L, "math");
    lua_getfield(L, -1, "randomseed");
    lua_pushinteger(L, static_cast<lua_Integer>(r->seed));
    lua_call(L, 1, 0);
    lua_pop(L, 1);
    // The string metatable is not reachable from scripts.
    lua_pushstring(L, "");
    lua_getmetatable(L, -1);
    lua_pushboolean(L, 0);
    lua_setfield(L, -2, "__metatable");
    lua_pop(L, 2);
    static const struct
    {
        const char* name;
        int kind;
    } waits[] = {
        {"wait", kWaitSeconds}, {"wait_frames", kWaitFrames}, {"wait_event", kWaitEvent}, {"wait_task", kWaitTask}};
    for (const auto& w : waits)
    {
        lua_pushinteger(L, w.kind);
        lua_pushcclosure(L, &WaitFunction, 1);
        lua_setglobal(L, w.name);
    }
    lua_pushcfunction(L, &PrintFunction);
    lua_setglobal(L, "print");
    lua_pushcfunction(L, &GuardedRawSet);
    lua_setglobal(L, "rawset");
    // The Lua half (runs with the original pcall, resume ... in hand).
    if (luaL_loadbuffer(L, kBootstrap, CStrLength(kBootstrap), "=sandbox") != 0)
    {
        lua_error(L);
    }
    lua_pushcfunction(L, &TrippedFunction);
    lua_call(L, 1, 0);
    if (luaL_loadbuffer(L, kOperators, CStrLength(kOperators), "=operators") != 0)
    {
        lua_error(L);
    }
    for (int t = kTagVec2; t <= kTagEntity; ++t)
    {
        lua_getfield(L, LUA_REGISTRYINDEX, kTagNames[t]);
    }
    lua_call(L, 5, 0);
    // The libraries become read-only views (their real tables stay in the registry only through the views).
    static const char* const libraries[] = {"string", "math", "table", "bit", "coroutine"};
    for (const char* name : libraries)
    {
        lua_getglobal(L, name);
        MakeReadOnlyProxy(L);
        lua_setglobal(L, name);
    }
    // E is kept in the registry; scripts get a fresh environment S that reads through to E and is theirs to write.
    lua_pushvalue(L, LUA_GLOBALSINDEX);
    lua_pushnil(L);
    lua_setfield(L, -2, "_G");
    lua_setfield(L, LUA_REGISTRYINDEX, "engine.env");
    lua_newtable(L);  // roots of the registered native tables
    lua_setfield(L, LUA_REGISTRYINDEX, "engine.roots");
    lua_newtable(L);  // S
    lua_newtable(L);  // its metatable
    lua_getfield(L, LUA_REGISTRYINDEX, "engine.env");
    lua_setfield(L, -2, "__index");
    lua_pushboolean(L, 0);
    lua_setfield(L, -2, "__metatable");
    lua_setmetatable(L, -2);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "_G");
    lua_replace(L, LUA_GLOBALSINDEX);
}

// Pushes the value at the end of a dotted path, or nil when a step is missing. The first name is looked up in the
// script environment and then in the engine environment; deeper names are raw.
void ResolvePath(lua_State* L, StringView path)
{
    usize start = 0;
    bool first = true;
    lua_pushvalue(L, LUA_GLOBALSINDEX);
    for (usize i = 0; i <= path.Length(); ++i)
    {
        if (i == path.Length() || path[i] == '.')
        {
            if (lua_type(L, -1) != LUA_TTABLE)
            {
                lua_pop(L, 1);
                lua_pushnil(L);
                return;
            }
            lua_pushlstring(L, path.Data() + start, i - start);
            lua_rawget(L, -2);
            if (first && lua_type(L, -1) == LUA_TNIL)
            {
                lua_pop(L, 1);
                lua_getfield(L, LUA_REGISTRYINDEX, "engine.env");
                lua_pushlstring(L, path.Data() + start, i - start);
                lua_rawget(L, -2);
                lua_remove(L, -2);
            }
            lua_remove(L, -2);
            first = false;
            start = i + 1;
        }
    }
}

// Registers a native in the engine environment. The first segment of a dotted name is a table the scripts see through a
// read-only view; the real table is kept in the registry for further registrations.
void RegisterNative(lua_State* L, Request* r)
{
    const StringView name = r->name;
    lua_getfield(L, LUA_REGISTRYINDEX, "engine.env");
    lua_getfield(L, LUA_REGISTRYINDEX, "engine.roots");  // stack: E roots
    usize start = 0;
    bool in_table = false;  // whether a table to put into is on top of the stack (above E and roots)
    for (usize i = 0; i <= name.Length(); ++i)
    {
        if (i < name.Length() && name[i] != '.')
        {
            continue;
        }
        const char* seg = name.Data() + start;
        const usize seg_len = i - start;
        if (i == name.Length())
        {
            lua_pushlstring(L, seg, seg_len);
            lua_pushlightuserdata(L, r->native);
            lua_pushcclosure(L, &Trampoline, 1);
            lua_rawset(L, in_table ? -3 : -4);
            return;
        }
        if (!in_table)
        {
            lua_pushlstring(L, seg, seg_len);
            lua_rawget(L, -2);  // roots[seg]
            if (lua_type(L, -1) != LUA_TTABLE)
            {
                lua_pop(L, 1);
                lua_newtable(L);  // R
                lua_pushlstring(L, seg, seg_len);
                lua_pushvalue(L, -2);
                lua_rawset(L, -4);  // roots[seg] = R
                lua_pushlstring(L, seg, seg_len);
                lua_pushvalue(L, -2);
                MakeReadOnlyProxy(L);  // proxy(R) replaces its copy
                lua_rawset(L, -5);     // E[seg] = proxy
            }
            in_table = true;
        }
        else
        {
            lua_pushlstring(L, seg, seg_len);
            lua_rawget(L, -2);
            if (lua_type(L, -1) != LUA_TTABLE)
            {
                lua_pop(L, 1);
                lua_newtable(L);
                lua_pushlstring(L, seg, seg_len);
                lua_pushvalue(L, -2);
                lua_rawset(L, -4);
            }
            lua_remove(L, -2);
        }
        start = i + 1;
    }
}

int DumpWriter(lua_State*, const void* data, size_t size, void* user)
{
    Vector<byte>* out = static_cast<Vector<byte>*>(user);
    const byte* p = static_cast<const byte*>(data);
    for (size_t i = 0; i < size; ++i)
    {
        out->Push(p[i]);
    }
    return 0;
}

// Compiles without running; dumps the chunk when asked.
void CompileChunk(lua_State* L, Request* r)
{
    const int rc = luaL_loadbufferx(L, r->source.Data(), r->source.Length(), r->chunk, "t");
    if (rc != 0)
    {
        r->status = rc == LUA_ERRMEM ? Status::OutOfMemory : Status::SyntaxError;
        TakeError(L, L, r);
        r->trace_text = MakeText(r->error_text.View());
        return;
    }
    if (r->bytecode != nullptr && lua_dump(L, &DumpWriter, r->bytecode) != 0)
    {
        r->status = Status::RuntimeError;
        r->failure = "the chunk could not be dumped";
    }
    lua_pop(L, 1);
}

void LoadChunk(lua_State* L, Request* r)
{
    const int rc = luaL_loadbufferx(L, r->source.Data(), r->source.Length(), r->chunk,
                                    "t");  // text only: bytecode is not verified
    if (rc != 0)
    {
        r->status = rc == LUA_ERRMEM ? Status::OutOfMemory : Status::SyntaxError;
        TakeError(L, L, r);
        r->trace_text = MakeText(r->error_text.View());
        return;
    }
    lua_pushcfunction(L, &TracebackHandler);
    lua_insert(L, 1);
    Accounting* acct = AccountingOf(L);
    const bool hooked = acct->instructions_limited || acct->deadline_ns != 0;
    if (hooked)
    {
        lua_sethook(L, &BudgetHook, LUA_MASKCOUNT, static_cast<int>(kHookCount));
    }
    const int run = lua_pcall(L, 0, 0, 1);
    if (hooked)
    {
        lua_sethook(L, nullptr, 0, 0);
    }
    if (run != 0)
    {
        r->status = run == LUA_ERRMEM ? Status::OutOfMemory
                                      : (acct->tripped ? Status::BudgetExceeded : Status::RuntimeError);
        usize len = 0;
        const char* full = lua_tolstring(L, -1, &len);
        len = len < kMaxMessageBytes + kMaxTraceBytes ? len : kMaxMessageBytes + kMaxTraceBytes;
        const StringView text(full != nullptr ? full : "", full != nullptr ? len : 0);
        const usize at = text.Find(StringView("\nstack traceback:"));
        r->error_text = MakeText(text.Substring(0, at));
        r->trace_text = MakeText(at < text.Length() ? text.Substring(at + 1) : StringView());
    }
}

// Starts a call (a new coroutine around the function) or resumes a suspended one, and runs it until it ends or yields.
void RunCoroutine(lua_State* L, Request* r)
{
    lua_State* T = nullptr;
    int nargs = static_cast<int>(r->args.Size());
    if (lua_checkstack(L, nargs + 8) == 0)
    {
        r->failure = "too many arguments";
        r->status = Status::Invalid;
        return;
    }
    if (!r->resume)
    {
        T = lua_newthread(L);
        r->new_ref = luaL_ref(L, LUA_REGISTRYINDEX);
        ResolvePath(L, r->path);
        if (lua_type(L, -1) != LUA_TFUNCTION)
        {
            r->failure = "attempt to call a nil value";
            r->status = Status::RuntimeError;
            return;
        }
    }
    else
    {
        lua_rawgeti(L, LUA_REGISTRYINDEX, r->ref);
        T = lua_tothread(L, -1);
        lua_pop(L, 1);
        r->new_ref = r->ref;
        if (T == nullptr || lua_status(T) != LUA_YIELD)
        {
            r->failure = "that call is not suspended";
            r->status = Status::Invalid;
            return;
        }
    }
    for (usize i = 0; i < r->args.Size(); ++i)
    {
        PushValue(L, r->args[i]);
    }
    lua_xmove(L, T, nargs + (r->resume ? 0 : 1));
    Accounting* acct = AccountingOf(L);
    const bool hooked = acct->instructions_limited || acct->deadline_ns != 0;
    if (hooked)
    {
        lua_sethook(L, &BudgetHook, LUA_MASKCOUNT, static_cast<int>(kHookCount));
    }
    const int rc = lua_resume(T, nargs);
    if (hooked)
    {
        lua_sethook(L, nullptr, 0, 0);
    }
    if (rc == 0)
    {
        const int n = lua_gettop(T);
        if (static_cast<usize>(n) > kMaxResults || lua_checkstack(L, n + 8) == 0)
        {
            r->failure = "a function returned too many values";
            r->status = Status::Invalid;
            return;
        }
        lua_xmove(T, L, n);
        for (int i = 1; i <= n; ++i)
        {
            if (!ReadValue(L, i, &r->scratch_value, &r->marshal, 0))
            {
                r->failure = r->marshal.problem;
                r->status = Status::Invalid;
                return;
            }
            r->out->results.Push(Move(r->scratch_value));
        }
        return;
    }
    if (rc == LUA_YIELD)
    {
        if (lua_gettop(T) < 2)
        {
            r->failure = "a coroutine yielded without a wait request";
            r->status = Status::RuntimeError;
            return;
        }
        lua_xmove(T, L, lua_gettop(T));
        const int top = lua_gettop(L);
        r->wait_kind = static_cast<int>(lua_tointeger(L, top - 1));
        const f64 amount = lua_type(L, top) == LUA_TNUMBER ? lua_tonumber(L, top) : -1.0;
        const bool good_kind = r->wait_kind >= kWaitSeconds && r->wait_kind <= kWaitTask;
        const bool good_arg = r->wait_kind == kWaitEvent
                                  ? lua_type(L, top) == LUA_TSTRING
                                  : (amount >= 0.0 && amount <= (r->wait_kind == kWaitFrames ? 4294967295.0
                                                                 : r->wait_kind == kWaitTask ? 9007199254740991.0
                                                                                             : 1.0e12));
        if (!good_kind || !good_arg)
        {
            r->failure = "a coroutine yielded something that is not a wait request";
            r->status = Status::RuntimeError;
            return;
        }
        if (r->wait_kind == kWaitEvent)
        {
            usize len = 0;
            const char* ev = lua_tolstring(L, top, &len);
            r->event = MakeText(StringView(ev, len));
        }
        else
        {
            r->wait_number = lua_tonumber(L, top);
        }
        r->suspended = true;
        return;
    }
    if (rc == LUA_ERRMEM)
    {
        // Building a message and a traceback would allocate again at the cap; the status says it.
        r->status = Status::OutOfMemory;
        r->error_text = MakeText("not enough memory");
        return;
    }
    r->status = acct->tripped ? Status::BudgetExceeded : Status::RuntimeError;
    TakeError(L, T, r);
}

int ProtectedBody(lua_State* L)
{
    Request* r = static_cast<Request*>(lua_touserdata(L, 1));
    lua_settop(L, 0);
    switch (r->op)
    {
    case Op::OpenLibs: OpenLibs(L, r); break;
    case Op::Collect: lua_gc(L, LUA_GCCOLLECT, 0); break;
    case Op::Unref: luaL_unref(L, LUA_REGISTRYINDEX, r->ref); break;
    case Op::Register: RegisterNative(L, r); break;
    case Op::Load: LoadChunk(L, r); break;
    case Op::Compile: CompileChunk(L, r); break;
    case Op::Call: RunCoroutine(L, r); break;
    }
    return 0;
}

// ------------------------------------------------------------------ the state and the backend

class LuaJitState : public State
{
public:
    LuaJitState(u64 memory_cap, const LuaJitOptions& options) : options_(options)
    {
        state_cap_ = memory_cap != 0 ? memory_cap : kDefaultMemoryCap;
        accounting_.cap = state_cap_;
    }

    ~LuaJitState() override { DoClose(); }

    // Opens the Lua state; false when it cannot be made.
    bool Open()
    {
        L_ = lua_newstate(&CountingAlloc, &accounting_);
        if (L_ == nullptr)
        {
            return false;
        }
        Request r;
        r.op = Op::OpenLibs;
        r.budgeted = options_.budgeted;
        r.seed = options_.random_seed;
        const Outcome o = Execute(&r);
        if (!o.IsOk())
        {
            DoClose();
            return false;
        }
        return true;
    }

protected:
    Outcome DoLoad(StringView chunk_name, StringView source) override
    {
        if (L_ == nullptr)
        {
            return Outcome(Status::Invalid, "the state is closed");
        }
        if (chunk_name.Length() == 0)
        {
            return Outcome(Status::Invalid, "empty chunk name");
        }
        Text chunk = MakeText("=");
        chunk.Append(chunk_name);
        Request r;
        r.op = Op::Load;
        r.chunk = chunk.CStr();
        r.source = source;
        // A chunk's top level runs under a default budget in a budgeted state (a trusted state has none).
        Budget load_budget;
        if (options_.budgeted)
        {
            load_budget.instructions = kLoadInstructions;
            load_budget.nanoseconds = kLoadNanoseconds;
        }
        BeginBudget(load_budget);
        Outcome o = Execute(&r);
        EndBudget();
        return o;
    }

    Outcome DoCompile(StringView chunk_name, StringView source, Vector<byte>* bytecode) override
    {
        if (L_ == nullptr)
        {
            return Outcome(Status::Invalid, "the state is closed");
        }
        if (chunk_name.Length() == 0)
        {
            return Outcome(Status::Invalid, "empty chunk name");
        }
        Text chunk = MakeText("=");
        chunk.Append(chunk_name);
        Request r;
        r.op = Op::Compile;
        r.chunk = chunk.CStr();
        r.source = source;
        r.bytecode = bytecode;
        return Execute(&r);
    }

    Outcome DoCall(StringView path, Span<const Value> args, const Budget& budget, CallOutput* out) override
    {
        if (L_ == nullptr)
        {
            return Outcome(Status::Invalid, "the state is closed");
        }
        if (path.Length() == 0)
        {
            return Outcome(Status::Invalid, "empty function path");
        }
        Outcome refused = CheckCall(args, budget);
        if (!refused.IsOk())
        {
            return refused;
        }
        Request r;
        r.op = Op::Call;
        r.path = path;
        r.args = args;
        r.out = out;
        return Finish(&r, next_id_++, out, budget);
    }

    Outcome DoResume(u64 coroutine, Span<const Value> values, const Budget& budget, CallOutput* out) override
    {
        if (L_ == nullptr)
        {
            return Outcome(Status::Invalid, "the state is closed");
        }
        Outcome refused = CheckCall(values, budget);
        if (!refused.IsOk())
        {
            return refused;
        }
        for (usize i = 0; i < suspended_.Size(); ++i)
        {
            if (coroutine != 0 && suspended_[i].id == coroutine)
            {
                Request r;
                r.op = Op::Call;
                r.resume = true;
                r.ref = suspended_[i].ref;
                r.args = values;
                r.out = out;
                suspended_.Remove(i);
                return Finish(&r, coroutine, out, budget);
            }
        }
        return Outcome(Status::Invalid, "that call is not suspended in this state");
    }

    void DoAbandon(u64 coroutine) override
    {
        for (usize i = 0; i < suspended_.Size(); ++i)
        {
            if (suspended_[i].id == coroutine)
            {
                Drop(suspended_[i].ref);
                suspended_.Remove(i);
                return;
            }
        }
    }

    Outcome DoRegisterFunction(StringView name, NativeFunction function, void* user) override
    {
        if (L_ == nullptr)
        {
            return Outcome(Status::Invalid, "the state is closed");
        }
        if (name.Length() == 0 || function == nullptr)
        {
            return Outcome(Status::Invalid, "a native function needs a name and a function");
        }
        UniquePtr<NativeEntry> entry = MakeUnique<NativeEntry>();
        entry->function = function;
        entry->user = user;
        entry->scratch = &scratch_;
        Request r;
        r.op = Op::Register;
        r.name = name;
        r.native = entry.Get();
        Outcome o = Execute(&r);
        if (o.IsOk())
        {
            natives_.Push(Move(entry));
        }
        return o;
    }

    void DoCollect() override
    {
        if (L_ != nullptr)
        {
            Request r;
            r.op = Op::Collect;
            accounting_.cap += kCleanupSlack;
            Execute(&r);
            accounting_.cap -= kCleanupSlack;
        }
    }

    usize DoMemoryUsed() override { return static_cast<usize>(accounting_.used); }

    void DoClose() override
    {
        if (L_ != nullptr)
        {
            lua_close(L_);
            L_ = nullptr;
        }
        suspended_.Clear();
        natives_.Clear();
    }

    Capabilities DoCapabilities() const override
    {
        Capabilities caps;
        caps.coroutines = true;
        caps.instruction_budget = options_.budgeted;
        caps.time_budget = options_.budgeted;
        caps.memory_budget = true;
        caps.jit = !options_.budgeted;
        return caps;
    }

private:
    Outcome CheckCall(Span<const Value> args, const Budget& budget) const
    {
        if (!options_.budgeted && (budget.instructions != 0 || budget.nanoseconds != 0))
        {
            return Outcome(Status::Unsupported, "a trusted (JIT) state cannot count instructions or time");
        }
        for (usize i = 0; i < args.Size(); ++i)
        {
            if (args[i].Check(ValueLimits()) != ValueCheck::Ok)
            {
                return Outcome(Status::Invalid, "an argument is over the value limits");
            }
            const char* why = nullptr;
            if (!Representable(args[i], &why))
            {
                return Outcome(Status::Invalid, why);
            }
        }
        return Outcome();
    }

    // Runs the request under lua_cpcall; a Lua error that escapes the body becomes a failure here.
    Outcome Execute(Request* r)
    {
        const int rc = lua_cpcall(L_, &ProtectedBody, r);
        // A Lua error inside a native (out of memory) skips the trampoline's own withdrawal: never leave a provider
        // that names this state behind.
        SetCallerLocationProvider(nullptr, nullptr);
        if (rc != 0)
        {
            r->status = rc == LUA_ERRMEM ? Status::OutOfMemory : Status::RuntimeError;
            usize len = 0;
            const char* msg = lua_tolstring(L_, -1, &len);
            r->error_text = MakeText(msg != nullptr ? StringView(msg, len) : StringView("a Lua error"));
            lua_settop(L_, 0);
        }
        if (r->marshal.problem != nullptr && r->failure == nullptr)
        {
            r->failure = r->marshal.problem;
            r->status = Status::Invalid;
        }
        if (r->failure != nullptr)
        {
            return Outcome(r->status, StringView(r->failure));
        }
        if (r->status != Status::Ok)
        {
            Outcome o(r->status, r->error_text.View());
            o.trace = MakeText(r->trace_text.View());
            return o;
        }
        return Outcome();
    }

    void Drop(int ref)
    {
        if (L_ != nullptr && ref != LUA_NOREF)
        {
            Request r;
            r.op = Op::Unref;
            r.ref = ref;
            accounting_.cap += kCleanupSlack;
            Execute(&r);
            accounting_.cap -= kCleanupSlack;
        }
    }

    // The call's budget: the hook reads it from the accounting; a memory budget lowers the cap for the call.
    void BeginBudget(const Budget& budget)
    {
        accounting_.tripped = false;
        accounting_.instructions_limited = budget.instructions != 0;
        accounting_.instructions_left = budget.instructions;
        accounting_.deadline_ns = budget.nanoseconds != 0 ? os::MonotonicTimeNanos() + budget.nanoseconds : 0;
        accounting_.cap = budget.memory_bytes != 0 && budget.memory_bytes < state_cap_ ? budget.memory_bytes
                                                                                       : state_cap_;
    }

    void EndBudget()
    {
        accounting_.instructions_limited = false;
        accounting_.deadline_ns = 0;
        accounting_.cap = state_cap_;
    }

    // Executes a call or resume, then settles the coroutine's anchor and the output.
    Outcome Finish(Request* r, u64 id, CallOutput* out, const Budget& budget)
    {
        // The call's budget: the hook reads it from the accounting; a memory budget lowers the cap for the call.
        accounting_.tripped = false;
        accounting_.instructions_limited = budget.instructions != 0;
        accounting_.instructions_left = budget.instructions;
        accounting_.deadline_ns = budget.nanoseconds != 0 ? os::MonotonicTimeNanos() + budget.nanoseconds : 0;
        accounting_.cap = budget.memory_bytes != 0 && budget.memory_bytes < state_cap_ ? budget.memory_bytes
                                                                                       : state_cap_;
        Outcome o = Execute(r);
        accounting_.instructions_limited = false;
        accounting_.deadline_ns = 0;
        accounting_.cap = state_cap_;
        if (o.IsOk() && r->suspended)
        {
            Suspended s;
            s.id = id;
            s.ref = r->new_ref;
            suspended_.Push(s);
            out->coroutine = id;
            out->wait.kind = r->wait_kind == kWaitSeconds  ? WaitRequest::Kind::Seconds
                             : r->wait_kind == kWaitFrames ? WaitRequest::Kind::Frames
                             : r->wait_kind == kWaitTask   ? WaitRequest::Kind::Task
                                                           : WaitRequest::Kind::Event;
            out->wait.task = r->wait_kind == kWaitTask ? static_cast<u64>(r->wait_number) : 0;
            out->wait.seconds = r->wait_kind == kWaitSeconds ? r->wait_number : 0.0;
            out->wait.frames = r->wait_kind == kWaitFrames ? static_cast<u32>(r->wait_number) : 0;
            out->wait.event = MakeText(r->event.View());
            return o;
        }
        Drop(r->new_ref);
        if (!o.IsOk())
        {
            out->results.Clear();
        }
        return o;
    }

    LuaJitOptions options_;
    u64 state_cap_ = 0;
    Accounting accounting_;
    lua_State* L_ = nullptr;
    Vector<UniquePtr<NativeEntry>> natives_;
    Vector<Suspended> suspended_;
    NativeScratch scratch_;
    u64 next_id_ = 1;
};

class LuaJitBackend : public Backend
{
public:
    explicit LuaJitBackend(const LuaJitOptions& options) : options_(options) {}
    StringView Name() const override { return StringView("luajit"); }
    StringView Version() const override { return StringView(LUAJIT_VERSION); }
    Capabilities Caps() const override
    {
        Capabilities caps;
        caps.coroutines = true;
        caps.instruction_budget = options_.budgeted;
        caps.time_budget = options_.budgeted;
        caps.memory_budget = true;
        caps.jit = !options_.budgeted;
        return caps;
    }
    UniquePtr<State> CreateState(u64 memory_cap_bytes) override
    {
        UniquePtr<LuaJitState> state = MakeUnique<LuaJitState>(memory_cap_bytes, options_);
        if (!state->Open())
        {
            return UniquePtr<State>();
        }
        return UniquePtr<State>(state.Release());
    }

private:
    LuaJitOptions options_;
};

UniquePtr<Backend> MakeLuaJitBackend()
{
    return MakeUnique<LuaJitBackend>(LuaJitOptions());
}

}  // namespace

UniquePtr<Backend> CreateLuaJitBackend(const LuaJitOptions& options)
{
    return MakeUnique<LuaJitBackend>(options);
}

bool RegisterLuaJitBackend()
{
    BackendDescriptor descriptor;
    descriptor.name = "luajit";
    descriptor.create = &MakeLuaJitBackend;
    return RegisterBackend(descriptor);
}

}  // namespace engine_placeholder::script::luajit
