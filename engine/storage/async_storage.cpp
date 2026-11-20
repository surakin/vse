#include "storage/async_storage.h"

#include "storage/save_plan.h"
#include "sync/thread.h"

namespace vse::storage
{

namespace
{

jobs::Job SaveJob(AsyncSave* save)
{
    save->Run();
    co_return;
}

jobs::Job LoadJob(AsyncLoad* load)
{
    load->Run();
    co_return;
}

// Waits for a job of `scheduler`. With workers they run it; a scheduler of the main thread alone has nobody else to run
// it, so the main thread does.
void WaitForCounter(jobs::Scheduler& scheduler, jobs::Counter& counter)
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

// Copies the records the load asked for into a memory store.
class Copier final : public RecordVisitor
{
public:
    Copier(MemoryStore& to, const LoadOptions& options, usize total) : to_(&to), options_(&options), total_(total) {}
    bool Visit(const EntityRecord& record) override
    {
        if (options_->cancel != nullptr && options_->cancel->IsCancelled())
        {
            status = Status::Make(ErrorCode::Cancelled, "the load was cancelled");
            return false;
        }
        if (done_ % 256 == 0 && options_->progress != nullptr)
        {
            options_->progress(done_, total_, options_->user);
        }
        ++done_;
        status = to_->Put(record);
        return status.Ok();
    }
    Status status;

private:
    MemoryStore* to_;
    const LoadOptions* options_;
    usize total_;
    usize done_ = 0;
};

Status CopyMetadata(const Store& from, MemoryStore& to, StringView name)
{
    String<HeapAllocator> value(HeapAllocator::Default());
    const Status s = from.GetMetadata(name, &value);
    if (s.Is(ErrorCode::NotFound))
    {
        return Status::Success();
    }
    ENGINE_TRY(s);
    return to.SetMetadata(name, value.View());
}

}  // namespace

// ---- AsyncSave ----

AsyncSave::AsyncSave(WorldStorage& storage, jobs::Scheduler& scheduler, SaveSession* session,
                     UniquePtr<detail::SavePlan> plan)
    : storage_(&storage), scheduler_(&scheduler), session_(session), plan_(Move(plan)), counter_(scheduler)
{
    jobs::Spawn(counter_, SaveJob(this));
}

AsyncSave::~AsyncSave()
{
    WaitForCounter(*scheduler_, counter_);
    if (!finished_)
    {
        if (result_.Ok() && session_ != nullptr && plan_->track_session)
        {
            WorldStorage::Adopt(*session_, *plan_);  // the records are committed: keep the session in step with them
        }
        storage_->EndWriter();
    }
}

void AsyncSave::Run()
{
    result_ = storage_->Write(*plan_);
}

Status AsyncSave::Wait(Report* report)
{
    scheduler_->HelpUntil(counter_);
    return Finish(report);
}

Status AsyncSave::Finish(Report* report)
{
    if (!finished_)
    {
        if (!IsDone())
        {
            return Status::Make(ErrorCode::Busy, "the writer job is still running");
        }
        if (result_.Ok() && session_ != nullptr && plan_->track_session)
        {
            WorldStorage::Adopt(*session_, *plan_);
        }
        finished_ = true;
        storage_->EndWriter();
    }
    if (report != nullptr)
    {
        *report = plan_->report;
    }
    return result_;
}

// ---- AsyncLoad ----

AsyncLoad::AsyncLoad(WorldStorage& storage, jobs::Scheduler& scheduler, const LoadOptions& options)
    : storage_(&storage), scheduler_(&scheduler), counter_(scheduler)
{
    options_.mode = options.mode;
    options_.selection.kind = options.selection.kind;
    for (usize i = 0; i < options.selection.keys.Size(); ++i)
    {
        options_.selection.keys.Push(options.selection.keys[i]);
    }
    options_.unknown = options.unknown;
    options_.missing = options.missing;
    options_.cancel = options.cancel;
    options_.progress = options.progress;
    options_.user = options.user;
    jobs::Spawn(counter_, LoadJob(this));
}

AsyncLoad::~AsyncLoad()
{
    WaitForCounter(*scheduler_, counter_);
}

void AsyncLoad::Run()
{
    Store& from = *storage_->store_;
    result_ = CopyMetadata(from, records_, metadata::kFormatVersion);
    if (result_.Ok())
    {
        result_ = CopyMetadata(from, records_, metadata::kManifest);
    }
    if (result_.Ok())
    {
        result_ = CopyMetadata(from, records_, metadata::kLastSaveLabel);
    }
    if (result_.Failed())
    {
        return;
    }
    if (options_.selection.kind == Selection::Kind::Keys)
    {
        Copier copier(records_, options_, options_.selection.keys.Size());
        for (usize i = 0; i < options_.selection.keys.Size() && result_.Ok(); ++i)
        {
            EntityRecord record;
            const Status g = from.Get(options_.selection.keys[i], &record);
            if (g.Is(ErrorCode::NotFound))
            {
                continue;
            }
            result_ = g;
            if (result_.Ok() && !copier.Visit(record))
            {
                result_ = copier.status;
            }
        }
        return;
    }
    usize total = 0;
    result_ = from.Count(&total);
    if (result_.Failed())
    {
        return;
    }
    Copier copier(records_, options_, total);
    result_ = from.Scan(KeyRange(), copier);
    if (result_.Ok())
    {
        result_ = copier.status;
    }
}

Status AsyncLoad::Wait(ecs::World& world, KeyIndex& index, Report* report)
{
    scheduler_->HelpUntil(counter_);
    return Finish(world, index, report);
}

Status AsyncLoad::Finish(ecs::World& world, KeyIndex& index, Report* report)
{
    if (!IsDone())
    {
        return Status::Make(ErrorCode::Busy, "the reading job is still running");
    }
    ENGINE_TRY(result_);
    WorldStorage from_memory(*storage_->registry_, records_);
    from_memory.SetLimits(storage_->limits_);
    return from_memory.Load(world, index, options_, report);
}

// ---- WorldStorage ----

Status WorldStorage::SaveAsync(jobs::Scheduler& scheduler, ecs::World& world, KeyIndex& index,
                               const SaveOptions& options, UniquePtr<AsyncSave>* out)
{
    if (!BeginWriter())
    {
        return Status::Make(ErrorCode::Busy, "a save is still running");
    }
    UniquePtr<detail::SavePlan> plan = MakeUnique<detail::SavePlan>();
    const Status s = Plan(world, index, options, &scheduler, plan.Get());
    if (s.Failed())
    {
        EndWriter();
        return s;
    }
    *out = MakeUnique<AsyncSave>(*this, scheduler, options.session, Move(plan));
    return Status::Success();
}

Status WorldStorage::LoadAsync(jobs::Scheduler& scheduler, const LoadOptions& options, UniquePtr<AsyncLoad>* out)
{
    if (registry_->Count() == 0)
    {
        return Status::Make(ErrorCode::Invalid, "no component is registered, so nothing can be loaded");
    }
    if (options.selection.kind == Selection::Kind::Filter)
    {
        return Status::Make(ErrorCode::Invalid, "a load cannot filter by components; select all or a set of keys");
    }
    *out = MakeUnique<AsyncLoad>(*this, scheduler, options);
    return Status::Success();
}

// ---- Autosaver ----

Autosaver::Autosaver(WorldStorage& storage, jobs::Scheduler& scheduler, ecs::World& world, KeyIndex& index,
                     f64 interval_seconds)
    : storage_(&storage), scheduler_(&scheduler), world_(&world), index_(&index), interval_(interval_seconds)
{
}

Autosaver::~Autosaver() = default;

Status Autosaver::SaveNow()
{
    if (running_.Get() != nullptr)
    {
        return Status::Success();
    }
    SaveOptions options;
    options.mode = SaveMode::Incremental;
    options.session = &session_;
    elapsed_ = 0;
    return storage_->SaveAsync(*scheduler_, *world_, *index_, options, &running_);
}

Status Autosaver::Update(f64 dt)
{
    Status collected = Status::Success();
    if (running_.Get() != nullptr && running_->IsDone())
    {
        collected = running_->Finish(nullptr);
        running_.Reset();
        if (collected.Ok())
        {
            ++completed_;
        }
    }
    elapsed_ += dt;
    if (interval_ > 0 && running_.Get() == nullptr && elapsed_ >= interval_)
    {
        const Status started = SaveNow();
        if (collected.Ok())
        {
            collected = started;
        }
    }
    return collected;
}

Status Autosaver::Flush()
{
    if (running_.Get() == nullptr)
    {
        return Status::Success();
    }
    const Status s = running_->Wait(nullptr);
    running_.Reset();
    if (s.Ok())
    {
        ++completed_;
    }
    return s;
}

}  // namespace vse::storage
