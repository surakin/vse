#include "asset/memory_backend.h"

#include "asset/blob.h"
#include "cstr/mem_ops.h"

namespace engine_placeholder::asset
{
struct HostMemoryBackend::Entry
{
    Entry() = default;
    Entry(const Entry&) = delete;
    Entry& operator=(const Entry&) = delete;
    // The last holder (the backend, or the last reader of a replaced or removed resource) frees borrowed bytes by
    // handing them back to the host: exactly once, and only for an entry that was installed.
    ~Entry()
    {
        if (release != nullptr)
        {
            release(release_user, ByteSpan(borrowed, borrowed_size));
        }
    }
    u64 Size() const { return borrowed != nullptr ? borrowed_size : bytes.Size(); }
    const u8* Data() const { return borrowed != nullptr ? borrowed : bytes.Data(); }

    Vector<u8> bytes;              // the resource's bytes: a copy of what the host gave (empty for a borrowed resource)
    const u8* borrowed = nullptr;  // the host's bytes, read in place, when the host lent them
    usize borrowed_size = 0;
    void (*release)(void* user, ByteSpan bytes) = nullptr;  // set when the entry is installed, never before
    void* release_user = nullptr;
    ContentHash hash;
    ResourceType type;
    i64 stamp = 0;
    Vector<String<HeapAllocator>> paths;
};

namespace
{

class MemoryReader : public ResourceReader
{
public:
    explicit MemoryReader(SharedPtr<HostMemoryBackend::Entry> entry) : entry_(Move(entry)) {}
    u64 Size() const override { return entry_->Size(); }
    Status ReadAt(u64 offset, void* buffer, usize length) override
    {
        if (offset > entry_->Size() || length > entry_->Size() - offset)
        {
            return Status::Make(ErrorCode::Invalid, "the read is not inside the resource");
        }
        MemCopy(buffer, entry_->Data() + offset, length);
        return Status::Success();
    }

private:
    SharedPtr<HostMemoryBackend::Entry> entry_;
};

}  // namespace

HostMemoryBackend::HostMemoryBackend(const TypeRegistry& types, u64 budget_bytes)
    : types_(&types), budget_(budget_bytes), entries_(HeapAllocator::Default())
{
}

HostMemoryBackend::~HostMemoryBackend() = default;

void HostMemoryBackend::SetInUseQuery(const ResourceInUseQuery& query)
{
    LockGuard<Mutex> lock(change_mutex_);
    LockGuard<Mutex> inner(mutex_);
    query_ = query;
}

u64 HostMemoryBackend::BytesHeld() const
{
    LockGuard<Mutex> lock(mutex_);
    return held_;
}

Status HostMemoryBackend::CheckDependents(ResourceId id, bool force, Vector<ResourceId>* named,
                                          Vector<ResourceId>* to_unload, ResourceInUseQuery* query)
{
    {
        LockGuard<Mutex> lock(mutex_);
        *query = query_;
    }
    if (query->dependents == nullptr)
    {
        return Status::Success();
    }
    Vector<ResourceId> found;
    query->dependents(query->user, id, &found);
    if (found.Size() == 0)
    {
        return Status::Success();
    }
    if (!force)
    {
        if (named != nullptr)
        {
            named->Clear();
            for (usize i = 0; i < found.Size(); ++i)
            {
                named->Push(found[i]);
            }
        }
        return Status::Make(ErrorCode::InUse, "a loaded resource depends on this one");
    }
    for (usize i = 0; i < found.Size(); ++i)
    {
        to_unload->Push(found[i]);
    }
    return Status::Success();
}

void HostMemoryBackend::UnloadDependents(const ResourceInUseQuery& query, const Vector<ResourceId>& to_unload)
{
    if (query.unload == nullptr)
    {
        return;
    }
    for (usize i = 0; i < to_unload.Size(); ++i)
    {
        query.unload(query.user, to_unload[i]);
    }
}

Status HostMemoryBackend::CheckBlob(ResourceId id, ByteSpan bytes, const MemoryAddOptions& options, ContentHash* hash,
                                    ResourceType* type, Vector<String<HeapAllocator>>* paths)
{
    if (id.IsNone())
    {
        return Status::Make(ErrorCode::Invalid, "the null id cannot name a resource");
    }
    BlobReader blob;
    ENGINE_TRY(blob.Open(bytes, *types_));
    if (blob.Id() != id)
    {
        return Status::Make(ErrorCode::Corrupt, "the blob's own id is not the id it was added under");
    }
    ENGINE_TRY(blob.Verify());
    {
        BlobHeaderInfo header;
        ENGINE_TRY(PeekBlobHeader(bytes, &header));
        *hash = header.content_hash;
    }
    if (options.expected_hash != nullptr && !(*options.expected_hash == *hash))
    {
        return Status::Make(ErrorCode::Corrupt, "the blob's hash is not the hash the host supplied");
    }
    *type = blob.Type();
    for (usize i = 0; i < options.paths.Size(); ++i)
    {
        String<HeapAllocator> normalized(HeapAllocator::Default());
        ENGINE_TRY(NormalizePath(options.paths[i], &normalized));
        paths->Push(Move(normalized));
    }
    return Status::Success();
}

Status HostMemoryBackend::Install(ResourceId id, ByteSpan bytes, const MemoryAddOptions& options, bool replacing,
                                  bool force, Vector<ResourceId>* dependents, EntryPtr* replaced)
{
    if (id.IsNone())
    {
        return Status::Make(ErrorCode::Invalid, "the null id cannot name a resource");
    }
    LockGuard<Mutex> change(change_mutex_);

    // Check the blob before anything changes.
    ContentHash hash;
    ResourceType type;
    Vector<String<HeapAllocator>> paths;
    ENGINE_TRY(CheckBlob(id, bytes, options, &hash, &type, &paths));

    {
        LockGuard<Mutex> lock(mutex_);
        const bool live = entries_.Find(id.Value()) != nullptr;
        if (replacing && !live)
        {
            return Status::Make(ErrorCode::NotFound, "the backend does not have the resource");
        }
        if (!replacing && live)
        {
            return Status::Make(ErrorCode::DuplicateId, "the id is already live; use Replace");
        }
    }
    Vector<ResourceId> to_unload;
    ResourceInUseQuery query;
    if (replacing)
    {
        ENGINE_TRY(CheckDependents(id, force, dependents, &to_unload, &query));
    }

    EntryPtr entry = MakeShared<Entry>();
    if (!entry)
    {
        return Status::Make(ErrorCode::Other, "memory for the resource ran out");
    }
    if (options.release != nullptr)
    {
        entry->borrowed = bytes.Data();  // read in place; `release` is attached at the commit below
        entry->borrowed_size = bytes.Size();
    }
    else
    {
        if (!entry->bytes.TryResizeUninitialized(bytes.Size()))
        {
            return Status::Make(ErrorCode::Other, "memory for the resource ran out");
        }
        MemCopy(entry->bytes.Data(), bytes.Data(), bytes.Size());
    }
    entry->hash = hash;
    entry->type = type;

    // Nothing changes until the new bytes are known to fit and their paths to be free: a forced replace unloads the
    // dependents only for a change that will happen.
    EntryPtr& old = *replaced;  // dropped by the caller after the change lock was released
    {
        LockGuard<Mutex> lock(mutex_);
        if (replacing)
        {
            old = *entries_.Find(id.Value());
        }
        const u64 held_without_old = held_ - (old ? old->Size() : 0);
        if (held_without_old + bytes.Size() > budget_)
        {
            return Status::Make(ErrorCode::LimitExceeded, "the memory backend budget is exhausted");
        }
        // Paths: a replace keeps the old ones unless the host names new ones; none may belong to another id.
        if (replacing && paths.Size() == 0)
        {
            for (usize i = 0; i < old->paths.Size(); ++i)
            {
                paths.Push(String<HeapAllocator>(HeapAllocator::Default(), old->paths[i].View()));
            }
        }
        for (usize i = 0; i < paths.Size(); ++i)
        {
            ResourceId owner;
            if (table_.Resolve(paths[i].View(), &owner) && owner != id)
            {
                return Status::Make(ErrorCode::DuplicateId, "a path belongs to another resource");
            }
        }
    }
    UnloadDependents(query, to_unload);  // outside mutex_: the manager's unload reads through this backend
    {
        LockGuard<Mutex> lock(mutex_);
        const u64 held_without_old = held_ - (old ? old->Size() : 0);
        if (replacing)
        {
            for (usize i = 0; i < old->paths.Size(); ++i)
            {
                table_.Remove(old->paths[i].View());
            }
        }
        for (usize i = 0; i < paths.Size(); ++i)
        {
            const Status added = table_.Add(paths[i].View(), id, entry->type);
            if (added.Failed())
            {
                return added;  // only memory running out reaches here; the table may lack this resource's paths
            }
        }
        entry->paths = Move(paths);
        entry->release = options.release;  // from here the entry owns the host's bytes: it hands them back once
        entry->release_user = options.release_user;
        entries_.Insert(id.Value(), entry);
        held_ = held_without_old + bytes.Size();
        ++stamp_counter_;
        entry->stamp = stamp_counter_;
        changes_.Push(ChangeRecord{id, replacing ? ChangeKind::Replaced : ChangeKind::Added});
    }
    return Status::Success();
}

Status HostMemoryBackend::Add(ResourceId id, ByteSpan bytes, const MemoryAddOptions& options)
{
    EntryPtr replaced;  // released after the change lock
    return Install(id, bytes, options, false, false, nullptr, &replaced);
}

Status HostMemoryBackend::AddMany(Span<const MemoryAddItem> items, usize* failed_index)
{
    if (failed_index != nullptr)
    {
        *failed_index = 0;
    }
    LockGuard<Mutex> change(change_mutex_);
    struct Prepared
    {
        EntryPtr entry;
        ResourceId id;
    };
    Vector<Prepared> prepared;
    u64 total = 0;
    for (usize n = 0; n < items.Size(); ++n)
    {
        const MemoryAddItem& item = items[n];
        if (failed_index != nullptr)
        {
            *failed_index = n;
        }
        if (item.options.release != nullptr)
        {
            return Status::Make(ErrorCode::Invalid, "AddMany copies the bytes: it takes no release function");
        }
        ContentHash hash;
        ResourceType type;
        Vector<String<HeapAllocator>> paths;
        ENGINE_TRY(CheckBlob(item.id, ByteSpan(item.data, item.size), item.options, &hash, &type, &paths));
        for (usize k = 0; k < prepared.Size(); ++k)
        {
            if (prepared[k].id == item.id)
            {
                return Status::Make(ErrorCode::DuplicateId, "the batch names one id twice");
            }
            for (usize p = 0; p < paths.Size(); ++p)
            {
                for (usize q = 0; q < prepared[k].entry->paths.Size(); ++q)
                {
                    if (prepared[k].entry->paths[q].View() == paths[p].View())
                    {
                        return Status::Make(ErrorCode::DuplicateId, "the batch names one path twice");
                    }
                }
            }
        }
        EntryPtr entry = MakeShared<Entry>();
        if (!entry || !entry->bytes.TryResizeUninitialized(item.size))
        {
            return Status::Make(ErrorCode::Other, "memory for the resource ran out");
        }
        MemCopy(entry->bytes.Data(), item.data, item.size);
        entry->hash = hash;
        entry->type = type;
        entry->paths = Move(paths);
        total += item.size;
        prepared.Push(Prepared{Move(entry), item.id});
    }
    if (failed_index != nullptr)
    {
        *failed_index = items.Size();  // from here the batch as a whole is what may be refused
    }
    {
        LockGuard<Mutex> lock(mutex_);
        if (held_ + total > budget_)
        {
            return Status::Make(ErrorCode::LimitExceeded, "the memory backend budget is exhausted");
        }
        for (usize n = 0; n < prepared.Size(); ++n)
        {
            if (failed_index != nullptr)
            {
                *failed_index = n;
            }
            if (entries_.Find(prepared[n].id.Value()) != nullptr)
            {
                return Status::Make(ErrorCode::DuplicateId, "the id is already live; use Replace");
            }
            for (usize i = 0; i < prepared[n].entry->paths.Size(); ++i)
            {
                ResourceId owner;
                if (table_.Resolve(prepared[n].entry->paths[i].View(), &owner))
                {
                    return Status::Make(ErrorCode::DuplicateId, "a path belongs to another resource");
                }
            }
        }
        // Everything fits and is free: the commit below can fail only when memory runs out (the table), and then
        // the entries made so far are taken out again.
        usize done = 0;
        Status failure = Status::Success();
        for (; done < prepared.Size() && !failure.Failed(); ++done)
        {
            Prepared& one = prepared[done];
            usize added_paths = 0;
            for (; added_paths < one.entry->paths.Size(); ++added_paths)
            {
                failure = table_.Add(one.entry->paths[added_paths].View(), one.id, one.entry->type);
                if (failure.Failed())
                {
                    break;
                }
            }
            if (failure.Failed())
            {
                for (usize i = 0; i < added_paths; ++i)
                {
                    table_.Remove(one.entry->paths[i].View());
                }
                break;
            }
            entries_.Insert(one.id.Value(), one.entry);
        }
        if (failure.Failed())
        {
            if (failed_index != nullptr)
            {
                *failed_index = done;
            }
            for (usize n = 0; n < done; ++n)
            {
                for (usize i = 0; i < prepared[n].entry->paths.Size(); ++i)
                {
                    table_.Remove(prepared[n].entry->paths[i].View());
                }
                entries_.Remove(prepared[n].id.Value());
            }
            return failure;
        }
        for (usize n = 0; n < prepared.Size(); ++n)
        {
            ++stamp_counter_;
            prepared[n].entry->stamp = stamp_counter_;
            held_ += prepared[n].entry->Size();
            changes_.Push(ChangeRecord{prepared[n].id, ChangeKind::Added});
        }
    }
    return Status::Success();
}

Status HostMemoryBackend::Replace(ResourceId id, ByteSpan bytes, bool force, Vector<ResourceId>* dependents,
                                  const MemoryAddOptions& options)
{
    EntryPtr replaced;  // released after the change lock
    return Install(id, bytes, options, true, force, dependents, &replaced);
}

Status HostMemoryBackend::Remove(ResourceId id, bool force, Vector<ResourceId>* dependents)
{
    EntryPtr old;  // released last, after both locks
    LockGuard<Mutex> change(change_mutex_);
    {
        LockGuard<Mutex> lock(mutex_);
        if (entries_.Find(id.Value()) == nullptr)
        {
            return Status::Make(ErrorCode::NotFound, "the backend does not have the resource");
        }
    }
    Vector<ResourceId> to_unload;
    ResourceInUseQuery query;
    ENGINE_TRY(CheckDependents(id, force, dependents, &to_unload, &query));
    UnloadDependents(query, to_unload);
    {
        LockGuard<Mutex> lock(mutex_);
        EntryPtr* slot = entries_.Find(id.Value());
        old = *slot;
        *slot = EntryPtr();  // a removed hash slot keeps its value alive: drop it so the release is not delayed
        for (usize i = 0; i < old->paths.Size(); ++i)
        {
            table_.Remove(old->paths[i].View());
        }
        entries_.Remove(id.Value());
        held_ -= old->Size();
        changes_.Push(ChangeRecord{id, ChangeKind::Removed});
    }
    return Status::Success();  // `old` is released after this returns, outside both locks
}

Status HostMemoryBackend::Stat(ResourceId id, ResourceInfo* out)
{
    LockGuard<Mutex> lock(mutex_);
    const EntryPtr* entry = entries_.Find(id.Value());
    if (entry == nullptr)
    {
        return Status::Make(ErrorCode::NotFound, "the backend does not have the resource");
    }
    out->size = (*entry)->Size();
    out->hash = (*entry)->hash;
    out->has_hash = true;
    out->modified_ns = (*entry)->stamp;
    out->type = (*entry)->type;
    return Status::Success();
}

Status HostMemoryBackend::Open(ResourceId id, UniquePtr<ResourceReader>* out)
{
    EntryPtr entry;
    {
        LockGuard<Mutex> lock(mutex_);
        const EntryPtr* found = entries_.Find(id.Value());
        if (found == nullptr)
        {
            return Status::Make(ErrorCode::NotFound, "the backend does not have the resource");
        }
        entry = *found;
    }
    UniquePtr<MemoryReader> reader = MakeUnique<MemoryReader>(Move(entry));
    if (!reader)
    {
        return Status::Make(ErrorCode::Other, "memory for the reader ran out");
    }
    *out = Move(reader);
    return Status::Success();
}

Status HostMemoryBackend::ReadTable(ResourceTable* out)
{
    LockGuard<Mutex> lock(mutex_);
    for (usize i = 0; i < table_.Size(); ++i)
    {
        const TableEntryView e = table_.At(i);
        ENGINE_TRY(out->Add(e.path, e.id, e.type));
    }
    return Status::Success();
}

Status HostMemoryBackend::Watch(Vector<ChangeRecord>* out)
{
    out->Clear();
    LockGuard<Mutex> lock(mutex_);
    for (usize i = 0; i < changes_.Size(); ++i)
    {
        out->Push(changes_[i]);
    }
    changes_.Clear();
    return Status::Success();
}

}  // namespace engine_placeholder::asset
