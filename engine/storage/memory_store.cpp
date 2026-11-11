#include "storage/memory_store.h"

namespace engine_placeholder::storage
{

namespace
{

String<HeapAllocator> Text(StringView sv)
{
    return String<HeapAllocator>(HeapAllocator::Default(), sv);
}

}  // namespace

MemoryStore::State MemoryStore::State::Clone() const
{
    State s;
    s.records.Reserve(records.Size());
    for (usize i = 0; i < records.Size(); ++i)
    {
        s.records.Push(records[i].Clone());
    }
    for (usize i = 0; i < meta.Size(); ++i)
    {
        s.meta.Push(Meta{Text(meta[i].name.View()), Text(meta[i].value.View())});
    }
    return s;
}

MemoryStore::MemoryStore() = default;

const char* MemoryStore::Name() const
{
    return "memory";
}

usize MemoryStore::LowerBound(const State& s, EntityKey key)
{
    usize lo = 0;
    usize hi = s.records.Size();
    while (lo < hi)
    {
        const usize mid = lo + (hi - lo) / 2;
        if (s.records[mid].key < key)
        {
            lo = mid + 1;
        }
        else
        {
            hi = mid;
        }
    }
    return lo;
}

const MemoryStore::State& MemoryStore::ViewFor() const
{
    return (in_batch_ && owner_ == ThisThread::Id()) ? working_ : committed_;
}

bool MemoryStore::ShouldFail()
{
    if (fail_in_ == 0)
    {
        return false;
    }
    --fail_in_;
    return fail_in_ == 0;
}

Status MemoryStore::Writable(State** out)
{
    if (in_batch_ && owner_ != ThisThread::Id())
    {
        return Status::Make(ErrorCode::Busy, "a batch belongs to another thread");
    }
    if (ShouldFail())
    {
        return Status::Make(ErrorCode::Io, "an injected failure");
    }
    *out = in_batch_ ? &working_ : &committed_;
    return Status::Success();
}

void MemoryStore::FailAfter(u32 n)
{
    LockGuard<Mutex> guard(lock_);
    fail_in_ = n;
}

Status MemoryStore::Put(const EntityRecord& record)
{
    if (record.key.IsNull())
    {
        return Status::Make(ErrorCode::Invalid, "a record needs a non-null key");
    }
    if (!record.HashMatches())
    {
        return Status::Make(ErrorCode::Invalid, "the record's hash is not the hash of its bytes");
    }
    LockGuard<Mutex> guard(lock_);
    State* s = nullptr;
    ENGINE_TRY(Writable(&s));
    const usize at = LowerBound(*s, record.key);
    if (at < s->records.Size() && s->records[at].key == record.key)
    {
        s->records[at] = record.Clone();
    }
    else
    {
        s->records.Insert(at, record.Clone());
    }
    return Status::Success();
}

Status MemoryStore::Get(EntityKey key, EntityRecord* out) const
{
    LockGuard<Mutex> guard(lock_);
    const State& s = ViewFor();
    const usize at = LowerBound(s, key);
    if (at >= s.records.Size() || !(s.records[at].key == key))
    {
        return Status::Make(ErrorCode::NotFound, "no record has this key");
    }
    if (!s.records[at].HashMatches())
    {
        return Status::Make(ErrorCode::Corrupt, "a record does not match its hash");
    }
    *out = s.records[at].Clone();
    return Status::Success();
}

Status MemoryStore::Remove(EntityKey key)
{
    LockGuard<Mutex> guard(lock_);
    State* s = nullptr;
    ENGINE_TRY(Writable(&s));
    const usize at = LowerBound(*s, key);
    if (at >= s->records.Size() || !(s->records[at].key == key))
    {
        return Status::Make(ErrorCode::NotFound, "no record has this key");
    }
    s->records.Remove(at);
    return Status::Success();
}

Status MemoryStore::Scan(const KeyRange& range, RecordVisitor& visitor) const
{
    LockGuard<Mutex> guard(lock_);  // held across the visits: a visitor must not call the store
    const State& s = ViewFor();
    for (usize i = LowerBound(s, range.first); i < s.records.Size(); ++i)
    {
        const EntityRecord& r = s.records[i];
        if (!range.Contains(r.key))
        {
            break;
        }
        if (!r.HashMatches())
        {
            return Status::Make(ErrorCode::Corrupt, "a record does not match its hash");
        }
        if (!visitor.Visit(r))
        {
            break;
        }
    }
    return Status::Success();
}

Status MemoryStore::Count(usize* out) const
{
    LockGuard<Mutex> guard(lock_);
    *out = ViewFor().records.Size();
    return Status::Success();
}

Status MemoryStore::Begin()
{
    LockGuard<Mutex> guard(lock_);
    if (in_batch_)
    {
        return Status::Make(ErrorCode::Busy, "a batch is open already");
    }
    if (ShouldFail())
    {
        return Status::Make(ErrorCode::Io, "an injected failure");
    }
    working_ = committed_.Clone();
    in_batch_ = true;
    owner_ = ThisThread::Id();
    return Status::Success();
}

Status MemoryStore::Commit()
{
    LockGuard<Mutex> guard(lock_);
    if (!in_batch_ || owner_ != ThisThread::Id())
    {
        return Status::Make(ErrorCode::Invalid, "this thread has no open batch");
    }
    if (ShouldFail())
    {
        return Status::Make(ErrorCode::Io, "an injected failure");  // the batch stays open
    }
    committed_ = Move(working_);
    working_ = State();
    in_batch_ = false;
    return Status::Success();
}

Status MemoryStore::Rollback()
{
    LockGuard<Mutex> guard(lock_);
    if (!in_batch_ || owner_ != ThisThread::Id())
    {
        return Status::Make(ErrorCode::Invalid, "this thread has no open batch");
    }
    working_ = State();
    in_batch_ = false;
    return Status::Success();
}

Status MemoryStore::GetMetadata(StringView name, String<HeapAllocator>* out) const
{
    LockGuard<Mutex> guard(lock_);
    const State& s = ViewFor();
    for (usize i = 0; i < s.meta.Size(); ++i)
    {
        if (s.meta[i].name.View() == name)
        {
            *out = Text(s.meta[i].value.View());
            return Status::Success();
        }
    }
    return Status::Make(ErrorCode::NotFound, "no metadata has this name");
}

Status MemoryStore::SetMetadata(StringView name, StringView value)
{
    if (name.Length() == 0)
    {
        return Status::Make(ErrorCode::Invalid, "a metadata name must not be empty");
    }
    LockGuard<Mutex> guard(lock_);
    State* s = nullptr;
    ENGINE_TRY(Writable(&s));
    for (usize i = 0; i < s->meta.Size(); ++i)
    {
        if (s->meta[i].name.View() == name)
        {
            s->meta[i].value = Text(value);
            return Status::Success();
        }
    }
    s->meta.Push(Meta{Text(name), Text(value)});
    return Status::Success();
}

bool MemoryStore::CorruptRecord(EntityKey key)
{
    LockGuard<Mutex> guard(lock_);
    const usize at = LowerBound(committed_, key);
    if (at >= committed_.records.Size() || !(committed_.records[at].key == key))
    {
        return false;
    }
    EntityRecord& r = committed_.records[at];
    if (r.blob.Empty())
    {
        r.hash ^= 1;  // nothing to flip: damage the stored hash instead
    }
    else
    {
        r.blob[0] = static_cast<byte>(r.blob[0] ^ 1);
    }
    return true;
}

}  // namespace engine_placeholder::storage
