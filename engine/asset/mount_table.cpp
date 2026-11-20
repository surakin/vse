#include "asset/mount_table.h"

#include "algorithm.h"
#include "sync/thread.h"

namespace vse::asset
{

MountTable::~MountTable()
{
    // Move every slot out under the lock, destroy them (which waits for their callbacks) outside it.
    Vector<UniquePtr<Slot>> doomed;
    {
        LockGuard<Mutex> lock(mutex_);
        for (usize i = 0; i < slots_.Size(); ++i)
        {
            if (slots_[i]->ticket)
            {
                slots_[i]->ticket->Cancel();
            }
            doomed.Push(Move(slots_[i]));
        }
        slots_.Clear();
    }
}

usize MountTable::MountCount() const
{
    LockGuard<Mutex> lock(mutex_);
    return slots_.Size();
}

bool MountTable::CouldHold(const Slot& slot, StringView path)
{
    const StringView prefix = slot.prefix.View();
    if (prefix.Length() == 0)
    {
        return true;
    }
    return path.Length() > prefix.Length() && path.Substring(0, prefix.Length()) == prefix &&
           path.Substring(prefix.Length(), 1) == StringView("/");
}

bool MountTable::AllReadyLocked() const
{
    for (usize i = 0; i < slots_.Size(); ++i)
    {
        if (slots_[i]->pending || !SlotReady(*slots_[i]))
        {
            return false;
        }
    }
    return true;
}

Status MountTable::Mount(StringView prefix, ResourceBackend* backend, i32 priority, MountId* out)
{
    if (backend == nullptr)
    {
        return Status::Make(ErrorCode::Invalid, "a mount needs a backend");
    }
    String<HeapAllocator> normalized(HeapAllocator::Default());
    if (prefix != StringView("/"))
    {
        ENGINE_TRY(NormalizePath(prefix, &normalized));
    }
    UniquePtr<Slot> slot = MakeUnique<Slot>();
    if (!slot)
    {
        return Status::Make(ErrorCode::Other, "memory for the mount ran out");
    }
    slot->prefix = Move(normalized);
    slot->backend = backend;
    slot->priority = priority;
    slot->owner = this;
    LockGuard<Mutex> lock(mutex_);
    slot->id = next_id_++;
    slot->sequence = next_sequence_++;
    *out = slot->id;
    // slots_ stays ordered: higher priority first, the later of two equal priorities first.
    usize position = 0;
    while (position < slots_.Size() && slots_[position]->priority > priority)
    {
        ++position;
    }
    slots_.Insert(position, Move(slot));
    return Status::Success();
}

Status MountTable::Unmount(MountId id)
{
    UniquePtr<Slot> doomed;
    {
        LockGuard<Mutex> lock(mutex_);
        for (usize i = 0; i < slots_.Size(); ++i)
        {
            if (slots_[i]->id == id)
            {
                if (slots_[i]->ticket)
                {
                    slots_[i]->ticket->Cancel();
                }
                doomed = Move(slots_[i]);
                slots_.Remove(i);
                break;
            }
        }
    }
    if (!doomed)
    {
        return Status::Make(ErrorCode::NotFound, "no such mount");
    }
    return Status::Success();  // `doomed` is destroyed here, outside the lock, waiting for its table callback
}

Status MountTable::Stat(ResourceId id, ResourceInfo* out)
{
    Vector<ResourceBackend*> backends;
    {
        LockGuard<Mutex> lock(mutex_);
        for (usize i = 0; i < slots_.Size(); ++i)
        {
            backends.Push(slots_[i]->backend);
        }
    }
    for (usize i = 0; i < backends.Size(); ++i)
    {
        const Status s = backends[i]->Stat(id, out);
        if (!s.Is(ErrorCode::NotFound))
        {
            return s;
        }
    }
    return Status::Make(ErrorCode::NotFound, "no mount has the resource");
}

Status MountTable::Open(ResourceId id, UniquePtr<ResourceReader>* out, MountId* mount)
{
    Vector<ResourceBackend*> backends;
    Vector<MountId> ids;
    {
        LockGuard<Mutex> lock(mutex_);
        for (usize i = 0; i < slots_.Size(); ++i)
        {
            backends.Push(slots_[i]->backend);
            ids.Push(slots_[i]->id);
        }
    }
    for (usize i = 0; i < backends.Size(); ++i)
    {
        const Status s = backends[i]->Open(id, out);
        if (s.Ok() && mount != nullptr)
        {
            *mount = ids[i];
        }
        if (!s.Is(ErrorCode::NotFound))
        {
            return s;
        }
    }
    return Status::Make(ErrorCode::NotFound, "no mount has the resource");
}

void MountTable::TakeFinishedLocked(Slot* slot)
{
    slot->pending = false;
    ResourceTable raw;
    const Status s = slot->ticket->Finish(&raw);
    slot->table_status = s;
    slot->table.Reset();
    if (s.Failed())
    {
        return;
    }
    UniquePtr<ResourceTable> prefixed = MakeUnique<ResourceTable>();
    if (!prefixed)
    {
        slot->table_status = Status::Make(ErrorCode::Other, "memory for the table ran out");
        return;
    }
    for (usize i = 0; i < raw.Size(); ++i)
    {
        const TableEntryView e = raw.At(i);
        String<HeapAllocator> path(HeapAllocator::Default(), slot->prefix.View());
        path.Append(e.path);
        const Status added = prefixed->Add(path.View(), e.id, e.type);
        if (added.Failed())
        {
            slot->table_status = added;
            return;
        }
    }
    slot->table = Move(prefixed);
}

void MountTable::OnTicketDone(void* slot_pointer)
{
    Slot* slot = static_cast<Slot*>(slot_pointer);
    MountTable* self = slot->owner;
    void (*callback)(void*) = nullptr;
    void* user = nullptr;
    {
        LockGuard<Mutex> lock(self->mutex_);
        bool present = false;
        for (usize i = 0; i < self->slots_.Size(); ++i)
        {
            present = present || self->slots_[i].Get() == slot;
        }
        if (!present || !slot->pending || !slot->ticket || !slot->ticket->IsDone())
        {
            return;  // unmounted, or a cancelled read that a newer one replaced
        }
        self->TakeFinishedLocked(slot);
        if (self->AllReadyLocked())
        {
            callback = self->ready_callback_;
            user = self->ready_user_;
        }
    }
    if (callback != nullptr)
    {
        callback(user);
    }
}

void MountTable::StartLocked(Slot* slot, Vector<UniquePtr<TableTicket>>* retired)
{
    if (slot->ticket)
    {
        slot->ticket->Cancel();
        retired->Push(Move(slot->ticket));
    }
    slot->pending = true;
    slot->ticket = slot->backend->ReadTableAsync();
    if (!slot->ticket)
    {
        slot->pending = false;
        slot->table_status = Status::Make(ErrorCode::Other, "memory for the table read ran out");
        return;
    }
    if (!slot->ticket->SetOnComplete(&MountTable::OnTicketDone, slot))
    {
        TakeFinishedLocked(slot);  // it ended before the callback could be registered
    }
}

void MountTable::RefreshTablesAsync()
{
    Vector<UniquePtr<TableTicket>> retired;  // destroyed after the lock: a stale callback may be waiting for it
    void (*callback)(void*) = nullptr;
    void* user = nullptr;
    {
        LockGuard<Mutex> lock(mutex_);
        for (usize i = 0; i < slots_.Size(); ++i)
        {
            StartLocked(slots_[i].Get(), &retired);
        }
        if (slots_.Size() > 0 && AllReadyLocked())
        {
            callback = ready_callback_;
            user = ready_user_;
        }
    }
    retired.Clear();
    if (callback != nullptr)
    {
        callback(user);
    }
}

Status MountTable::RefreshTables()
{
    RefreshTablesAsync();
    while (!TablesReady())
    {
        ThisThread::SleepForMilliseconds(1);
    }
    LockGuard<Mutex> lock(mutex_);
    for (usize i = 0; i < slots_.Size(); ++i)
    {
        if (slots_[i]->table_status.Failed())
        {
            return slots_[i]->table_status;
        }
    }
    return Status::Success();
}

bool MountTable::TablesReady() const
{
    LockGuard<Mutex> lock(mutex_);
    return AllReadyLocked();
}

void MountTable::SetTablesReadyCallback(void (*function)(void* user), void* user)
{
    LockGuard<Mutex> lock(mutex_);
    ready_callback_ = function;
    ready_user_ = user;
}

Status MountTable::Resolve(StringView path, ResourceId* out) const
{
    String<HeapAllocator> normalized(HeapAllocator::Default());
    ENGINE_TRY(NormalizePath(path, &normalized));
    LockGuard<Mutex> lock(mutex_);
    for (usize i = 0; i < slots_.Size(); ++i)
    {
        const Slot& slot = *slots_[i];
        if (!CouldHold(slot, normalized.View()))
        {
            continue;
        }
        if (slot.pending)
        {
            return Status::Make(ErrorCode::NotReady, "a mount's table has not arrived yet");
        }
        if (slot.table_status.Failed())
        {
            return slot.table_status;
        }
        if (!slot.table)
        {
            return Status::Make(ErrorCode::NotReady, "a mount's table has not arrived yet");
        }
        if (slot.table->Resolve(normalized.View(), out))
        {
            return Status::Success();
        }
    }
    return Status::Make(ErrorCode::NotFound, "no mount maps the path");
}

Status MountTable::PathsOf(ResourceId id, Vector<String<HeapAllocator>>* out) const
{
    out->Clear();
    LockGuard<Mutex> lock(mutex_);
    for (usize i = 0; i < slots_.Size(); ++i)
    {
        const Slot& slot = *slots_[i];
        if (slot.pending)
        {
            return Status::Make(ErrorCode::NotReady, "a mount's table has not arrived yet");
        }
        if (slot.table_status.Failed())
        {
            return slot.table_status;
        }
        if (!slot.table)
        {
            return Status::Make(ErrorCode::NotReady, "a mount's table has not arrived yet");
        }
    }
    for (usize i = 0; i < slots_.Size(); ++i)
    {
        Vector<StringView> paths;
        slots_[i]->table->PathsOf(id, &paths);
        for (usize p = 0; p < paths.Size(); ++p)
        {
            // A path a higher mount maps to another id is shadowed, and a path already listed is not listed twice.
            bool wins = true;
            for (usize h = 0; h < i && wins; ++h)
            {
                ResourceId other;
                if (slots_[h]->table->Resolve(paths[p], &other))
                {
                    wins = false;
                }
            }
            if (wins)
            {
                out->Push(String<HeapAllocator>(HeapAllocator::Default(), paths[p]));
            }
        }
    }
    return Status::Success();
}

Status MountTable::Snapshot(ResourceTable* merged, Vector<ShadowedEntry>* shadowed) const
{
    LockGuard<Mutex> lock(mutex_);
    for (usize i = 0; i < slots_.Size(); ++i)
    {
        const Slot& slot = *slots_[i];
        if (slot.pending)
        {
            return Status::Make(ErrorCode::NotReady, "a mount's table has not arrived yet");
        }
        if (slot.table_status.Failed())
        {
            return slot.table_status;
        }
        if (!slot.table)
        {
            return Status::Make(ErrorCode::NotReady, "a mount's table has not arrived yet");
        }
    }
    for (usize i = 0; i < slots_.Size(); ++i)
    {
        ENGINE_TRY(merged->Merge(*slots_[i]->table, shadowed));
    }
    return Status::Success();
}

Status MountTable::PollChanges(Vector<ChangeRecord>* changes)
{
    changes->Clear();
    // Ask the backends outside the lock (a scan can take a while), then restart the table of each one that changed.
    Vector<ResourceBackend*> backends;
    Vector<MountId> ids;
    {
        LockGuard<Mutex> lock(mutex_);
        for (usize i = 0; i < slots_.Size(); ++i)
        {
            const Slot& slot = *slots_[i];
            if ((slot.backend->Capabilities() & kBackendWatch) != 0)
            {
                backends.Push(slot.backend);
                ids.Push(slot.id);
            }
        }
    }
    Vector<MountId> changed;
    for (usize i = 0; i < backends.Size(); ++i)
    {
        Vector<ChangeRecord> own;
        ENGINE_TRY(backends[i]->Watch(&own));
        if (own.Size() > 0)
        {
            for (usize c = 0; c < own.Size(); ++c)
            {
                changes->Push(own[c]);
            }
            changed.Push(ids[i]);
        }
    }
    Vector<UniquePtr<TableTicket>> retired;  // destroyed after the lock
    {
        LockGuard<Mutex> lock(mutex_);
        for (usize i = 0; i < slots_.Size(); ++i)
        {
            for (usize c = 0; c < changed.Size(); ++c)
            {
                if (slots_[i]->id == changed[c])
                {
                    StartLocked(slots_[i].Get(), &retired);
                }
            }
        }
    }
    return Status::Success();
}

}  // namespace vse::asset
