#pragma once

//! [[storage::LocalStorage]]: a database connection, a SQL store and a [[storage::WorldStorage]]
//! opened from the settings.

#include "database/database.h"
#include "storage/sql_store.h"
#include "storage/storage_settings.h"
#include "storage/world_storage.h"

namespace vse::storage
{

/// Everything a program needs to save worlds to a database file, built from the settings: the connection
/// (`database.*`), the store (`storage.layout`), the limits
/// (`storage.max_*`) and the world layer. The file is `storage.path`; empty keeps a private in-memory database.
///
/// - Thread-safety: thread-compatible; the parts follow their own rules.
/// - Ownership: owns the connection, the store and the world layer; borrows the component registry, which must outlive
/// it.
class LocalStorage
{
public:
    /// Opens the storage the settings describe.
    ///
    /// - Param backend: the database backend that opens the file (the program links it; `storage` itself does not name
    /// one).
    /// - Param settings: a registry in which [[storage::RegisterStorageSettings]] and
    /// [[database::RegisterDatabaseSettings]] ran.
    /// - Param components: the registry of stored components; must outlive the result.
    /// - Param out: receives the storage on success; unchanged on failure.
    /// - Aborts: the settings are not registered.
    /// - Ownership: all borrowed for the call except `components`, which the result borrows.
    /// - Fails: the errors of `Database::Open` and `SqlStore::Open` (`Io`, `Corrupt`, `Busy`, `TooNew`, `Invalid`).
    static Status OpenFromSettings(database::Database& backend, const settings::Registry& settings,
                                   const ComponentRegistry& components, UniquePtr<LocalStorage>* out);

    LocalStorage() = default;
    LocalStorage(const LocalStorage&) = delete;
    LocalStorage& operator=(const LocalStorage&) = delete;

    /// The world layer.
    /// - Fails: never.
    WorldStorage& Worlds() { return *worlds_; }
    /// The SQL store.
    /// - Fails: never.
    SqlStore& Records() { return *store_; }
    /// The connection.
    /// - Fails: never.
    database::Connection& Connection() { return *connection_; }
    /// `storage.autosave_seconds` at the time of opening.
    /// - Fails: never.
    u32 AutosaveSeconds() const { return autosave_seconds_; }
    /// The path that was opened (empty for a private in-memory database).
    /// - Fails: never.
    StringView Path() const { return path_.View(); }

private:
    String<HeapAllocator> path_{HeapAllocator::Default()};
    UniquePtr<database::Connection> connection_;
    UniquePtr<SqlStore> store_;
    UniquePtr<WorldStorage> worlds_;
    u32 autosave_seconds_ = 0;
};

}  // namespace vse::storage
