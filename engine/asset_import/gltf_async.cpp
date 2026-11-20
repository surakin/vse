#include "asset_import/gltf_async.h"

#include "containers/unique_ptr.h"
#include "jobs/job.h"
#include "sync/atomic.h"
#include "sync/thread.h"

namespace vse::asset
{

struct ImportTicketState
{
    explicit ImportTicketState(jobs::Scheduler& s) : scheduler(&s), outstanding(s) {}
    jobs::Scheduler* scheduler;
    jobs::Counter outstanding;
    Vector<byte> file;
    GltfSource* source = nullptr;
    ImportOptions options;
    ImportResult result;
    GltfError error;
    Status status;
    Atomic<bool> cancelled{false};
    Atomic<u32> steps{0};
    Atomic<bool> done{false};
    bool finished = false;
};

namespace
{

jobs::Job RunImport(ImportTicketState* state)
{
    ImportOptions options = state->options;
    options.cancel = &state->cancelled;
    options.steps_done = &state->steps;
    state->status = ImportGltf(ByteSpan(state->file.Data(), state->file.Size()), *state->source, options,
                               &state->result, &state->error);
    state->done.Store(true, MemoryOrder::Release);
    co_return;
}

void WaitFor(jobs::Scheduler& scheduler, jobs::Counter& counter)
{
    if (counter.IsZero())
    {
        return;
    }
    if (scheduler.ParticipantCount() == 1)
    {
        scheduler.HelpUntil(counter);
        return;
    }
    while (!counter.IsZero())
    {
        ThisThread::Yield();
    }
}

}  // namespace

ImportTicket::ImportTicket(jobs::Scheduler& scheduler) : state_(MakeUnique<ImportTicketState>(scheduler)) {}

ImportTicket::~ImportTicket()
{
    state_->cancelled.Store(true, MemoryOrder::Release);
    WaitFor(*state_->scheduler, state_->outstanding);
}

bool ImportTicket::IsDone() const
{
    return state_->done.Load(MemoryOrder::Acquire);
}

f32 ImportTicket::Progress() const
{
    const u32 steps = state_->steps.Load(MemoryOrder::Acquire);
    return steps >= kImportSteps ? 1.0f : static_cast<f32>(steps) / static_cast<f32>(kImportSteps);
}

void ImportTicket::Cancel()
{
    state_->cancelled.Store(true, MemoryOrder::Release);
}

Status ImportTicket::Finish(ImportResult* result, GltfError* error)
{
    WaitFor(*state_->scheduler, state_->outstanding);
    if (state_->finished)
    {
        return Status::Make(ErrorCode::NotFound, "the result was taken already");
    }
    state_->finished = true;
    *error = state_->error;
    if (state_->status.Ok())
    {
        *result = static_cast<ImportResult&&>(state_->result);
    }
    return state_->status;
}

UniquePtr<ImportTicket> ImportGltfAsync(jobs::Scheduler& scheduler, Vector<byte> file, GltfSource& source,
                                        const ImportOptions& options)
{
    UniquePtr<ImportTicket> ticket = MakeUnique<ImportTicket>(scheduler);
    ImportTicketState* state = ticket->state_.Get();
    state->file = static_cast<Vector<byte>&&>(file);
    state->source = &source;
    state->options = options;
    jobs::Spawn(state->outstanding, RunImport(state));
    return ticket;
}

}  // namespace vse::asset
