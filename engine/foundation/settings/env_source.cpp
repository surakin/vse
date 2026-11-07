#include "settings/env_source.h"

#include <cstdio>
#include <cstring>

#include "os/os.h"

namespace engine_placeholder::settings {

void EnvironmentSnapshot::Add(StringView name, StringView value) {
    for (usize i = 0; i < vars_.Size(); ++i) {
        if (vars_[i].name.View() == name) {
            vars_[i].value = String<HeapAllocator>(HeapAllocator::Default(), value);
            return;
        }
    }
    Var v;
    v.name = String<HeapAllocator>(HeapAllocator::Default(), name);
    v.value = String<HeapAllocator>(HeapAllocator::Default(), value);
    vars_.Push(static_cast<Var&&>(v));
}

const String<HeapAllocator>* EnvironmentSnapshot::Find(StringView name) const {
    for (usize i = 0; i < vars_.Size(); ++i) {
        if (vars_[i].name.View() == name) return &vars_[i].value;
    }
    return nullptr;
}

EnvironmentSnapshot EnvironmentSnapshot::Capture() {
    EnvironmentSnapshot snapshot;
    for (usize i = 0;; ++i) {
        String<HeapAllocator> text(HeapAllocator::Default());
        if (!os::EnvironmentEntry(i, &text)) break;
        const StringView entry = text.View();
        const usize eq = entry.IndexOf('=');
        if (eq == entry.Length() || eq == 0) continue;
        snapshot.Add(entry.Substring(0, eq), entry.Substring(eq + 1));
    }
    return snapshot;
}

void EnvironmentVariableFor(const Key& key, char* out, usize capacity) {
    if (capacity == 0) return;
    static const char kPrefix[] = "ENGINE_SETTING_";
    usize n = 0;
    auto put = [&](char c) {
        if (n + 1 < capacity) out[n] = c;
        ++n;
    };
    for (const char* p = kPrefix; *p != '\0'; ++p) put(*p);
    for (usize i = 0; i < key.View().Length(); ++i) {
        const char c = key.View()[i];
        if (c == '.') {
            put('_');
            put('_');
        } else {
            put(c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c);
        }
    }
    out[n < capacity - 1 ? n : capacity - 1] = '\0';
}

EnvironmentSource::EnvironmentSource(const Registry& registry, const EnvironmentSnapshot& environment) {
    for (u32 i = 0; i < registry.Count(); ++i) {
        const SettingId id = registry.SortedAt(i);
        const SettingDesc& desc = *registry.Desc(id);
        char standard[kMaxEnvNameBytes];
        EnvironmentVariableFor(registry.KeyOf(id), standard, sizeof(standard));
        // A variable that is set but empty counts as unset, so a wrapper that exports an empty string does not hide the lower layers.
        const String<HeapAllocator>* value = environment.Find(standard);
        StringView variable = standard;
        if ((value == nullptr || value->Length() == 0) && desc.env != nullptr) {
            value = environment.Find(desc.env);
            variable = desc.env;
        }
        if (value == nullptr || value->Length() == 0) continue;
        char origin[kMaxEnvNameBytes + 2];
        std::snprintf(origin, sizeof(origin), "$%.*s", static_cast<int>(variable.Length()), variable.Data());
        Put(registry.KeyOf(id), value->View(), origin);
    }
}

void EnvironmentSource::Describe(char* out, usize capacity) const {
    if (capacity != 0) std::snprintf(out, capacity, "environment");
}

Status TextEntryStore::Get(StringView key, StoredValue* out) {
    for (usize i = 0; i < entries_.Size(); ++i) {
        if (entries_[i].key.View() == key) {
            if (entries_[i].text.Length() > kMaxTextBytes) return Status::Of(StoreError::TooLong);
            out->typed = false;
            out->value = Value::FromText(entries_[i].text.View());
            return Status();
        }
    }
    return Status::Of(StoreError::NotFound);
}

Status TextEntryStore::Enumerate(StringView prefix, Vector<Key>* out) {
    for (usize i = 0; i < entries_.Size(); ++i) {
        const StringView k = entries_[i].key.View();
        if (prefix.Length() == 0 || k == prefix || (k.Length() > prefix.Length() && k.StartsWith(prefix) && k[prefix.Length()] == '.')) out->Push(entries_[i].key);
    }
    return Status();
}

void TextEntryStore::Origin(StringView key, char* out, usize capacity) const {
    if (capacity == 0) return;
    out[0] = '\0';
    for (usize i = 0; i < entries_.Size(); ++i) {
        if (entries_[i].key.View() == key) {
            std::snprintf(out, capacity, "%s", entries_[i].origin.CStr());
            return;
        }
    }
}

void TextEntryStore::Put(const Key& key, StringView text, StringView origin) {
    for (usize i = 0; i < entries_.Size(); ++i) {
        if (entries_[i].key == key) {
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

} // namespace engine_placeholder::settings
