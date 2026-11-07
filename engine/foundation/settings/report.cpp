#include "settings/report.h"

#include <cstdarg>
#include <cstdio>

#include "settings/file_store.h"

namespace engine_placeholder::settings {

namespace {

using Text = String<HeapAllocator>;

void Line(Text* out, const char* format, ...) __attribute__((format(printf, 2, 3)));
void Line(Text* out, const char* format, ...) {
    va_list args;
    va_start(args, format);
    va_list again;
    va_copy(again, args);
    const int needed = std::vsnprintf(nullptr, 0, format, args);
    va_end(args);
    if (needed > 0) {
        Vector<char> buffer;
        buffer.Resize(static_cast<usize>(needed) + 1);
        std::vsnprintf(buffer.Data(), buffer.Size(), format, again);
        out->AppendRawBytes(buffer.Data(), static_cast<usize>(needed));
    }
    va_end(again);
}

void ValueText(const SettingDesc& desc, const Value& value, char* out, usize capacity) {
    if ((desc.flags & kSecret) != 0) {
        std::snprintf(out, capacity, "<secret>");
    } else if (FormatValue(desc, value, out, capacity) == 0) {
        std::snprintf(out, capacity, "?");
    }
}

}  // namespace

void FormatSettingsListing(Settings& settings, Text* out) {
    const Registry& registry = settings.GetRegistry();
    for (u32 i = 0; i < registry.Count(); ++i) {
        const SettingId id = registry.SortedAt(i);
        const SettingDesc& d = *registry.Desc(id);
        if ((d.flags & kHidden) != 0) continue;
        const Resolution r = settings.Provenance(id);
        char value[kMaxValueTextBytes], def[kMaxValueTextBytes];
        ValueText(d, r.value, value, sizeof(value));
        ValueText(d, d.default_value, def, sizeof(def));
        char source[160];
        // A secret's source is the layer only: the origin of a --set entry would repeat the value.
        if (r.origin[0] != '\0' && (d.flags & kSecret) == 0) std::snprintf(source, sizeof(source), "%s (%s)", r.layer_name, r.origin);
        else std::snprintf(source, sizeof(source), "%s", r.layer_name);
        Line(out, "%-36s %-14s default %-12s from %-34s %s\n", registry.KeyOf(id).View().Data(), value, def, source, d.description != nullptr ? d.description : "");
    }
}

void FormatSettingsHelp(const Registry& registry, Text* out) {
    Line(out, "Settings:\n");
    Line(out, "  --set KEY=VALUE         set a setting (repeatable; the value may be quoted); --list-settings shows the keys\n");
    Line(out, "  --settings FILE         read settings from FILE (above the project file, below the environment)\n");
    Line(out, "  --save-settings[=FILE]  write the settings that differ from their defaults to FILE (default: the user's settings file) and exit\n");
    Line(out, "  --list-settings         print every setting: its value, default, where it came from and what it is for, and exit\n");
    for (u32 i = 0; i < registry.Count(); ++i) {
        const SettingId id = registry.SortedAt(i);
        const SettingDesc& d = *registry.Desc(id);
        if (d.flag == nullptr || (d.flags & kHidden) != 0) continue;
        char head[96];
        if (d.type == Type::Bool) std::snprintf(head, sizeof(head), "--%s, --no-%s", d.flag, d.flag);
        else std::snprintf(head, sizeof(head), "--%s %s", d.flag, d.type == Type::Enum ? "NAME" : d.type == Type::Int ? "N" : d.type == Type::Float ? "X" : "TEXT");
        Line(out, "  %-23s %s: %s\n", head, registry.KeyOf(id).View().Data(), d.description != nullptr ? d.description : "");
    }
    Line(out, "Environment: ENGINE_SETTING_<KEY> sets any setting (the key in capitals, dots written as __, e.g. ENGINE_SETTING_RENDERER__PRESENT_MODE); also:\n");
    for (u32 i = 0; i < registry.Count(); ++i) {
        const SettingId id = registry.SortedAt(i);
        const SettingDesc& d = *registry.Desc(id);
        if (d.env != nullptr) Line(out, "  %-37s %s\n", d.env, registry.KeyOf(id).View().Data());
    }
}

Status SaveSettingsTo(Settings& settings, const io::Path& path) {
    const Registry& registry = settings.GetRegistry();
    FileStoreOptions options;
    options.permissions = 0600; // the session's values may include what the environment or the command line supplied
    options.create_directories = true;
    FileConfigStore store(path, options);
    // A missing file is an empty store and a damaged one is replaced (the old text goes to .bak); a file that cannot be read at all is not overwritten.
    const Status loaded = store.Load();
    if (loaded.Failed() && !loaded.Is(StoreError::Damaged)) return loaded;
    SharedPtr<const SettingsSnapshot> snapshot = settings.Snapshot();
    for (u32 i = 0; i < registry.Count(); ++i) {
        const SettingId id = registry.SortedAt(i);
        const SettingDesc& d = *registry.Desc(id);
        const Value& v = snapshot->Get(id);
        if ((d.flags & kSecret) != 0) continue; // a saved session file is not the place for a secret
        if (v == d.default_value) {
            store.Remove(registry.KeyOf(id).View());
            continue;
        }
        StoredValue stored;
        const StoreError converted = ToStored(d, v, false, &stored);
        if (converted != StoreError::None) return Status::Of(converted);
        const Status s = store.Set(registry.KeyOf(id).View(), stored);
        if (s.Failed()) return s;
    }
    StampVersions(registry, store, true);
    CommitOptions commit;
    commit.replace_damaged = true;
    return store.Commit(commit);
}

} // namespace engine_placeholder::settings
