#include "database/database_settings.h"

#include "settings/desc.h"
#include "settings/setting.h"

namespace vse::database
{
namespace
{

const char* const kJournalModes[] = {"default", "delete", "truncate", "wal"};
const char* const kSyncModes[] = {"default", "off", "normal", "full"};
const char* const kIntegrityChecks[] = {"off", "quick", "full"};

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
    d.group = "database";
    d.flags = settings::kAdvanced;
    registry->Register(d);
}

}  // namespace

void RegisterDatabaseSettings(settings::Registry* registry)
{
    Declare(registry, settings::MakeEnum("database.journal_mode", kJournalModes, 4, 0), "ENGINE_DATABASE_JOURNAL_MODE",
            "database-journal-mode", "Database journal mode",
            "how commits are made atomic: default (the write-ahead log for a file), delete, truncate or wal");
    Declare(registry, settings::MakeEnum("database.synchronous", kSyncModes, 4, 0), "ENGINE_DATABASE_SYNCHRONOUS",
            "database-synchronous", "Database sync mode",
            "how hard commits are pushed to the disk: default (normal), off, normal or full");
    Declare(registry, settings::MakeInt("database.busy_timeout_ms", 5000, 0, 600000), "ENGINE_DATABASE_BUSY_TIMEOUT_MS",
            "database-busy-timeout-ms", "Database busy timeout",
            "milliseconds to wait for a lock held by another connection before reporting busy");
    Declare(registry, settings::MakeEnum("database.integrity_check", kIntegrityChecks, 3, 0),
            "ENGINE_DATABASE_INTEGRITY_CHECK", "database-integrity-check", "Database integrity check",
            "a check made when a database file opens: off, quick or full (full reads the whole file)");
    Declare(registry, settings::MakeInt("database.statement_cache", 32, 0, 1024), "ENGINE_DATABASE_STATEMENT_CACHE",
            "database-statement-cache", "Database statement cache",
            "how many finished statements each connection keeps for reuse (0 keeps none)");
}

OpenOptions OptionsFromSettings(const settings::Registry& registry, StringView location)
{
    OpenOptions options;
    options.location = location;
    options.journal_mode = settings::Setting<JournalMode>::Find(registry, StringView("database.journal_mode")).Get();
    options.sync_mode = settings::Setting<SyncMode>::Find(registry, StringView("database.synchronous")).Get();
    options.busy_timeout_ms = static_cast<u32>(
        settings::Setting<i64>::Find(registry, StringView("database.busy_timeout_ms")).Get());
    options.integrity_check = settings::Setting<IntegrityCheck>::Find(registry, StringView("database.integrity_check"))
                                  .Get();
    options.statement_cache = static_cast<u32>(
        settings::Setting<i64>::Find(registry, StringView("database.statement_cache")).Get());
    return options;
}

}  // namespace vse::database
