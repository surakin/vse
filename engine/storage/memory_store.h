#pragma once

//! [[storage::MemoryStore]]: the reference record store (Phase 19 Day 259), correct and simple rather than fast. Every other store is compared with it.

#include "storage/store.h"
#include "sync/mutex.h"
#include "sync/thread.h"

namespace engine_placeholder::storage {

/// A record store in memory: a vector of records sorted by key. A batch works on a copy of the records and replaces the committed set at `Commit`, so a
/// reader on another thread sees the old state until then.
///
/// For tests it can inject a failure: [[storage::MemoryStore::FailAfter]] makes the nth following mutation (or commit) return `Io`.
///
/// Thread-safety: thread-safe as [[storage::Store]] describes; one internal lock.
/// Ownership: owns the records.
class MemoryStore final : public Store {
public:
    /// Creates an empty store.
    MemoryStore();

    /// Returns `"memory"`.
    /// Ownership: the string is static.
    /// Fails: never.
    const char* Name() const override;
    Status Put(const EntityRecord& record) override;
    Status Get(EntityKey key, EntityRecord* out) const override;
    Status Remove(EntityKey key) override;
    Status Scan(const KeyRange& range, RecordVisitor& visitor) const override;
    Status Count(usize* out) const override;
    Status Begin() override;
    Status Commit() override;
    Status Rollback() override;
    Status GetMetadata(StringView name, String<HeapAllocator>* out) const override;
    Status SetMetadata(StringView name, StringView value) override;

    /// Makes the `n`th next mutation or commit fail with `Io` (1 is the next); that one only. 0 clears a pending injection.
    ///
    /// Param n: the operation number.
    /// Fails: never.
    void FailAfter(u32 n);
    /// Damages the stored bytes of `key` (flips one bit, keeping the stored hash) so that reading it is `Corrupt`; for tests.
    ///
    /// Returns: false when there is no such record.
    /// Fails: returns false as described.
    bool CorruptRecord(EntityKey key);

private:
    struct Meta {
        String<HeapAllocator> name;
        String<HeapAllocator> value;
    };
    struct State {
        Vector<EntityRecord> records;
        Vector<Meta> meta;
        State Clone() const;
    };
    // `view_` is the state the calling thread sees: the open batch's for its owner, the committed one for everyone else.
    const State& ViewFor() const;
    Status Writable(State** out);
    bool ShouldFail();
    static usize LowerBound(const State& s, EntityKey key);

    mutable Mutex lock_;
    State committed_;
    State working_;
    bool in_batch_ = false;
    ThreadId owner_;
    u32 fail_in_ = 0;
};

} // namespace engine_placeholder::storage
