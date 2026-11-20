#include "asset/pak.h"

#include "algorithm.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"

namespace vse::asset
{
namespace
{

constexpr u32 kHeaderMagic = static_cast<u32>('A') | (static_cast<u32>('P') << 8) | (static_cast<u32>('A') << 16) |
                             (static_cast<u32>('K') << 24);
constexpr u32 kFooterMagic = static_cast<u32>('K') | (static_cast<u32>('P') << 8) | (static_cast<u32>('A') << 16) |
                             (static_cast<u32>('K') << 24);
constexpr u32 kPakAlignment = 64;
constexpr u32 kMaxPakTypes = 4096;
constexpr u64 kLargeEntryBytes = u64(1) << 20;

const char* const kRepackMessage = "the pak is not the current version; run asset_mint pack again";

u64 PakLoad(const u8* p, u32 width)
{
    u64 v = 0;
    for (u32 i = 0; i < width; ++i)
    {
        v |= static_cast<u64>(p[i]) << (8 * i);
    }
    return v;
}

void PakStore(u8* p, u64 value, u32 width)
{
    for (u32 i = 0; i < width; ++i)
    {
        p[i] = static_cast<u8>(value >> (8 * i));
    }
}

u64 PakAlignUp(u64 value, u64 alignment)
{
    return (value + alignment - 1) / alignment * alignment;
}

u64 PakEntryAlignment(u64 size)
{
    return size >= kLargeEntryBytes ? kPageAlignment : kPakAlignment;
}

Status PakFromIo(const io::Status& s)
{
    if (s.Ok())
    {
        return Status::Success();
    }
    if (s.Is(io::Error::EndOfFile))
    {
        return Status::Make(ErrorCode::Corrupt, "the pak ends before the data it announces");
    }
    return Status::Make(ErrorCode::Io, "reading the pak failed");
}

usize PakSlotCountFor(usize entries)
{
    usize n = 16;
    while (n < entries * 2)
    {
        n <<= 1;
    }
    return n;
}

usize PakSlotOf(u64 id, usize mask)
{
    u64 x = id * 0x9e3779b97f4a7c15ull;
    x ^= x >> 29;
    return static_cast<usize>(x) & mask;
}

// Writes sequentially through an io::File and tracks the position.
class PakEmitter
{
public:
    explicit PakEmitter(io::File& file) : file_(file) {}

    Status Put(const void* data, usize length)
    {
        if (length == 0)
        {
            return Status::Success();
        }
        const io::Status s = file_.Write(data, length, position_);
        if (s.Failed())
        {
            return Status::Make(ErrorCode::Io, "writing the pak failed");
        }
        position_ += length;
        return Status::Success();
    }

    Status PadTo(u64 target)
    {
        u8 zeros[4096] = {};
        while (position_ < target)
        {
            const u64 n = target - position_ < sizeof(zeros) ? target - position_ : sizeof(zeros);
            ENGINE_TRY(Put(zeros, static_cast<usize>(n)));
        }
        return Status::Success();
    }

    u64 Position() const { return position_; }

private:
    io::File& file_;
    u64 position_ = 0;
};

}  // namespace

// ---- PakWriter ----

PakWriter::PakWriter(const TypeRegistry& types, const PakWriterOptions& options)
    : types_(&types), options_(options), id_index_(HeapAllocator::Default())
{
}

Status PakWriter::Add(ResourceId id, ByteSpan blob, StringView path)
{
    if (id.IsNone())
    {
        return Status::Make(ErrorCode::Invalid, "the null id cannot name a resource");
    }
    if (items_.Size() >= kMaxPakEntries)
    {
        return Status::Make(ErrorCode::LimitExceeded, "the pak would hold more entries than the format allows");
    }
    if (id_index_.Find(id.Value()) != nullptr)
    {
        return Status::Make(ErrorCode::DuplicateId, "the id is already in the pak");
    }
    BlobReader reader;
    ENGINE_TRY(reader.Open(blob, *types_));
    if (reader.Id() != id)
    {
        return Status::Make(ErrorCode::Corrupt, "the blob's own id is not the id it was added under");
    }
    if (reader.Target() != options_.target)
    {
        return Status::Make(ErrorCode::WrongTarget,
                            "the blob was laid out for another target; run asset_mint pack --target");
    }
    ENGINE_TRY(reader.Verify());
    BlobHeaderInfo header;
    ENGINE_TRY(PeekBlobHeader(blob, &header));
    if (path.Length() > 0)
    {
        ENGINE_TRY(names_.Add(path, id, reader.Type()));
    }
    Item item;
    item.id = id;
    item.bytes = blob;
    item.type = reader.Type();
    item.type_version = reader.TypeVersion();
    item.hash = header.content_hash;
    id_index_.Insert(id.Value(), static_cast<u32>(items_.Size()));
    items_.Push(item);
    return Status::Success();
}

Status PakWriter::CheckClosure()
{
    problems_.Clear();
    bool missing = false;
    for (usize i = 0; i < items_.Size(); ++i)
    {
        BlobReader reader;
        ENGINE_TRY(reader.Open(items_[i].bytes, *types_));
        for (usize d = 0; d < reader.DependencyCount(); ++d)
        {
            const Dependency dep = reader.DependencyAt(d);
            ResourceType type;
            u32 version = 0;
            bool found = false;
            const u32* own = id_index_.Find(dep.id.Value());
            if (own != nullptr)
            {
                type = items_[*own].type;
                version = items_[*own].type_version;
                found = true;
            }
            else if (base_ != nullptr)
            {
                PakEntry entry;
                if (base_->Find(dep.id, &entry) && base_->TypeVersionOf(entry.type, &version))
                {
                    type = entry.type;
                    found = true;
                }
            }
            if (!found)
            {
                problems_.Push(Problem{items_[i].id, dep.id, ErrorCode::MissingDependency});
                missing = true;
            }
            else if (type != dep.type || version != dep.type_version)
            {
                problems_.Push(Problem{items_[i].id, dep.id, ErrorCode::DependencyTypeMismatch});
            }
        }
    }
    if (problems_.Size() == 0)
    {
        return Status::Success();
    }
    return missing ? Status::Make(ErrorCode::MissingDependency, "a blob depends on a resource the pak does not hold")
                   : Status::Make(ErrorCode::DependencyTypeMismatch, "a dependency is not of the declared type");
}

Status PakWriter::Finish(io::File& out)
{
    ENGINE_TRY(CheckClosure());
    Sort(Span<Item>(items_), [](const Item& a, const Item& b) { return a.id < b.id; });
    // The items moved: rewrite the positions the id index holds (Insert replaces an existing key).
    for (usize i = 0; i < items_.Size(); ++i)
    {
        id_index_.Insert(items_[i].id.Value(), static_cast<u32>(i));
    }

    // The type table: every type the pak holds, with the current version, sorted by tag.
    Vector<u64> type_pairs;  // tag in the high half, version in the low half: sorts by tag
    for (usize i = 0; i < items_.Size(); ++i)
    {
        const u64 pair = (static_cast<u64>(items_[i].type.Tag()) << 32) | items_[i].type_version;
        bool present = false;
        for (usize t = 0; t < type_pairs.Size(); ++t)
        {
            present = present || type_pairs[t] == pair;
        }
        if (!present)
        {
            type_pairs.Push(pair);
        }
    }
    Sort(Span<u64>(type_pairs));

    Vector<byte> names_bytes;
    if (options_.include_names)
    {
        ENGINE_TRY(names_.WriteNamesBlob(*types_, &names_bytes));
    }

    // Layout.
    const u64 type_table_end = kPakHeaderBytes + type_pairs.Size() * 8;
    u64 position = type_table_end;
    u64 names_offset = 0;
    if (options_.include_names)
    {
        names_offset = PakAlignUp(position, kPakAlignment);
        position = names_offset + names_bytes.Size();
    }
    if (position < options_.first_entry_offset)
    {
        position = options_.first_entry_offset;
    }
    Vector<u64> offsets;
    for (usize i = 0; i < items_.Size(); ++i)
    {
        position = PakAlignUp(position, PakEntryAlignment(items_[i].bytes.Size()));
        offsets.Push(position);
        position += items_[i].bytes.Size();
    }
    const u64 index_offset = PakAlignUp(position, 8);

    Vector<byte> index;
    index.Resize(items_.Size() * kPakIndexEntryBytes);
    for (usize i = 0; i < items_.Size(); ++i)
    {
        u8* e = index.Data() + i * kPakIndexEntryBytes;
        PakStore(e, items_[i].id.Value(), 8);
        PakStore(e + 8, offsets[i], 8);
        PakStore(e + 16, items_[i].bytes.Size(), 8);
        MemCopy(e + 24, items_[i].hash.bytes, 16);
        PakStore(e + 40, items_[i].type.Tag(), 4);
        PakStore(e + 44, 0, 4);
    }
    const ContentHash index_hash = HashBytes(ByteSpan(index.Data(), index.Size()));
    const u64 file_size = index_offset + index.Size() + kPakFooterBytes;

    u8 header[kPakHeaderBytes] = {};
    PakStore(header, kHeaderMagic, 4);
    PakStore(header + 4, kPakFormatVersion, 4);
    PakStore(header + 8, options_.include_names ? kPakFlagNames : 0, 4);
    PakStore(header + 12, options_.target, 4);
    PakStore(header + 16, kPakAlignment, 4);
    PakStore(header + 20, type_pairs.Size(), 4);
    PakStore(header + 24, items_.Size(), 8);
    PakStore(header + 32, kPakHeaderBytes, 8);
    PakStore(header + 40, names_offset, 8);
    PakStore(header + 48, options_.include_names ? names_bytes.Size() : 0, 8);
    if (base_ != nullptr)
    {
        MemCopy(header + 56, base_->Info().identity.bytes, 16);
    }

    PakEmitter emit(out);
    ENGINE_TRY(emit.Put(header, sizeof(header)));
    for (usize t = 0; t < type_pairs.Size(); ++t)
    {
        u8 record[8];
        PakStore(record, type_pairs[t] >> 32, 4);
        PakStore(record + 4, type_pairs[t] & 0xffffffffu, 4);
        ENGINE_TRY(emit.Put(record, sizeof(record)));
    }
    if (options_.include_names)
    {
        ENGINE_TRY(emit.PadTo(names_offset));
        ENGINE_TRY(emit.Put(names_bytes.Data(), names_bytes.Size()));
    }
    for (usize i = 0; i < items_.Size(); ++i)
    {
        ENGINE_TRY(emit.PadTo(offsets[i]));
        ENGINE_TRY(emit.Put(items_[i].bytes.Data(), items_[i].bytes.Size()));
    }
    ENGINE_TRY(emit.PadTo(index_offset));
    ENGINE_TRY(emit.Put(index.Data(), index.Size()));
    u8 footer[kPakFooterBytes] = {};
    PakStore(footer, index_offset, 8);
    PakStore(footer + 8, items_.Size(), 8);
    MemCopy(footer + 16, index_hash.bytes, 16);
    PakStore(footer + 32, file_size, 8);
    PakStore(footer + 40, kFooterMagic, 4);
    PakStore(footer + 44, kPakFormatVersion, 4);
    ENGINE_TRY(emit.Put(footer, sizeof(footer)));
    const io::Status flushed = out.Flush();
    if (flushed.Failed())
    {
        return Status::Make(ErrorCode::Io, "writing the pak failed");
    }
    return Status::Success();
}

// ---- PakReader ----

Status PakReader::Open(io::FilePtr file, const TypeRegistry& types, u32 expected_target, PakReader* out)
{
    *out = PakReader();
    if (!file)
    {
        return Status::Make(ErrorCode::Invalid, "no file was given");
    }
    u64 size = 0;
    {
        const io::Status s = file->Size(&size);
        if (s.Failed())
        {
            return PakFromIo(s);
        }
    }
    if (size < kPakHeaderBytes + kPakFooterBytes)
    {
        return Status::Make(ErrorCode::Corrupt, "the file is shorter than a pak header and footer");
    }
    u8 header[kPakHeaderBytes];
    u8 footer[kPakFooterBytes];
    ENGINE_TRY(PakFromIo(io::ReadExact(*file, header, sizeof(header), 0)));
    ENGINE_TRY(PakFromIo(io::ReadExact(*file, footer, sizeof(footer), size - kPakFooterBytes)));

    if (PakLoad(header, 4) != kHeaderMagic || PakLoad(footer + 40, 4) != kFooterMagic)
    {
        return Status::Make(ErrorCode::Corrupt, "the file is not a pak (or its end is missing)");
    }
    if (PakLoad(header + 4, 4) != kPakFormatVersion || PakLoad(footer + 44, 4) != kPakFormatVersion)
    {
        return Status::Make(ErrorCode::UnsupportedVersion, kRepackMessage);
    }
    const u64 flags = PakLoad(header + 8, 4);
    const u32 target = static_cast<u32>(PakLoad(header + 12, 4));
    const u64 type_count = PakLoad(header + 20, 4);
    const u64 entry_count = PakLoad(header + 24, 8);
    const u64 type_table_offset = PakLoad(header + 32, 8);
    const u64 names_offset = PakLoad(header + 40, 8);
    const u64 names_size = PakLoad(header + 48, 8);

    out->info_.format_version = kPakFormatVersion;
    out->info_.target = target;
    out->info_.file_size = size;
    out->info_.has_names = (flags & kPakFlagNames) != 0;
    MemCopy(out->info_.identity.bytes, footer + 16, 16);
    MemCopy(out->info_.base.bytes, header + 56, 16);
    out->info_.has_base = !out->info_.base.IsZero();
    if (target != expected_target)
    {
        FormatTo(out->detail_.Data(), out->detail_.Size(),
                 "the pak was packed for target %u but this program runs target %u; run asset_mint pack --target %u",
                 static_cast<unsigned>(target), static_cast<unsigned>(expected_target),
                 static_cast<unsigned>(expected_target));
        return Status::Make(ErrorCode::WrongTarget,
                            "the pak was packed for another target; run asset_mint pack --target");
    }

    if ((flags & ~static_cast<u64>(kPakFlagNames)) != 0 || PakLoad(header + 16, 4) != kPakAlignment ||
        type_count > kMaxPakTypes || type_table_offset != kPakHeaderBytes)
    {
        return Status::Make(ErrorCode::Corrupt, "the pak header holds a value this format does not define");
    }
    for (usize i = 72; i < kPakHeaderBytes; ++i)
    {
        if (header[i] != 0)
        {
            return Status::Make(ErrorCode::Corrupt, "a reserved pak header field is not zero");
        }
    }
    const u64 index_offset = PakLoad(footer, 8);
    if (PakLoad(footer + 8, 8) != entry_count || PakLoad(footer + 32, 8) != size)
    {
        return Status::Make(ErrorCode::Corrupt, "the pak footer disagrees with the header or the file size");
    }
    if (entry_count > kMaxPakEntries)
    {
        return Status::Make(ErrorCode::TooLarge, "the pak holds more entries than the format allows");
    }
    const u64 index_bytes = entry_count * kPakIndexEntryBytes;
    if (index_offset < kPakHeaderBytes || index_offset > size - kPakFooterBytes ||
        size - kPakFooterBytes - index_offset != index_bytes)
    {
        return Status::Make(ErrorCode::Corrupt, "the pak index does not end where the footer begins");
    }
    const u64 type_table_end = kPakHeaderBytes + type_count * 8;
    if (type_table_end > index_offset)
    {
        return Status::Make(ErrorCode::Corrupt, "the pak type table is out of range");
    }
    u64 data_start = type_table_end;
    if (out->info_.has_names)
    {
        if (names_offset % kPakAlignment != 0 || names_offset < type_table_end || names_size < kBlobHeaderBytes ||
            names_offset > index_offset || names_size > index_offset - names_offset)
        {
            return Status::Make(ErrorCode::Corrupt, "the pak names section is out of range");
        }
        data_start = names_offset + names_size;
    }
    else if (names_offset != 0 || names_size != 0)
    {
        return Status::Make(ErrorCode::Corrupt, "the pak has no names flag but a names section");
    }
    out->names_offset_ = names_offset;
    out->names_size_ = names_size;
    out->index_offset_ = index_offset;

    // The type table: sorted, registered and at the current version.
    Vector<byte> table_bytes;
    table_bytes.Resize(static_cast<usize>(type_count * 8));
    ENGINE_TRY(PakFromIo(io::ReadExact(*file, table_bytes.Data(), table_bytes.Size(), kPakHeaderBytes)));
    for (u64 t = 0; t < type_count; ++t)
    {
        TypeRecord record;
        record.tag = static_cast<u32>(PakLoad(table_bytes.Data() + t * 8, 4));
        record.version = static_cast<u32>(PakLoad(table_bytes.Data() + t * 8 + 4, 4));
        if (t > 0 && record.tag <= out->types_[t - 1].tag)
        {
            return Status::Make(ErrorCode::Corrupt, "the pak type table is not sorted by type");
        }
        TypeInfo info;
        if (!types.Find(ResourceType::FromTag(record.tag), &info))
        {
            return Status::Make(ErrorCode::TypeMismatch, "the pak holds a resource type that is not registered");
        }
        if (record.version != info.current_version)
        {
            return Status::Make(ErrorCode::UnsupportedVersion, kRepackMessage);
        }
        out->types_.Push(record);
    }

    // The index: bounded by the file size before it is used.
    Vector<byte> raw;
    raw.Resize(static_cast<usize>(index_bytes));
    ENGINE_TRY(PakFromIo(io::ReadExact(*file, raw.Data(), raw.Size(), index_offset)));
    out->entries_.Reserve(static_cast<usize>(entry_count));
    u64 previous_end = data_start;
    u64 previous_id = 0;
    for (u64 i = 0; i < entry_count; ++i)
    {
        const u8* e = raw.Data() + i * kPakIndexEntryBytes;
        PakEntry entry;
        entry.id = ResourceId::FromValue(PakLoad(e, 8));
        entry.offset = PakLoad(e + 8, 8);
        entry.size = PakLoad(e + 16, 8);
        MemCopy(entry.hash.bytes, e + 24, 16);
        entry.type = ResourceType::FromTag(static_cast<u32>(PakLoad(e + 40, 4)));
        if (entry.id.IsNone() || (i > 0 && entry.id.Value() <= previous_id))
        {
            return Status::Make(ErrorCode::Corrupt, "the pak index is not sorted by id, or repeats an id");
        }
        if (PakLoad(e + 44, 4) != 0 || entry.size < kBlobHeaderBytes || entry.size > kMaxBlobBytes)
        {
            return Status::Make(ErrorCode::Corrupt, "a pak index entry holds a value this format does not define");
        }
        if (entry.offset % kPakAlignment != 0 || entry.offset < previous_end || entry.offset > index_offset ||
            entry.size > index_offset - entry.offset)
        {
            return Status::Make(ErrorCode::Corrupt, "a pak index entry is out of range, unaligned or overlaps another");
        }
        bool known_type = false;
        for (usize t = 0; t < out->types_.Size() && !known_type; ++t)
        {
            known_type = out->types_[t].tag == entry.type.Tag();
        }
        if (!known_type)
        {
            return Status::Make(ErrorCode::Corrupt, "a pak index entry has a type the type table does not list");
        }
        previous_end = entry.offset + entry.size;
        previous_id = entry.id.Value();
        out->entries_.Push(entry);
    }
    out->info_.entry_count = out->entries_.Size();

    // The lookup table, built at open (Day 313 Spike 1).
    const usize slot_count = PakSlotCountFor(out->entries_.Size());
    out->slots_.Resize(slot_count);
    MemSet(out->slots_.Data(), 0, slot_count * sizeof(u32));
    const usize mask = slot_count - 1;
    for (usize i = 0; i < out->entries_.Size(); ++i)
    {
        usize s = PakSlotOf(out->entries_[i].id.Value(), mask);
        while (out->slots_[s] != 0)
        {
            s = (s + 1) & mask;
        }
        out->slots_[s] = static_cast<u32>(i + 1);
    }
    out->file_ = Move(file);
    return Status::Success();
}

const PakEntry& PakReader::At(usize index) const
{
    ENGINE_ASSERT(index < entries_.Size(), "PakReader::At: index out of range");
    return entries_[index];
}

bool PakReader::Find(ResourceId id, PakEntry* out) const
{
    if (slots_.Size() == 0 || id.IsNone())
    {
        return false;
    }
    const usize mask = slots_.Size() - 1;
    usize s = PakSlotOf(id.Value(), mask);
    while (slots_[s] != 0)
    {
        const PakEntry& e = entries_[slots_[s] - 1];
        if (e.id == id)
        {
            *out = e;
            return true;
        }
        s = (s + 1) & mask;
    }
    return false;
}

bool PakReader::TypeVersionOf(ResourceType type, u32* version) const
{
    for (usize t = 0; t < types_.Size(); ++t)
    {
        if (types_[t].tag == type.Tag())
        {
            *version = types_[t].version;
            return true;
        }
    }
    return false;
}

Status PakReader::ReadEntry(const PakEntry& entry, u64 offset, void* buffer, usize length) const
{
    if (offset > entry.size || length > entry.size - offset)
    {
        return Status::Make(ErrorCode::Invalid, "the read is not inside the entry");
    }
    if (length == 0)
    {
        return Status::Success();
    }
    return PakFromIo(io::ReadExact(*file_, buffer, length, entry.offset + offset));
}

Status PakReader::ReadNames(const TypeRegistry& types, ResourceTable* out) const
{
    if (!info_.has_names)
    {
        return Status::Make(ErrorCode::NotFound, "the pak holds no names section");
    }
    Vector<byte> bytes;
    bytes.Resize(static_cast<usize>(names_size_));
    ENGINE_TRY(PakFromIo(io::ReadExact(*file_, bytes.Data(), bytes.Size(), names_offset_)));
    BlobReader blob;
    ENGINE_TRY(blob.Open(ByteSpan(bytes.Data(), bytes.Size()), types));
    return out->ReadNamesBlob(blob);
}

Status PakReader::Verify(const TypeRegistry& types) const
{
    Vector<byte> index;
    index.Resize(entries_.Size() * kPakIndexEntryBytes);
    ENGINE_TRY(PakFromIo(io::ReadExact(*file_, index.Data(), index.Size(), index_offset_)));
    if (HashBytes(ByteSpan(index.Data(), index.Size())) != info_.identity)
    {
        return Status::Make(ErrorCode::Corrupt, "the pak index hash does not match the index");
    }
    for (usize i = 0; i < entries_.Size(); ++i)
    {
        const PakEntry& e = entries_[i];
        Vector<byte> blob;
        blob.Resize(static_cast<usize>(e.size));
        ENGINE_TRY(ReadEntry(e, 0, blob.Data(), blob.Size()));
        BlobReader reader;
        ENGINE_TRY(reader.Open(ByteSpan(blob.Data(), blob.Size()), types));
        if (reader.Id() != e.id || reader.Type() != e.type)
        {
            return Status::Make(ErrorCode::Corrupt, "a pak entry's blob has another id or type than its index entry");
        }
        ENGINE_TRY(reader.Verify());
        BlobHeaderInfo header;
        ENGINE_TRY(PeekBlobHeader(ByteSpan(blob.Data(), blob.Size()), &header));
        if (header.content_hash != e.hash)
        {
            return Status::Make(ErrorCode::Corrupt, "a pak index entry's hash differs from its blob's hash");
        }
    }
    return Status::Success();
}

}  // namespace vse::asset
