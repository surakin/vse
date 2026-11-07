#pragma once

//! Stores that live in memory: [[settings::MemoryConfigStore]] (full-featured) and [[settings::ReadOnlyConfigStore]] (shipped defaults).

#include <mutex>

#include "containers/shared_ptr.h"
#include "settings/config_store.h"

namespace engine_placeholder::settings {

class MemoryConfigStore;

/// The durable state of memory stores. Two stores that share one backing see each other's committed changes (and are told about them),
/// which models two processes or two threads using one configuration; a store made without a backing has its own.
///
/// Thread-safety: thread-safe.
/// Ownership: shared by the stores that use it (see [[settings::MemoryBackingPtr]]).
class MemoryBacking {
public:
    /// One committed entry.
    struct Entry {
        /// The key.
        Key key;
        /// The value.
        StoredValue value;
    };

    MemoryBacking() = default;
    MemoryBacking(const MemoryBacking&) = delete;
    MemoryBacking& operator=(const MemoryBacking&) = delete;

    /// Returns the number of committed entries.
    usize Size() const;

private:
    friend class MemoryConfigStore;
    mutable std::mutex mutex_;
    Vector<Entry> entries_;
    std::recursive_mutex notify_mutex_; // held while listeners are called and while one is added or removed
    Vector<MemoryConfigStore*> stores_;
};

/// A shared pointer to a [[settings::MemoryBacking]].
using MemoryBackingPtr = SharedPtr<MemoryBacking>;

/// Makes a new empty backing.
MemoryBackingPtr MakeMemoryBacking();

/// A full-featured store in memory: typed values, staged writes with an atomic `Commit`, insertion order, change notification.
///
/// Thread-safety: thread-compatible per instance; the backing it uses is thread-safe.
/// Ownership: owns its working copy; shares the backing.
class MemoryConfigStore final : public ConfigStore {
public:
    /// Makes a store over a private empty backing.
    MemoryConfigStore();
    /// Makes a store over `backing`, which other stores may share.
    explicit MemoryConfigStore(MemoryBackingPtr backing);
    /// Destroys the store and removes it from its backing; no change listener of this store runs after this returns.
    ~MemoryConfigStore() override;

    u32 Capabilities() const override;
    void Describe(char* out, usize capacity) const override;
    Status Load() override;
    Status Get(StringView key, StoredValue* out) override;
    Status Set(StringView key, const StoredValue& value) override;
    Status Remove(StringView key) override;
    Status Enumerate(StringView prefix, Vector<Key>* out) override;
    Status Commit(const CommitOptions& options) override;
    Status SetChangeListener(ChangeListener listener, void* user) override;

private:
    // One staged change; Commit applies the log to the backing, so two stores sharing a backing merge their changes (the later one wins per key).
    struct Op {
        bool remove = false;
        MemoryBacking::Entry entry; // the key, and the value unless `remove`
    };
    usize FindWorking(const Key& key) const;
    void Stage(const Op& op);
    MemoryBackingPtr backing_;
    Vector<MemoryBacking::Entry> working_;
    Vector<Op> staged_;
    ChangeListener listener_ = nullptr;
    void* listener_user_ = nullptr;
};

/// One entry of a [[settings::ReadOnlyConfigStore]]: a key and the text of its value.
struct DefaultEntry {
    /// The key text.
    const char* key;
    /// The value text, as it would appear in a file (`1280`, `"fifo"`, `true`).
    const char* text;
};

/// A store over a fixed array of text entries: the defaults a game ships. It cannot be written; `Load` always succeeds.
///
/// Thread-safety: thread-safe (immutable after construction).
/// Ownership: borrows the array, which must outlive the store.
class ReadOnlyConfigStore final : public ConfigStore {
public:
    /// Makes a store over `entries[0..count)`.
    ///
    /// Param entries: the entries; an entry with a null key or text, or a key that is not a valid [[settings::Key]], is skipped, and the first
    ///   entry for a repeated key wins.
    ReadOnlyConfigStore(const DefaultEntry* entries, usize count);

    u32 Capabilities() const override;
    void Describe(char* out, usize capacity) const override;
    Status Load() override;
    Status Get(StringView key, StoredValue* out) override;
    Status Set(StringView key, const StoredValue& value) override;
    Status Remove(StringView key) override;
    Status Enumerate(StringView prefix, Vector<Key>* out) override;
    Status Commit(const CommitOptions& options) override;

private:
    const DefaultEntry* entries_;
    usize count_;
};

} // namespace engine_placeholder::settings
