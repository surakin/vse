#include "storage/local_storage.h"

#include "database/database_settings.h"

namespace engine_placeholder::storage
{

Status LocalStorage::OpenFromSettings(database::Database& backend, const settings::Registry& settings,
                                      const ComponentRegistry& components, UniquePtr<LocalStorage>* out)
{
    UniquePtr<LocalStorage> result = MakeUnique<LocalStorage>();
    result->path_ = PathFromSettings(settings);
    const database::OpenOptions options = database::OptionsFromSettings(settings, result->path_.View());
    ENGINE_TRY(detail::MapDatabaseStatus(backend.Open(options, &result->connection_)));
    SqlStoreOptions store_options;
    store_options.registry = &components;
    store_options.columns = ColumnsFromSettings(settings);
    store_options.limits = LimitsFromSettings(settings);
    ENGINE_TRY(SqlStore::Open(*result->connection_, &result->store_, store_options));
    result->worlds_ = MakeUnique<WorldStorage>(components, *result->store_);
    result->worlds_->SetLimits(store_options.limits);
    result->autosave_seconds_ = AutosaveSecondsFromSettings(settings);
    *out = Move(result);
    return Status::Success();
}

}  // namespace engine_placeholder::storage
