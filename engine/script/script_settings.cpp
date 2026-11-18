#include "script/script_settings.h"

#include "settings/setting.h"

namespace engine_placeholder::script
{

void RegisterScriptSettings(settings::Registry* registry)
{
    settings::SettingDesc desc = settings::MakeText("script.backend", "luajit");
    desc.env = "ENGINE_SCRIPT_BACKEND";
    desc.flag = "script-backend";
    desc.label = "Script language";
    desc.description = "the scripting language backend: luajit (the default), or another one the program registers";
    desc.group = "script";
    desc.flags = settings::kRestartRequired;
    registry->Register(desc);

    settings::SettingDesc budget = settings::MakeText("script.budget", "budgeted");
    budget.env = "ENGINE_SCRIPT_BUDGET";
    budget.flag = "script-budget";
    budget.label = "Script budget mode";
    budget.description =
        "budgeted (the default): scripts run without the JIT and every call can be limited in instructions and time; trusted: "
        "the JIT is on and only memory is limited";
    budget.group = "script";
    budget.flags = settings::kRestartRequired;
    registry->Register(budget);

    settings::SettingDesc seed = settings::MakeInt("script.random_seed", 1, 0, 4294967295LL);
    seed.env = "ENGINE_SCRIPT_RANDOM_SEED";
    seed.flag = "script-random-seed";
    seed.label = "Script random seed";
    seed.description = "the seed of math.random in each new script state (the same seed repeats a run)";
    seed.group = "script";
    seed.flags = settings::kRestartRequired;
    registry->Register(seed);

    settings::SettingDesc partitions = settings::MakeInt("script.partitions", 64, 1, 1024);
    partitions.env = "ENGINE_SCRIPT_PARTITIONS";
    partitions.flag = "script-partitions";
    partitions.label = "Script partitions";
    partitions.description =
        "how many partitions a script world has (an entity's scripts live in one, chosen by a fixed hash); the world's state "
        "is the same for any thread count but may differ between partition counts";
    partitions.group = "script";
    partitions.flags = settings::kRestartRequired;
    registry->Register(partitions);

    settings::SettingDesc memory = settings::MakeInt("script.partition_memory", 67108864, 1048576, 4294967296LL);
    memory.env = "ENGINE_SCRIPT_PARTITION_MEMORY";
    memory.flag = "script-partition-memory";
    memory.label = "Script partition memory";
    memory.description = "the most bytes one partition's script state may hold; a partition that reaches it is reset";
    memory.group = "script";
    memory.flags = settings::kRestartRequired;
    registry->Register(memory);

    settings::SettingDesc conflicts = settings::MakeText("script.conflicts", "last_wins");
    conflicts.env = "ENGINE_SCRIPT_CONFLICTS";
    conflicts.flag = "script-conflicts";
    conflicts.label = "Script write conflicts";
    conflicts.description =
        "what the merge does when two partitions change the same thing of one entity in one tick: last_wins (the higher "
        "partition's command applies; the default) or error (the later command is refused and counted)";
    conflicts.group = "script";
    conflicts.flags = settings::kRestartRequired;
    registry->Register(conflicts);
}

String<HeapAllocator> ScriptBackendName(const settings::Registry& registry)
{
    const settings::SettingId id = registry.Find(StringView("script.backend"));
    if (id.IsValid())
    {
        char text[64];
        const usize length = settings::TextSetting(registry, id).Get(text, sizeof(text));
        if (length > 0 && length < sizeof(text))
        {
            return String<HeapAllocator>(HeapAllocator::Default(), StringView(text, length));
        }
    }
    return String<HeapAllocator>(HeapAllocator::Default(), "luajit");
}

bool ScriptBudgeted(const settings::Registry& registry)
{
    const settings::SettingId id = registry.Find(StringView("script.budget"));
    if (id.IsValid())
    {
        char text[32];
        const usize length = settings::TextSetting(registry, id).Get(text, sizeof(text));
        if (length > 0 && length < sizeof(text))
        {
            return !(StringView(text, length) == StringView("trusted"));
        }
    }
    return true;
}

u32 ScriptRandomSeed(const settings::Registry& registry)
{
    const settings::SettingId id = registry.Find(StringView("script.random_seed"));
    return id.IsValid() ? static_cast<u32>(settings::Setting<i64>(registry, id).Get()) : 1u;
}

bool ScriptConflictsAreErrors(const settings::Registry& registry)
{
    const settings::SettingId id = registry.Find(StringView("script.conflicts"));
    if (id.IsValid())
    {
        char text[32];
        const usize length = settings::TextSetting(registry, id).Get(text, sizeof(text));
        if (length > 0 && length < sizeof(text))
        {
            return StringView(text, length) == StringView("error");
        }
    }
    return false;
}

u32 ScriptPartitions(const settings::Registry& registry)
{
    const settings::SettingId id = registry.Find(StringView("script.partitions"));
    return id.IsValid() ? static_cast<u32>(settings::Setting<i64>(registry, id).Get()) : 64u;
}

u64 ScriptPartitionMemory(const settings::Registry& registry)
{
    const settings::SettingId id = registry.Find(StringView("script.partition_memory"));
    return id.IsValid() ? static_cast<u64>(settings::Setting<i64>(registry, id).Get()) : u64{67108864};
}

}  // namespace engine_placeholder::script
