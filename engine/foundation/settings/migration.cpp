#include "settings/migration.h"

#include "cstr/format.h"

#include "log/assert.h"

namespace engine_placeholder::settings
{

namespace
{

using Text = String<HeapAllocator>;

// Reads a quoted text the way a text setting is read.
bool ParseTextForMigration(StringView quoted, Value* out)
{
    static const SettingDesc desc = MakeText("migration.text", "");
    return ParseValue(desc, quoted, out) == ValueError::None;
}

Text MigMake(StringView v)
{
    return Text(HeapAllocator::Default(), v);
}

Text Format2(const char* format, StringView a, StringView b = StringView())
{
    char buffer[300];
    FormatTo(buffer, sizeof(buffer), format, static_cast<int>(a.Length()), a.Data(), static_cast<int>(b.Length()),
             b.Data());
    return MigMake(StringView(buffer));
}

bool GetText(ConfigStore& store, StringView key, Text* out)
{
    StoredValue sv;
    if (store.Get(key, &sv).Failed() || sv.typed || sv.value.GetType() != Type::Text)
    {
        return false;
    }
    *out = MigMake(sv.value.AsText());
    return true;
}

bool SetText(ConfigStore& store, StringView key, StringView text)
{
    StoredValue sv;
    sv.typed = false;
    sv.value = Value::FromText(text);
    return store.Set(key, sv).Ok();
}

// A version that cannot be read is treated as newer than any program (compatibility mode: nothing is migrated or
// rewritten), not as an old one.
constexpr u32 kUnreadableVersion = 0xFFFFFFFFu;

u32 ParseVersion(StringView text)
{
    if (text.Length() == 0 || text.Length() > 9)
    {
        return kUnreadableVersion;
    }
    u32 v = 0;
    for (usize i = 0; i < text.Length(); ++i)
    {
        if (text[i] < '0' || text[i] > '9')
        {
            return kUnreadableVersion;
        }
        v = v * 10 + static_cast<u32>(text[i] - '0');
    }
    return v;
}

Text SchemaKey(StringView section)
{
    Text key = MigMake(section);
    key.Append(".schema");
    return key;
}

}  // namespace

void MigrationSet::Add(const MigrationStep& step)
{
    ENGINE_ASSERT(step.section != nullptr && step.key != nullptr &&
                      Key::Parse(StringView(step.section), nullptr) == KeyError::None,
                  "settings::MigrationSet::Add: a step needs a valid section and a key");
    steps_.Push(step);
}

u32 StoredFormatVersion(ConfigStore& store)
{
    Text text = MigMake(StringView());
    return GetText(store, "version", &text) ? ParseVersion(text.View()) : 0;
}

u32 StoredSchemaVersion(ConfigStore& store, StringView section)
{
    Text text = MigMake(StringView());
    return GetText(store, SchemaKey(section).View(), &text) ? ParseVersion(text.View()) : 1;
}

void StampVersions(const Registry& registry, ConfigStore& store, bool overwrite)
{
    StoredValue sv;
    if (overwrite || store.Get("version", &sv).Is(StoreError::NotFound))
    {
        char text[16];
        FormatTo(text, sizeof(text), "%u", static_cast<unsigned>(kFormatVersion));
        SetText(store, "version", text);
    }
    for (u32 i = 0; i < registry.SchemaCount(); ++i)
    {
        u32 version = 0;
        const StringView section = registry.SchemaAt(i, &version);
        const Text key = SchemaKey(section);
        if (overwrite || store.Get(key.View(), &sv).Is(StoreError::NotFound))
        {
            char text[16];
            FormatTo(text, sizeof(text), "%u", static_cast<unsigned>(version));
            SetText(store, key.View(), text);
        }
    }
}

void MigrationSet::Migrate(const Registry& registry, ConfigStore& store, MigrationReport* report) const
{
    MigrationReport scratch;
    MigrationReport* rep = report != nullptr ? report : &scratch;
    const u32 format = StoredFormatVersion(store);
    if (format > kFormatVersion)
    {
        rep->newer_format = true;
    }
    for (u32 i = 0; i < registry.SchemaCount(); ++i)
    {
        u32 program = 0;
        const StringView section = registry.SchemaAt(i, &program);
        const u32 file = StoredSchemaVersion(store, section);
        if (file > program)
        {
            rep->newer_sections.Push(MigMake(section));
            continue;
        }
        if (file == program)
        {
            continue;
        }
        bool failed = false;
        for (u32 from = file; from < program; ++from)
        {
            for (usize s = 0; s < steps_.Size(); ++s)
            {
                const MigrationStep& step = steps_[s];
                if (StringView(step.section) != section || step.from_version != from)
                {
                    continue;
                }
                const StringView key(step.key);
                switch (step.op)
                {
                case MigrationOp::Rename:
                {
                    Text value = MigMake(StringView());
                    if (step.new_key == nullptr)
                    {
                        break;
                    }
                    if (!GetText(store, key, &value))
                    {
                        StoredValue present;
                        if (store.Get(key, &present).Ok())
                        {  // there, but not as text: it cannot be moved
                            rep->notes.Push(Format2("%.*s: could not be migrated (not a text value)", key));
                            failed = true;
                        }
                        break;
                    }
                    Text existing = MigMake(StringView());
                    if (GetText(store, step.new_key, &existing))
                    {
                        store.Remove(key);
                        rep->notes.Push(Format2("%.*s: dropped, %.*s is already set", key, StringView(step.new_key)));
                    }
                    else
                    {
                        SetText(store, step.new_key, value.View());
                        store.Remove(key);
                        rep->notes.Push(Format2("%.*s renamed to %.*s", key, StringView(step.new_key)));
                    }
                    ++rep->steps_applied;
                    break;
                }
                case MigrationOp::MapValue:
                {
                    Text value = MigMake(StringView());
                    if (step.old_value == nullptr || step.new_value == nullptr || !GetText(store, key, &value))
                    {
                        break;
                    }
                    // A text setting is stored quoted; compare what it says, so a step written with the plain text
                    // matches either form.
                    Value unquoted;
                    StringView stored_text = value.View();
                    if (stored_text.Length() >= 2 && stored_text[0] == '"' &&
                        ParseTextForMigration(stored_text, &unquoted))
                    {
                        stored_text = unquoted.AsText();
                    }
                    if (stored_text != StringView(step.old_value) && value.View() != StringView(step.old_value))
                    {
                        break;
                    }
                    if (value.View() != StringView(step.old_value))
                    {  // the old value was stored quoted: keep the new one in the same form
                        static const SettingDesc quoted_desc = MakeText("migration.text", "");
                        char formatted[kMaxValueTextBytes];
                        if (FormatValue(quoted_desc, Value::FromText(StringView(step.new_value)), formatted,
                                        sizeof(formatted)) != 0)
                        {
                            SetText(store, key, formatted);
                        }
                        else
                        {
                            break;
                        }
                    }
                    else
                    {
                        SetText(store, key, step.new_value);
                    }
                    rep->notes.Push(Format2("%.*s: value changed to %.*s", key, StringView(step.new_value)));
                    ++rep->steps_applied;
                    break;
                }
                case MigrationOp::Drop:
                    if (store.Remove(key).Ok())
                    {
                        rep->notes.Push(Format2("%.*s: dropped (no longer a setting)", key));
                        ++rep->steps_applied;
                    }
                    break;
                case MigrationOp::Callback:
                    if (step.fn != nullptr)
                    {
                        if (step.fn(step.user, store))
                        {
                            rep->notes.Push(Format2("%.*s: migration function ran", key));
                            ++rep->steps_applied;
                        }
                        else
                        {
                            rep->notes.Push(Format2("%.*s: migration function failed", key));
                            failed = true;
                        }
                    }
                    break;
                }
            }
        }
        if (failed)
        {  // the schema stays where it was, so the next load tries again
            rep->notes.Push(Format2("%.*s: some steps failed; the schema version is not advanced", section));
            continue;
        }
        char text[16];
        FormatTo(text, sizeof(text), "%u", static_cast<unsigned>(program));
        SetText(store, SchemaKey(section).View(), text);
        ++rep->sections_upgraded;
    }
}

}  // namespace engine_placeholder::settings
