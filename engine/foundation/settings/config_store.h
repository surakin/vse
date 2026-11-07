#pragma once

//! Where setting values live: the abstract [[settings::ConfigStore]] and the values it holds.
//!
//! The settings system reads and writes through this interface and never assumes a file. A store is a flat collection of
//! `key -> value` entries. Implementations: a text file (Day 133), memory, read-only shipped defaults (all in the foundation); the
//! Windows registry, `NSUserDefaults`, Android `SharedPreferences` and console save data follow in the platform phases.
//!
//! **Staged and immediate writes.** `Set` and `Remove` change the store's *working* state; every later `Get` and `Enumerate` on the
//! same store sees them. A store with [[settings::kCapPerKeyWrites]] (a registry) makes each change durable at once; any other
//! store keeps them staged until [[settings::ConfigStore::Commit]] makes them durable together. [[settings::ConfigStore::Load]]
//! discards staged changes and reads the durable state again.

#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "settings/desc.h"
#include "settings/key.h"
#include "settings/value.h"
#include "types.h"

namespace engine_placeholder::settings {

/// What went wrong in a store call.
enum class StoreError : u8 {
    None = 0,         ///< Success.
    NotFound,         ///< No entry for the key (`Get`, `Remove`).
    TypeMismatch,     ///< The stored value cannot be read as the requested type.
    ReadOnly,         ///< The store cannot be written.
    Damaged,          ///< The stored content could not be read as a whole (a file with errors); `Commit` refuses to replace it.
    Io,               ///< The medium failed.
    Unsupported,      ///< The store does not do this (`SetChangeListener` without [[settings::kCapNotifies]]).
    TooLong,          ///< A key or value is longer than the store can hold.
    Invalid,          ///< The key is not a valid [[settings::Key]], or an argument is wrong.
    PermissionDenied, ///< The medium refused access.
    Interrupted,      ///< The call was abandoned (a shutdown).
};

/// Returns a short name for a store error (`none`, `not_found`, `type_mismatch`, `read_only`, `damaged`, `io`, `unsupported`, `too_long`, `invalid`, `permission_denied`, `interrupted`).
///
/// Returns: a static string.
/// Fails: never.
/// Ownership: the string is static and never freed.
const char* StoreErrorName(StoreError error);

/// The result of a store call: an error value plus, for `Commit`, how far it got.
///
/// Thread-safety: thread-compatible plain data.
struct Status {
    /// What went wrong; `None` means success.
    StoreError error = StoreError::None;
    /// For `Commit`: the number of changes made durable before it stopped (all of them on success, 0 on an atomic store that failed).
    u32 done = 0;
    /// For `Commit`: the number of changes that were staged.
    u32 total = 0;

    /// Returns whether the call succeeded.
    bool Ok() const { return error == StoreError::None; }
    /// Returns whether the call failed.
    bool Failed() const { return error != StoreError::None; }
    /// Returns whether the call failed with `e`.
    bool Is(StoreError e) const { return error == e; }
    /// Makes a status from an error.
    static Status Of(StoreError e) { return Status{e, 0, 0}; }
};

/// A value as a store holds it: typed where the medium has types (a registry), text only where it does not (a file).
///
/// A text-only store receives and returns `Text` values holding the value's canonical text; [[settings::ToStored]] and
/// [[settings::FromStored]] are the one place that converts between a typed [[settings::Value]] and either form.
///
/// Thread-safety: thread-compatible plain data.
struct StoredValue {
    /// True if `value` is a typed value; false if it is the text of a value of a type the store does not record.
    bool typed = false;
    /// The value (a `Text` value when `typed` is false).
    Value value;
};

/// Capabilities of a store, as bits of [[settings::ConfigStore::Capabilities]].
enum StoreCapability : u32 {
    kCapWritable = 1u << 0,          ///< `Set`, `Remove` and `Commit` can succeed.
    kCapPreservesOrder = 1u << 1,    ///< `Enumerate` returns keys in the order they were first stored, and a text store keeps comments and layout.
    kCapAtomicCommit = 1u << 2,      ///< `Commit` makes all staged changes durable or none of them.
    kCapPerKeyWrites = 1u << 3,      ///< `Set` and `Remove` are durable at once; `Commit` has nothing to do.
    kCapNotifies = 1u << 4,          ///< [[settings::ConfigStore::SetChangeListener]] works.
    kCapTypedValues = 1u << 6,       ///< `Get` returns typed values and `Set` records them as typed.
};

/// Options of [[settings::ConfigStore::Commit]].
struct CommitOptions {
    /// Replace content that [[settings::ConfigStore::Load]] found damaged (the store keeps a backup first, where it can).
    bool replace_damaged = false;
};

/// Called when another writer changed the store's durable state; any thread, best effort, may be late or coalesced.
using ChangeListener = void (*)(void* user);

/// Converts a typed value into the form a store holds.
///
/// Param desc: the declaration of the setting.
/// Param value: a value of the declared type.
/// Param typed: true for a store with [[settings::kCapTypedValues]], false for a text store (the value is written as its canonical text).
/// Param out: receives the value to pass to `Set`; unchanged on failure.
/// Returns: `None`, or `TooLong` when the canonical text of a text-store value (quotes and escapes included) exceeds 511 bytes.
/// Fails: reports `TooLong` as the return value.
/// Aborts: the value's type differs from `desc.type`.
StoreError ToStored(const SettingDesc& desc, const Value& value, bool typed, StoredValue* out);

/// Converts a stored value into a typed one for the setting `desc` declares.
///
/// Param desc: the declaration; its range and enum names apply to text.
/// Param stored: what `Get` returned.
/// Param out: receives the value; unchanged on failure.
/// Returns: `None` or `Clamped` (stored), or why the stored value is not acceptable.
/// Fails: a typed stored value of another type than `desc.type` returns `ValueError::BadNumber`, and one outside the range or the enum is checked by
///   [[settings::ValidateValue]]; text is parsed by [[settings::ParseValue]].
ValueError FromStored(const SettingDesc& desc, const StoredValue& stored, Value* out);

/// A store of setting values. Abstract; see the file comment for the staging rules.
///
/// Thread-safety: thread-compatible. One thread at a time per instance, except that a change listener may be called from any thread.
/// Ownership: the caller owns the store (see [[settings::ConfigStorePtr]]).
class ConfigStore {
public:
    virtual ~ConfigStore() = default;

    /// Returns the store's [[settings::StoreCapability]] bits.
    virtual u32 Capabilities() const = 0;

    /// Writes a short description for logs into `out` (a path, `memory`, `registry HKCU\Software\app`), NUL-terminated and cut to fit.
    virtual void Describe(char* out, usize capacity) const = 0;

    /// Reads the durable state, discarding any staged changes. Call it once before using the store, and again to pick up external changes.
    ///
    /// Returns: success, or why the state could not be read. A store whose medium does not exist yet loads as empty and succeeds.
    /// Fails: `Damaged` (the content is unreadable as a whole; reads still see what could be parsed), `Io`, `PermissionDenied`.
    virtual Status Load() = 0;

    /// Reads one entry (staged changes included).
    ///
    /// Param key: a valid key.
    /// Param out: receives the value.
    /// Fails: `NotFound`, `Invalid` (not a valid key).
    virtual Status Get(StringView key, StoredValue* out) = 0;

    /// Sets one entry.
    ///
    /// Param key: a valid key.
    /// Param value: the value; typed only when the store has [[settings::kCapTypedValues]] (use [[settings::ToStored]]).
    /// Returns: success; staged until `Commit` unless the store has [[settings::kCapPerKeyWrites]].
    /// Fails: `ReadOnly`, `Invalid`, `TooLong`, `Io` (a per-key store).
    virtual Status Set(StringView key, const StoredValue& value) = 0;

    /// Removes one entry.
    ///
    /// Fails: `NotFound` (nothing changes), `ReadOnly`, `Invalid`, `Io` (a per-key store).
    virtual Status Remove(StringView key) = 0;

    /// Appends the keys under `prefix` to `out`: the keys equal to `prefix` or starting with `prefix.`; an empty prefix means every key.
    /// The order is unspecified unless the store has [[settings::kCapPreservesOrder]] (callers that need an order sort).
    ///
    /// Fails: `Invalid` (a non-empty prefix that is not a valid key), `Io`.
    virtual Status Enumerate(StringView prefix, Vector<Key>* out) = 0;

    /// Makes the staged changes durable.
    ///
    /// Param options: see [[settings::CommitOptions]].
    /// Returns: success; with nothing staged it succeeds and does nothing.
    /// Fails: `ReadOnly`, `Damaged` (unless `replace_damaged`), `Io`, `PermissionDenied`. With [[settings::kCapAtomicCommit]] a failure leaves the
    ///   durable state as it was and the changes staged (retry is allowed); a store without it reports in `Status::done` how many changes landed.
    virtual Status Commit(const CommitOptions& options) = 0;

    /// Registers a function to call when another writer changes the durable state. Replaces any earlier listener; a null function removes it.
    ///
    /// Param listener: the function; called on an unspecified thread, never from inside a call on this store by the same thread. It should only
///   record that a change happened (set a flag, queue work): it must not block on locks other threads hold while they use stores, and must not create or
///   destroy stores.
    /// Param user: passed back to `listener`.
    /// Fails: `Unsupported` (no [[settings::kCapNotifies]]).
    virtual Status SetChangeListener(ChangeListener listener, void* user) {
        (void)listener;
        (void)user;
        return Status::Of(StoreError::Unsupported);
    }
};

/// An owning pointer to a store.
using ConfigStorePtr = UniquePtr<ConfigStore>;

} // namespace engine_placeholder::settings
