#pragma once

//! The settings service an application owns: it builds the layer stack, publishes values to the typed handles, tells interested code what
//! changed, and saves.
//!
//! ```cpp
//! settings::Settings settings(settings::Registry::Global());
//! settings::LoadOptions load;
//! load.app_name = "mygame";
//! load.argc = argc; load.argv = argv;
//! settings::LoadReport report = settings.Load(load);   // every problem is in the report; nothing stops the others
//! ...
//! settings::SaveOnExit save(settings);                 // writes the user's file at the end of the scope if anything changed
//! ```

#include "sync/mutex.h"

#include "containers/shared_ptr.h"
#include "io/path.h"
#include "settings/env_source.h"
#include "settings/layers.h"
#include "settings/memory_store.h"
#include "settings/migration.h"

namespace engine_placeholder::settings {

/// One setting that changed: its id and the values before and after.
///
/// - Thread-safety: thread-compatible plain data.
struct Change {
    /// The setting.
    SettingId id;
    /// The value before the change.
    Value old_value;
    /// The value after it.
    Value new_value;
};

/// The changes of one `Set` or `Apply` call, handed to listeners and to the apply function together.
///
/// - Thread-safety: thread-compatible; valid only during the call it is passed to.
struct ChangeEvent {
    /// The registry the ids belong to.
    const Registry* registry = nullptr;
    /// The changes, in the order they were requested.
    const Change* changes = nullptr;
    /// How many.
    usize count = 0;
    /// The highest apply class among the changes (the registrant's cost class), for a consumer that does the cheapest sufficient action.
    u8 highest_apply_class = 0;
    /// Whether any change is to a setting marked `kRestartRequired`.
    bool restart_required = false;
};

/// A function called with the changes whose keys match a subscription.
using SettingsListener = void (*)(void* user, const ChangeEvent& event);
/// A function that applies a batch of changes (the renderer's `ApplySettings`); returning false rejects the batch and rolls it back.
using ApplyFn = bool (*)(void* user, const ChangeEvent& event);
/// A function that says whether a capability is available (see [[settings::SettingDesc::capability]]).
using CapabilityFn = bool (*)(void* user, const char* capability);

/// A request to set one setting.
struct ChangeRequest {
    /// The setting.
    SettingId id;
    /// The new value (checked against the declaration).
    Value value;
};

/// Why a `Set` or `Apply` did nothing.
enum class SetError : u8 {
    None = 0,       ///< The change was made (or it was already the value).
    UnknownKey,     ///< No such setting.
    BadValue,       ///< The value is of the wrong type, outside the range (`Reject`), not in the enum, or the text does not parse (see `value`).
    Unsupported,    ///< The setting needs a capability that is missing and the value is not the default.
    Store,          ///< The write layer refused (see `store`).
    ApplyFailed,    ///< The apply function rejected the batch; it was rolled back.
    Reentrant,      ///< Called from a listener nested more than 8 deep.
    Enforced,       ///< A [[settings::LayerRole::Policy]] layer holds the setting: an administrator decides its value, the program cannot change it.
};

/// The result of `Set`, `SetText` or `Apply`.
///
/// - Thread-safety: thread-compatible plain data.
struct SetResult {
    /// What went wrong.
    SetError error = SetError::None;
    /// For `BadValue`: the reason.
    ValueError value = ValueError::None;
    /// For `Store`: the store's error.
    StoreError store = StoreError::None;
    /// How many settings actually changed.
    u32 changed = 0;
    /// Returns whether the call succeeded.
    bool Ok() const { return error == SetError::None; }
};

/// How `Load` should build the layers.
struct LoadOptions {
    /// The application's name: the user's file is `<user config directory>/<app_name>/settings.cfg`.
    const char* app_name = "engine";
    /// A file for the user layer instead of the standard one, or null.
    const io::Path* user_file = nullptr;
    /// A read-only system-wide file, or null.
    const io::Path* system_file = nullptr;
    /// A project file next to the content (read-only), or null.
    const io::Path* project_file = nullptr;
    /// Defaults the program ships, as text entries (the `System` layer), or null.
    const DefaultEntry* shipped = nullptr;
    /// How many entries `shipped` holds.
    usize shipped_count = 0;
    /// The environment to read, or null for the process's own.
    const EnvironmentSnapshot* environment = nullptr;
    /// Read the environment at all.
    bool use_environment = true;
    /// The program's arguments, or null/0 for none.
    const char* const* argv = nullptr;
    /// The argument count including the program name.
    int argc = 0;
    /// `MinimalWrites` for later `Set` calls: a value equal to what the lower layers give is not written to the file.
    bool minimal_writes = true;
    /// Add the platform's policy layers ([[settings::LayerRole::Policy]]): on Windows the group-policy keys `Software\Policies\<name>` under
    /// HKCU and then HKLM (the machine's win); elsewhere none yet.
    bool use_policy = true;
    /// The name the policy layers read under, or null for `app_name`.
    const char* policy_name = nullptr;
    /// The registry key the policy layers read instead of `Software\Policies\<name>` (a test's own key), or null.
    const char* policy_path = nullptr;
};

/// What `Load` found.
struct LoadReport {
    /// One entry per layer, lowest first.
    struct LayerInfo {
        /// The layer's name.
        char name[128] = {};
        /// How it fared.
        LayerState state = LayerState::NotLoaded;
    };
    /// The layers.
    Vector<LayerInfo> layers;
    /// Every problem: damaged and failed layers, bad values that were skipped, command-line errors.
    Vector<LayerProblem> problems;
    /// What was noticed but is not wrong: migrations applied to the user's file, unknown keys (kept as they are, with a suggestion), a file
    /// written by a newer version (read in compatibility mode).
    Vector<LayerProblem> notes;
    /// Returns whether no layer failed or was damaged and nothing was skipped.
    bool Clean() const { return problems.Empty(); }
};

/// Options of `Save`.
struct SaveOptions {
    /// Also write the values the command line supplied (`--set` is not saved otherwise). They become part of the user's file: a later plain
    /// `Save` keeps them. If the save fails they are taken out of the write layer again.
    bool include_command_line = false;
    /// Replace a user file that was damaged on load (the old text goes to `.bak`).
    bool replace_damaged = false;
    /// Replace a user file written by a newer version of the program (it is otherwise never rewritten in an older format; the old text goes to `.bak`).
    bool replace_newer = false;
};

/// A snapshot of every setting's value, consistent at one instant.
///
/// - Thread-safety: thread-safe; immutable once published, so any thread may read it.
struct SettingsSnapshot {
    /// The values, indexed by `SettingId::value - 1`. Each is immutable and shared with the snapshots before and after this one, so a change
    /// copies a table of pointers, not every value.
    Vector<SharedPtr<const Value>> values;
    /// Increases with every published change.
    u64 generation = 0;
    /// Returns a setting's value.
    ///
    /// - Aborts: `id` is not in the snapshot.
    const Value& Get(SettingId id) const;
};

/// The settings of one application: registry, layers, current values, listeners and saving.
///
/// - Thread-safety: thread-safe. `Set`, `Apply`, `Save`, `Load`, `Provenance` and listener registration are serialized by one recursive lock that
///   is held while the apply function and the listeners run, so changes are announced in the order they were published, a rejected batch is
///   rolled back before anyone else can change anything, and a listener or the apply function may call `Set` again on its own thread (up to 8
///   deep). A listener must not wait for another thread that is itself waiting to change a setting. Typed handles and
///   [[settings::Settings::Snapshot]] never wait and may be read from any thread at any time.
/// - Ownership: borrows the registry, which must outlive it; owns its layers.
class Settings {
public:
    /// Makes a service over `registry` and freezes it.
    explicit Settings(Registry& registry);
    /// Destroys the service; handles made from the registry keep working.
    ~Settings();
    Settings(const Settings&) = delete;
    Settings& operator=(const Settings&) = delete;

    /// Returns the registry.
    Registry& GetRegistry() { return *registry_; }

    /// Builds the standard stack (shipped defaults, system file, the user's file, project file, `--settings` files, environment, command line), reads
    /// every layer, resolves every setting and publishes the values. Replaces earlier layers and drops staged changes. A problem in one layer
    /// never stops the others; all are in the report.
    ///
    /// - Param options: see [[settings::LoadOptions]].
    /// - Returns: the report.
    LoadReport Load(const LoadOptions& options);

    /// Reads the layers already in [[settings::Settings::Layers]] (added by hand, for tests or a custom stack), resolves every setting and
    /// publishes the values. `Load` is this after building the standard stack.
    LoadReport LoadFromLayers();

    /// Returns the migration steps applied to the user's file on load; add steps before `Load`.
    MigrationSet& Migrations() { return migrations_; }
    /// Returns whether the last load found the user's file written by a newer version (compatibility mode: `Save` refuses without `replace_newer`).
    bool UserFileIsNewer() const { return newer_file_; }

    /// Returns the layer stack (for tests and for a custom stack: add layers, then call `LoadFromLayers`).
    LayerStack& Layers() { return stack_; }
    /// Returns what the command line asked for in the last `Load`.
    const CommandLineResult& CommandLine() const { return command_line_; }

    /// Sets one setting.
    ///
    /// - Param id: the setting.
    /// - Param value: checked against the declaration (range policy, enum, capability).
    /// - Returns: what happened; on success the value is published and listeners have run.
    SetResult Set(SettingId id, const Value& value);
    /// Sets a setting from its text form (`1920`, `mailbox`, `"quoted"`), found by key or deprecated key.
    SetResult SetText(StringView key, StringView text);
    /// Sets a setting the way the command line would: the value goes into the command-line layer (so it outranks files and the environment, shows
    /// `origin` as its source, and is not written to the user's file), and is published like any change. For programs that keep an old flag
    /// (`--no-vsync`) next to the settings options. An explicit `--set` of the same key wins and nothing changes.
    ///
    /// - Param key: the setting's key.
    /// - Param text: the value in its text form.
    /// - Param origin: shown as the source, for example `--no-vsync`.
    /// - Returns: as [[settings::Settings::Set]]; with no command-line layer (a hand-built stack) it is `SetText`.
    SetResult SetFromCommandLine(StringView key, StringView text, StringView origin);
    /// Sets several settings as one change: all are checked first and written to the write layer, then `apply` (if any) is called with the whole
    /// batch (the handles and the snapshot still show the old values during the call: the event carries the new ones); if it returns false the
    /// write layer is restored and nothing is published or announced. Otherwise the values are published together and the listeners see the
    /// batch together.
    ///
    /// - Param requests: the changes.
    /// - Param count: how many.
    /// - Param apply: the consumer that acts on the change, or null.
    /// - Param user: passed to `apply`.
    SetResult Apply(const ChangeRequest* requests, usize count, ApplyFn apply, void* user);

    /// Subscribes to changes. A listener is called, in the order added, with the changes whose key equals `prefix` or lies below it
    /// (an empty prefix matches everything).
    ///
    /// - Returns: an id for `RemoveListener`.
    u32 AddListener(StringView prefix, SettingsListener listener, void* user);
    /// Removes a subscription; a call already under way may still happen.
    void RemoveListener(u32 id);
    /// Sets the function that says which capabilities exist (default: all do).
    void SetCapabilityCheck(CapabilityFn check, void* user);

    /// Returns an immutable snapshot of all values.
    SharedPtr<const SettingsSnapshot> Snapshot() const;
    /// Resolves a setting's value now, with its provenance (the layer, the line or variable).
    Resolution Provenance(SettingId id);

    /// Writes the user's file: only the write layer, atomically, with a `.bak`. The layers above it are not written (unless asked).
    ///
    /// - Returns: success, or why not (`Damaged`, `PermissionDenied`, `Io`, ...); the in-memory values are unaffected by a failure.
    Status Save(const SaveOptions& options = SaveOptions());
    /// Returns the result of the most recent `Save` (including the ones `SaveIfChanged`, `SaveOnExit` and autosave make), so an application can tell the
    /// user that saving is failing (`TooNew`, `PermissionDenied`, ...). Success until a save has failed.
    Status LastSaveStatus() const {
        LockGuard<Mutex> lock(mutex_);
        return last_save_status_;
    }
    /// Saves only if a change was made since the last load or save.
    Status SaveIfChanged();
    /// Returns whether a change has not been saved.
    bool HasUnsavedChanges() const;
    /// Seconds of quiet after which `PollAutosave` saves; 0 (the default) disables autosave.
    void SetAutosaveQuietSeconds(double seconds);
    /// Saves if autosave is on, there are unsaved changes and the last change is old enough. Call it now and then (once a frame is fine).
    ///
    /// - Returns: whether a save was attempted and succeeded.
    bool PollAutosave();

private:
    struct Subscription;
    SetResult DoApply(const ChangeRequest* requests, usize count, ApplyFn apply, void* user, bool memory_only = false); // memory_only: skip the write layer
    void RebuildSnapshotLocked();
    void PublishChangesLocked(const Change* changes, usize count); // a new snapshot that differs from the last in these settings only
    void UpdateCrashState();
    Registry* registry_;
    // `op_` serializes every operation that changes or reads the layer stack (it is recursive, so a listener or apply function may call Set);
    // `mutex_` guards only the published snapshot and the dirty flag, so Snapshot() never waits behind an operation.
    mutable RecursiveMutex op_;
    mutable Mutex mutex_;
    LayerStack stack_;
    CommandLineResult command_line_;
    SharedPtr<const SettingsSnapshot> snapshot_;
    Vector<UniquePtr<Subscription>> subscriptions_;
    u32 next_listener_ = 1;
    CapabilityFn capability_ = nullptr;
    void* capability_user_ = nullptr;
    MigrationSet migrations_;
    bool newer_file_ = false;
    bool cli_from_load_ = false; // the command-line layer is a CommandLineSource that Load made
    Status last_save_status_;
    bool stamp_versions_ = false; // write `version`/`<section>.schema` into the file at the next save
    bool minimal_ = true;
    bool dirty_ = false;
    double autosave_quiet_ = 0;
    double last_change_ = 0;
    u64 generation_ = 0;
    int crash_entries_used_ = 0;
};

/// Saves at the end of a scope if anything changed.
///
/// - Thread-safety: thread-compatible.
class SaveOnExit {
public:
    /// Watches `settings`, which must outlive the guard.
    explicit SaveOnExit(Settings& settings) : settings_(&settings) {}
    /// Calls `SaveIfChanged`; a failure is logged by the caller if it cares: a destructor cannot report it.
    ~SaveOnExit() { settings_->SaveIfChanged(); }
    SaveOnExit(const SaveOnExit&) = delete;
    SaveOnExit& operator=(const SaveOnExit&) = delete;

private:
    Settings* settings_;
};

} // namespace engine_placeholder::settings
