#include "asset/blob.h"

#include "algorithm.h"
#include "cstr/mem_ops.h"
#include "log/assert.h"

namespace engine_placeholder::asset
{

namespace
{

constexpr u32 kMagic = static_cast<u32>('A') | (static_cast<u32>('B') << 8) | (static_cast<u32>('L') << 16) |
                       (static_cast<u32>('B') << 24);
constexpr usize kHashOffset = 48;
constexpr usize kTargetOffset = 24;

u64 LoadLe(const byte* p, u32 width)
{
    u64 value = 0;
    for (u32 i = 0; i < width; ++i)
    {
        value |= static_cast<u64>(p[i]) << (8 * i);
    }
    return value;
}

void StoreLe(byte* p, u64 value, u32 width)
{
    for (u32 i = 0; i < width; ++i)
    {
        p[i] = static_cast<byte>((value >> (8 * i)) & 0xff);
    }
}

u64 AlignUp(u64 value, u64 alignment)
{
    return (value + alignment - 1) / alignment * alignment;
}

u64 SectionAlignment(u64 size)
{
    return size >= kPageAlignedSectionBytes ? kPageAlignment : kSectionAlignment;
}

const char* const kRemintMessage = "the blob is not the current version; run asset_mint remint";

}  // namespace

BlobWriter::BlobWriter(const TypeRegistry& types, ResourceType type, ResourceId id, u32 target)
    : types_(&types), type_(type), id_(id), target_(target)
{
}

Status BlobWriter::AddSection(u32 section_id, ByteSpan bytes)
{
    if (section_id < kFirstUserSection)
    {
        return Status::Make(ErrorCode::Invalid, "section ids below 16 are reserved");
    }
    for (usize i = 0; i < sections_.Size(); ++i)
    {
        if (sections_[i].id == section_id)
        {
            return Status::Make(ErrorCode::Invalid, "the section id is used already");
        }
    }
    if (sections_.Size() >= kMaxUserSections)
    {
        return Status::Make(ErrorCode::LimitExceeded, "the blob has the most sections it may have");
    }
    PendingSection section;
    section.id = section_id;
    if (!section.bytes.TryResizeUninitialized(bytes.Size()))
    {
        return Status::Make(ErrorCode::TooLarge, "the section does not fit in memory");
    }
    if (!bytes.Empty())
    {
        MemCopy(section.bytes.Data(), bytes.Data(), bytes.Size());
    }
    sections_.Push(Move(section));
    return Status::Success();
}

Status BlobWriter::Finish(Vector<byte>* out) const
{
    TypeInfo info;
    if (!types_->Find(type_, &info))
    {
        return Status::Make(ErrorCode::TypeMismatch, "the resource type is not registered");
    }
    if (id_.IsNone() && type_ != types::kNames)
    {
        return Status::Make(ErrorCode::Invalid, "a resource blob needs a non-null id");
    }
    if (dependencies_.Size() > kMaxDependencies)
    {
        return Status::Make(ErrorCode::LimitExceeded, "the blob declares more dependencies than the limit");
    }

    Vector<Dependency> sorted;
    for (usize i = 0; i < dependencies_.Size(); ++i)
    {
        sorted.Push(dependencies_[i]);
    }
    Sort(Span<Dependency>(sorted.Data(), sorted.Size()),
         [](const Dependency& a, const Dependency& b) { return a.id < b.id; });
    for (usize i = 0; i < sorted.Size(); ++i)
    {
        const Dependency& d = sorted[i];
        if (d.id.IsNone() || d.id == id_ || d.type.IsNone() || d.type_version == 0)
        {
            return Status::Make(ErrorCode::Invalid,
                                "a dependency is null, the blob itself, of no type or of version 0");
        }
        if (i != 0 && sorted[i - 1].id == d.id)
        {
            return Status::Make(ErrorCode::Invalid, "a dependency is declared twice");
        }
    }

    // The dependency section, serialized.
    Vector<byte> dependency_bytes;
    dependency_bytes.Resize(4 + sorted.Size() * kDependencyRecordBytes);
    StoreLe(dependency_bytes.Data(), sorted.Size(), 4);
    for (usize i = 0; i < sorted.Size(); ++i)
    {
        byte* p = dependency_bytes.Data() + 4 + i * kDependencyRecordBytes;
        StoreLe(p, sorted[i].id.Value(), 8);
        StoreLe(p + 8, sorted[i].type.Tag(), 4);
        StoreLe(p + 12, sorted[i].type_version, 4);
        StoreLe(p + 16, sorted[i].role, 4);
    }

    // Layout: the dependency section first, then the user sections in id order.
    Vector<usize> order;
    for (usize i = 0; i < sections_.Size(); ++i)
    {
        order.Push(i);
    }
    Sort(Span<usize>(order.Data(), order.Size()),
         [this](usize a, usize b) { return sections_[a].id < sections_[b].id; });

    const usize section_count = 1 + sections_.Size();
    u64 position = AlignUp(kBlobHeaderBytes + section_count * kSectionEntryBytes, kSectionAlignment);
    Vector<u64> offsets;
    {
        offsets.Push(position = AlignUp(position, SectionAlignment(dependency_bytes.Size())));
        position += dependency_bytes.Size();
        for (usize k = 0; k < order.Size(); ++k)
        {
            const u64 size = sections_[order[k]].bytes.Size();
            const u64 offset = AlignUp(position, SectionAlignment(size));
            offsets.Push(offset);
            position = offset + size;
        }
    }
    const u64 total = AlignUp(position, kSectionAlignment);
    if (total > kMaxBlobBytes)
    {
        return Status::Make(ErrorCode::TooLarge, "the blob would be larger than the blob size limit");
    }

    out->Clear();
    if (!out->TryResizeUninitialized(static_cast<usize>(total)))
    {
        return Status::Make(ErrorCode::TooLarge, "the blob does not fit in memory");
    }
    byte* base = out->Data();
    MemSet(base, 0, static_cast<usize>(total));
    StoreLe(base, kMagic, 4);
    StoreLe(base + 4, kContainerVersion, 4);
    StoreLe(base + 8, type_.Tag(), 4);
    StoreLe(base + 12, info.current_version, 4);
    StoreLe(base + 16, id_.Value(), 8);
    StoreLe(base + 24, target_, 4);
    StoreLe(base + 28, 0, 4);
    StoreLe(base + 32, section_count, 4);
    StoreLe(base + 36, 0, 4);
    StoreLe(base + 40, total, 8);

    auto write_entry = [&](usize index, u32 id, u64 offset, u64 size)
    {
        byte* e = base + kBlobHeaderBytes + index * kSectionEntryBytes;
        StoreLe(e, id, 4);
        StoreLe(e + 4, 0, 4);
        StoreLe(e + 8, offset, 8);
        StoreLe(e + 16, size, 8);
    };
    write_entry(0, kSectionDependencies, offsets[0], dependency_bytes.Size());
    MemCopy(base + offsets[0], dependency_bytes.Data(), dependency_bytes.Size());
    for (usize k = 0; k < order.Size(); ++k)
    {
        const PendingSection& s = sections_[order[k]];
        write_entry(1 + k, s.id, offsets[1 + k], s.bytes.Size());
        if (!s.bytes.Empty())
        {
            MemCopy(base + offsets[1 + k], s.bytes.Data(), s.bytes.Size());
        }
    }

    const ContentHash hash = HashBytes(ByteSpan(base, static_cast<usize>(total)));
    MemCopy(base + kHashOffset, hash.bytes, sizeof(hash.bytes));
    return Status::Success();
}

Status BlobReader::Open(ByteSpan data, const TypeRegistry& types)
{
    *this = BlobReader();
    if (data.Size() < kBlobHeaderBytes)
    {
        return Status::Make(ErrorCode::Corrupt, "the blob is shorter than its header");
    }
    const byte* p = data.Data();
    if (LoadLe(p, 4) != kMagic)
    {
        return Status::Make(ErrorCode::Corrupt, "the blob does not start with the blob magic");
    }
    if (LoadLe(p + 4, 4) != kContainerVersion)
    {
        return Status::Make(ErrorCode::UnsupportedVersion, kRemintMessage);
    }
    const ResourceType type = ResourceType::FromTag(static_cast<u32>(LoadLe(p + 8, 4)));
    TypeInfo info;
    if (!types.Find(type, &info))
    {
        return Status::Make(ErrorCode::TypeMismatch, "the resource type is not registered");
    }
    if (LoadLe(p + 12, 4) != info.current_version)
    {
        return Status::Make(ErrorCode::UnsupportedVersion, kRemintMessage);
    }
    const u64 id_value = LoadLe(p + 16, 8);
    if (id_value == 0 && type != types::kNames)
    {
        return Status::Make(ErrorCode::Corrupt, "a resource blob carries the null id");
    }
    const u64 section_count = LoadLe(p + 32, 4);
    if (LoadLe(p + 36, 4) != 0)
    {
        return Status::Make(ErrorCode::Corrupt, "a reserved header field is not zero");
    }
    if (LoadLe(p + 28, 4) != 0)
    {
        return Status::Make(ErrorCode::Corrupt, "a header flag this container version does not define is set");
    }
    if (LoadLe(p + 40, 8) != data.Size())
    {
        return Status::Make(ErrorCode::Corrupt, "the recorded size differs from the buffer size");
    }
    if (section_count < 1 || section_count > static_cast<u64>(kMaxUserSections) + 1)
    {
        return Status::Make(ErrorCode::Corrupt, "the section count is outside its bounds");
    }
    const u64 table_end = kBlobHeaderBytes + section_count * kSectionEntryBytes;
    if (table_end > data.Size())
    {
        return Status::Make(ErrorCode::Corrupt, "the section table runs past the end of the blob");
    }

    Vector<SectionEntry> sections;
    u64 previous_end = AlignUp(table_end, kSectionAlignment);
    u32 previous_id = 0;
    for (u64 i = 0; i < section_count; ++i)
    {
        const byte* e = p + kBlobHeaderBytes + i * kSectionEntryBytes;
        SectionEntry entry;
        entry.id = static_cast<u32>(LoadLe(e, 4));
        entry.offset = LoadLe(e + 8, 8);
        entry.size = LoadLe(e + 16, 8);
        if (LoadLe(e + 4, 4) != 0)
        {
            return Status::Make(ErrorCode::Corrupt, "a reserved section field is not zero");
        }
        if (i != 0 && entry.id <= previous_id)
        {
            return Status::Make(ErrorCode::Corrupt, "section ids are not strictly increasing");
        }
        if (entry.offset % kSectionAlignment != 0 ||
            (entry.size >= kPageAlignedSectionBytes && entry.offset % kPageAlignment != 0))
        {
            return Status::Make(ErrorCode::Corrupt, "a section is not aligned");
        }
        if (entry.offset < previous_end)
        {
            return Status::Make(ErrorCode::Corrupt, "a section overlaps the table or the section before it");
        }
        if (entry.offset > data.Size() || entry.size > data.Size() - entry.offset)
        {
            return Status::Make(ErrorCode::Corrupt, "a section runs past the end of the blob");
        }
        previous_end = entry.offset + entry.size;
        previous_id = entry.id;
        sections.Push(entry);
    }
    if (sections[0].id != kSectionDependencies)
    {
        return Status::Make(ErrorCode::Corrupt, "the blob has no dependency section");
    }

    const SectionEntry& deps = sections[0];
    if (deps.size < 4)
    {
        return Status::Make(ErrorCode::Corrupt, "the dependency section is shorter than its count");
    }
    const u64 count = LoadLe(p + deps.offset, 4);
    if (count > kMaxDependencies || deps.size != 4 + count * kDependencyRecordBytes)
    {
        return Status::Make(ErrorCode::Corrupt, "the dependency count and the section size disagree");
    }
    u64 last = 0;
    for (u64 i = 0; i < count; ++i)
    {
        const byte* r = p + deps.offset + 4 + i * kDependencyRecordBytes;
        const u64 dep_id = LoadLe(r, 8);
        if (dep_id == 0 || dep_id == id_value || (i != 0 && dep_id <= last))
        {
            return Status::Make(ErrorCode::Corrupt, "dependencies are null, self-referencing, repeated or unsorted");
        }
        if (LoadLe(r + 8, 4) == 0 || LoadLe(r + 12, 4) == 0)
        {
            return Status::Make(ErrorCode::Corrupt, "a dependency has no type or a type version of 0");
        }
        last = dep_id;
    }

    data_ = data;
    type_ = type;
    id_ = ResourceId::FromValue(id_value);
    type_version_ = info.current_version;
    target_ = static_cast<u32>(LoadLe(p + 24, 4));
    flags_ = static_cast<u32>(LoadLe(p + 28, 4));
    MemCopy(hash_.bytes, p + kHashOffset, sizeof(hash_.bytes));
    sections_ = Move(sections);
    dependency_count_ = static_cast<usize>(count);
    dependency_offset_ = static_cast<usize>(deps.offset) + 4;
    return Status::Success();
}

bool BlobReader::FindSection(u32 section_id, ByteSpan* out) const
{
    for (usize i = 0; i < sections_.Size(); ++i)
    {
        if (sections_[i].id == section_id)
        {
            if (out != nullptr)
            {
                *out = data_.Subspan(static_cast<usize>(sections_[i].offset), static_cast<usize>(sections_[i].size));
            }
            return true;
        }
    }
    return false;
}

Dependency BlobReader::DependencyAt(usize index) const
{
    ENGINE_ASSERT(index < dependency_count_, "BlobReader::DependencyAt: the index is out of range");
    const byte* r = data_.Data() + dependency_offset_ + index * kDependencyRecordBytes;
    Dependency d;
    d.id = ResourceId::FromValue(LoadLe(r, 8));
    d.type = ResourceType::FromTag(static_cast<u32>(LoadLe(r + 8, 4)));
    d.type_version = static_cast<u32>(LoadLe(r + 12, 4));
    d.role = static_cast<u32>(LoadLe(r + 16, 4));
    return d;
}

Status BlobReader::Verify() const
{
    if (data_.Size() < kBlobHeaderBytes)
    {
        return Status::Make(ErrorCode::Corrupt, "no blob is open");
    }
    byte zero[16] = {};
    ContentHasher hasher;
    hasher.Update(data_.First(kHashOffset));
    hasher.Update(ByteSpan(zero, sizeof(zero)));
    hasher.Update(data_.Subspan(kHashOffset + sizeof(zero)));
    if (hasher.Finish() != hash_)
    {
        return Status::Make(ErrorCode::Corrupt, "the content hash does not match the blob");
    }
    return Status::Success();
}

ContentHash ComputeBlobHash(ByteSpan blob)
{
    if (blob.Size() < kBlobHeaderBytes)
    {
        return HashBytes(blob);
    }
    byte zero[16] = {};
    ContentHasher hasher;
    hasher.Update(blob.First(kHashOffset));
    hasher.Update(ByteSpan(zero, sizeof(zero)));
    hasher.Update(blob.Subspan(kHashOffset + sizeof(zero)));
    return hasher.Finish();
}

Status SetBlobTarget(ByteSpan blob, u32 target, Vector<byte>* out)
{
    BlobHeaderInfo info;
    ENGINE_TRY(PeekBlobHeader(blob, &info));
    out->Resize(blob.Size());
    MemCopy(out->Data(), blob.Data(), blob.Size());
    for (u32 i = 0; i < 4; ++i)
    {
        (*out)[kTargetOffset + i] = static_cast<byte>(target >> (8 * i));
    }
    const ContentHash hash = ComputeBlobHash(ByteSpan(out->Data(), out->Size()));
    MemCopy(out->Data() + kHashOffset, hash.bytes, sizeof(hash.bytes));
    return Status::Success();
}

Status PeekBlobHeader(ByteSpan header, BlobHeaderInfo* out)
{
    *out = BlobHeaderInfo();
    if (header.Size() < kBlobHeaderBytes)
    {
        return Status::Make(ErrorCode::Corrupt, "the blob is shorter than its header");
    }
    const byte* p = header.Data();
    if (LoadLe(p, 4) != kMagic)
    {
        return Status::Make(ErrorCode::Corrupt, "the blob does not start with the blob magic");
    }
    if (LoadLe(p + 4, 4) != kContainerVersion)
    {
        return Status::Make(ErrorCode::UnsupportedVersion, kRemintMessage);
    }
    out->type = ResourceType::FromTag(static_cast<u32>(LoadLe(p + 8, 4)));
    out->type_version = static_cast<u32>(LoadLe(p + 12, 4));
    out->id = ResourceId::FromValue(LoadLe(p + 16, 8));
    out->target = static_cast<u32>(LoadLe(p + 24, 4));
    out->total_size = LoadLe(p + 40, 8);
    MemCopy(out->content_hash.bytes, p + kHashOffset, sizeof(out->content_hash.bytes));
    return Status::Success();
}

}  // namespace engine_placeholder::asset
