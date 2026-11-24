#include "asset/instance_blob.h"

#include "cstr/mem_ops.h"
#include "lang/scalar.h"
#include "log/assert.h"

namespace vse::asset
{

namespace
{

void IbStore(byte* p, u32 value)
{
    p[0] = static_cast<byte>(value & 0xFF);
    p[1] = static_cast<byte>((value >> 8) & 0xFF);
    p[2] = static_cast<byte>((value >> 16) & 0xFF);
    p[3] = static_cast<byte>((value >> 24) & 0xFF);
}

u32 IbLoad(const byte* p)
{
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) | (static_cast<u32>(p[2]) << 16) |
           (static_cast<u32>(p[3]) << 24);
}

void IbStore64(byte* p, u64 value)
{
    IbStore(p, static_cast<u32>(value & 0xFFFFFFFFu));
    IbStore(p + 4, static_cast<u32>(value >> 32));
}

u64 IbLoad64(const byte* p)
{
    return static_cast<u64>(IbLoad(p)) | (static_cast<u64>(IbLoad(p + 4)) << 32);
}

u32 IbFloatBits(f32 value)
{
    u32 bits = 0;
    MemCopy(&bits, &value, sizeof(bits));
    return bits;
}

f32 IbBitsFloat(u32 bits)
{
    f32 value = 0.0f;
    MemCopy(&value, &bits, sizeof(value));
    return value;
}

u64 IbDoubleBits(f64 value)
{
    u64 bits = 0;
    MemCopy(&bits, &value, sizeof(bits));
    return bits;
}

f64 IbBitsDouble(u64 bits)
{
    f64 value = 0.0;
    MemCopy(&value, &bits, sizeof(value));
    return value;
}

// The record and data layouts are the little-endian float layouts the sections hold: the records are copied out of
// the blob as they are (the build is little-endian only).
static_assert(sizeof(renderer::InstanceRecord) == 40, "the record is 10 floats");
static_assert(sizeof(renderer::InstanceExtra) == 20, "the data is five 32-bit values");

}  // namespace

Status BuildInstanceBlob(const TypeRegistry& types, ResourceId id, const renderer::InstanceSetDesc& desc,
                         Span<const renderer::InstanceTransform> transforms, Span<const renderer::InstanceExtra> data,
                         Vector<byte>* out, u32 target)
{
    renderer::InstanceSetPtr set = renderer::InstanceSet::Create(desc);
    if (set.Get() == nullptr)
    {
        return Status::Make(ErrorCode::Invalid, "the instance set description is not valid");
    }
    if (data.Size() != 0 && data.Size() != transforms.Size())
    {
        return Status::Make(ErrorCode::Invalid, "the instance data list is not as long as the transform list");
    }
    for (usize i = 0; i < transforms.Size(); ++i)
    {
        const renderer::InstanceStatus status = set->Add(transforms[i], data.Size() != 0 ? &data[i] : nullptr, nullptr);
        if (status == renderer::InstanceStatus::Full)
        {
            return Status::Make(ErrorCode::Invalid, "more instances than the set holds");
        }
        if (status != renderer::InstanceStatus::Ok)
        {
            return Status::Make(ErrorCode::Invalid, "an instance holds a value an instance set refuses");
        }
    }
    set->Rebuild();
    const u32 chunk_count = set->ChunkCount();
    Vector<byte> head;
    head.Resize(kInstanceBlobHeaderBytes);
    MemSet(head.Data(), 0, head.Size());
    IbStore(head.Data(), set->Count());
    IbStore(head.Data() + 4, chunk_count);
    IbStore(head.Data() + 8, desc.has_data ? 1u : 0u);
    IbStore(head.Data() + 12, IbFloatBits(desc.move_threshold));
    IbStore64(head.Data() + 16, IbDoubleBits(desc.key_cell_size));
    Vector<byte> chunks;
    chunks.Resize(static_cast<usize>(chunk_count) * kInstanceBlobChunkBytes);
    MemSet(chunks.Data(), 0, chunks.Size());
    Vector<byte> records;
    Vector<byte> extras;
    for (u32 c = 0; c < chunk_count; ++c)
    {
        const renderer::InstanceChunkInfo info = set->Chunk(c);
        byte* at = chunks.Data() + static_cast<usize>(c) * kInstanceBlobChunkBytes;
        IbStore64(at, IbDoubleBits(info.origin.x));
        IbStore64(at + 8, IbDoubleBits(info.origin.y));
        IbStore64(at + 16, IbDoubleBits(info.origin.z));
        IbStore(at + 24, info.count);
        const Span<const renderer::InstanceRecord> r = set->Records(c);
        const usize old = records.Size();
        records.Resize(old + r.Size() * sizeof(renderer::InstanceRecord));
        MemCopy(records.Data() + old, r.Data(), r.Size() * sizeof(renderer::InstanceRecord));
        if (desc.has_data)
        {
            const Span<const renderer::InstanceExtra> e = set->Extras(c);
            const usize old_extras = extras.Size();
            extras.Resize(old_extras + e.Size() * sizeof(renderer::InstanceExtra));
            MemCopy(extras.Data() + old_extras, e.Data(), e.Size() * sizeof(renderer::InstanceExtra));
        }
    }
    BlobWriter writer(types, types::kInstances, id, target);
    ENGINE_TRY(writer.AddSection(kInstanceBlobSectionHeader, ByteSpan(head.Data(), head.Size())));
    ENGINE_TRY(writer.AddSection(kInstanceBlobSectionChunks, ByteSpan(chunks.Data(), chunks.Size())));
    ENGINE_TRY(writer.AddSection(kInstanceBlobSectionRecords, ByteSpan(records.Data(), records.Size())));
    if (desc.has_data)
    {
        ENGINE_TRY(writer.AddSection(kInstanceBlobSectionData, ByteSpan(extras.Data(), extras.Size())));
    }
    return writer.Finish(out);
}

Status BuildInstanceBlobFromSet(const TypeRegistry& types, ResourceId id, const renderer::InstanceSet& set,
                                Vector<byte>* out, u32 target)
{
    renderer::InstanceSetDesc desc;
    desc.has_data = set.HasData();
    desc.max_instances = set.Capacity();
    Vector<renderer::InstanceTransform> transforms;
    Vector<renderer::InstanceExtra> data;
    for (u32 c = 0; c < set.ChunkCount(); ++c)
    {
        const renderer::InstanceChunkInfo info = set.Chunk(c);
        const Span<const renderer::InstanceRecord> records = set.Records(c);
        const Span<const renderer::InstanceExtra> extras = set.Extras(c);
        for (u32 i = 0; i < info.count; ++i)
        {
            renderer::InstanceTransform t;
            t.position = info.origin + Vec3d(records[i].position);
            t.rotation = records[i].rotation;
            t.scale = records[i].scale;
            transforms.Push(t);
            if (set.HasData())
            {
                data.Push(extras[i]);
            }
        }
    }
    return BuildInstanceBlob(types, id, desc,
                             Span<const renderer::InstanceTransform>(transforms.Data(), transforms.Size()),
                             Span<const renderer::InstanceExtra>(data.Data(), data.Size()), out, target);
}

Status InstanceBlobView::Open(const SharedBytesPtr& bytes, const TypeRegistry& types)
{
    *this = InstanceBlobView();
    if (!bytes)
    {
        return Status::Make(ErrorCode::Invalid, "no bytes were given");
    }
    const ByteSpan data = bytes->Bytes();
    BlobReader blob;
    ENGINE_TRY(blob.Open(data, types));
    if (blob.Type() != types::kInstances)
    {
        return Status::Make(ErrorCode::TypeMismatch, "the blob is not an instance set");
    }
    ByteSpan head;
    if (!blob.FindSection(kInstanceBlobSectionHeader, &head) || head.Size() != kInstanceBlobHeaderBytes)
    {
        return Status::Make(ErrorCode::Corrupt, "the instance header section is missing or has the wrong size");
    }
    InstanceBlobHeader hd;
    hd.instance_count = IbLoad(head.Data());
    hd.chunk_count = IbLoad(head.Data() + 4);
    const u32 flags = IbLoad(head.Data() + 8);
    hd.has_data = (flags & 1u) != 0;
    hd.move_threshold = IbBitsFloat(IbLoad(head.Data() + 12));
    hd.key_cell_size = IbBitsDouble(IbLoad64(head.Data() + 16));
    if ((flags & ~1u) != 0 || IbLoad64(head.Data() + 24) != 0 || !IsFinite(hd.move_threshold) ||
        hd.move_threshold < 0.0f || !IsFinite(hd.key_cell_size) || !(hd.key_cell_size > 0.0) ||
        hd.instance_count > renderer::kMaxInstances ||
        static_cast<u64>(hd.chunk_count) * renderer::kInstanceChunkSize < hd.instance_count ||
        hd.chunk_count > hd.instance_count)
    {
        return Status::Make(ErrorCode::Corrupt, "an instance header field is outside its limits");
    }
    ByteSpan chunks;
    if (!blob.FindSection(kInstanceBlobSectionChunks, &chunks) ||
        static_cast<u64>(chunks.Size()) != static_cast<u64>(hd.chunk_count) * kInstanceBlobChunkBytes)
    {
        return Status::Make(ErrorCode::Corrupt, "the instance chunk section is missing or has the wrong size");
    }
    u64 total = 0;
    for (u32 c = 0; c < hd.chunk_count; ++c)
    {
        const byte* at = chunks.Data() + static_cast<usize>(c) * kInstanceBlobChunkBytes;
        const u32 count = IbLoad(at + 24);
        if (count == 0 || count > renderer::kInstanceChunkSize || IbLoad(at + 28) != 0)
        {
            return Status::Make(ErrorCode::Corrupt, "an instance chunk is empty, too large or has a reserved field");
        }
        total += count;
    }
    if (total != hd.instance_count)
    {
        return Status::Make(ErrorCode::Corrupt, "the chunk counts do not add up to the instance count");
    }
    ByteSpan records;
    if (!blob.FindSection(kInstanceBlobSectionRecords, &records) ||
        static_cast<u64>(records.Size()) != total * sizeof(renderer::InstanceRecord))
    {
        return Status::Make(ErrorCode::Corrupt, "the instance record section is missing or has the wrong size");
    }
    ByteSpan extras;
    if (hd.has_data)
    {
        if (!blob.FindSection(kInstanceBlobSectionData, &extras) ||
            static_cast<u64>(extras.Size()) != total * sizeof(renderer::InstanceExtra))
        {
            return Status::Make(ErrorCode::Corrupt, "the instance data section is missing or has the wrong size");
        }
    }
    else if (blob.FindSection(kInstanceBlobSectionData, &extras))
    {
        return Status::Make(ErrorCode::Corrupt, "an instance blob without data flag has a data section");
    }
    if ((reinterpret_cast<usize>(records.Data()) & 3u) != 0 ||
        (hd.has_data && (reinterpret_cast<usize>(extras.Data()) & 3u) != 0))
    {
        return Status::Make(ErrorCode::Corrupt, "an instance section is not aligned to 4 bytes");
    }
    header_ = hd;
    id_ = blob.Id();
    bytes_ = bytes;
    chunks_ = static_cast<usize>(chunks.Data() - data.Data());
    records_ = static_cast<usize>(records.Data() - data.Data());
    data_ = hd.has_data ? static_cast<usize>(extras.Data() - data.Data()) : 0;
    return Status::Success();
}

Status MakeInstanceSet(const InstanceBlobView& view, renderer::InstanceSetPtr* out)
{
    if (!view.Valid())
    {
        return Status::Make(ErrorCode::Invalid, "the instance view is empty");
    }
    const InstanceBlobHeader& h = view.header_;
    const byte* base = view.bytes_->Bytes().Data();
    Vector<renderer::InstanceChunkSource> sources;
    sources.Resize(h.chunk_count);
    usize first = 0;
    for (u32 c = 0; c < h.chunk_count; ++c)
    {
        const byte* at = base + view.chunks_ + static_cast<usize>(c) * kInstanceBlobChunkBytes;
        const u32 count = IbLoad(at + 24);
        renderer::InstanceChunkSource& s = sources[c];
        s.origin = Vec3d(IbBitsDouble(IbLoad64(at)), IbBitsDouble(IbLoad64(at + 8)), IbBitsDouble(IbLoad64(at + 16)));
        s.records = Span<const renderer::InstanceRecord>(
            reinterpret_cast<const renderer::InstanceRecord*>(base + view.records_) + first, count);
        if (h.has_data)
        {
            s.extras = Span<const renderer::InstanceExtra>(
                reinterpret_cast<const renderer::InstanceExtra*>(base + view.data_) + first, count);
        }
        first += count;
    }
    renderer::InstanceSetDesc desc;
    desc.has_data = h.has_data;
    desc.key_cell_size = h.key_cell_size;
    desc.move_threshold = h.move_threshold;
    renderer::InstanceSetPtr set = renderer::InstanceSet::FromChunks(
        desc, Span<const renderer::InstanceChunkSource>(sources.Data(), sources.Size()));
    if (set.Get() == nullptr)
    {
        return Status::Make(ErrorCode::Corrupt, "an instance blob holds a value an instance set refuses");
    }
    *out = set;
    return Status::Success();
}

}  // namespace vse::asset
