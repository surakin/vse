#pragma once

//! Changing a setting between versions: the format version, per-section schema versions and migration steps.
//!
//! A settings file records `version = N` (the file format, [[settings::kFormatVersion]]) and, for each section its
//! owner gave a schema version with [[settings::Registry::SetSchemaVersion]], `<section>.schema = N`. When a file is
//! loaded, a [[settings::MigrationSet]] upgrades the user's store from the version the file has to the version the
//! program has, in order. The upgraded content is in memory (and staged in the store); the file is rewritten, comments
//! and all, the next time the application saves. A file *newer* than the program is read in compatibility mode and
//! never rewritten in an older format.

#include "containers/vector.h"
#include "settings/config_store.h"
#include "settings/registry.h"

namespace engine_placeholder::settings
{

/// The version of the settings file format this program writes (`version = 1`).
inline constexpr u32 kFormatVersion = 1;

/// A function that changes a store in a way the declarative steps cannot (splitting one key into two, merging two into
/// one). It may call `Get`, `Set`, `Remove` and `Enumerate` on the store. Returning false reports a failure in the
/// migration report; the store keeps whatever the function had already changed.
using MigrationFn = bool (*)(void* user, ConfigStore& store);

/// What a migration step does.
enum class MigrationOp : u8
{
    Rename,    ///< Move the value of `old_key` to `new_key` (an existing `new_key` wins and `old_key` is dropped).
    MapValue,  ///< Where `key` holds the text `old_value`, write `new_value`.
    Drop,      ///< Remove `key`: it no longer exists.
    Callback,  ///< Call a function.
};

/// One step: it upgrades `section` from schema version `from_version` to `from_version + 1`.
///
/// - Thread-safety: thread-compatible plain data; the strings are borrowed (use literals).
struct MigrationStep
{
    /// The section (a valid key such as `renderer`).
    const char* section = nullptr;
    /// The version this step upgrades from.
    u32 from_version = 1;
    /// What it does.
    MigrationOp op = MigrationOp::Rename;
    /// `Rename`: the old full key. `MapValue` and `Drop`: the key.
    const char* key = nullptr;
    /// `Rename`: the new full key.
    const char* new_key = nullptr;
    /// `MapValue`: the old value as written in the file (`mailbox`, `"fifo"`); a text setting is stored quoted, and the
    /// plain text (`fifo`) matches it too.
    const char* old_value = nullptr;
    /// `MapValue`: the new value as it will be written.
    const char* new_value = nullptr;
    /// `Callback`: the function.
    MigrationFn fn = nullptr;
    /// `Callback`: passed to `fn`.
    void* user = nullptr;
};

/// What a migration run did, one line per step applied.
///
/// - Thread-safety: thread-compatible.
struct MigrationReport
{
    /// A line for each step that changed something, and for each failure.
    Vector<String<HeapAllocator>> notes;
    /// How many steps ran.
    u32 steps_applied = 0;
    /// How many sections were upgraded.
    u32 sections_upgraded = 0;
    /// Sections whose file schema is newer than the program's (read in compatibility mode).
    Vector<String<HeapAllocator>> newer_sections;
    /// Whether the file format version is newer than [[settings::kFormatVersion]].
    bool newer_format = false;
};

/// The steps an application registers, applied to a store on load.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: copies the steps (the strings in them are borrowed).
class MigrationSet
{
public:
    /// Adds a step.
    ///
    /// - Aborts: `section` or `key` is null, or the section is not a valid key.
    void Add(const MigrationStep& step);
    /// Returns the number of steps.
    usize Size() const { return steps_.Size(); }

    /// Upgrades `store` (already loaded) from the versions it records to the versions `registry` declares, and records
    /// the new versions
    /// (`<section>.schema`) in it. Steps run in order of `from_version`, then registration order. Running it again does
    /// nothing.
    ///
    /// - Param registry: the declared schema versions.
    /// - Param store: the writable store to upgrade (normally the user's file).
    /// - Param report: receives what was done; may be null.
    void Migrate(const Registry& registry, ConfigStore& store, MigrationReport* report) const;

private:
    Vector<MigrationStep> steps_;
};

/// Reads the file format version recorded in `store` (`version = N`): 0 if there is none, and 0xFFFFFFFF if it cannot
/// be read as a number (which counts as newer than any program).
u32 StoredFormatVersion(ConfigStore& store);
/// Reads the schema version recorded for `section` (`<section>.schema`): 1 if there is none, and 0xFFFFFFFF if it
/// cannot be read as a number.
u32 StoredSchemaVersion(ConfigStore& store, StringView section);
/// Writes `version = N` and each declared `<section>.schema = N` into `store` where they are missing (so a saved file
/// records what wrote it); with `overwrite` it replaces the ones that are there (a newer file being replaced).
void StampVersions(const Registry& registry, ConfigStore& store, bool overwrite = false);

}  // namespace engine_placeholder::settings
