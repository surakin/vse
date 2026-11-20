#pragma once

//! [[asset::FaultyBackend]]: a wrapper around any [[asset::ResourceBackend]] that injects failures, chunked reads and
//! latency, for the tests of every later day (the file layer has a network-like double for the same job).

#include "asset/backend.h"
#include "containers/array.h"
#include "sync/mutex.h"

namespace vse::asset
{

/// The operations a fault can hit.
enum class FaultOp : u8
{
    Stat,       ///< `Stat`.
    Open,       ///< `Open`.
    Read,       ///< `ReadAt` of a reader this wrapper opened.
    ReadTable,  ///< `ReadTable` (and so a default `ReadTableAsync`).
    Watch,      ///< `Watch`.
};

/// Wraps `inner`; the wrapper's own members are never faulty, only the calls it forwards.
///
/// - Thread-safety: thread-safe.
/// - Ownership: borrows `inner`, which must outlive the wrapper and every reader and ticket made through it.
class FaultyBackend : public ResourceBackend
{
public:
    /// Wraps `inner`, with no fault planned.
    explicit FaultyBackend(ResourceBackend& inner);

    /// Makes the next `times` calls of `op` fail with `status` (after `skip` calls that succeed).
    void FailNext(FaultOp op, Status status, u32 times = 1, u32 skip = 0);

    /// Delays every call of `op` by `milliseconds` before it is forwarded.
    void SetLatencyMs(FaultOp op, u64 milliseconds);

    /// Makes readers read at most `bytes` per call to the inner reader (0: no limit), so callers that assume one call
    /// reads everything are caught. The result of `ReadAt` is still the whole range.
    void SetReadChunk(usize bytes);

    /// Calls forwarded to `op` so far (failed ones included).
    u64 CallCount(FaultOp op) const;

    const char* Name() const override { return inner_->Name(); }
    u32 Capabilities() const override { return inner_->Capabilities() & ~u32(kBackendAsyncTable); }
    Status Stat(ResourceId id, ResourceInfo* out) override;
    Status Open(ResourceId id, UniquePtr<ResourceReader>* out) override;
    Status ReadTable(ResourceTable* out) override;
    Status Watch(Vector<ChangeRecord>* out) override;

    /// Applies the plan of `op`: sleeps for its latency and returns the injected failure, if one is due. Public for the
    /// readers this wrapper hands out.
    Status Apply(FaultOp op);
    /// The configured read chunk.
    usize ReadChunk() const;

private:
    static constexpr usize kOps = 5;
    struct Plan
    {
        Status status;
        u32 times = 0;
        u32 skip = 0;
        u64 latency_ms = 0;
        u64 calls = 0;
    };

    ResourceBackend* inner_;
    mutable Mutex mutex_;
    Array<Plan, kOps> plans_;
    usize chunk_ = 0;
};

}  // namespace vse::asset
