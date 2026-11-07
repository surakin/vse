#pragma once

// Test doubles for ConfigStore: wrappers that make a store fail, run slowly on another thread, or enumerate in a different order each time.
// They prove the settings system assumes nothing a registry or a server could not give. (Documented in docs/guide/settings.md,
// "Writing a store backend".)

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

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
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        cv_.notify_all();
        thread_.join();
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
    std::thread::id last_thread() const { return last_thread_; }

private:
    template <typename F>
    Status Run(F&& f) {
        struct Job {
            F* fn;
            Status result;
        } job{&f, Status()};
        std::unique_lock<std::mutex> lock(mutex_);
        task_ = [](void* p) {
            Job* j = static_cast<Job*>(p);
            j->result = (*j->fn)();
        };
        arg_ = &job;
        done_ = false;
        cv_.notify_all();
        cv_.wait(lock, [&] { return done_; });
        return job.result;
    }
    void Loop() {
        std::unique_lock<std::mutex> lock(mutex_);
        for (;;) {
            cv_.wait(lock, [&] { return stopping_ || (task_ != nullptr && !done_); });
            if (stopping_) return;
            void (*task)(void*) = task_;
            void* arg = arg_;
            lock.unlock();
            std::this_thread::sleep_for(std::chrono::microseconds(delay_us_));
            last_thread_ = std::this_thread::get_id();
            task(arg);
            lock.lock();
            task_ = nullptr;
            done_ = true;
            cv_.notify_all();
        }
    }
    ConfigStorePtr owned_;
    ConfigStore* inner_;
    int delay_us_;
    std::mutex mutex_;
    std::condition_variable cv_;
    void (*task_)(void*) = nullptr;
    void* arg_ = nullptr;
    bool done_ = true;
    bool stopping_ = false;
    std::thread::id last_thread_;
    std::thread thread_;
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

} // namespace settings_test
