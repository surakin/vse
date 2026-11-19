#include "storage/store.h"
#include "containers/hash.h"

namespace engine_placeholder::storage
{

u64 HashBytes(ByteSpan bytes)
{
    return Fnv1a64(bytes);
}

EntityRecord EntityRecord::Clone() const
{
    EntityRecord r;
    r.key = key;
    r.hash = hash;
    r.blob.Reserve(blob.Size());
    for (usize i = 0; i < blob.Size(); ++i)
    {
        r.blob.Push(blob[i]);
    }
    return r;
}

EntityRecord EntityRecord::Make(EntityKey key, ByteSpan bytes)
{
    EntityRecord r;
    r.key = key;
    r.blob.Reserve(bytes.Size());
    for (usize i = 0; i < bytes.Size(); ++i)
    {
        r.blob.Push(bytes[i]);
    }
    r.hash = HashBytes(bytes);
    return r;
}

bool EntityRecord::HashMatches() const
{
    return hash == HashBytes(ByteSpan(blob.Data(), blob.Size()));
}

namespace
{

class KeyCollector final : public RecordVisitor
{
public:
    KeyCollector(usize limit, Vector<EntityKey>* out) : limit_(limit), out_(out) {}
    bool Visit(const EntityRecord& record) override
    {
        if (out_->Size() >= limit_)
        {
            return false;
        }
        out_->Push(record.key);
        return out_->Size() < limit_;
    }

private:
    usize limit_;
    Vector<EntityKey>* out_;
};

}  // namespace

Status ListKeys(const Store& store, const KeyRange& range, usize limit, Vector<EntityKey>* out)
{
    out->Clear();
    if (limit == 0)
    {
        return Status::Success();
    }
    KeyCollector collector(limit, out);
    return store.Scan(range, collector);
}

}  // namespace engine_placeholder::storage
