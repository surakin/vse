#include "settings/env_source.h"

#include "cstr/format.h"

#include "os/os.h"

namespace vse::settings
{

bool EnvironmentSnapshot::SameName(StringView a, StringView b) const
{
    if (!ignore_case_)
    {
        return a == b;
    }
    if (a.Length() != b.Length())
    {
        return false;
    }
    for (usize i = 0; i < a.Length(); ++i)
    {
        const char x = a[i] >= 'a' && a[i] <= 'z' ? static_cast<char>(a[i] - 'a' + 'A') : a[i];
        const char y = b[i] >= 'a' && b[i] <= 'z' ? static_cast<char>(b[i] - 'a' + 'A') : b[i];
        if (x != y)
        {
            return false;
        }
    }
    return true;
}

void EnvironmentSnapshot::Add(StringView name, StringView value)
{
    for (usize i = 0; i < vars_.Size(); ++i)
    {
        if (SameName(vars_[i].name.View(), name))
        {
            vars_[i].value = String<HeapAllocator>(HeapAllocator::Default(), value);
            return;
        }
    }
    Var v;
    v.name = String<HeapAllocator>(HeapAllocator::Default(), name);
    v.value = String<HeapAllocator>(HeapAllocator::Default(), value);
    vars_.Push(static_cast<Var&&>(v));
}

const String<HeapAllocator>* EnvironmentSnapshot::Find(StringView name) const
{
    for (usize i = 0; i < vars_.Size(); ++i)
    {
        if (SameName(vars_[i].name.View(), name))
        {
            return &vars_[i].value;
        }
    }
    return nullptr;
}

EnvironmentSnapshot EnvironmentSnapshot::Capture()
{
    EnvironmentSnapshot snapshot(os::EnvironmentNamesIgnoreCase());
    for (usize i = 0;; ++i)
    {
        char stack_entry[1024];
        usize length = 0;
        if (!os::EnvironmentEntry(i, stack_entry, sizeof(stack_entry), &length))
        {
            break;
        }
        StringView entry(stack_entry, length);
        Vector<char> large;  // only for an entry that does not fit the stack buffer
        if (length >= sizeof(stack_entry))
        {
            large.Resize(length + 1);
            if (!os::EnvironmentEntry(i, large.Data(), large.Size(), &length) || length >= large.Size())
            {
                continue;
            }
            entry = StringView(large.Data(), length);
        }
        const usize eq = entry.IndexOf('=');
        if (eq == entry.Length() || eq == 0)
        {
            continue;
        }
        snapshot.Add(entry.Substring(0, eq), entry.Substring(eq + 1));
    }
    return snapshot;
}

void EnvironmentVariableFor(const Key& key, char* out, usize capacity)
{
    if (capacity == 0)
    {
        return;
    }
    static const char kPrefix[] = "ENGINE_SETTING_";
    usize n = 0;
    auto put = [&](char c)
    {
        if (n + 1 < capacity)
        {
            out[n] = c;
        }
        ++n;
    };
    for (const char* p = kPrefix; *p != '\0'; ++p)
    {
        put(*p);
    }
    for (usize i = 0; i < key.View().Length(); ++i)
    {
        const char c = key.View()[i];
        if (c == '.')
        {
            put('_');
            put('_');
        }
        else
        {
            put(c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c);
        }
    }
    out[n < capacity - 1 ? n : capacity - 1] = '\0';
}

EnvironmentSource::EnvironmentSource(const Registry& registry, const EnvironmentSnapshot& environment)
{
    for (u32 i = 0; i < registry.Count(); ++i)
    {
        const SettingId id = registry.SortedAt(i);
        const SettingDesc& desc = *registry.Desc(id);
        char standard[kMaxEnvNameBytes];
        EnvironmentVariableFor(registry.KeyOf(id), standard, sizeof(standard));
        // A variable that is set but empty counts as unset, so a wrapper that exports an empty string does not hide the
        // lower layers.
        const String<HeapAllocator>* value = environment.Find(standard);
        StringView variable = standard;
        if ((value == nullptr || value->Length() == 0) && desc.env != nullptr)
        {
            value = environment.Find(desc.env);
            variable = desc.env;
        }
        if (value == nullptr || value->Length() == 0)
        {
            continue;
        }
        char origin[kMaxEnvNameBytes + 2];
        FormatTo(origin, sizeof(origin), "$%.*s", static_cast<int>(variable.Length()), variable.Data());
        Put(registry.KeyOf(id), value->View(), origin);
    }
}

void EnvironmentSource::Describe(char* out, usize capacity) const
{
    if (capacity != 0)
    {
        FormatTo(out, capacity, "environment");
    }
}

Status TextEntryStore::Get(StringView key, StoredValue* out)
{
    for (usize i = 0; i < entries_.Size(); ++i)
    {
        if (entries_[i].key.View() == key)
        {
            if (entries_[i].text.Length() > kMaxTextBytes)
            {
                return Status::Of(StoreError::TooLong);
            }
            out->typed = false;
            out->value = Value::FromText(entries_[i].text.View());
            return Status();
        }
    }
    return Status::Of(StoreError::NotFound);
}

Status TextEntryStore::Enumerate(StringView prefix, Vector<Key>* out)
{
    for (usize i = 0; i < entries_.Size(); ++i)
    {
        const StringView k = entries_[i].key.View();
        if (prefix.Length() == 0 || k == prefix ||
            (k.Length() > prefix.Length() && k.StartsWith(prefix) && k[prefix.Length()] == '.'))
        {
            out->Push(entries_[i].key);
        }
    }
    return Status();
}

void TextEntryStore::Origin(StringView key, char* out, usize capacity) const
{
    if (capacity == 0)
    {
        return;
    }
    out[0] = '\0';
    for (usize i = 0; i < entries_.Size(); ++i)
    {
        if (entries_[i].key.View() == key)
        {
            FormatTo(out, capacity, "%s", entries_[i].origin.CStr());
            return;
        }
    }
}

void TextEntryStore::Put(const Key& key, StringView text, StringView origin)
{
    for (usize i = 0; i < entries_.Size(); ++i)
    {
        if (entries_[i].key == key)
        {
            entries_[i].text = String<HeapAllocator>(HeapAllocator::Default(), text);
            entries_[i].origin = String<HeapAllocator>(HeapAllocator::Default(), origin);
            return;
        }
    }
    Entry e;
    e.key = key;
    e.text = String<HeapAllocator>(HeapAllocator::Default(), text);
    e.origin = String<HeapAllocator>(HeapAllocator::Default(), origin);
    entries_.Push(static_cast<Entry&&>(e));
}

}  // namespace vse::settings
