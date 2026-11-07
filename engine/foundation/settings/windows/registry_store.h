#pragma once
//! The Windows registry as a [[settings::ConfigStore]] (Phase 13 Day 157; `engine/foundation/settings/BACKEND.md`).
//!
//! A store lives under one registry key, `HKCU\Software\<app>` by default. A dotted key's sections are subkeys and its last segment is the
//! value name: `renderer.width` is the value `width` under `<root>\renderer`. Every value is a `REG_SZ` holding the value's canonical text,
//! the same text a settings file holds (`true`, `-42`, `0.1`, `fifo`, `"a name"`), so `regedit` shows and edits what a file would, and every
//! type reads back exactly; a value of another registry type is reported as `TypeMismatch` for its key, never converted. Each write is durable
//! when it returns; there is no order and no comment. The 64-bit view of the registry is used from every process, so a 32-bit program sees
//! the same values. The store tells a listener about other writers' changes from a thread of its own (`RegNotifyChangeKeyValue`).
#include "containers/vector.h"
#include "settings/config_store.h"
#include "string/string.h"
#include "types.h"

#include <atomic>
#include <mutex>
#include <thread>

namespace engine_placeholder::settings {

/// The registry hive a [[settings::RegistryConfigStore]] lives in.
enum class RegistryHive : u8 {
    CurrentUser,  ///< `HKEY_CURRENT_USER`: the user's own settings, and the user's policies.
    LocalMachine, ///< `HKEY_LOCAL_MACHINE`: the machine's policies (read-only for a program that is not elevated).
};

/// Where a [[settings::RegistryConfigStore]] keeps its values.
///
/// Thread-safety: thread-compatible plain data.
struct RegistryStoreOptions {
    /// The hive.
    RegistryHive hive = RegistryHive::CurrentUser;
    /// The key under the hive, with backslashes (`Software\mygame`); borrowed for the constructor's call.
    const char* path = "Software\\engine";
    /// Refuse every write (a policy store): `Set`, `Remove` and `Commit` return `ReadOnly`.
    bool read_only = false;
};

/// The registry key of an application's own settings: `Software\<app>`.
///
/// Param app: the application's name (one path component).
/// Param out: receives the path; NUL-terminated and cut to fit.
/// Fails: never.
void RegistrySettingsPath(StringView app, char* out, usize capacity);

/// A [[settings::ConfigStore]] over one registry key (see the file comment for the layout).
///
/// Thread-safety: thread-compatible, as every store; the change listener runs on the store's own watch thread.
/// Ownership: the caller owns the store; destroying it stops the watch thread before it returns.
class RegistryConfigStore final : public ConfigStore {
public:
    /// Makes a store over `options.path` in `options.hive`; nothing is opened until the first call.
    explicit RegistryConfigStore(const RegistryStoreOptions& options);
    /// Stops the watch thread (waiting for a listener call in progress) and releases the store; the registry keeps what was written.
    ~RegistryConfigStore() override;
    RegistryConfigStore(const RegistryConfigStore&) = delete;
    RegistryConfigStore& operator=(const RegistryConfigStore&) = delete;

    /// `kCapWritable` (unless read-only), `kCapPerKeyWrites` and `kCapNotifies`. Values are text (no `kCapTypedValues`): see the file comment.
    u32 Capabilities() const override;
    /// `registry HKCU\<path>` (or `HKLM\`).
    void Describe(char* out, usize capacity) const override;
    /// Checks that the key can be read; a key that does not exist loads as empty. Every read goes to the registry, so nothing is cached.
    Status Load() override;
    /// Reads one value. `TypeMismatch` for a value that is not `REG_SZ`, `TooLong` for one longer than a setting's text.
    Status Get(StringView key, StoredValue* out) override;
    /// Writes one value at once (text only: a typed value is `Invalid`), creating the section subkeys it needs.
    Status Set(StringView key, const StoredValue& value) override;
    /// Deletes one value at once, and the section subkeys left empty.
    Status Remove(StringView key) override;
    /// Lists the values under the prefix's subkey, three levels deep at most; names that are not valid keys (written by hand) are skipped.
    Status Enumerate(StringView prefix, Vector<Key>* out) override;
    /// Nothing to do: every write was durable when it returned (`ReadOnly` for a read-only store).
    Status Commit(const CommitOptions& options) override;
    /// The value's place: `HKCU\<path>\<section>` and the value name.
    void Origin(StringView key, char* out, usize capacity) const override;
    /// Whether the key exists.
    bool IsPresent() const override;
    /// Starts (or, with null, stops) the watch thread; the listener is called on it for another writer's change, never for this store's own.
    ///
    /// Fails: `Invalid` when called from the listener itself (stopping the thread would wait for the call in progress); `Io` if the thread's
    ///   event cannot be made. A listener must not destroy its store either.
    Status SetChangeListener(ChangeListener listener, void* user) override;

private:
    struct Entry {
        Key key;
        String<HeapAllocator> text = String<HeapAllocator>(HeapAllocator::Default());
    };
    void StopWatching();
    void WatchLoop();
    // Reads every value under the root into `out` (sorted by key), the comparison the watch thread makes; false if the root cannot be read.
    bool ReadAll(Vector<Entry>* out) const;
    // Records this store's own change in `known_`, so the watch thread does not report it (call with mutex_ held).
    void RememberOwn(const Key& key, const StringView* text);
    // Re-reads the state; if another writer changed it, updates `known_` and calls the listener.
    void CheckForChange();

    RegistryHive hive_;
    bool read_only_;
    String<HeapAllocator> path_ = String<HeapAllocator>(HeapAllocator::Default());
    std::mutex mutex_;              // a write and the watch thread's comparison never interleave
    Vector<Entry> known_;           // the state as this store last saw it (its own writes included)
    ChangeListener listener_ = nullptr;
    void* listener_user_ = nullptr;
    std::thread watcher_;
    void* stop_event_ = nullptr;    // HANDLE
    std::atomic<bool> stopping_{false};
};

} // namespace engine_placeholder::settings
