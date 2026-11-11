#pragma once

//! The database settings (Phase 19 Day 255): [[database::RegisterDatabaseSettings]] declares them,
//! [[database::OptionsFromSettings]] turns them into
//! [[database::OpenOptions]]. Anything a user can configure about the database is declared here, not read from the
//! environment by hand.

#include "database/database.h"
#include "settings/registry.h"

namespace engine_placeholder::database
{

/// Declares `database.journal_mode`, `database.synchronous`, `database.busy_timeout_ms`, `database.integrity_check` and
/// `database.statement_cache` (each with an environment variable `ENGINE_DATABASE_*` and a flag `database-*`).
/// Registering twice is harmless: keys already present are kept.
///
/// - Param registry: the registry to declare them in, before it is frozen.
/// - Ownership: `registry` is borrowed for the call.
/// - Fails: never.
void RegisterDatabaseSettings(settings::Registry* registry);

/// Returns options for `location` with the journal mode, sync mode, busy timeout, integrity check and statement cache
/// size taken from the settings (the current values; a later change affects the next `Open`). Other fields keep their
/// defaults.
///
/// - Param registry: a registry in which [[database::RegisterDatabaseSettings]] ran.
/// - Param location: copied into the options as a view; the caller keeps its text alive while the options are used.
/// - Aborts: the settings are not registered.
/// - Ownership: `registry` is borrowed for the call.
OpenOptions OptionsFromSettings(const settings::Registry& registry, StringView location);

}  // namespace engine_placeholder::database
