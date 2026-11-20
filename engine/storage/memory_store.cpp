#include "storage/memory_store.h"

#include "algorithm.h"

namespace vse::storage
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
    for (const auto& [key, record] : records)
    {
        s.records.Insert(key, record.Clone());
    }
    s.order.Append(order);
    s.order_dirty = order_dirty;
    for (usize i = 0; i < meta.Size(); ++i)
    {
        s.meta.Push(Meta{Text(meta[i].name.View()), Text(meta[i].value.View())});
    }
    return s;
}

const Vector<EntityKey>& MemoryStore::State::Sorted() const
{
    if (order_dirty)
    {
        order.Clear();
        order.Reserve(records.Size());
        for (const auto& [key, record] : records)
        {
            order.Push(key);
        }
        Sort(order, [](const EntityKey& x, const EntityKey& y) { return x < y; });
        order_dirty = false;
    }
    return order;
}

MemoryStore::MemoryStore() = default;

const char* MemoryStore::Name() const
{
    return "memory";
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
    if (EntityRecord* existing = s->records.Find(record.key))
    {
        *existing = record.Clone();
    }
    else
    {
        s->records.Insert(record.key, record.Clone());
        s->order_dirty = true;
    }
    return Status::Success();
}

Status MemoryStore::Get(EntityKey key, EntityRecord* out) const
{
    LockGuard<Mutex> guard(lock_);
    const State& s = ViewFor();
    const EntityRecord* r = s.records.Find(key);
    if (r == nullptr)
    {
        return Status::Make(ErrorCode::NotFound, "no record has this key");
    }
    if (!r->HashMatches())
    {
        return Status::Make(ErrorCode::Corrupt, "a record does not match its hash");
    }
    *out = r->Clone();
    return Status::Success();
}

Status MemoryStore::Remove(EntityKey key)
{
    LockGuard<Mutex> guard(lock_);
    State* s = nullptr;
    ENGINE_TRY(Writable(&s));
    if (!s->records.Remove(key))
    {
        return Status::Make(ErrorCode::NotFound, "no record has this key");
    }
    s->order_dirty = true;
    return Status::Success();
}

Status MemoryStore::Scan(const KeyRange& range, RecordVisitor& visitor) const
{
    LockGuard<Mutex> guard(lock_);  // held across the visits: a visitor must not call the store
    const State& s = ViewFor();
    const Vector<EntityKey>& keys = s.Sorted();
    for (usize i = LowerBound(keys, range.first, [](const EntityKey& k, const EntityKey& v) { return k < v; });
         i < keys.Size(); ++i)
    {
        const EntityRecord& r = *s.records.Find(keys[i]);
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
    Meta entry{Text(name), Text(value)};
    s->meta.Push(Move(entry));
    return Status::Success();
}  // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks): the strings are moved into the vector

bool MemoryStore::CorruptRecord(EntityKey key)
{
    LockGuard<Mutex> guard(lock_);
    EntityRecord* found = committed_.records.Find(key);
    if (found == nullptr)
    {
        return false;
    }
    EntityRecord& r = *found;
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

}  // namespace vse::storage
