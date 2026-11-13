#include "asset/backend.h"

#include "sync/mutex.h"

namespace engine_placeholder::asset
{

TableTicket::TableTicket() : table_(MakeUnique<ResourceTable>()) {}

TableTicket::~TableTicket()
{
    Cancel();
    UniqueLock<Mutex> lock(mutex_);
    ended_.Wait(lock, [this] { return done_ && !callback_running_; });
}

bool TableTicket::SetOnComplete(void (*function)(void* user), void* user)
{
    LockGuard<Mutex> lock(mutex_);
    if (done_)
    {
        return false;
    }
    on_complete_ = function;
    on_complete_user_ = user;
    return true;
}

void TableTicket::RunJob(io::IoWork* work)
{
    Job* job = reinterpret_cast<Job*>(work);
    TableTicket* ticket = job->ticket;
    ResourceTable table;
    Status status = job->backend->ReadTable(&table);
    if (status.Ok() && ticket->IsCancelled())
    {
        status = Status::Make(ErrorCode::Cancelled, "the table read was cancelled");
    }
    if (status.Ok())
    {
        status = ticket->Publish(table);
    }
    ticket->Complete(status);
}

UniquePtr<TableTicket> TableTicket::RunBlocking(ResourceBackend& backend)
{
    UniquePtr<TableTicket> ticket = MakeUnique<TableTicket>();
    if (!ticket)
    {
        return ticket;
    }
    ticket->job_.work.run = &TableTicket::RunJob;
    ticket->job_.ticket = ticket.Get();
    ticket->job_.backend = &backend;
    if (!io::DefaultExecutor().Submit(&ticket->job_.work))
    {
        RunJob(&ticket->job_.work);  // the executor is shut down: read here rather than never
    }
    return ticket;
}

bool TableTicket::IsDone() const
{
    LockGuard<Mutex> lock(mutex_);
    return done_;
}

void TableTicket::Wait()
{
    UniqueLock<Mutex> lock(mutex_);
    ended_.Wait(lock, [this] { return done_; });
}

void TableTicket::Cancel()
{
    cancelled_.Store(true, MemoryOrder::Release);
}

Status TableTicket::Finish(ResourceTable* out)
{
    Wait();
    LockGuard<Mutex> lock(mutex_);
    if (status_.Failed())
    {
        return status_;
    }
    for (usize i = 0; i < table_->Size(); ++i)
    {
        const TableEntryView e = table_->At(i);
        ENGINE_TRY(out->Add(e.path, e.id, e.type));
    }
    return Status::Success();
}

usize TableTicket::TakePage(ResourceTable* out, bool* last)
{
    LockGuard<Mutex> lock(mutex_);
    usize added = 0;
    while (taken_ < table_->Size())
    {
        const TableEntryView e = table_->At(taken_);
        if (out->Add(e.path, e.id, e.type).Failed())
        {
            break;  // the caller's table refuses it; the entry stays for the next call
        }
        ++taken_;
        ++added;
    }
    *last = done_ && status_.Ok() && taken_ == table_->Size();
    return added;
}

Status TableTicket::Publish(const ResourceTable& page)
{
    if (IsCancelled())
    {
        return Status::Make(ErrorCode::Cancelled, "the table read was cancelled");
    }
    LockGuard<Mutex> lock(mutex_);
    for (usize i = 0; i < page.Size(); ++i)
    {
        const TableEntryView e = page.At(i);
        ENGINE_TRY(table_->Add(e.path, e.id, e.type));
    }
    return Status::Success();
}

void TableTicket::Complete(Status status)
{
    void (*callback)(void*) = nullptr;
    void* callback_user = nullptr;
    {
        LockGuard<Mutex> lock(mutex_);
        CompleteLocked(status);
        callback = on_complete_;
        callback_user = on_complete_user_;
        on_complete_ = nullptr;
        callback_running_ = callback != nullptr;
    }
    if (callback != nullptr)
    {
        callback(callback_user);
        LockGuard<Mutex> lock(mutex_);
        callback_running_ = false;
        ended_.NotifyAll();
    }
}

void TableTicket::CompleteLocked(Status status)
{
    if (status.Ok() && IsCancelled())
    {
        status = Status::Make(ErrorCode::Cancelled, "the table read was cancelled");
    }
    status_ = status;
    if (status.Failed())
    {
        table_ = MakeUnique<ResourceTable>();  // a failed or cancelled read leaves no table
        taken_ = 0;
    }
    done_ = true;
    ended_.NotifyAll();
}

Status ListFromTable(ResourceBackend& backend, StringView prefix, usize max, Vector<ResourceId>* out)
{
    out->Clear();
    ResourceTable table;
    ENGINE_TRY(backend.ReadTable(&table));
    Vector<TableEntryView> entries;
    table.ListPrefix(prefix, &entries);
    for (usize i = 0; i < entries.Size() && i < max; ++i)
    {
        out->Push(entries[i].id);
    }
    return Status::Success();
}

}  // namespace engine_placeholder::asset
