#pragma once

// Test doubles for ConfigStore: wrappers that make a store fail, run slowly on another thread, or enumerate in a different order each time.
// They prove the settings system assumes nothing a registry or a server could not give. (Documented in docs/guide/settings.md,
// "Writing a store backend".)

#include <chrono>
#include <cstdio>
#include "sync/mutex.h"
#include "sync/thread.h"

#include "settings/config_store.h"
#include "test_text.h"

namespace settings_test {

using namespace engine_placeholder;
using namespace engine_placeholder::settings;

/// Fails the Nth call (counting Load, Get, Set, Remove, Enumerate and Commit; 1-based) with a chosen error, without touching the inner store
/// (so a failed Commit leaves the durable state as it was), then behaves normally unless `sticky`.
class FailingConfigStore final : public ConfigStore {
public:
    FailingConfigStore(ConfigStorePtr inner, u32 fail_at, StoreError error, bool sticky = false)
        : owned_(static_cast<ConfigStorePtr&&>(inner)), inner_(owned_.Get()), fail_at_(fail_at), error_(error), sticky_(sticky) {}

    /// From now on every call fails.
    void FailFromNextCall() {
        fail_at_ = calls_ + 1;
        sticky_ = true;
    }
    u32 calls() const { return calls_; }
    u32 failures() const { return failures_; }

    u32 Capabilities() const override { return inner_->Capabilities(); }
    void Describe(char* out, usize capacity) const override { inner_->Describe(out, capacity); }
    Status Load() override { return Hit() ? Fail() : inner_->Load(); }
    Status Get(StringView key, StoredValue* out) override { return Hit() ? Fail() : inner_->Get(key, out); }
    Status Set(StringView key, const StoredValue& value) override { return Hit() ? Fail() : inner_->Set(key, value); }
    Status Remove(StringView key) override { return Hit() ? Fail() : inner_->Remove(key); }
    Status Enumerate(StringView prefix, Vector<Key>* out) override { return Hit() ? Fail() : inner_->Enumerate(prefix, out); }
    Status Commit(const CommitOptions& options) override { return Hit() ? Fail() : inner_->Commit(options); }
    Status SetChangeListener(ChangeListener listener, void* user) override { return inner_->SetChangeListener(listener, user); }

private:
    bool Hit() {
        ++calls_;
        return calls_ == fail_at_ || (sticky_ && calls_ > fail_at_);
    }
    Status Fail() {
        ++failures_;
        return Status::Of(error_);
    }
    ConfigStorePtr owned_;
    ConfigStore* inner_;
    u32 fail_at_;
    StoreError error_;
    bool sticky_;
    u32 calls_ = 0;
    u32 failures_ = 0;
};

/// Runs every call on the store's own thread after a delay and blocks the caller until it completes, so nothing may assume the answer
/// comes from the calling thread or at once.
class SlowConfigStore final : public ConfigStore {
public:
    SlowConfigStore(ConfigStorePtr inner, int delay_us)
        : owned_(static_cast<ConfigStorePtr&&>(inner)), inner_(owned_.Get()), delay_us_(delay_us), thread_([this] { Loop(); }) {}
    ~SlowConfigStore() override {
        {
            LockGuard<Mutex> lock(mutex_);
            stopping_ = true;
        }
        cv_.NotifyAll();
        thread_.Join();
    }

    u32 Capabilities() const override { return inner_->Capabilities(); }
    void Describe(char* out, usize capacity) const override { inner_->Describe(out, capacity); }
    Status Load() override { return Run([&] { return inner_->Load(); }); }
    Status Get(StringView key, StoredValue* out) override { return Run([&] { return inner_->Get(key, out); }); }
    Status Set(StringView key, const StoredValue& value) override { return Run([&] { return inner_->Set(key, value); }); }
    Status Remove(StringView key) override { return Run([&] { return inner_->Remove(key); }); }
    Status Enumerate(StringView prefix, Vector<Key>* out) override { return Run([&] { return inner_->Enumerate(prefix, out); }); }
    Status Commit(const CommitOptions& options) override { return Run([&] { return inner_->Commit(options); }); }
    Status SetChangeListener(ChangeListener listener, void* user) override { return Run([&] { return inner_->SetChangeListener(listener, user); }); }

    /// The id of the thread that ran the last call (differs from the caller's).
    ThreadId last_thread() const { return last_thread_; }

private:
    template <typename F>
    Status Run(F&& f) {
        struct Job {
            F* fn;
            Status result;
        } job{&f, Status()};
        UniqueLock<Mutex> lock(mutex_);
        task_ = [](void* p) {
            Job* j = static_cast<Job*>(p);
            j->result = (*j->fn)();
        };
        arg_ = &job;
        done_ = false;
        cv_.NotifyAll();
        cv_.Wait(lock, [&] { return done_; });
        return job.result;
    }
    void Loop() {
        UniqueLock<Mutex> lock(mutex_);
        for (;;) {
            cv_.Wait(lock, [&] { return stopping_ || (task_ != nullptr && !done_); });
            if (stopping_) return;
            void (*task)(void*) = task_;
            void* arg = arg_;
            lock.Unlock();
            ThisThread::SleepForMicroseconds(delay_us_);
            last_thread_ = ThisThread::Id();
            task(arg);
            lock.Lock();
            task_ = nullptr;
            done_ = true;
            cv_.NotifyAll();
        }
    }
    ConfigStorePtr owned_;
    ConfigStore* inner_;
    int delay_us_;
    Mutex mutex_;
    ConditionVariable cv_;
    void (*task_)(void*) = nullptr;
    void* arg_ = nullptr;
    bool done_ = true;
    bool stopping_ = false;
    ThreadId last_thread_;
    Thread thread_;
};

/// Enumerates in a different order on every call (rotated, and reversed on odd calls), and does not claim to preserve order: models a registry.
class NoOrderConfigStore final : public ConfigStore {
public:
    explicit NoOrderConfigStore(ConfigStorePtr inner) : owned_(static_cast<ConfigStorePtr&&>(inner)), inner_(owned_.Get()) {}

    u32 Capabilities() const override { return inner_->Capabilities() & ~static_cast<u32>(kCapPreservesOrder); }
    void Describe(char* out, usize capacity) const override { inner_->Describe(out, capacity); }
    Status Load() override { return inner_->Load(); }
    Status Get(StringView key, StoredValue* out) override { return inner_->Get(key, out); }
    Status Set(StringView key, const StoredValue& value) override { return inner_->Set(key, value); }
    Status Remove(StringView key) override { return inner_->Remove(key); }
    Status Commit(const CommitOptions& options) override { return inner_->Commit(options); }
    Status SetChangeListener(ChangeListener listener, void* user) override { return inner_->SetChangeListener(listener, user); }
    Status Enumerate(StringView prefix, Vector<Key>* out) override {
        Vector<Key> keys;
        const Status s = inner_->Enumerate(prefix, &keys);
        if (s.Failed()) return s;
        ++calls_;
        const usize n = keys.Size();
        for (usize i = 0; i < n; ++i) {
            const usize j = (i + calls_) % n;
            out->Push(keys[(calls_ & 1u) ? n - 1 - j : j]);
        }
        return s;
    }

private:
    ConfigStorePtr owned_;
    ConfigStore* inner_;
    u32 calls_ = 0;
};

/// A store without an atomic commit, for testing the `Status::done`/`total` contract: it stages changes itself and, on `Commit`, makes them durable
/// through the inner store one at a time (a Set or Remove followed by the inner Commit), failing with `Io` after `land` changes. The changes that did
/// not land stay staged. Reads go straight to the inner store and do not see staged changes, so this double is for commit tests, not for the
/// conformance suite.
class PartialCommitConfigStore final : public ConfigStore {
public:
    PartialCommitConfigStore(ConfigStorePtr inner, u32 land) : owned_(static_cast<ConfigStorePtr&&>(inner)), inner_(owned_.Get()), land_(land) {}

    u32 Capabilities() const override { return inner_->Capabilities() & ~static_cast<u32>(kCapAtomicCommit); }
    void Describe(char* out, usize capacity) const override { inner_->Describe(out, capacity); }
    Status Load() override {
        staged_.Clear();
        return inner_->Load();
    }
    Status Get(StringView key, StoredValue* out) override { return inner_->Get(key, out); }
    Status Enumerate(StringView prefix, Vector<Key>* out) override { return inner_->Enumerate(prefix, out); }
    Status Set(StringView key, const StoredValue& value) override {
        Change c;
        c.key = engine_test::MakeText(key);
        c.value = value;
        staged_.Push(static_cast<Change&&>(c));
        return Status();
    }
    Status Remove(StringView key) override {
        Change c;
        c.key = engine_test::MakeText(key);
        c.remove = true;
        staged_.Push(static_cast<Change&&>(c));
        return Status();
    }
    Status Commit(const CommitOptions& options) override {
        Status result;
        result.total = static_cast<u32>(staged_.Size());
        while (!staged_.Empty()) {
            if (result.done == land_) {
                result.error = StoreError::Io;
                return result;
            }
            const Change& c = staged_[0];
            const Status applied = c.remove ? inner_->Remove(c.key.View()) : inner_->Set(c.key.View(), c.value);
            if (applied.Failed() && !applied.Is(StoreError::NotFound)) {
                result.error = applied.error;
                return result;
            }
            const Status durable = inner_->Commit(options);
            if (durable.Failed()) {
                result.error = durable.error;
                return result;
            }
            staged_.Remove(0);
            ++result.done;
        }
        return result;
    }

private:
    struct Change {
        engine_test::Text key = engine_test::MakeText();
        bool remove = false;
        StoredValue value;
    };
    ConfigStorePtr owned_;
    ConfigStore* inner_;
    u32 land_;
    Vector<Change> staged_;
};

/// The model of a registry-shaped store (the Windows registry, `NSUserDefaults`): typed values, every write durable at once (`kCapPerKeyWrites`),
/// no order, and change notifications delivered from another thread. Stores that share a `RegistryHive` see each other's writes. The Windows port
/// starts from this class, so it passes the whole conformance suite.
class RegistryHive {
public:
    RegistryHive() : worker_([this] { Loop(); }) {}
    ~RegistryHive() {
        {
            LockGuard<Mutex> lock(mutex_);
            stopping_ = true;
        }
        cv_.NotifyAll();
        worker_.Join();
    }
    RegistryHive(const RegistryHive&) = delete;
    RegistryHive& operator=(const RegistryHive&) = delete;

private:
    friend class RegistryShapedStore;
    struct Entry {
        Key key;
        StoredValue value;
    };
    struct Watcher {
        const void* owner;
        ChangeListener listener;
        void* user;
    };
    void Notify(const void* writer) {
        LockGuard<Mutex> lock(mutex_);
        pending_writers_.Push(writer);
        cv_.NotifyAll();
    }
    void Loop() {
        UniqueLock<Mutex> lock(mutex_);
        for (;;) {
            cv_.Wait(lock, [&] { return stopping_ || !pending_writers_.Empty(); });
            if (pending_writers_.Empty()) return;
            const void* writer = pending_writers_[0];
            pending_writers_.Remove(0);
            // Listeners run on this thread, never on the writer's, and never for the writer's own change. The lock is held so that a store that is
            // being destroyed (it removes its watcher under the same lock) is never called afterwards.
            for (usize i = 0; i < watchers_.Size(); ++i) {
                if (watchers_[i].owner != writer && watchers_[i].listener != nullptr) watchers_[i].listener(watchers_[i].user);
            }
        }
    }
    Mutex mutex_;
    ConditionVariable cv_;
    Vector<Entry> entries_;
    Vector<Watcher> watchers_;
    Vector<const void*> pending_writers_;
    bool stopping_ = false;
    Thread worker_;
};

class RegistryShapedStore final : public ConfigStore {
public:
    explicit RegistryShapedStore(RegistryHive* hive) : hive_(hive) {}
    ~RegistryShapedStore() override { SetChangeListener(nullptr, nullptr); }

    u32 Capabilities() const override { return kCapWritable | kCapPerKeyWrites | kCapTypedValues | kCapNotifies; }
    void Describe(char* out, usize capacity) const override {
        if (capacity != 0) std::snprintf(out, capacity, "registry-shaped model");
    }
    Status Load() override { return Status(); }
    Status Get(StringView key, StoredValue* out) override {
        Key k;
        if (Key::Parse(key, &k) != KeyError::None) return Status::Of(StoreError::Invalid);
        LockGuard<Mutex> lock(hive_->mutex_);
        for (usize i = 0; i < hive_->entries_.Size(); ++i) {
            if (hive_->entries_[i].key == k) {
                *out = hive_->entries_[i].value;
                return Status();
            }
        }
        return Status::Of(StoreError::NotFound);
    }
    Status Set(StringView key, const StoredValue& value) override {
        Key k;
        if (Key::Parse(key, &k) != KeyError::None) return Status::Of(StoreError::Invalid);
        {
            LockGuard<Mutex> lock(hive_->mutex_);
            usize at = 0;
            while (at < hive_->entries_.Size() && !(hive_->entries_[at].key == k)) ++at;
            if (at == hive_->entries_.Size()) {
                RegistryHive::Entry e;
                e.key = k;
                e.value = value;
                hive_->entries_.Push(e);
            } else {
                hive_->entries_[at].value = value;
            }
        }
        hive_->Notify(this);
        return Status();
    }
    Status Remove(StringView key) override {
        Key k;
        if (Key::Parse(key, &k) != KeyError::None) return Status::Of(StoreError::Invalid);
        {
            LockGuard<Mutex> lock(hive_->mutex_);
            usize at = 0;
            while (at < hive_->entries_.Size() && !(hive_->entries_[at].key == k)) ++at;
            if (at == hive_->entries_.Size()) return Status::Of(StoreError::NotFound);
            hive_->entries_.Remove(at);
        }
        hive_->Notify(this);
        return Status();
    }
    Status Enumerate(StringView prefix, Vector<Key>* out) override {
        if (prefix.Length() != 0 && Key::Parse(prefix, nullptr) != KeyError::None) return Status::Of(StoreError::Invalid);
        LockGuard<Mutex> lock(hive_->mutex_);
        for (usize i = hive_->entries_.Size(); i-- > 0;) { // no order: backwards, which is not the order they were written in
            const StringView k = hive_->entries_[i].key.View();
            if (prefix.Length() == 0 || k == prefix || (k.Length() > prefix.Length() && k.StartsWith(prefix) && k[prefix.Length()] == '.')) out->Push(hive_->entries_[i].key);
        }
        return Status();
    }
    Status Commit(const CommitOptions&) override { return Status(); } // every write was durable when it returned
    Status SetChangeListener(ChangeListener listener, void* user) override {
        LockGuard<Mutex> lock(hive_->mutex_);
        for (usize i = 0; i < hive_->watchers_.Size(); ++i) {
            if (hive_->watchers_[i].owner == this) {
                if (listener == nullptr) hive_->watchers_.Remove(i);
                else {
                    hive_->watchers_[i].listener = listener;
                    hive_->watchers_[i].user = user;
                }
                return Status();
            }
        }
        if (listener != nullptr) hive_->watchers_.Push(RegistryHive::Watcher{this, listener, user});
        return Status();
    }

private:
    RegistryHive* hive_;
};

} // namespace settings_test
