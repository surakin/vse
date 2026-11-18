#include "script/settings_bindings.h"

#include "cstr/cstr.h"
#include "cstr/mem_ops.h"
#include "script/backend.h"
#include "script/partition_context.h"
#include "settings/desc.h"
#include "string/string.h"

namespace engine_placeholder::script
{

// ------------------------------------------------------------------ the policy

namespace
{

// `game.*` and `game` both mean the key `game` and everything below it.
StringView Trimmed(StringView prefix)
{
    if (prefix.Length() >= 2 && prefix[prefix.Length() - 2] == '.' && prefix[prefix.Length() - 1] == '*')
    {
        return StringView(prefix.Data(), prefix.Length() - 2);
    }
    return prefix;
}

bool Below(StringView prefix, StringView key)
{
    if (prefix.Length() == 0)
    {
        return true;  // the empty prefix is every key
    }
    if (key.Length() < prefix.Length() || !(StringView(key.Data(), prefix.Length()) == prefix))
    {
        return false;
    }
    return key.Length() == prefix.Length() || key[prefix.Length()] == '.';
}

}  // namespace

SettingsPolicy::SettingsPolicy()
{
    const StringView game("game");
    SetDefaultWritable(Span<const StringView>(&game, 1));
}

void SettingsPolicy::Fill(Vector<String<HeapAllocator>>* out, Span<const StringView> prefixes)
{
    out->Clear();
    for (usize i = 0; i < prefixes.Size(); ++i)
    {
        String<HeapAllocator> text(HeapAllocator::Default());
        text.Append(Trimmed(prefixes[i]));
        out->Push(Move(text));
    }
}

void SettingsPolicy::SetDefaultWritable(Span<const StringView> prefixes)
{
    Fill(&default_, prefixes);
}

void SettingsPolicy::SetScriptWritable(StringView script, Span<const StringView> prefixes)
{
    for (usize i = 0; i < scripts_.Size(); ++i)
    {
        if (scripts_[i].script.View() == script)
        {
            Fill(&scripts_[i].prefixes, prefixes);
            return;
        }
    }
    ScriptList list;
    list.script.Append(script);
    Fill(&list.prefixes, prefixes);
    scripts_.Push(Move(list));
}

bool SettingsPolicy::Covers(const Vector<String<HeapAllocator>>& list, StringView key)
{
    for (usize i = 0; i < list.Size(); ++i)
    {
        if (Below(list[i].View(), key))
        {
            return true;
        }
    }
    return false;
}

bool SettingsPolicy::CanWrite(StringView script, StringView key) const
{
    for (usize i = 0; i < scripts_.Size(); ++i)
    {
        if (scripts_[i].script.View() == script)
        {
            return Covers(scripts_[i].prefixes, key);
        }
    }
    return Covers(default_, key);
}

// ------------------------------------------------------------------ the bindings

namespace
{

void Fail(StringView what, StringView key, const char* why)
{
    StringBuilder<HeapAllocator> sb(HeapAllocator::Default());
    sb.Append(what).Append(StringView(" '")).Append(key).Append(StringView("' ")).Append(StringView(why));
    RaiseScriptError(sb.View());
}

SettingsAccess* AccessOrFail()
{
    PartitionContext* context = CurrentPartitionContext();
    if (context == nullptr || context->settings == nullptr || context->settings->registry == nullptr ||
        context->settings->snapshot.Get() == nullptr)
    {
        RaiseScriptError("settings are not available to this script");
        return nullptr;
    }
    return context->settings;
}

// A setting's value as a script sees it: a bool, an integer, a float, text; an enum is its name.
Value ToScriptValue(const settings::SettingDesc& desc, const settings::Value& value)
{
    switch (value.GetType())
    {
    case settings::Type::Bool: return Value::FromBool(value.AsBool());
    case settings::Type::Int: return Value::FromInt(value.AsInt());
    case settings::Type::Float: return Value::FromFloat(value.AsFloat());
    case settings::Type::Enum:
        return Value::FromText(value.AsEnum() < desc.enum_count ? StringView(desc.enum_names[value.AsEnum()])
                                                                : StringView());
    case settings::Type::Text: return Value::FromText(value.AsText());
    }
    return Value();
}

// Finds a readable setting, or raises and returns false. A secret is reported as not readable, never as its value.
bool Lookup(const SettingsAccess& access, StringView key, settings::SettingId* id, const settings::SettingDesc** desc)
{
    *id = access.registry->Find(key);
    if (!id->IsValid())
    {
        Fail("no setting", key, "exists");
        return false;
    }
    *desc = access.registry->Desc(*id);
    if (((*desc)->flags & settings::kSecret) != 0)
    {
        Fail("the setting", key, "is secret and cannot be read by a script");
        return false;
    }
    return true;
}

Value SettingsGet(StringView key)
{
    SettingsAccess* access = AccessOrFail();
    settings::SettingId id;
    const settings::SettingDesc* desc = nullptr;
    if (access == nullptr || !Lookup(*access, key, &id, &desc))
    {
        return Value();
    }
    return ToScriptValue(*desc, access->snapshot->Get(id));
}

bool SettingsExists(StringView key)
{
    SettingsAccess* access = AccessOrFail();
    return access != nullptr && access->registry->Find(key).IsValid();
}

Value SettingsDescribe(StringView key)
{
    SettingsAccess* access = AccessOrFail();
    settings::SettingId id;
    const settings::SettingDesc* desc = nullptr;
    if (access == nullptr || !Lookup(*access, key, &id, &desc))
    {
        return Value();
    }
    Value out = Value::MakeMap();
    out.Set("key", Value::FromText(key));
    out.Set("type", Value::FromText(settings::TypeName(desc->type)));
    out.Set("default", ToScriptValue(*desc, desc->default_value));
    if (desc->type == settings::Type::Int)
    {
        out.Set("min", Value::FromInt(desc->min_int));
        out.Set("max", Value::FromInt(desc->max_int));
    }
    else if (desc->type == settings::Type::Float)
    {
        out.Set("min", Value::FromFloat(desc->min_float));
        out.Set("max", Value::FromFloat(desc->max_float));
    }
    else if (desc->type == settings::Type::Enum)
    {
        Value names = Value::MakeArray();
        for (u32 i = 0; i < desc->enum_count; ++i)
        {
            names.Push(Value::FromText(StringView(desc->enum_names[i])));
        }
        out.Set("names", Move(names));
    }
    out.Set("restart_required", Value::FromBool((desc->flags & settings::kRestartRequired) != 0));
    if (desc->description != nullptr)
    {
        out.Set("description", Value::FromText(StringView(desc->description)));
    }
    if (desc->label != nullptr)
    {
        out.Set("label", Value::FromText(StringView(desc->label)));
    }
    return out;
}

// Converts the script's value to the setting's type; false (with the reason in `why`) when it does not fit.
bool ToSettingValue(const settings::SettingDesc& desc, const Value& v, settings::Value* out, const char** why)
{
    switch (desc.type)
    {
    case settings::Type::Bool:
        if (v.Kind() != ValueKind::Bool)
        {
            *why = "needs a bool";
            return false;
        }
        *out = settings::Value::FromBool(v.AsBool());
        return true;
    case settings::Type::Int:
        if (v.Kind() != ValueKind::Int)
        {
            *why = "needs an integer";
            return false;
        }
        *out = settings::Value::FromInt(v.AsInt());
        return true;
    case settings::Type::Float:
    {
        if (!v.IsNumber())
        {
            *why = "needs a number";
            return false;
        }
        const f64 f = v.ToDouble();
        if (!(f > -1.0e300 && f < 1.0e300))
        {
            *why = "needs a finite number";
            return false;
        }
        *out = settings::Value::FromFloat(f);
        return true;
    }
    case settings::Type::Enum:
    {
        if (v.Kind() == ValueKind::Int)
        {
            if (v.AsInt() < 0 || v.AsInt() >= static_cast<i64>(desc.enum_count))
            {
                *why = "is not an index of this enum";
                return false;
            }
            *out = settings::Value::FromEnum(static_cast<u32>(v.AsInt()));
            return true;
        }
        if (v.Kind() == ValueKind::Text)
        {
            for (u32 i = 0; i < desc.enum_count; ++i)
            {
                if (v.Text() == StringView(desc.enum_names[i]))
                {
                    *out = settings::Value::FromEnum(i);
                    return true;
                }
            }
        }
        *why = "needs one of the enum's names";
        return false;
    }
    case settings::Type::Text:
        if (v.Kind() != ValueKind::Text)
        {
            *why = "needs text";
            return false;
        }
        if (v.Text().Length() > settings::kMaxTextBytes)
        {
            *why = "has text that is too long";
            return false;
        }
        *out = settings::Value::FromText(v.Text());
        return true;
    }
    *why = "has an unknown type";
    return false;
}

// The payload of a set command: a header, then the key bytes, then the text bytes.
struct SetHeader
{
    u8 type;
    u8 pad;
    u16 key_length;
    u32 text_length;
    i64 int_value;
    f64 float_value;
};

void SettingsSet(StringView key, Value value)
{
    SettingsAccess* access = AccessOrFail();
    if (access == nullptr)
    {
        return;
    }
    PartitionContext* context = CurrentPartitionContext();
    if (context->commands == nullptr)
    {
        RaiseScriptError("this script cannot change settings (no command buffer)");
        return;
    }
    const settings::SettingId id = access->registry->Find(key);
    if (!id.IsValid())
    {
        Fail("no setting", key, "exists");
        return;
    }
    const settings::SettingDesc* desc = access->registry->Desc(id);
    // The policy is asked about the declared key, never an alias the script spelled, and a script whose name was cut to
    // fit does not count as named (it gets the default list).
    const StringView canonical(desc->key);
    SourceLocation where;
    const bool named = CurrentCallerLocation(&where) && !where.truncated;
    const StringView script = named ? StringView(where.chunk, CStrLength(where.chunk)) : StringView();
    if (access->policy == nullptr || !access->policy->CanWrite(script, canonical))
    {
        Fail("the script may not set", key, "(not in its allowed prefixes)");
        return;
    }
    if ((desc->flags & settings::kSecret) != 0)
    {
        Fail("the setting", key, "is secret and cannot be set by a script");
        return;
    }
    settings::Value typed;
    const char* why = nullptr;
    if (!ToSettingValue(*desc, value, &typed, &why))
    {
        Fail("the setting", key, why);
        return;
    }
    settings::Value checked;
    const settings::ValueError verdict = settings::ValidateValue(*desc, typed, &checked);
    if (verdict != settings::ValueError::None && verdict != settings::ValueError::Clamped)
    {
        Fail("the value for", key, settings::ValueErrorName(verdict));
        return;
    }
    SetHeader header = {};
    header.type = static_cast<u8>(checked.GetType());
    key = canonical;  // the merge never resolves an alias again
    header.key_length = static_cast<u16>(key.Length());
    StringView text;
    switch (checked.GetType())
    {
    case settings::Type::Bool: header.int_value = checked.AsBool() ? 1 : 0; break;
    case settings::Type::Int: header.int_value = checked.AsInt(); break;
    case settings::Type::Float: header.float_value = checked.AsFloat(); break;
    case settings::Type::Enum: header.int_value = checked.AsEnum(); break;
    case settings::Type::Text: text = checked.AsText(); break;
    }
    header.text_length = static_cast<u32>(text.Length());
    Vector<u8> payload;
    payload.Resize(sizeof(SetHeader) + key.Length() + text.Length());
    MemCopy(payload.Data(), &header, sizeof(SetHeader));
    MemCopy(payload.Data() + sizeof(SetHeader), key.Data(), key.Length());
    MemCopy(payload.Data() + sizeof(SetHeader) + key.Length(), text.Data(), text.Length());
    if (!context->commands->Record(kCommandSettingsSet, context->current_entity_key, ecs::Entity(), payload.Data(),
                                   static_cast<u32>(payload.Size())))
    {
        RaiseScriptError("the command buffer is full");
    }
}

void SettingsOnChange(StringView prefix, StringView function)
{
    SettingsAccess* access = AccessOrFail();
    if (access == nullptr)
    {
        return;
    }
    if (function.Length() == 0)
    {
        RaiseScriptError("on_change needs the name of a global function");
        return;
    }
    PartitionContext* context = CurrentPartitionContext();
    u32 own = 0;
    for (usize i = 0; i < access->subscriptions.Size(); ++i)
    {
        own += access->subscriptions[i].entity_key == context->current_entity_key ? 1u : 0u;
    }
    if (own >= kMaxSettingsSubscriptionsPerEntity || access->subscriptions.Size() >= kMaxSettingsSubscriptions)
    {
        RaiseScriptError("too many settings subscriptions");
        return;
    }
    SettingsSubscription subscription;
    subscription.entity_key = context->current_entity_key;
    subscription.prefix.Append(Trimmed(prefix));
    subscription.function.Append(function);
    access->subscriptions.Push(Move(subscription));
}

bool ApplySettingsSet(MergeContext& context, ecs::Entity, Span<const u8> payload)
{
    MergeServices* services = static_cast<MergeServices*>(context.User());
    if (services == nullptr || services->settings == nullptr || payload.Size() < sizeof(SetHeader))
    {
        return false;
    }
    SetHeader header;
    MemCopy(&header, payload.Data(), sizeof(SetHeader));
    if (payload.Size() != sizeof(SetHeader) + header.key_length + header.text_length)
    {
        return false;
    }
    const StringView key(reinterpret_cast<const char*>(payload.Data() + sizeof(SetHeader)), header.key_length);
    const StringView text(reinterpret_cast<const char*>(payload.Data() + sizeof(SetHeader) + header.key_length),
                          header.text_length);
    const settings::SettingId id = services->settings->GetRegistry().Find(key);
    if (!id.IsValid())
    {
        return false;
    }
    settings::Value value;
    switch (static_cast<settings::Type>(header.type))
    {
    case settings::Type::Bool: value = settings::Value::FromBool(header.int_value != 0); break;
    case settings::Type::Int: value = settings::Value::FromInt(header.int_value); break;
    case settings::Type::Float: value = settings::Value::FromFloat(header.float_value); break;
    case settings::Type::Enum: value = settings::Value::FromEnum(static_cast<u32>(header.int_value)); break;
    case settings::Type::Text: value = settings::Value::FromText(text); break;
    default: return false;
    }
    return services->settings->Set(id, value).Ok();
}

}  // namespace

bool RegisterSettingsCommandKind(CommandKindTable* kinds)
{
    return kinds->Register(kCommandSettingsSet, CommandKind{"settings.set", &ApplySettingsSet, false, false, true});
}

usize RemoveSettingsSubscriptions(SettingsAccess* access, u64 entity_key)
{
    usize removed = 0;
    usize kept = 0;
    for (usize i = 0; i < access->subscriptions.Size(); ++i)
    {
        if (access->subscriptions[i].entity_key == entity_key)
        {
            ++removed;
        }
        else
        {
            if (kept != i)
            {
                access->subscriptions[kept] = Move(access->subscriptions[i]);
            }
            ++kept;
        }
    }
    access->subscriptions.Resize(kept);
    return removed;
}

Vector<SettingNotification> CollectSettingChanges(const settings::Registry& registry,
                                                  const settings::SettingsSnapshot& before,
                                                  const settings::SettingsSnapshot& after,
                                                  Span<SettingsAccess* const> partitions)
{
    Vector<SettingNotification> out;
    for (u32 p = 0; p < partitions.Size(); ++p)
    {
        if (partitions[p] == nullptr)
        {
            continue;
        }
        const SettingsAccess& access = *partitions[p];
        for (usize s = 0; s < access.subscriptions.Size(); ++s)
        {
            const SettingsSubscription& sub = access.subscriptions[s];
            for (u32 k = 0; k < registry.Count(); ++k)
            {
                const settings::SettingId id = registry.SortedAt(k);
                const StringView key(registry.Desc(id)->key);
                if ((registry.Desc(id)->flags & settings::kSecret) != 0 || !Below(sub.prefix.View(), key))
                {
                    continue;
                }
                if (before.Get(id) == after.Get(id))
                {
                    continue;
                }
                SettingNotification note;
                note.partition = p;
                note.entity_key = sub.entity_key;
                note.function.Append(sub.function.View());
                note.key.Append(key);
                out.Push(Move(note));
            }
        }
    }
    return out;
}

bool RegisterSettingsBindings(BindingRegistry* registry)
{
    bool ok = true;
    ok &= registry->Add<&SettingsGet>(
        "settings", "get", ThreadRule::Pure, "key",
        "Returns the value of a setting as of the tick start: a bool, an integer, a float or "
        "text (an enum is its name). An unknown or secret setting is an error.");
    ok &= registry->Add<&SettingsExists>("settings", "exists", ThreadRule::Pure, "key",
                                         "Returns whether a setting with this key is declared.");
    ok &= registry->Add<&SettingsDescribe>(
        "settings", "describe", ThreadRule::Pure, "key",
        "Returns a table describing a setting: `key`, `type`, `default`, `min` and `max` "
        "(numbers), `names` (an enum), `restart_required`, `label`, `description`. An "
        "unknown or secret setting is an error.");
    ok &= registry->Add<&SettingsSet>(
        "settings", "set", ThreadRule::WorldWrite, "key, value",
        "Sets a setting at the sync point, if the script's policy allows the key (by default "
        "only `game.*`) and the registry accepts the value (type, range, enum name); "
        "otherwise an error and nothing is stored. Reads in the same tick still see the old "
        "value.");
    ok &= registry->Add<&SettingsOnChange>(
        "settings", "on_change", ThreadRule::WorldWrite, "prefix, function",
        "Subscribes the running entity's script to changes of the setting `prefix` or "
        "anything below it; at the sync point after a change the global function named "
        "`function` is called (by the event delivery of Day 459) with the key. Removed "
        "when the entity is destroyed.");
    return ok;
}

}  // namespace engine_placeholder::script
