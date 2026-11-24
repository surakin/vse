#include "renderer/instance_set.h"

#include "algorithm.h"
#include "cstr/mem_ops.h"
#include "log/assert.h"
#include "lang/scalar.h"

namespace vse::renderer
{

namespace
{

constexpr u32 kNoInstance = 0xFFFFFFFFu;
constexpr u32 kInstanceKeyBits = 21;
constexpr f64 kInstanceKeyBias = static_cast<f64>(1u << (kInstanceKeyBits - 1));
constexpr f64 kInstanceKeyMax = static_cast<f64>((1u << kInstanceKeyBits) - 1u);

// Spreads the low 21 bits of `v` so there are two zero bits between each.
u64 SpreadInstanceKeyBits(u64 v)
{
    v &= 0x1fffffull;
    v = (v | (v << 32)) & 0x1f00000000ffffull;
    v = (v | (v << 16)) & 0x1f0000ff0000ffull;
    v = (v | (v << 8)) & 0x100f00f00f00f00full;
    v = (v | (v << 4)) & 0x10c30c30c30c30c3ull;
    v = (v | (v << 2)) & 0x1249249249249249ull;
    return v;
}

u64 InstanceKeyCell(f64 coordinate, f64 cell)
{
    f64 c = Floor(coordinate / cell) + kInstanceKeyBias;
    if (c < 0.0)
    {
        c = 0.0;
    }
    if (c > kInstanceKeyMax)
    {
        c = kInstanceKeyMax;
    }
    return static_cast<u64>(c);
}

bool FiniteInstanceVec(const Vec3& v)
{
    return IsFinite(v.x) && IsFinite(v.y) && IsFinite(v.z);
}

u32 NextInstanceGeneration(u32 g)
{
    ++g;
    return g == 0 ? 1u : g;
}

f32 MaxInstanceScale(const Vec3& v)
{
    return Max(v.x, Max(v.y, v.z));
}

struct InstanceRebuildEntry
{
    u64 key;
    u32 handle;
    u32 old_index;
};

}  // namespace

InstanceSetPtr InstanceSet::Create(const InstanceSetDesc& desc)
{
    if (desc.max_instances == 0 || desc.max_instances > kMaxInstances || !IsFinite(desc.key_cell_size) ||
        !(desc.key_cell_size > 0.0) || !IsFinite(desc.move_threshold) || desc.move_threshold < 0.0f)
    {
        return InstanceSetPtr();
    }
    return MakeShared<InstanceSet>(desc);
}

InstanceSetPtr InstanceSet::FromChunks(const InstanceSetDesc& desc, Span<const InstanceChunkSource> chunks)
{
    InstanceSetPtr set = Create(desc);
    if (set.Get() == nullptr)
    {
        return InstanceSetPtr();
    }
    u64 total = 0;
    for (usize c = 0; c < chunks.Size(); ++c)
    {
        const InstanceChunkSource& src = chunks[c];
        if (src.records.Size() == 0 || src.records.Size() > kInstanceChunkSize ||
            src.extras.Size() != (desc.has_data ? src.records.Size() : usize{0}) || !IsFinite(src.origin.x) ||
            !IsFinite(src.origin.y) || !IsFinite(src.origin.z) || Abs(src.origin.x) > kMaxInstanceCoordinate ||
            Abs(src.origin.y) > kMaxInstanceCoordinate || Abs(src.origin.z) > kMaxInstanceCoordinate)
        {
            return InstanceSetPtr();
        }
        total += src.records.Size();
        if (total > desc.max_instances)
        {
            return InstanceSetPtr();
        }
        for (usize i = 0; i < src.records.Size(); ++i)
        {
            const InstanceRecord& r = src.records[i];
            if (!IsFinite(r.position.x) || !IsFinite(r.position.y) || !IsFinite(r.position.z) ||
                !IsFinite(r.rotation.x) || !IsFinite(r.rotation.y) || !IsFinite(r.rotation.z) ||
                !IsFinite(r.rotation.w) || !(r.scale.x > 0.0f) || !(r.scale.y > 0.0f) || !(r.scale.z > 0.0f) ||
                !IsFinite(r.scale.x) || !IsFinite(r.scale.y) || !IsFinite(r.scale.z))
            {
                return InstanceSetPtr();
            }
            if (desc.has_data)
            {
                const InstanceExtra& e = src.extras[i];
                if (!IsFinite(e.tint.x) || !IsFinite(e.tint.y) || !IsFinite(e.tint.z) || !IsFinite(e.tint.w))
                {
                    return InstanceSetPtr();
                }
            }
        }
    }
    InstanceSet& s = *set.Get();
    const usize chunk_total = chunks.Size();
    s.chunks_.Reserve(chunk_total);
    s.order_.Reserve(chunk_total);
    s.records_.Resize(chunk_total * kInstanceChunkSize);
    s.handles_.Resize(chunk_total * kInstanceChunkSize);
    if (desc.has_data)
    {
        s.extras_.Resize(chunk_total * kInstanceChunkSize);
    }
    s.slots_.Reserve(static_cast<usize>(total));
    for (usize ci = 0; ci < chunk_total; ++ci)
    {
        const InstanceChunkSource& src = chunks[ci];
        Chunk_ c;
        c.origin = src.origin;
        c.key = s.KeyOf(src.origin);
        if (ci > 0 && c.key < s.chunks_[ci - 1].key)
        {
            return InstanceSetPtr();  // the chunks are not in key order
        }
        c.listed = true;
        c.version = ++s.counter_;
        c.count = static_cast<u32>(src.records.Size());
        const usize base = ci * kInstanceChunkSize;
        MemCopy(s.records_.Data() + base, src.records.Data(), src.records.Size() * sizeof(InstanceRecord));
        if (desc.has_data)
        {
            MemCopy(s.extras_.Data() + base, src.extras.Data(), src.extras.Size() * sizeof(InstanceExtra));
        }
        for (u32 i = 0; i < c.count; ++i)
        {
            const InstanceRecord& r = s.records_[base + i];
            const u32 handle = static_cast<u32>(s.slots_.Size());
            Slot slot;
            slot.chunk = static_cast<u32>(ci);
            slot.pos = i;
            s.slots_.Push(slot);
            s.handles_[base + i] = handle;
            const Vec3& p = r.position;
            c.box.min = Vec3(Min(c.box.min.x, p.x), Min(c.box.min.y, p.y), Min(c.box.min.z, p.z));
            c.box.max = Vec3(Max(c.box.max.x, p.x), Max(c.box.max.y, p.y), Max(c.box.max.z, p.z));
            c.max_scale = Max(c.max_scale, MaxInstanceScale(r.scale));
        }
        s.chunks_.Push(c);
        s.order_.Push(static_cast<u32>(ci));
    }
    s.count_ = static_cast<u32>(total);
    return set;
}

u64 InstanceSet::KeyOf(const Vec3d& p) const
{
    const f64 cell = desc_.key_cell_size;
    return SpreadInstanceKeyBits(InstanceKeyCell(p.x, cell)) |
           (SpreadInstanceKeyBits(InstanceKeyCell(p.y, cell)) << 1) |
           (SpreadInstanceKeyBits(InstanceKeyCell(p.z, cell)) << 2);
}

bool InstanceSet::ValidTransform(const InstanceTransform& t)
{
    const Vec3d& p = t.position;
    if (!IsFinite(p.x) || !IsFinite(p.y) || !IsFinite(p.z) || Abs(p.x) > kMaxInstanceCoordinate ||
        Abs(p.y) > kMaxInstanceCoordinate || Abs(p.z) > kMaxInstanceCoordinate)
    {
        return false;
    }
    if (!IsFinite(t.rotation.x) || !IsFinite(t.rotation.y) || !IsFinite(t.rotation.z) || !IsFinite(t.rotation.w))
    {
        return false;
    }
    return FiniteInstanceVec(t.scale) && t.scale.x > 0.0f && t.scale.y > 0.0f && t.scale.z > 0.0f;
}

bool InstanceSet::Resolve(InstanceId id, u32* chunk, u32* pos) const
{
    if (id.index >= slots_.Size())
    {
        return false;
    }
    const Slot& s = slots_[id.index];
    if (s.generation != id.generation || s.chunk == kNoInstance)
    {
        return false;
    }
    *chunk = s.chunk;
    *pos = s.pos;
    return true;
}

bool InstanceSet::Contains(InstanceId id) const
{
    u32 c = 0;
    u32 p = 0;
    return Resolve(id, &c, &p);
}

// The first position in `order_` whose (key, chunk index) is not below (key, chunk).
u32 InstanceSet::OrderLowerBound(u64 key, u32 chunk) const
{
    u32 lo = 0;
    u32 hi = static_cast<u32>(order_.Size());
    while (lo < hi)
    {
        const u32 mid = lo + (hi - lo) / 2;
        const u32 c = order_[mid];
        const u64 k = chunks_[c].key;
        if (k < key || (k == key && c < chunk))
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

u32 InstanceSet::NewChunk(u64 key, const Vec3d& origin)
{
    u32 index = 0;
    if (!free_chunks_.Empty())
    {
        index = free_chunks_[free_chunks_.Size() - 1];
        free_chunks_.Pop();
    }
    else
    {
        index = static_cast<u32>(chunks_.Size());
        chunks_.Push(Chunk_());
        const usize total = chunks_.Size() * kInstanceChunkSize;
        records_.Resize(total);
        handles_.Resize(total);
        if (desc_.has_data)
        {
            extras_.Resize(total);
        }
    }
    Chunk_& c = chunks_[index];
    c.origin = origin;
    c.box = Aabb::Empty();
    c.max_scale = 0.0f;
    c.count = 0;
    c.version = ++counter_;
    c.key = key;
    c.listed = true;
    order_.Insert(OrderLowerBound(key, index), index);
    return index;
}

void InstanceSet::FreeChunk(u32 chunk)
{
    Chunk_& c = chunks_[chunk];
    const u32 at = OrderLowerBound(c.key, chunk);
    ENGINE_ASSERT(at < order_.Size() && order_[at] == chunk, "InstanceSet: a listed chunk is in the order");
    order_.Remove(at);
    c.listed = false;
    c.box = Aabb::Empty();
    c.max_scale = 0.0f;
    c.count = 0;
    c.version = ++counter_;
    free_chunks_.Push(chunk);
}

u32 InstanceSet::ChooseChunk(u64 key, const Vec3d& position)
{
    if (!order_.Empty())
    {
        const u32 after = OrderLowerBound(key, kNoInstance);
        const u32 nearest = after > 0 ? after - 1 : 0;
        if (chunks_[order_[nearest]].count < kInstanceChunkSize)
        {
            return order_[nearest];
        }
        if (nearest + 1 < order_.Size() && chunks_[order_[nearest + 1]].count < kInstanceChunkSize)
        {
            return order_[nearest + 1];
        }
    }
    return NewChunk(key, position);
}

void InstanceSet::Refit(u32 chunk)
{
    Chunk_& c = chunks_[chunk];
    const InstanceRecord* r = records_.Data() + static_cast<usize>(chunk) * kInstanceChunkSize;
    Aabb box = Aabb::Empty();
    f32 max_scale = 0.0f;
    for (u32 i = 0; i < c.count; ++i)
    {
        const Vec3& p = r[i].position;
        box.min = Vec3(Min(box.min.x, p.x), Min(box.min.y, p.y), Min(box.min.z, p.z));
        box.max = Vec3(Max(box.max.x, p.x), Max(box.max.y, p.y), Max(box.max.z, p.z));
        max_scale = Max(max_scale, MaxInstanceScale(r[i].scale));
    }
    c.box = box;
    c.max_scale = max_scale;
    c.version = ++counter_;
}

void InstanceSet::Attach(u32 chunk, u32 handle, const InstanceTransform& t, const InstanceExtra& extra)
{
    Chunk_& c = chunks_[chunk];
    const usize at = static_cast<usize>(chunk) * kInstanceChunkSize + c.count;
    InstanceRecord& r = records_[at];
    r.position = (t.position - c.origin).Narrow();
    r.rotation = t.rotation;
    r.scale = t.scale;
    handles_[at] = handle;
    if (desc_.has_data)
    {
        extras_[at] = extra;
    }
    slots_[handle].chunk = chunk;
    slots_[handle].pos = c.count;
    const Vec3& p = r.position;
    c.box.min = Vec3(Min(c.box.min.x, p.x), Min(c.box.min.y, p.y), Min(c.box.min.z, p.z));
    c.box.max = Vec3(Max(c.box.max.x, p.x), Max(c.box.max.y, p.y), Max(c.box.max.z, p.z));
    c.max_scale = Max(c.max_scale, MaxInstanceScale(t.scale));
    ++c.count;
    c.version = ++counter_;
}

void InstanceSet::Detach(u32 chunk, u32 pos)
{
    Chunk_& c = chunks_[chunk];
    const usize base = static_cast<usize>(chunk) * kInstanceChunkSize;
    const u32 last = c.count - 1;
    if (pos != last)
    {
        records_[base + pos] = records_[base + last];
        handles_[base + pos] = handles_[base + last];
        if (desc_.has_data)
        {
            extras_[base + pos] = extras_[base + last];
        }
        slots_[handles_[base + pos]].pos = pos;
    }
    --c.count;
    if (c.count == 0)
    {
        FreeChunk(chunk);
    }
    else
    {
        Refit(chunk);
    }
}

InstanceStatus InstanceSet::Add(const InstanceTransform& transform, const InstanceExtra* data, InstanceId* out)
{
    ++add_calls_;
    if (!ValidTransform(transform))
    {
        return InstanceStatus::InvalidValue;
    }
    InstanceExtra extra;
    if (data != nullptr && desc_.has_data)
    {
        extra = *data;
        if (!IsFinite(extra.tint.x) || !IsFinite(extra.tint.y) || !IsFinite(extra.tint.z) || !IsFinite(extra.tint.w))
        {
            return InstanceStatus::InvalidValue;
        }
    }
    if (count_ >= desc_.max_instances)
    {
        return InstanceStatus::Full;
    }
    u32 handle = 0;
    if (free_slot_ != kNoInstance)
    {
        handle = free_slot_;
        free_slot_ = slots_[handle].next_free;
    }
    else
    {
        handle = static_cast<u32>(slots_.Size());
        slots_.Push(Slot());
    }
    const u32 chunk = ChooseChunk(KeyOf(transform.position), transform.position);
    Attach(chunk, handle, transform, extra);
    ++count_;
    if (out != nullptr)
    {
        out->index = handle;
        out->generation = slots_[handle].generation;
    }
    return InstanceStatus::Ok;
}

InstanceStatus InstanceSet::Remove(InstanceId id)
{
    u32 chunk = 0;
    u32 pos = 0;
    if (!Resolve(id, &chunk, &pos))
    {
        return InstanceStatus::StaleId;
    }
    Detach(chunk, pos);
    Slot& s = slots_[id.index];
    s.chunk = kNoInstance;
    s.generation = NextInstanceGeneration(s.generation);
    s.next_free = free_slot_;
    free_slot_ = id.index;
    --count_;
    return InstanceStatus::Ok;
}

InstanceStatus InstanceSet::SetTransform(InstanceId id, const InstanceTransform& transform)
{
    u32 chunk = 0;
    u32 pos = 0;
    if (!Resolve(id, &chunk, &pos))
    {
        return InstanceStatus::StaleId;
    }
    if (!ValidTransform(transform))
    {
        return InstanceStatus::InvalidValue;
    }
    Chunk_& c = chunks_[chunk];
    const usize at = static_cast<usize>(chunk) * kInstanceChunkSize + pos;
    const Vec3 offset = (transform.position - c.origin).Narrow();
    const f32 t = desc_.move_threshold;
    const bool near_box = offset.x >= c.box.min.x - t && offset.x <= c.box.max.x + t && offset.y >= c.box.min.y - t &&
                          offset.y <= c.box.max.y + t && offset.z >= c.box.min.z - t && offset.z <= c.box.max.z + t;
    if (!near_box)
    {
        InstanceExtra extra;
        if (desc_.has_data)
        {
            extra = extras_[at];
        }
        Detach(chunk, pos);
        const u32 target = ChooseChunk(KeyOf(transform.position), transform.position);
        Attach(target, id.index, transform, extra);
        return InstanceStatus::Ok;
    }
    InstanceRecord& r = records_[at];
    const Vec3 old_p = r.position;
    const f32 old_scale = MaxInstanceScale(r.scale);
    const bool on_edge = old_p.x == c.box.min.x || old_p.x == c.box.max.x || old_p.y == c.box.min.y ||
                         old_p.y == c.box.max.y || old_p.z == c.box.min.z || old_p.z == c.box.max.z ||
                         old_scale == c.max_scale;
    r.position = offset;
    r.rotation = transform.rotation;
    r.scale = transform.scale;
    if (on_edge)
    {
        Refit(chunk);
    }
    else
    {
        c.box.min = Vec3(Min(c.box.min.x, offset.x), Min(c.box.min.y, offset.y), Min(c.box.min.z, offset.z));
        c.box.max = Vec3(Max(c.box.max.x, offset.x), Max(c.box.max.y, offset.y), Max(c.box.max.z, offset.z));
        c.max_scale = Max(c.max_scale, MaxInstanceScale(transform.scale));
        c.version = ++counter_;
    }
    return InstanceStatus::Ok;
}

InstanceStatus InstanceSet::SetData(InstanceId id, const InstanceExtra& data)
{
    u32 chunk = 0;
    u32 pos = 0;
    if (!Resolve(id, &chunk, &pos))
    {
        return InstanceStatus::StaleId;
    }
    if (!desc_.has_data)
    {
        return InstanceStatus::NoData;
    }
    if (!IsFinite(data.tint.x) || !IsFinite(data.tint.y) || !IsFinite(data.tint.z) || !IsFinite(data.tint.w))
    {
        return InstanceStatus::InvalidValue;
    }
    extras_[static_cast<usize>(chunk) * kInstanceChunkSize + pos] = data;
    chunks_[chunk].version = ++counter_;
    return InstanceStatus::Ok;
}

InstanceStatus InstanceSet::Get(InstanceId id, InstanceTransform* out) const
{
    u32 chunk = 0;
    u32 pos = 0;
    if (!Resolve(id, &chunk, &pos))
    {
        return InstanceStatus::StaleId;
    }
    const InstanceRecord& r = records_[static_cast<usize>(chunk) * kInstanceChunkSize + pos];
    out->position = chunks_[chunk].origin + Vec3d(r.position);
    out->rotation = r.rotation;
    out->scale = r.scale;
    return InstanceStatus::Ok;
}

InstanceStatus InstanceSet::GetData(InstanceId id, InstanceExtra* out) const
{
    u32 chunk = 0;
    u32 pos = 0;
    if (!Resolve(id, &chunk, &pos))
    {
        return InstanceStatus::StaleId;
    }
    if (!desc_.has_data)
    {
        return InstanceStatus::NoData;
    }
    *out = extras_[static_cast<usize>(chunk) * kInstanceChunkSize + pos];
    return InstanceStatus::Ok;
}

void InstanceSet::Clear()
{
    for (usize i = 0; i < slots_.Size(); ++i)
    {
        if (slots_[i].chunk != kNoInstance)
        {
            slots_[i].chunk = kNoInstance;
            slots_[i].generation = NextInstanceGeneration(slots_[i].generation);
        }
    }
    free_slot_ = kNoInstance;
    for (usize i = slots_.Size(); i > 0; --i)
    {
        slots_[i - 1].next_free = free_slot_;
        free_slot_ = static_cast<u32>(i - 1);
    }
    chunks_.Clear();
    records_.Clear();
    handles_.Clear();
    extras_.Clear();
    order_.Clear();
    free_chunks_.Clear();
    count_ = 0;
    ++counter_;
}

void InstanceSet::Rebuild()
{
    Vector<InstanceRebuildEntry> entries;
    entries.Reserve(count_);
    for (u32 c = 0; c < chunks_.Size(); ++c)
    {
        const usize base = static_cast<usize>(c) * kInstanceChunkSize;
        for (u32 i = 0; i < chunks_[c].count; ++i)
        {
            const Vec3d world = chunks_[c].origin + Vec3d(records_[base + i].position);
            InstanceRebuildEntry e;
            e.key = KeyOf(world);
            e.handle = handles_[base + i];
            e.old_index = static_cast<u32>(base + i);
            entries.Push(e);
        }
    }
    StableSort(Span<InstanceRebuildEntry>(entries.Data(), entries.Size()),
               [](const InstanceRebuildEntry& a, const InstanceRebuildEntry& b)
               { return a.key < b.key || (a.key == b.key && a.handle < b.handle); });

    const usize chunk_total = (entries.Size() + kInstanceChunkSize - 1) / kInstanceChunkSize;
    Vector<Chunk_> chunks;
    Vector<InstanceRecord> records;
    Vector<u32> handles;
    Vector<InstanceExtra> extras;
    Vector<u32> order;
    chunks.Reserve(chunk_total);
    order.Reserve(chunk_total);
    records.Resize(chunk_total * kInstanceChunkSize);
    handles.Resize(chunk_total * kInstanceChunkSize);
    if (desc_.has_data)
    {
        extras.Resize(chunk_total * kInstanceChunkSize);
    }
    for (usize n = 0; n < entries.Size(); ++n)
    {
        const usize ci = n / kInstanceChunkSize;
        const InstanceRebuildEntry& e = entries[n];
        const Chunk_& oc = chunks_[e.old_index / kInstanceChunkSize];
        const InstanceRecord& old = records_[e.old_index];
        const Vec3d world = oc.origin + Vec3d(old.position);
        if (n % kInstanceChunkSize == 0)
        {
            Chunk_ c;
            c.origin = world;
            c.key = e.key;
            c.listed = true;
            c.version = ++counter_;
            chunks.Push(c);
            order.Push(static_cast<u32>(ci));
        }
        Chunk_& c = chunks[ci];
        const usize at = ci * kInstanceChunkSize + c.count;
        InstanceRecord& r = records[at];
        r.position = (world - c.origin).Narrow();
        r.rotation = old.rotation;
        r.scale = old.scale;
        handles[at] = e.handle;
        if (desc_.has_data)
        {
            extras[at] = extras_[e.old_index];
        }
        slots_[e.handle].chunk = static_cast<u32>(ci);
        slots_[e.handle].pos = c.count;
        const Vec3& p = r.position;
        c.box.min = Vec3(Min(c.box.min.x, p.x), Min(c.box.min.y, p.y), Min(c.box.min.z, p.z));
        c.box.max = Vec3(Max(c.box.max.x, p.x), Max(c.box.max.y, p.y), Max(c.box.max.z, p.z));
        c.max_scale = Max(c.max_scale, MaxInstanceScale(r.scale));
        ++c.count;
    }
    chunks_.Swap(chunks);
    records_.Swap(records);
    handles_.Swap(handles);
    extras_.Swap(extras);
    order_.Swap(order);
    free_chunks_.Clear();
}

InstanceChunkInfo InstanceSet::Chunk(u32 index) const
{
    ENGINE_ASSERT(index < chunks_.Size(), "InstanceSet::Chunk: index is past the last chunk");
    const Chunk_& c = chunks_[index];
    InstanceChunkInfo info;
    info.origin = c.origin;
    info.box = c.box;
    info.max_scale = c.max_scale;
    info.count = c.count;
    info.first = index * kInstanceChunkSize;
    info.version = c.version;
    return info;
}

Span<const InstanceRecord> InstanceSet::Records(u32 index) const
{
    ENGINE_ASSERT(index < chunks_.Size(), "InstanceSet::Records: index is past the last chunk");
    return Span<const InstanceRecord>(records_.Data() + static_cast<usize>(index) * kInstanceChunkSize,
                                      chunks_[index].count);
}

Span<const InstanceExtra> InstanceSet::Extras(u32 index) const
{
    ENGINE_ASSERT(index < chunks_.Size(), "InstanceSet::Extras: index is past the last chunk");
    if (!desc_.has_data)
    {
        return Span<const InstanceExtra>();
    }
    return Span<const InstanceExtra>(extras_.Data() + static_cast<usize>(index) * kInstanceChunkSize,
                                     chunks_[index].count);
}

InstanceId InstanceSet::IdAt(u32 chunk, u32 slot) const
{
    InstanceId id;
    if (chunk >= chunks_.Size() || slot >= chunks_[chunk].count)
    {
        return id;
    }
    id.index = handles_[static_cast<usize>(chunk) * kInstanceChunkSize + slot];
    id.generation = slots_[id.index].generation;
    return id;
}

f64 InstanceSet::ChunkVolumeSum() const
{
    f64 sum = 0.0;
    for (usize i = 0; i < chunks_.Size(); ++i)
    {
        if (chunks_[i].count == 0)
        {
            continue;
        }
        const Vec3 e = chunks_[i].box.Extents();
        sum += static_cast<f64>(e.x) * static_cast<f64>(e.y) * static_cast<f64>(e.z);
    }
    return sum;
}

usize InstanceSet::MemoryBytes() const
{
    return chunks_.Capacity() * sizeof(Chunk_) + records_.Capacity() * sizeof(InstanceRecord) +
           handles_.Capacity() * sizeof(u32) + extras_.Capacity() * sizeof(InstanceExtra) +
           order_.Capacity() * sizeof(u32) + free_chunks_.Capacity() * sizeof(u32) + slots_.Capacity() * sizeof(Slot);
}

}  // namespace vse::renderer
