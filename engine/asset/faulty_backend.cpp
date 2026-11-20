#include "asset/faulty_backend.h"

#include "sync/thread.h"

namespace vse::asset
{
namespace
{

class FaultyReader : public ResourceReader
{
public:
    FaultyReader(FaultyBackend* owner, UniquePtr<ResourceReader> inner) : owner_(owner), inner_(Move(inner)) {}

    u64 Size() const override { return inner_->Size(); }

    Status ReadAt(u64 offset, void* buffer, usize length) override
    {
        ENGINE_TRY(owner_->Apply(FaultOp::Read));
        const usize chunk = owner_->ReadChunk();
        if (chunk == 0 || length <= chunk)
        {
            return inner_->ReadAt(offset, buffer, length);
        }
        u8* out = static_cast<u8*>(buffer);
        usize done = 0;
        while (done < length)
        {
            const usize n = length - done < chunk ? length - done : chunk;
            ENGINE_TRY(inner_->ReadAt(offset + done, out + done, n));
            done += n;
        }
        return Status::Success();
    }

private:
    FaultyBackend* owner_;
    UniquePtr<ResourceReader> inner_;
};

}  // namespace

FaultyBackend::FaultyBackend(ResourceBackend& inner) : inner_(&inner) {}

void FaultyBackend::FailNext(FaultOp op, Status status, u32 times, u32 skip)
{
    LockGuard<Mutex> lock(mutex_);
    Plan& plan = plans_[static_cast<usize>(op)];
    plan.status = status;
    plan.times = times;
    plan.skip = skip;
}

void FaultyBackend::SetLatencyMs(FaultOp op, u64 milliseconds)
{
    LockGuard<Mutex> lock(mutex_);
    plans_[static_cast<usize>(op)].latency_ms = milliseconds;
}

void FaultyBackend::SetReadChunk(usize bytes)
{
    LockGuard<Mutex> lock(mutex_);
    chunk_ = bytes;
}

usize FaultyBackend::ReadChunk() const
{
    LockGuard<Mutex> lock(mutex_);
    return chunk_;
}

u64 FaultyBackend::CallCount(FaultOp op) const
{
    LockGuard<Mutex> lock(mutex_);
    return plans_[static_cast<usize>(op)].calls;
}

Status FaultyBackend::Apply(FaultOp op)
{
    u64 latency = 0;
    Status injected;
    {
        LockGuard<Mutex> lock(mutex_);
        Plan& plan = plans_[static_cast<usize>(op)];
        ++plan.calls;
        latency = plan.latency_ms;
        if (plan.skip > 0)
        {
            --plan.skip;
        }
        else if (plan.times > 0)
        {
            --plan.times;
            injected = plan.status;
        }
    }
    if (latency > 0)
    {
        ThisThread::SleepForMilliseconds(latency);
    }
    return injected;
}

Status FaultyBackend::Stat(ResourceId id, ResourceInfo* out)
{
    ENGINE_TRY(Apply(FaultOp::Stat));
    return inner_->Stat(id, out);
}

Status FaultyBackend::Open(ResourceId id, UniquePtr<ResourceReader>* out)
{
    ENGINE_TRY(Apply(FaultOp::Open));
    UniquePtr<ResourceReader> reader;
    ENGINE_TRY(inner_->Open(id, &reader));
    UniquePtr<FaultyReader> wrapped = MakeUnique<FaultyReader>(this, Move(reader));
    if (!wrapped)
    {
        return Status::Make(ErrorCode::Other, "memory for the reader ran out");
    }
    *out = Move(wrapped);
    return Status::Success();
}

Status FaultyBackend::ReadTable(ResourceTable* out)
{
    ENGINE_TRY(Apply(FaultOp::ReadTable));
    return inner_->ReadTable(out);
}

Status FaultyBackend::Watch(Vector<ChangeRecord>* out)
{
    ENGINE_TRY(Apply(FaultOp::Watch));
    return inner_->Watch(out);
}

}  // namespace vse::asset
