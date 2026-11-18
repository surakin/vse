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

    settings::SettingDesc log_limit = settings::MakeInt("script.log_limit", 64, 0, 1000000);
    log_limit.env = "ENGINE_SCRIPT_LOG_LIMIT";
    log_limit.flag = "script-log-limit";
    log_limit.label = "Script log limit";
    log_limit.description =
        "how many log.* messages one script may write per tick (0: no limit); the rest are dropped and counted in one "
        "warning, so a script cannot flood the log";
    log_limit.group = "script";
    registry->Register(log_limit);

    settings::SettingDesc chain = settings::MakeInt("script.event_chain_limit", 8, 1, 1000);
    chain.env = "ENGINE_SCRIPT_EVENT_CHAIN_LIMIT";
    chain.flag = "script-event-chain-limit";
    chain.label = "Script event chain limit";
    chain.description =
        "how many handler hops an event chain may take (a handler's own emits are one hop further); an event past the "
        "limit is dropped and counted, so two scripts cannot answer each other forever";
    chain.group = "script";
    registry->Register(chain);

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

    settings::SettingDesc on_error = settings::MakeText("script.on_error", "disable");
    on_error.env = "ENGINE_SCRIPT_ON_ERROR";
    on_error.flag = "script-on-error";
    on_error.label = "Script error policy";
    on_error.description =
        "what happens to a script instance whose callback fails: disable (the default: it stops running), retry (the "
        "callback runs again next tick) or abort_in_debug (disable, and stop a debug build)";
    on_error.group = "script";
    registry->Register(on_error);

    settings::SettingDesc task_states = settings::MakeInt("script.task_states", 0, 0, 1024);
    task_states.env = "ENGINE_SCRIPT_TASK_STATES";
    task_states.flag = "script-task-states";
    task_states.label = "Script task states";
    task_states.description =
        "how many language states run script tasks (task.run, task.map, task.parallel_for) at once; 0 is one per "
        "scheduler thread. The results of a task do not depend on this number";
    task_states.group = "script";
    task_states.flags = settings::kRestartRequired;
    registry->Register(task_states);

    settings::SettingDesc task_queue = settings::MakeInt("script.task_queue_limit", 1024, 1, 1000000);
    task_queue.env = "ENGINE_SCRIPT_TASK_QUEUE_LIMIT";
    task_queue.flag = "script-task-queue-limit";
    task_queue.label = "Script task queue limit";
    task_queue.description =
        "how many tasks one partition may have queued or finished-but-not-taken; task.run past it returns an error "
        "value (nil and a message)";
    task_queue.group = "script";
    registry->Register(task_queue);

    settings::SettingDesc task_bytes = settings::MakeInt("script.task_arg_bytes", 16777216, 1024, 1073741824LL);
    task_bytes.env = "ENGINE_SCRIPT_TASK_ARG_BYTES";
    task_bytes.flag = "script-task-arg-bytes";
    task_bytes.label = "Script task argument bytes";
    task_bytes.description =
        "the most bytes of task source and arguments one partition may queue in a tick; a request past it is refused "
        "with an error value";
    task_bytes.group = "script";
    registry->Register(task_bytes);

    settings::SettingDesc dev_directory = settings::MakeText("script.dev_directory", "");
    dev_directory.env = "ENGINE_SCRIPT_DEV_DIRECTORY";
    dev_directory.flag = "script-dev-directory";
    dev_directory.label = "Script development directory";
    dev_directory.description =
        "a directory a development build reads script sources from, by asset id, instead of the minted blobs (the hot "
        "reload of Day 464); empty is off, and a shipped configuration leaves it empty";
    dev_directory.group = "script";
    dev_directory.flags = settings::kRestartRequired;
    registry->Register(dev_directory);

    settings::SettingDesc hot_reload = settings::MakeBool("script.hot_reload", false);
    hot_reload.env = "ENGINE_SCRIPT_HOT_RELOAD";
    hot_reload.flag = "script-hot-reload";
    hot_reload.label = "Script hot reload";
    hot_reload.description =
        "apply edited scripts to the running world between ticks (development only; off in a shipped build, where the "
        "reloader does nothing)";
    hot_reload.group = "script";
    hot_reload.flags = settings::kRestartRequired;
    registry->Register(hot_reload);

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

OnScriptError ScriptOnError(const settings::Registry& registry)
{
    const settings::SettingId id = registry.Find(StringView("script.on_error"));
    if (id.IsValid())
    {
        char text[32];
        const usize length = settings::TextSetting(registry, id).Get(text, sizeof(text));
        if (length > 0 && length < sizeof(text))
        {
            const StringView value(text, length);
            return value == StringView("retry")            ? OnScriptError::Retry
                   : value == StringView("abort_in_debug") ? OnScriptError::AbortInDebug
                                                           : OnScriptError::Disable;
        }
    }
    return OnScriptError::Disable;
}

u32 ScriptLogLimit(const settings::Registry& registry)
{
    const settings::SettingId id = registry.Find(StringView("script.log_limit"));
    return id.IsValid() ? static_cast<u32>(settings::Setting<i64>(registry, id).Get()) : 64u;
}

u32 ScriptEventChainLimit(const settings::Registry& registry)
{
    const settings::SettingId id = registry.Find(StringView("script.event_chain_limit"));
    return id.IsValid() ? static_cast<u32>(settings::Setting<i64>(registry, id).Get()) : 8u;
}

u32 ScriptTaskStates(const settings::Registry& registry)
{
    const settings::SettingId id = registry.Find(StringView("script.task_states"));
    return id.IsValid() ? static_cast<u32>(settings::Setting<i64>(registry, id).Get()) : 0u;
}

u32 ScriptTaskQueueLimit(const settings::Registry& registry)
{
    const settings::SettingId id = registry.Find(StringView("script.task_queue_limit"));
    return id.IsValid() ? static_cast<u32>(settings::Setting<i64>(registry, id).Get()) : 1024u;
}

u64 ScriptTaskArgBytes(const settings::Registry& registry)
{
    const settings::SettingId id = registry.Find(StringView("script.task_arg_bytes"));
    return id.IsValid() ? static_cast<u64>(settings::Setting<i64>(registry, id).Get()) : u64{16777216};
}

bool ScriptHotReload(const settings::Registry& registry)
{
    const settings::SettingId id = registry.Find(StringView("script.hot_reload"));
    return id.IsValid() && settings::Setting<bool>(registry, id).Get();
}

String<HeapAllocator> ScriptDevDirectory(const settings::Registry& registry)
{
    const settings::SettingId id = registry.Find(StringView("script.dev_directory"));
    if (id.IsValid())
    {
        char text[512];
        const usize length = settings::TextSetting(registry, id).Get(text, sizeof(text));
        if (length > 0 && length < sizeof(text))
        {
            return String<HeapAllocator>(HeapAllocator::Default(), StringView(text, length));
        }
    }
    return String<HeapAllocator>(HeapAllocator::Default());
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
