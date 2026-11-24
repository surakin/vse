#include "script/bindings.h"

#include "algorithm.h"
#include "log/assert.h"
#include "string/string.h"

namespace vse::script
{

const char* ThreadRuleName(ThreadRule rule)
{
    switch (rule)
    {
    case ThreadRule::Unmarked: return "unmarked";
    case ThreadRule::Pure: return "pure";
    case ThreadRule::WorldRead: return "world-read";
    case ThreadRule::WorldWrite: return "world-write";
    case ThreadRule::MainThreadOnly: return "main-thread-only";
    }
    return "unknown";
}

const char* ValueKindText(ValueKind kind)
{
    switch (kind)
    {
    case ValueKind::Nil: return "nil";
    case ValueKind::Bool: return "bool";
    case ValueKind::Int: return "integer";
    case ValueKind::Float: return "number";
    case ValueKind::Text: return "text";
    case ValueKind::Vec2: return "vec2";
    case ValueKind::Vec3: return "vec3";
    case ValueKind::Vec4: return "vec4";
    case ValueKind::Quat: return "quat";
    case ValueKind::Entity: return "entity";
    case ValueKind::Bytes: return "bytes";
    case ValueKind::Array: return "array";
    case ValueKind::Map: return "map";
    }
    return "unknown";
}

namespace detail
{

namespace
{

// The `index`-th name of a comma-separated list, trimmed of leading spaces.
StringView NameAt(StringView params, usize index)
{
    usize start = 0;
    for (usize i = 0;; ++i)
    {
        usize end = start;
        while (end < params.Length() && params[end] != ',')
        {
            ++end;
        }
        if (i == index)
        {
            while (start < end && params[start] == ' ')
            {
                ++start;
            }
            return StringView(params.Data() + start, end - start);
        }
        if (end >= params.Length())
        {
            return StringView();
        }
        start = end + 1;
    }
}

}  // namespace

usize CountNames(StringView params)
{
    if (params.Length() == 0)
    {
        return 0;
    }
    usize count = 1;
    for (usize i = 0; i < params.Length(); ++i)
    {
        if (params[i] == ',')
        {
            ++count;
        }
    }
    return count;
}

void ArityError(const CallEnv& env, usize got, Outcome* out)
{
    StringBuilder<HeapAllocator> sb(HeapAllocator::Default());
    sb.Append(env.info->full_name.View()).Append(StringView(": expected "));
    sb.AppendInt(env.info->arity).Append(StringView(env.info->arity == 1 ? " argument, got " : " arguments, got "));
    sb.AppendInt(static_cast<i64>(got));
    *out = Outcome(Status::RuntimeError, sb.View());
}

void TypeError(const CallEnv& env, usize index, const Value& got, const char* wanted, Outcome* out)
{
    StringBuilder<HeapAllocator> sb(HeapAllocator::Default());
    sb.Append(env.info->full_name.View()).Append(StringView(": argument "));
    sb.AppendInt(static_cast<i64>(index + 1));
    const StringView name = NameAt(env.info->params.View(), index);
    if (name.Length() != 0)
    {
        sb.Append(StringView(" ("));
        sb.Append(name);
        sb.Append(StringView(")"));
    }
    sb.Append(StringView(": expected ")).Append(StringView(wanted)).Append(StringView(", got "));
    sb.Append(StringView(ValueKindText(got.Kind())));
    *out = Outcome(Status::RuntimeError, sb.View());
}

namespace
{

thread_local char g_raised[256];
thread_local usize g_raised_length = 0;
thread_local bool g_raised_set = false;

}  // namespace

bool TakeRaisedError(const CallEnv& env, Outcome* out)
{
    if (!g_raised_set)
    {
        return false;
    }
    g_raised_set = false;
    StringBuilder<HeapAllocator> sb(HeapAllocator::Default());
    sb.Append(env.info->full_name.View()).Append(StringView(": "));
    sb.Append(StringView(g_raised, g_raised_length));
    *out = Outcome(Status::RuntimeError, sb.View());
    return true;
}

void DeadEntityError(const CallEnv& env, usize index, ecs::Entity entity, Outcome* out)
{
    StringBuilder<HeapAllocator> sb(HeapAllocator::Default());
    sb.Append(env.info->full_name.View()).Append(StringView(": argument "));
    sb.AppendInt(static_cast<i64>(index + 1));
    const StringView name = NameAt(env.info->params.View(), index);
    if (name.Length() != 0)
    {
        sb.Append(StringView(" ("));
        sb.Append(name);
        sb.Append(StringView(")"));
    }
    sb.Append(StringView(": the entity "));
    sb.AppendInt(entity.index).Append(StringView(":"));
    sb.AppendInt(entity.generation);
    sb.Append(StringView(" no longer exists (a stale handle)"));
    *out = Outcome(Status::RuntimeError, sb.View());
}

}  // namespace detail

void RaiseScriptError(StringView message)
{
    usize length = message.Length() < sizeof(detail::g_raised) - 1 ? message.Length() : sizeof(detail::g_raised) - 1;
    for (usize i = 0; i < length; ++i)
    {
        detail::g_raised[i] = message[i];
    }
    detail::g_raised_length = length;
    detail::g_raised_set = true;
}

struct Installation::Bound
{
    const detail::Entry* entry = nullptr;
    detail::CallEnv env;
    bool refuse = false;
};

Installation::Installation() = default;
Installation::~Installation() = default;

namespace
{

Outcome Dispatch(void* user, Span<const Value> args, Vector<Value>* results)
{
    const Installation::Bound* bound = static_cast<const Installation::Bound*>(user);
    if (bound->refuse)
    {
        StringBuilder<HeapAllocator> sb(HeapAllocator::Default());
        sb.Append(bound->entry->info.full_name.View());
        sb.Append(StringView(" is main-thread-only and cannot be called from a partition job"));
        return Outcome(Status::RuntimeError, sb.View());
    }
    if (bound->entry->dynamic != nullptr)
    {
        // A parameter list that ends in `...` takes any number of further arguments (a binding added at run time).
        const StringView params = bound->entry->info.params.View();
        const bool variadic = params.Length() >= 3 && params.Substring(params.Length() - 3) == StringView("...");
        const usize least = variadic ? bound->entry->info.arity - 1 : bound->entry->info.arity;
        if (variadic ? args.Size() < least : args.Size() != bound->entry->info.arity)
        {
            StringBuilder<HeapAllocator> sb(HeapAllocator::Default());
            sb.Append(bound->entry->info.full_name.View()).Append(StringView(": expected "));
            sb.Append(StringView(variadic ? "at least " : "")).AppendInt(least).Append(StringView(" arguments, got "));
            sb.AppendInt(static_cast<i64>(args.Size()));
            return Outcome(Status::RuntimeError, sb.View());
        }
        return bound->entry->dynamic(bound->entry->user, args, results);
    }
    return bound->entry->invoke(bound->env, args, results);
}

u32 HashName(StringView name)
{
    u32 h = 2166136261u;
    for (usize i = 0; i < name.Length(); ++i)
    {
        h ^= static_cast<u8>(name[i]);
        h *= 16777619u;
    }
    return h == 0 ? 1u : h;
}

bool GoodIdentifier(StringView s)
{
    if (s.Length() == 0 || s.Length() > 63 || (s[0] >= '0' && s[0] <= '9'))
    {
        return false;
    }
    for (usize i = 0; i < s.Length(); ++i)
    {
        const char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
        {
            return false;
        }
    }
    return true;
}

bool Refuse(Outcome* error, StringView ns, StringView name, const char* why)
{
    if (error != nullptr)
    {
        StringBuilder<HeapAllocator> sb(HeapAllocator::Default());
        sb.Append(StringView("binding "));
        sb.Append(ns).Append(StringView(".")).Append(name).Append(StringView(": ")).Append(StringView(why));
        *error = Outcome(Status::Invalid, sb.View());
    }
    return false;
}

}  // namespace

BindingRegistry::~BindingRegistry() = default;

bool BindingRegistry::AddEntry(StringView ns, StringView name, ThreadRule rule, StringView params, StringView doc,
                               StringView signature, detail::InvokeFn invoke, u32 arity, Outcome* error)
{
    if (rule == ThreadRule::Unmarked)
    {
        return Refuse(error, ns, name, "no thread rule (pure, world-read, world-write or main-thread-only)");
    }
    if (!GoodIdentifier(ns) || !GoodIdentifier(name))
    {
        return Refuse(error, ns, name,
                      "names are lower case letters, digits and '_', at most 63 bytes, not starting with a digit");
    }
    static const char* const kReserved[] = {"math", "string", "table", "bit", "coroutine"};
    for (const char* reserved : kReserved)
    {
        if (ns == StringView(reserved))
        {
            return Refuse(error, ns, name, "the namespace is the name of one of Lua's libraries");
        }
    }
    if (detail::CountNames(params) != arity)
    {
        return Refuse(error, ns, name, "the parameter name list does not match the parameter count");
    }
    auto entry = MakeUnique<detail::Entry>();
    BindingInfo& info = entry->info;
    info.ns.Append(ns);
    info.name.Append(name);
    info.full_name.Append(ns);
    info.full_name.Append('.');
    info.full_name.Append(name);
    info.id = HashName(info.full_name.View());
    for (usize i = 0; i < entries_.Size(); ++i)
    {
        if (entries_[i]->info.id == info.id)
        {
            return Refuse(error, ns, name, "the name (or its id) is already registered");
        }
    }
    info.signature.Append(signature);
    info.params.Append(params);
    info.doc.Append(doc);
    info.rule = rule;
    info.arity = arity;
    entry->invoke = invoke;
    entries_.Push(Move(entry));
    return true;
}

bool BindingRegistry::AddDynamic(StringView ns, StringView name, ThreadRule rule, StringView params, StringView doc,
                                 Outcome (*callback)(void* user, Span<const Value> args, Vector<Value>* results),
                                 void* user, Outcome* error)
{
    if (callback == nullptr)
    {
        return Refuse(error, ns, name, "the callback is null");
    }
    String<HeapAllocator> signature(HeapAllocator::Default());
    signature.Append(StringView("("));
    signature.Append(params);
    signature.Append(StringView(") -> values"));
    const usize before = entries_.Size();
    if (!AddEntry(ns, name, rule, params, doc, signature.View(), nullptr, static_cast<u32>(detail::CountNames(params)),
                  error))
    {
        return false;
    }
    detail::Entry& entry = *entries_[before];
    entry.dynamic = callback;
    entry.user = user;
    return true;
}

const BindingInfo& BindingRegistry::At(usize index) const
{
    ENGINE_ASSERT(index < entries_.Size(), "binding index out of range");
    return entries_[index]->info;
}

const BindingInfo* BindingRegistry::Find(StringView full_name) const
{
    for (usize i = 0; i < entries_.Size(); ++i)
    {
        if (entries_[i]->info.full_name.View() == full_name)
        {
            return &entries_[i]->info;
        }
    }
    return nullptr;
}

const BindingInfo* BindingRegistry::FindById(u32 id) const
{
    for (usize i = 0; i < entries_.Size(); ++i)
    {
        if (entries_[i]->info.id == id)
        {
            return &entries_[i]->info;
        }
    }
    return nullptr;
}

UniquePtr<Installation> BindingRegistry::Install(State* state, const InstallProfile& profile, Outcome* error) const
{
    if (error != nullptr)
    {
        *error = Outcome();
    }
    if (state == nullptr)
    {
        if (error != nullptr)
        {
            *error = Outcome(Status::Invalid, "Install needs a state");
        }
        return UniquePtr<Installation>();
    }
    auto installation = MakeUnique<Installation>();
    installation->profile_ = profile;
    installation->bound_.Reserve(entries_.Size());
    for (usize i = 0; i < entries_.Size(); ++i)
    {
        const detail::Entry& entry = *entries_[i];
        if ((profile.rules & InstallProfile::Bit(entry.info.rule)) == 0)
        {
            ++installation->skipped_;
            continue;
        }
        auto bound = MakeUnique<Installation::Bound>();
        bound->entry = &entry;
        bound->env.info = &entry.info;
        bound->env.profile = &installation->profile_;
        bound->refuse = entry.info.rule == ThreadRule::MainThreadOnly && !profile.main_thread;
        const Outcome result = state->RegisterFunction(entry.info.full_name.View(), &Dispatch, bound.Get());
        if (!result.IsOk())
        {
            if (error != nullptr)
            {
                *error = Outcome(result.status, result.message.View());
            }
            return UniquePtr<Installation>();
        }
        if (bound->refuse)
        {
            ++installation->refusing_;
        }
        else
        {
            ++installation->installed_;
        }
        installation->bound_.Push(Move(bound));
    }
    return installation;
}

String<HeapAllocator> BindingRegistry::RenderMarkdown() const
{
    Vector<usize> order;
    for (usize i = 0; i < entries_.Size(); ++i)
    {
        order.Push(i);
    }
    Sort(Span<usize>(order.Data(), order.Size()),
         [this](usize a, usize b) { return entries_[a]->info.full_name.View() < entries_[b]->info.full_name.View(); });
    StringBuilder<HeapAllocator> sb(HeapAllocator::Default());
    sb.Append(StringView("# Script API\n\n"));
    sb.Append(StringView("Generated from the binding registry by `script_api_doc --write`; do not edit by hand.\n\n"));
    sb.Append(
        StringView("Every function states the thread rule it follows: `pure` (any state, any thread), `world-read` "
                   "(reads the world as of the tick start), `world-write` (changes the world only through the "
                   "partition's command buffer) or `main-thread-only` (a partition job that calls it gets a script "
                   "error). A wrong argument count or type is a script error that names the function and the "
                   "argument; an entity handle that no longer exists is an error too.\n"));
    StringView current;
    for (usize k = 0; k < order.Size(); ++k)
    {
        const BindingInfo& info = entries_[order[k]]->info;
        if (current != info.ns.View())
        {
            current = info.ns.View();
            sb.Append(StringView("\n## "));
            sb.Append(current);
            sb.Append(StringView("\n"));
        }
        sb.Append(StringView("\n### `")).Append(info.full_name.View()).Append(StringView("`\n\n"));
        sb.Append(StringView("`")).Append(info.signature.View()).Append(StringView("`\n\n"));
        sb.Append(StringView("Thread rule: **")).Append(StringView(ThreadRuleName(info.rule)));
        sb.Append(StringView("**. Id: `0x")).AppendHex(info.id, 8);
        sb.Append(StringView("`.\n\n")).Append(info.doc.View()).Append(StringView("\n"));
    }
    return String<HeapAllocator>(HeapAllocator::Default(), sb.View());
}

}  // namespace vse::script
