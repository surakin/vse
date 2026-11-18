// The binding registry (Phase 27 Day 450): how engine functions become script functions. A binding is one typed C++
// function; the registry derives the argument checks, the conversions, the error messages, the signature text and the
// documentation from its signature, and installs it into any language state (docs/guide/scripting.md, "Bindings").
#pragma once

#include "containers/span.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "ecs/entity.h"
#include "lang/traits.h"
#include "mem/heap_allocator.h"
#include "math/matrix.h"
#include "math/quaternion.h"
#include "math/vector.h"
#include "script/backend.h"
#include "script/value.h"
#include "string/string.h"
#include "string_view.h"
#include "types.h"

namespace engine_placeholder::script
{

/// The thread rule a binding follows. Every binding states one; the registry refuses a binding without.
///
/// - Thread-safety: thread-compatible plain data.
enum class ThreadRule : u8
{
    /// No mark: refused by [[script::BindingRegistry::Add]].
    Unmarked,
    /// Touches nothing but its arguments: callable from any state on any thread, including a task state.
    Pure,
    /// Reads the world as of the tick start.
    WorldRead,
    /// Changes the world only through the partition's command buffer.
    WorldWrite,
    /// Must run on the main thread: a partition job that calls it gets a script error.
    MainThreadOnly,
};

/// The name of a thread rule as the documentation spells it (`pure`, `world-read`, `world-write`, `main-thread-only`).
///
/// - Thread-safety: thread-safe.
/// - Ownership: the text is a literal.
/// - Fails: never.
const char* ThreadRuleName(ThreadRule rule);

/// The name of a value kind as error messages spell it (`number`, `text`, `vec3`, ...).
///
/// - Thread-safety: thread-safe.
/// - Ownership: the text is a literal.
/// - Fails: never.
const char* ValueKindText(ValueKind kind);

/// How a state's bindings ask whether an entity handle is still alive.
///
/// - Thread-safety: thread-compatible plain data; the callback must be callable from the thread that runs the state.
/// - Ownership: `user` is borrowed and must outlive the installation.
struct EntityCheck
{
    /// Returns whether the entity is alive in the world the state sees; null means "any valid-looking handle".
    bool (*is_alive)(void* user, ecs::Entity entity) = nullptr;
    /// Passed to `is_alive`.
    void* user = nullptr;
};

/// Which bindings a state gets and how they check their arguments.
///
/// - Thread-safety: thread-compatible plain data; copied into the installation.
struct InstallProfile
{
    /// A bit per [[script::ThreadRule]] (`1 << rule`) that is installed; a rule not set is left out (a call to it is
    /// "attempt to call a nil value").
    u32 rules = 0;
    /// Whether the state runs on the main thread; a `MainThreadOnly` binding in any other state is installed as a
    /// function that raises a clear error.
    bool main_thread = false;
    /// The liveness check for `Entity` arguments.
    EntityCheck entities;

    /// The bit of one rule.
    ///
    /// - Fails: never.
    static constexpr u32 Bit(ThreadRule rule) { return u32{1} << static_cast<u32>(rule); }

    /// A partition state: every rule but `Unmarked`; `MainThreadOnly` bindings raise.
    ///
    /// - Fails: never.
    static InstallProfile Partition(EntityCheck entities)
    {
        InstallProfile p;
        p.rules = Bit(ThreadRule::Pure) | Bit(ThreadRule::WorldRead) | Bit(ThreadRule::WorldWrite) |
                  Bit(ThreadRule::MainThreadOnly);
        p.entities = entities;
        return p;
    }

    /// The main thread's state: every binding runs.
    ///
    /// - Fails: never.
    static InstallProfile Main(EntityCheck entities)
    {
        InstallProfile p = Partition(entities);
        p.main_thread = true;
        return p;
    }

    /// A task state (Day 461): pure bindings only, no world access.
    ///
    /// - Fails: never.
    static InstallProfile Task()
    {
        InstallProfile p;
        p.rules = Bit(ThreadRule::Pure);
        return p;
    }
};

class BindingRegistry;

/// What the registry keeps about one binding (the documentation reads it).
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns its strings.
struct BindingInfo
{
    /// The stable id: a hash of the full name, never 0.
    u32 id = 0;
    /// The namespace (`math`).
    String<HeapAllocator> ns;
    /// The function's own name (`clamp`).
    String<HeapAllocator> name;
    /// `ns.name`, the name scripts call.
    String<HeapAllocator> full_name;
    /// The signature as documentation spells it (`(f32 x, f32 lo, f32 hi) -> f32`).
    String<HeapAllocator> signature;
    /// The parameter names, comma separated.
    String<HeapAllocator> params;
    /// The description.
    String<HeapAllocator> doc;
    /// The thread rule.
    ThreadRule rule = ThreadRule::Unmarked;
    /// How many parameters the function takes.
    u32 arity = 0;

    /// Creates an empty record.
    BindingInfo()
        : ns(HeapAllocator::Default()), name(HeapAllocator::Default()), full_name(HeapAllocator::Default()),
          signature(HeapAllocator::Default()), params(HeapAllocator::Default()), doc(HeapAllocator::Default())
    {
    }
};

namespace detail
{

// The environment of one call: the profile of the installation and the binding's own record.
struct CallEnv
{
    const BindingInfo* info = nullptr;
    const InstallProfile* profile = nullptr;
};

using InvokeFn = Outcome (*)(const CallEnv& env, Span<const Value> args, Vector<Value>* results);

struct Entry
{
    BindingInfo info;
    InvokeFn invoke = nullptr;
};

template <usize... I> struct Indices
{
};
template <usize N, usize... I> struct MakeIndices : MakeIndices<N - 1, N - 1, I...>
{
};
template <usize... I> struct MakeIndices<0, I...>
{
    using Type = Indices<I...>;
};

template <class R, class... A> struct Signature
{
};
template <class R, class... A> Signature<R, A...> SignatureOf(R (*)(A...));

// How one parameter or result type is checked, read and written.
template <class T> struct Codec;

template <> struct Codec<f32>
{
    static constexpr const char* kName = "f32";
    static constexpr bool kIsEntity = false;
    static bool Check(const Value& v) { return v.IsNumber(); }
    static f32 Get(const Value& v) { return static_cast<f32>(v.ToDouble()); }
    static Value Put(f32 x) { return Value::FromFloat(x); }
};
template <> struct Codec<f64>
{
    static constexpr const char* kName = "f64";
    static constexpr bool kIsEntity = false;
    static bool Check(const Value& v) { return v.IsNumber(); }
    static f64 Get(const Value& v) { return v.ToDouble(); }
    static Value Put(f64 x) { return Value::FromFloat(x); }
};
template <> struct Codec<i32>
{
    static constexpr const char* kName = "i32";
    static constexpr bool kIsEntity = false;
    static bool Check(const Value& v)
    {
        return v.Kind() == ValueKind::Int && v.AsInt() >= -2147483647 - 1 && v.AsInt() <= 2147483647;
    }
    static i32 Get(const Value& v) { return static_cast<i32>(v.AsInt()); }
    static Value Put(i32 x) { return Value::FromInt(x); }
};
template <> struct Codec<i64>
{
    static constexpr const char* kName = "i64";
    static constexpr bool kIsEntity = false;
    static bool Check(const Value& v) { return v.Kind() == ValueKind::Int; }
    static i64 Get(const Value& v) { return v.AsInt(); }
    static Value Put(i64 x) { return Value::FromInt(x); }
};
template <> struct Codec<bool>
{
    static constexpr const char* kName = "bool";
    static constexpr bool kIsEntity = false;
    static bool Check(const Value& v) { return v.Kind() == ValueKind::Bool; }
    static bool Get(const Value& v) { return v.AsBool(); }
    static Value Put(bool x) { return Value::FromBool(x); }
};
template <> struct Codec<StringView>
{
    static constexpr const char* kName = "StringView";
    static constexpr bool kIsEntity = false;
    static bool Check(const Value& v) { return v.Kind() == ValueKind::Text; }
    static StringView Get(const Value& v) { return v.Text(); }
    static Value Put(StringView x) { return Value::FromText(x); }
};
template <> struct Codec<Vec2>
{
    static constexpr const char* kName = "Vec2";
    static constexpr bool kIsEntity = false;
    static bool Check(const Value& v) { return v.Kind() == ValueKind::Vec2; }
    static Vec2 Get(const Value& v) { return v.AsVec2(); }
    static Value Put(Vec2 x) { return Value::FromVec2(x); }
};
template <> struct Codec<Vec3>
{
    static constexpr const char* kName = "Vec3";
    static constexpr bool kIsEntity = false;
    static bool Check(const Value& v) { return v.Kind() == ValueKind::Vec3; }
    static Vec3 Get(const Value& v) { return v.AsVec3(); }
    static Value Put(Vec3 x) { return Value::FromVec3(x); }
};
template <> struct Codec<Vec4>
{
    static constexpr const char* kName = "Vec4";
    static constexpr bool kIsEntity = false;
    static bool Check(const Value& v) { return v.Kind() == ValueKind::Vec4; }
    static Vec4 Get(const Value& v) { return v.AsVec4(); }
    static Value Put(Vec4 x) { return Value::FromVec4(x); }
};
template <> struct Codec<Quaternion>
{
    static constexpr const char* kName = "Quaternion";
    static constexpr bool kIsEntity = false;
    static bool Check(const Value& v) { return v.Kind() == ValueKind::Quat; }
    static Quaternion Get(const Value& v) { return v.AsQuat(); }
    static Value Put(Quaternion x) { return Value::FromQuat(x); }
};
template <> struct Codec<ecs::Entity>
{
    static constexpr const char* kName = "Entity";
    static constexpr bool kIsEntity = true;
    static bool Check(const Value& v) { return v.Kind() == ValueKind::Entity; }
    static ecs::Entity Get(const Value& v) { return v.AsEntity(); }
    static Value Put(ecs::Entity x) { return Value::FromEntity(x); }
};
/// A 4x4 matrix crosses as an array of 16 numbers in column-major order (`m[1..4]` is the first column).
template <> struct Codec<Mat4>
{
    static constexpr const char* kName = "Mat4";
    static constexpr bool kIsEntity = false;
    static bool Check(const Value& v)
    {
        if (v.Kind() != ValueKind::Array || v.Size() != 16)
        {
            return false;
        }
        for (usize i = 0; i < 16; ++i)
        {
            if (!v.At(i).IsNumber())
            {
                return false;
            }
        }
        return true;
    }
    static Mat4 Get(const Value& v)
    {
        Mat4 m;
        for (usize c = 0; c < 4; ++c)
        {
            for (usize r = 0; r < 4; ++r)
            {
                m.col[c][r] = static_cast<f32>(v.At(c * 4 + r).ToDouble());
            }
        }
        return m;
    }
    static Value Put(const Mat4& m)
    {
        Value out = Value::MakeArray();
        for (usize c = 0; c < 4; ++c)
        {
            for (usize r = 0; r < 4; ++r)
            {
                out.Push(Value::FromFloat(m.col[c][r]));
            }
        }
        return out;
    }
};
template <> struct Codec<Value>
{
    static constexpr const char* kName = "Value";
    static constexpr bool kIsEntity = false;
    static bool Check(const Value&) { return true; }
    static Value Get(const Value& v) { return v; }
    static Value Put(Value x) { return x; }
};

// Checks the arguments of a call against the parameter types; returns the index of the first bad one, or `arity`.
template <class... A, usize... I> usize FirstBadArgument([[maybe_unused]] Span<const Value> args, Indices<I...>)
{
    const bool ok[sizeof...(A) + 1] = {Codec<A>::Check(args[I])..., true};
    usize i = 0;
    while (i < sizeof...(A) && ok[i])
    {
        ++i;
    }
    return i;
}

// The first Entity argument whose handle is dead, or `arity`.
template <class... A, usize... I>
usize FirstDeadEntity([[maybe_unused]] const CallEnv& env, [[maybe_unused]] Span<const Value> args, Indices<I...>)
{
    const bool is_entity[sizeof...(A) + 1] = {Codec<A>::kIsEntity..., false};
    for (usize i = 0; i < sizeof...(A); ++i)
    {
        if (!is_entity[i])
        {
            continue;
        }
        const ecs::Entity e = args[i].AsEntity();
        const EntityCheck& check = env.profile->entities;
        if (!e.IsValid() || (check.is_alive != nullptr && !check.is_alive(check.user, e)))
        {
            return i;
        }
    }
    return sizeof...(A);
}

void ArityError(const CallEnv& env, usize got, Outcome* out);
void TypeError(const CallEnv& env, usize index, const Value& got, const char* wanted, Outcome* out);
void DeadEntityError(const CallEnv& env, usize index, ecs::Entity entity, Outcome* out);
// Turns an error a binding raised with `RaiseScriptError` into the call's outcome; returns whether there was one.
bool TakeRaisedError(const CallEnv& env, Outcome* out);

template <class R, class... A, usize... I>
Outcome Run(R (*fn)(A...), const CallEnv& env, Span<const Value> args, Vector<Value>* results, Indices<I...> indices)
{
    static constexpr const char* kNames[sizeof...(A) + 1] = {Codec<A>::kName..., ""};
    Outcome out;
    if (args.Size() != sizeof...(A))
    {
        ArityError(env, args.Size(), &out);
        return out;
    }
    const usize bad = FirstBadArgument<A...>(args, indices);
    if (bad < sizeof...(A))
    {
        TypeError(env, bad, args[bad], kNames[bad], &out);
        return out;
    }
    const usize dead = FirstDeadEntity<A...>(env, args, indices);
    if (dead < sizeof...(A))
    {
        DeadEntityError(env, dead, args[dead].AsEntity(), &out);
        return out;
    }
    if constexpr (IsSame<R, void>)
    {
        fn(Codec<A>::Get(args[I])...);
    }
    else
    {
        results->Push(Codec<R>::Put(fn(Codec<A>::Get(args[I])...)));
    }
    if (TakeRaisedError(env, &out))
    {
        results->Clear();
    }
    return out;
}

template <auto Fn, class R, class... A>
Outcome Invoke(const CallEnv& env, Span<const Value> args, Vector<Value>* results)
{
    return Run(Fn, env, args, results, typename MakeIndices<sizeof...(A)>::Type());
}

template <auto Fn, class R, class... A> InvokeFn InvokerFor(Signature<R, A...>)
{
    return &Invoke<Fn, R, A...>;
}

template <class T> void AppendTypeName(String<HeapAllocator>* out, const char* name, StringView param, bool first)
{
    if (!first)
    {
        out->Append(StringView(", "));
    }
    out->Append(StringView(name));
    if (param.Length() != 0)
    {
        out->Append(StringView(" "));
        out->Append(param);
    }
}

// Writes `(f32 x, f32 y) -> f32` for the signature, taking the parameter names from the comma-separated list.
template <class R, class... A> void WriteSignature(String<HeapAllocator>* out, StringView params, Signature<R, A...>)
{
    static constexpr const char* kNames[sizeof...(A) + 1] = {Codec<A>::kName..., ""};
    out->Append(StringView("("));
    usize start = 0;
    for (usize i = 0; i < sizeof...(A); ++i)
    {
        usize end = start;
        while (end < params.Length() && params[end] != ',')
        {
            ++end;
        }
        StringView name(params.Data() + start, end - start);
        while (name.Length() != 0 && name[0] == ' ')
        {
            name = StringView(name.Data() + 1, name.Length() - 1);
        }
        AppendTypeName<void>(out, kNames[i], name, i == 0);
        start = end < params.Length() ? end + 1 : end;
    }
    out->Append(StringView(") -> "));
    if constexpr (IsSame<R, void>)
    {
        out->Append(StringView("void"));
    }
    else
    {
        out->Append(StringView(Codec<R>::kName));
    }
}

// The number of names in a comma-separated list (0 for an empty list).
usize CountNames(StringView params);

}  // namespace detail

/// What an installation did.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns the per-function records the state's native functions point at; it must stay alive as long as
/// scripts can run in the state.
class Installation
{
public:
    /// How many bindings are callable in the state.
    ///
    /// - Fails: never.
    usize Installed() const { return installed_; }
    /// How many are installed as a function that raises (main-thread-only in a non-main state).
    ///
    /// - Fails: never.
    usize Refusing() const { return refusing_; }
    /// How many the profile left out.
    ///
    /// - Fails: never.
    usize Skipped() const { return skipped_; }

    /// Destroys the installation; the state it was made for must already be gone.
    ~Installation();
    Installation(const Installation&) = delete;
    Installation& operator=(const Installation&) = delete;
    /// Creates an empty installation; [[script::BindingRegistry::Install]] makes the useful ones.
    Installation();

    /// One installed function's record (an implementation detail; named so the state's native function can read it).
    struct Bound;

private:
    friend class BindingRegistry;
    Vector<UniquePtr<Bound>> bound_;
    InstallProfile profile_;
    usize installed_ = 0;
    usize refusing_ = 0;
    usize skipped_ = 0;
};

/// The set of engine functions scripts can call: namespaces of typed, documented, thread-marked functions.
///
/// `Add` derives everything from the C++ signature (`f32`, `f64`, `i32`, `i64`, `bool`, `StringView`, `Vec2`, `Vec3`,
/// `Vec4`, `Quaternion`, `Mat4` (an array of 16 numbers), `ecs::Entity`, `Value`; any of them or `void` as the result).
/// A call with the wrong number of arguments or a wrong type becomes a script error that names the function and the
/// argument; an `Entity` argument whose handle is dead (checked by the state's [[script::EntityCheck]]) is an error
/// too, so a stale handle never reaches the function. `Entity` arguments nested inside a `Value` are not checked.
///
/// Fill the registry once, then install it into any number of states from any number of threads: installing reads
/// the registry and nothing else.
///
/// - Thread-safety: thread-compatible: `Add` needs exclusive access; the other members are safe from many threads once
/// the `Add` calls have finished.
/// - Ownership: owns its entries; it must outlive the installations made from it.
class BindingRegistry
{
public:
    /// Creates an empty registry.
    BindingRegistry() = default;
    /// Destroys the registry; every installation made from it must already be gone.
    ~BindingRegistry();
    BindingRegistry(const BindingRegistry&) = delete;
    BindingRegistry& operator=(const BindingRegistry&) = delete;

    /// Adds the function `Fn` as `ns.name`.
    ///
    /// `Fn` is a pointer to a function (not a member, not a lambda) whose parameter and result types are among the
    /// listed ones. `params` names the parameters, comma separated, for the documentation.
    ///
    /// - Param ns: the namespace; lower case letters, digits and `_`, not empty, not starting with a digit.
    /// - Param name: the function name, the same alphabet.
    /// - Param rule: the thread rule; `Unmarked` is refused.
    /// - Param params: one name per parameter, comma separated (empty for none).
    /// - Param doc: the description; copied.
    /// - Returns: true when the binding is added.
    /// - Fails: returns false (and fills `error`) for an unmarked rule, a bad name, a name or id already in the
    /// registry, or a `params` list that does not match the parameter count.
    /// - Ownership: the strings are copied; the registry owns the entry.
    template <auto Fn>
    bool Add(StringView ns, StringView name, ThreadRule rule, StringView params, StringView doc,
             Outcome* error = nullptr)
    {
        using Sig = decltype(detail::SignatureOf(Fn));
        String<HeapAllocator> signature(HeapAllocator::Default());
        detail::WriteSignature(&signature, params, Sig());
        return AddEntry(ns, name, rule, params, doc, signature.View(), detail::InvokerFor<Fn>(Sig()),
                        SignatureArity(Sig()), error);
    }

    /// How many bindings there are.
    ///
    /// - Fails: never.
    usize Count() const { return entries_.Size(); }

    /// The `index`-th binding in registration order.
    ///
    /// - Aborts: `index` is not less than `Count()`.
    const BindingInfo& At(usize index) const;

    /// Finds a binding by its full name (`ns.name`).
    ///
    /// - Returns: the record, or null.
    /// - Ownership: the record lives as long as the registry.
    /// - Fails: null, as described.
    const BindingInfo* Find(StringView full_name) const;

    /// Finds a binding by its stable id.
    ///
    /// - Returns: the record, or null.
    /// - Ownership: the record lives as long as the registry.
    /// - Fails: null, as described.
    const BindingInfo* FindById(u32 id) const;

    /// Installs the bindings the profile allows into `state` under their full names.
    ///
    /// - Param state: the state; the calling thread owns it.
    /// - Param profile: which rules to install, whether the state is the main thread's, the entity check.
    /// - Param error: receives the first failure; may be null.
    /// - Returns: the installation, or null when the state refused a registration (`error` says why).
    /// - Ownership: the caller owns the installation and keeps it alive as long as the state; `profile.entities.user`
    /// likewise.
    /// - Thread-safety: thread-safe: many threads may install into their own states at once.
    /// - Fails: null, as described; the state may then hold bindings whose records are gone, so discard it.
    UniquePtr<Installation> Install(State* state, const InstallProfile& profile, Outcome* error) const;

    /// Renders the registry as the Markdown of `docs/reference/script-api.md`: one section per namespace, sorted by
    /// name, each function with its signature, thread rule, id and description.
    ///
    /// - Returns: the text; it depends only on the registry's contents, not on the registration order.
    /// - Ownership: returns a new string.
    /// - Fails: never.
    String<HeapAllocator> RenderMarkdown() const;

private:
    template <class R, class... A> static u32 SignatureArity(detail::Signature<R, A...>) { return sizeof...(A); }

    bool AddEntry(StringView ns, StringView name, ThreadRule rule, StringView params, StringView doc,
                  StringView signature, detail::InvokeFn invoke, u32 arity, Outcome* error);

    Vector<UniquePtr<detail::Entry>> entries_;
};

/// Makes the binding that is running fail with a script error: the call's results are dropped and the message, with
/// the binding's name in front, becomes a `RuntimeError` in the calling script (which can `pcall` it). Call it from
/// inside a binding, once, before returning (the binding still returns normally; its result is discarded). The message
/// is kept per thread, up to 255 bytes.
///
/// - Param message: the text; copied.
/// - Thread-safety: thread-safe (a per-thread slot).
/// - Fails: never.
void RaiseScriptError(StringView message);

/// Adds the standard bindings: `num` (the Lua `math` library keeps its own name), `vec2`, `vec3`, `vec4`, `quat`,
/// `mat4` (a matrix is an array of 16 numbers, column-major), and, through [[script::RegisterRuntimeBindings]],
/// `random`, `log` and `time`. All are `pure`: they touch their arguments and the running job's own
/// [[script::PartitionContext]]; the `log` calls are buffered per partition.
///
/// - Param registry: the registry to add to; not null.
/// - Returns: true when all were added (false when a name is already taken).
/// - Ownership: borrowed for the call.
/// - Fails: returns false, as described.
bool RegisterStandardBindings(BindingRegistry* registry);

/// Adds `random.float`, `random.int`, `random.vec3_unit`, `log.debug/info/warn/error`, and
/// `time.dt/now/frame/tick/real`. They read the running job's [[script::PartitionContext]] (a stream per entity and
/// tick, the partition's log buffer, the tick's clock); outside a job they use fixed fallbacks (a per-thread stream,
/// direct logging, zero time).
///
/// - Param registry: the registry to add to; not null.
/// - Returns: true when all were added (false when a name is already taken).
/// - Ownership: borrowed for the call.
/// - Fails: returns false, as described.
bool RegisterRuntimeBindings(BindingRegistry* registry);

}  // namespace engine_placeholder::script
