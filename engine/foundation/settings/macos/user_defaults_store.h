#pragma once

//! The user defaults system (`NSUserDefaults`, `CFPreferences`) as a [[settings::ConfigStore]] (Phase 26 Day 309;
//! `engine/foundation/settings/BACKEND.md`).
//!
//! A store is one defaults domain (a *suite*: `defaults read <suite>` shows it), for the current user on any host. A
//! dotted key is the defaults key as it is (`renderer.width`). Every value is written as a string holding the value's
//! canonical text, the text a settings file holds (`true`, `-42`, `0.1`, `fifo`, `"a name"`), for the reason the
//! Windows registry store gives: a typed enum reaches a store as an index without its names, so only text round-trips
//! every type. A value written by hand with a type (`defaults write <suite> renderer.width -int 1280`, a boolean, a
//! real) reads as its text; a date, data, array or dictionary is `TypeMismatch`. Each write is durable when it returns
//! (the preferences daemon holds it for every process), so there is no staging; `Commit` asks the daemon to write the
//! domain's file now. There is no order and no comment.
//!
//! Change notifications: macOS posts `NSUserDefaultsDidChangeNotification` for this process's own writes only (checked
//! on Day 309: an outside `defaults write` posts nothing), so the store compares the domain with what it last saw, on
//! a thread of its own every quarter of a second, and tells the listener about another writer's change, never about
//! its own.
//!
//! A *forced-only* store is the read-only view of the values a configuration profile enforces for the suite (managed
//! preferences, `CFPreferencesAppValueIsForced`): the settings' `Policy` layer on macOS.
#include "containers/vector.h"
#include "settings/config_store.h"
#include "string/string.h"
#include "sync/atomic.h"
#include "sync/mutex.h"
#include "sync/thread.h"
#include "types.h"

namespace engine_placeholder::settings
{

/// Where a [[settings::UserDefaultsConfigStore]] keeps its values.
///
/// - Thread-safety: thread-compatible plain data.
struct UserDefaultsStoreOptions
{
    /// The defaults domain (the suite), such as `com.example.mygame`.
    ///
    /// The application's bundle identifier or name; borrowed for the constructor's call. Empty is refused by every call
    /// (`Invalid`).
    const char* suite = "engine";
    /// Refuse every write: `Set`, `Remove` and `Commit` return `ReadOnly`.
    bool read_only = false;
    /// Only the values a configuration profile enforces (managed preferences); implies `read_only`.
    bool forced_only = false;
};

/// A [[settings::ConfigStore]] over one defaults domain (see the file comment for the mapping).
///
/// - Thread-safety: thread-compatible, as every store; the change listener runs on the store's own watch thread.
/// - Ownership: the caller owns the store; destroying it stops the watch thread before it returns. The domain keeps
///   what was written.
class UserDefaultsConfigStore final : public ConfigStore
{
public:
    /// Makes a store over `options.suite`; nothing is read until the first call.
    ///
    /// - Param options: the suite and the mode.
    explicit UserDefaultsConfigStore(const UserDefaultsStoreOptions& options);
    /// Stops the watch thread (waiting for a listener call in progress) and releases the store.
    ~UserDefaultsConfigStore() override;
    UserDefaultsConfigStore(const UserDefaultsConfigStore&) = delete;
    UserDefaultsConfigStore& operator=(const UserDefaultsConfigStore&) = delete;

    /// `kCapWritable` (unless read-only), `kCapPerKeyWrites` and `kCapNotifies`. Values are text (no
    /// `kCapTypedValues`): see the file comment.
    ///
    /// - Returns: the capability flags.
    /// - Fails: never.
    u32 Capabilities() const override;
    /// `user defaults <suite>` (or `managed preferences <suite>`).
    ///
    /// - Param out: receives the text, NUL-terminated and cut to fit.
    /// - Param capacity: the size of `out`.
    /// - Fails: never.
    void Describe(char* out, usize capacity) const override;
    /// Asks the preferences daemon for the domain's current state, so another process's writes are seen; a domain that
    /// does not exist loads as empty.
    ///
    /// - Returns: success, or `Invalid` for an empty suite.
    /// - Fails: `Invalid` for an empty suite.
    Status Load() override;
    /// Reads one value as text.
    ///
    /// - Param key: a [[settings::Key]].
    /// - Param out: receives the value's text.
    /// - Returns: success, or the error.
    /// - Fails: `NotFound`, `Invalid` for a bad key, `TypeMismatch` for a date, data, array or dictionary,
    ///   `TooLong` for text longer than a setting's.
    Status Get(StringView key, StoredValue* out) override;
    /// Writes one value at once (text only: a typed value is `Invalid`).
    ///
    /// - Param key: a [[settings::Key]].
    /// - Param value: the value's canonical text.
    /// - Returns: success, or the error.
    /// - Fails: `ReadOnly`, `Invalid` (a bad key or a typed value), `TooLong`.
    Status Set(StringView key, const StoredValue& value) override;
    /// Removes one value at once.
    ///
    /// - Param key: a [[settings::Key]].
    /// - Returns: success, or the error.
    /// - Fails: `ReadOnly`, `Invalid`, `NotFound` when there is no value.
    Status Remove(StringView key) override;
    /// Lists the domain's keys under the prefix; names that are not valid keys (written by hand) are skipped.
    ///
    /// - Param prefix: a key, or empty for all.
    /// - Param out: the keys are appended.
    /// - Returns: success, or `Invalid` for a bad prefix.
    /// - Fails: `Invalid` for a bad prefix.
    Status Enumerate(StringView prefix, Vector<Key>* out) override;
    /// Asks the preferences daemon to write the domain's file now (every write was already visible to every process).
    ///
    /// - Param options: unused (a defaults domain is never damaged as a whole).
    /// - Returns: success, or the error.
    /// - Fails: `ReadOnly` for a read-only store, `Io` when the daemon reports failure.
    Status Commit(const CommitOptions& options) override;
    /// The value's place: `defaults <suite> <key>`.
    ///
    /// - Param key: the key.
    /// - Param out: receives the text, NUL-terminated and cut to fit.
    /// - Param capacity: the size of `out`.
    /// - Fails: never.
    void Origin(StringView key, char* out, usize capacity) const override;
    /// Whether the domain holds any value.
    ///
    /// - Returns: true if it does.
    /// - Fails: never.
    bool IsPresent() const override;
    /// Starts (or, with null, stops) the watch thread; the listener is called on it for another writer's change, never
    /// for this store's own.
    ///
    /// - Param listener: the function, or null to stop.
    /// - Param user: passed to the listener.
    /// - Returns: success, or the error.
    /// - Fails: `Invalid` when called from the listener itself (stopping the thread would wait for the call in
    ///   progress). A listener must not destroy its store either.
    Status SetChangeListener(ChangeListener listener, void* user) override;

private:
    struct Entry
    {
        Key key;
        String<HeapAllocator> text = String<HeapAllocator>(HeapAllocator::Default());
    };

    // Every valid key of the domain with its text (sorted by key); false if the domain cannot be read.
    bool ReadAll(Vector<Entry>* out) const;
    // Every valid key of the domain, without its value (a managed one only if enforced); false if it cannot be read.
    bool ReadKeys(Vector<Key>* out) const;
    // The domain's names as they are (valid keys or not): its key list, or (forced-only) the managed files' keys.
    void Names(Vector<String<HeapAllocator>>* out) const;
    // Forced-only: whether `name` is one of the managed files' keys, read once per Load (and at every poll).
    bool IsManagedName(StringView name) const;
    // Reads one key's text; the store error otherwise.
    StoreError ReadText(const Key& key, char* text, usize capacity, usize* length) const;
    // Records this store's own change in `known_`, so the watch thread does not report it (call with mutex_ held).
    void RememberOwn(const Key& key, const StringView* text);
    void CheckForChange();
    void WatchLoop();
    void StopWatching();

    String<HeapAllocator> suite_ = String<HeapAllocator>(HeapAllocator::Default());
    bool read_only_;
    bool forced_only_;
    Mutex mutex_;          // a write and the watch thread's comparison never interleave
    Vector<Entry> known_;  // the domain as this store last saw it (its own writes included)
    ChangeListener listener_ = nullptr;
    void* listener_user_ = nullptr;
    Thread watcher_;
    Mutex stop_mutex_;
    ConditionVariable stop_signal_;
    bool stop_requested_ = false;  // under stop_mutex_
    Atomic<bool> stopping_{false};
    // Forced-only: the managed files' keys, read when first needed after a Load (the watch thread reads them again
    // at every poll); under managed_mutex_, which the watch thread takes inside mutex_.
    mutable Mutex managed_mutex_;
    mutable Vector<String<HeapAllocator>> managed_names_;
    mutable bool managed_read_ = false;
};

}  // namespace engine_placeholder::settings
