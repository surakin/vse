#include "storage/storage_settings.h"

#include "settings/desc.h"
#include "containers/vector.h"
#include "settings/setting.h"

namespace engine_placeholder::storage
{
namespace
{

void Declare(settings::Registry* registry, settings::SettingDesc d, const char* env, const char* flag,
             const char* label, const char* description)
{
    if (registry->Find(StringView(d.key)).IsValid())
    {
        return;
    }
    d.env = env;
    d.flag = flag;
    d.label = label;
    d.description = description;
    d.group = "storage";
    d.flags = settings::kAdvanced;
    registry->Register(d);
}

const char* const kLayouts[] = {"blob", "columns"};

u64 Read(const settings::Registry& registry, const char* key)
{
    return static_cast<u64>(settings::Setting<i64>::Find(registry, StringView(key)).Get());
}

}  // namespace

void RegisterStorageSettings(settings::Registry* registry)
{
    const CodecLimits defaults;
    Declare(registry,
            settings::MakeInt("storage.max_record_bytes", static_cast<i64>(defaults.max_record_bytes), 64, 1ll << 30),
            "ENGINE_STORAGE_MAX_RECORD_BYTES", "storage-max-record-bytes", "Largest component record",
            "the most bytes of one stored component; a larger record is refused before it is decoded");
    Declare(registry,
            settings::MakeInt("storage.max_string_bytes", static_cast<i64>(defaults.max_string_bytes), 1, 1ll << 30),
            "ENGINE_STORAGE_MAX_STRING_BYTES", "storage-max-string-bytes", "Longest stored string",
            "the most bytes of one string field of a stored component");
    Declare(
        registry,
        settings::MakeInt("storage.max_array_elements", static_cast<i64>(defaults.max_array_elements), 1, 1ll << 24),
        "ENGINE_STORAGE_MAX_ARRAY_ELEMENTS", "storage-max-array-elements", "Longest stored array",
        "the most elements of one array field of a stored component");
    Declare(registry,
            settings::MakeInt("storage.max_components_per_entity", static_cast<i64>(defaults.max_components_per_entity),
                              1, 4096),
            "ENGINE_STORAGE_MAX_COMPONENTS_PER_ENTITY", "storage-max-components-per-entity",
            "Most components per stored entity", "the most component records of one stored entity");
    Declare(
        registry, settings::MakeText("storage.path", ""), "ENGINE_STORAGE_PATH", "storage-path", "Save file",
        "the database file worlds are saved to; empty keeps a private in-memory database that is gone when the program ends");
    Declare(
        registry, settings::MakeEnum("storage.layout", kLayouts, 2, 0), "ENGINE_STORAGE_LAYOUT", "storage-layout",
        "Save layout",
        "blob keeps one record per entity; columns also keeps a table of columns for each queryable component, so the file can be queried");
    Declare(registry, settings::MakeInt("storage.autosave_seconds", 0, 0, 86400), "ENGINE_STORAGE_AUTOSAVE_SECONDS",
            "storage-autosave-seconds", "Autosave interval",
            "seconds between automatic incremental saves of the running world; 0 turns autosave off");
}

CodecLimits LimitsFromSettings(const settings::Registry& registry)
{
    CodecLimits l;
    l.max_record_bytes = Read(registry, "storage.max_record_bytes");
    l.max_string_bytes = Read(registry, "storage.max_string_bytes");
    l.max_array_elements = Read(registry, "storage.max_array_elements");
    l.max_components_per_entity = Read(registry, "storage.max_components_per_entity");
    return l;
}

String<HeapAllocator> PathFromSettings(const settings::Registry& registry)
{
    const settings::TextSetting setting(registry, registry.Find(StringView("storage.path")));
    char small[256];
    usize length = setting.Get(small, sizeof(small));
    if (length < sizeof(small))
    {
        return String<HeapAllocator>(HeapAllocator::Default(), StringView(small, length));
    }
    Vector<char> big;
    for (usize i = 0; i < length + 1; ++i)
    {
        big.Push('\0');
    }
    length = setting.Get(big.Data(), big.Size());
    return String<HeapAllocator>(HeapAllocator::Default(),
                                 StringView(big.Data(), length < big.Size() ? length : big.Size() - 1));
}

bool ColumnsFromSettings(const settings::Registry& registry)
{
    return settings::Setting<StorageLayout>::Find(registry, StringView("storage.layout")).Get() ==
           StorageLayout::Columns;
}

u32 AutosaveSecondsFromSettings(const settings::Registry& registry)
{
    return static_cast<u32>(settings::Setting<i64>::Find(registry, StringView("storage.autosave_seconds")).Get());
}

}  // namespace engine_placeholder::storage
