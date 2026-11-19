#include "asset/resource_table.h"
#include "containers/hash.h"

#include "algorithm.h"
#include "bytes/byte_reader.h"
#include "bytes/byte_writer.h"
#include "log/assert.h"

namespace engine_placeholder::asset
{

namespace
{

constexpr u32 kNoEntry = 0xffffffffu;
constexpr u32 kSectionNames = kFirstUserSection;

}  // namespace

ResourceTable::ResourceTable() : by_path_(HeapAllocator::Default()), by_id_(HeapAllocator::Default()) {}

void ResourceTable::IndexEntry(u32 index)
{
    Entry& entry = entries_[index];
    by_path_.Insert(entry.path.View(), index);
    const u32* id_head = by_id_.Find(entry.id.Value());
    entry.next_by_id = id_head != nullptr ? *id_head : kNoEntry;
    by_id_.Insert(entry.id.Value(), index);
}

void ResourceTable::Clear()
{
    by_path_.Clear();
    by_id_.Clear();
    entries_.Clear();
}

// Replaces the reference to entry `from` in its id's chain (the head or the previous entry's link) with `to`.
void ResourceTable::RelinkId(u32 from, u32 to)
{
    u32* link = by_id_.Find(entries_[from].id.Value());
    while (link != nullptr && *link != from)
    {
        link = *link == kNoEntry ? nullptr : &entries_[*link].next_by_id;
    }
    if (link != nullptr)
    {
        *link = to;
    }
}

const ResourceTable::Entry* ResourceTable::FindByNormalized(StringView normalized) const
{
    const u32* at = by_path_.Find(normalized);
    return at != nullptr ? &entries_[*at] : nullptr;
}

Status ResourceTable::Add(StringView path, ResourceId id, ResourceType type)
{
    if (id.IsNone() || type.IsNone())
    {
        return Status::Make(ErrorCode::Invalid, "a table entry needs a non-null id and type");
    }
    String<HeapAllocator> normalized(HeapAllocator::Default());
    ENGINE_TRY(NormalizePath(path, &normalized));
    if (const Entry* existing = FindByNormalized(normalized.View()))
    {
        if (existing->id == id && existing->type == type)
        {
            return Status::Success();
        }
        if (existing->id != id)
        {
            return Status::Make(ErrorCode::DuplicateId, "the path names another id already");
        }
        return Status::Make(ErrorCode::TypeMismatch, "the id is known with another type");
    }
    ResourceType known;
    if (TypeOf(id, &known) && known != type)
    {
        return Status::Make(ErrorCode::TypeMismatch, "the id is known with another type");
    }
    if (entries_.Size() >= kMaxTableEntries)
    {
        return Status::Make(ErrorCode::LimitExceeded, "the table holds the most entries it may hold");
    }
    Entry entry{Move(normalized), id, type, kNoEntry};
    entries_.Push(Move(entry));
    IndexEntry(static_cast<u32>(entries_.Size() - 1));
    return Status::Success();
}

bool ResourceTable::Remove(StringView path)
{
    String<HeapAllocator> normalized(HeapAllocator::Default());
    if (NormalizePath(path, &normalized).Failed())
    {
        return false;
    }
    const u32* found = by_path_.Find(normalized.View());
    if (found == nullptr)
    {
        return false;
    }
    // O(1): unlink the entry, then move the last entry into its place (the table's order reaches no output: listings
    // and the NAMES blob are sorted).
    const u32 index = *found;
    const u32 next = entries_[index].next_by_id;
    RelinkId(index, next);
    if (*by_id_.Find(entries_[index].id.Value()) == kNoEntry)
    {
        by_id_.Remove(entries_[index].id.Value());
    }
    by_path_.Remove(normalized.View());
    const u32 last = static_cast<u32>(entries_.Size() - 1);
    if (index != last)
    {
        RelinkId(last, index);
        *by_path_.Find(entries_[last].path.View()) = index;
    }
    entries_.SwapRemove(index);
    return true;
}

bool ResourceTable::Resolve(StringView path, ResourceId* id) const
{
    String<HeapAllocator> normalized(HeapAllocator::Default());
    if (NormalizePath(path, &normalized).Failed())
    {
        return false;
    }
    const Entry* entry = FindByNormalized(normalized.View());
    if (entry == nullptr)
    {
        return false;
    }
    if (id != nullptr)
    {
        *id = entry->id;
    }
    return true;
}

bool ResourceTable::TypeOf(ResourceId id, ResourceType* type) const
{
    const u32* head = by_id_.Find(id.Value());
    if (head == nullptr)
    {
        return false;
    }
    if (type != nullptr)
    {
        *type = entries_[*head].type;
    }
    return true;
}

usize ResourceTable::PathsOf(ResourceId id, Vector<StringView>* out) const
{
    const usize before = out->Size();
    const u32* head = by_id_.Find(id.Value());
    for (u32 i = head != nullptr ? *head : kNoEntry; i != kNoEntry; i = entries_[i].next_by_id)
    {
        out->Push(entries_[i].path.View());
    }
    Sort(Span<StringView>(out->Data() + before, out->Size() - before),
         [](const StringView& a, const StringView& b) { return a < b; });
    return out->Size() - before;
}

usize ResourceTable::ListPrefix(StringView prefix, Vector<TableEntryView>* out) const
{
    const usize before = out->Size();
    for (usize i = 0; i < entries_.Size(); ++i)
    {
        const StringView path = entries_[i].path.View();
        if (path.Length() >= prefix.Length() && path.Substring(0, prefix.Length()) == prefix)
        {
            out->Push(TableEntryView{path, entries_[i].id, entries_[i].type});
        }
    }
    Sort(Span<TableEntryView>(out->Data() + before, out->Size() - before),
         [](const TableEntryView& a, const TableEntryView& b) { return a.path < b.path; });
    return out->Size() - before;
}

TableEntryView ResourceTable::At(usize index) const
{
    ENGINE_ASSERT(index < entries_.Size(), "ResourceTable::At: the index is out of range");
    return TableEntryView{entries_[index].path.View(), entries_[index].id, entries_[index].type};
}

Status ResourceTable::Merge(const ResourceTable& lower, Vector<ShadowedEntry>* shadowed)
{
    // First pass: check everything that can fail, so a failure merges nothing.
    usize added = 0;
    for (usize i = 0; i < lower.entries_.Size(); ++i)
    {
        const Entry& e = lower.entries_[i];
        ResourceType known;
        if (TypeOf(e.id, &known) && known != e.type)
        {
            return Status::Make(ErrorCode::TypeMismatch, "an id has one type in one table and another in the other");
        }
        if (FindByNormalized(e.path.View()) == nullptr)
        {
            ++added;
        }
    }
    if (entries_.Size() + added > kMaxTableEntries)
    {
        return Status::Make(ErrorCode::LimitExceeded, "the merged table would hold more entries than the limit");
    }
    for (usize i = 0; i < lower.entries_.Size(); ++i)
    {
        const Entry& e = lower.entries_[i];
        const Entry* mine = FindByNormalized(e.path.View());
        if (mine == nullptr)
        {
            ENGINE_TRY(Add(e.path.View(), e.id, e.type));
        }
        else if (mine->id != e.id && shadowed != nullptr)
        {
            shadowed->Push(
                ShadowedEntry{String<HeapAllocator>(HeapAllocator::Default(), e.path.View()), e.id, mine->id});
        }
    }
    return Status::Success();
}

Status ResourceTable::WriteNamesBlob(const TypeRegistry& types, Vector<byte>* out) const
{
    Vector<usize> order;
    for (usize i = 0; i < entries_.Size(); ++i)
    {
        order.Push(i);
    }
    Sort(Span<usize>(order.Data(), order.Size()),
         [this](usize a, usize b)
         {
             if (entries_[a].id != entries_[b].id)
             {
                 return entries_[a].id < entries_[b].id;
             }
             return entries_[a].path.View() < entries_[b].path.View();
         });
    bytes::ByteWriter writer;
    writer.WriteVarint(entries_.Size());
    for (usize k = 0; k < order.Size(); ++k)
    {
        const Entry& e = entries_[order[k]];
        writer.WriteU64(e.id.Value());
        writer.WriteU32(e.type.Tag());
        writer.WriteVarint(e.path.Length());
        writer.WriteBytes(ByteSpan(reinterpret_cast<const byte*>(e.path.CStr()), e.path.Length()));
    }
    BlobWriter blob(types, types::kNames, ResourceId::None());
    ENGINE_TRY(blob.AddSection(kSectionNames, writer.Bytes()));
    return blob.Finish(out);
}

Status ResourceTable::ReadNamesBlob(const BlobReader& blob)
{
    Clear();
    if (blob.Type() != types::kNames)
    {
        return Status::Make(ErrorCode::TypeMismatch, "the blob is not a NAMES blob");
    }
    ByteSpan section;
    if (!blob.FindSection(kSectionNames, &section))
    {
        return Status::Make(ErrorCode::Corrupt, "the NAMES blob has no names section");
    }
    bytes::ByteReader reader(section);
    u64 count = 0;
    if (reader.ReadLength(kMaxTableEntries, &count) != bytes::ByteError::Ok)
    {
        return Status::Make(ErrorCode::Corrupt, "the names section has a bad entry count");
    }
    String<HeapAllocator> previous_path(HeapAllocator::Default());
    u64 previous_id = 0;
    for (u64 i = 0; i < count; ++i)
    {
        u64 id_value = 0;
        u32 type_tag = 0;
        u64 length = 0;
        ByteSpan path_bytes;
        if (reader.ReadU64(&id_value) != bytes::ByteError::Ok || reader.ReadU32(&type_tag) != bytes::ByteError::Ok ||
            reader.ReadLength(kMaxPathBytes, &length) != bytes::ByteError::Ok ||
            reader.ReadBytes(static_cast<usize>(length), &path_bytes) != bytes::ByteError::Ok)
        {
            Clear();
            return Status::Make(ErrorCode::Corrupt, "a names entry is truncated or too long");
        }
        const StringView path(reinterpret_cast<const char*>(path_bytes.Data()), path_bytes.Size());
        // The writer's canonical form: entries strictly ordered by (id, path), every path already normalized.
        const bool ordered = i == 0 || id_value > previous_id ||
                             (id_value == previous_id && previous_path.View() < path);
        String<HeapAllocator> normalized(HeapAllocator::Default());
        if (!ordered || NormalizePath(path, &normalized).Failed() || normalized.View() != path)
        {
            Clear();
            return Status::Make(ErrorCode::Corrupt,
                                "a names entry is out of order or its path is not in canonical form");
        }
        const Status added = Add(path, ResourceId::FromValue(id_value), ResourceType::FromTag(type_tag));
        if (added.Failed())
        {
            Clear();
            return Status::Make(ErrorCode::Corrupt, "a names entry breaks the table's rules");
        }
        previous_id = id_value;
        previous_path = String<HeapAllocator>(HeapAllocator::Default(), path);
    }
    if (!reader.AtEnd())
    {
        Clear();
        return Status::Make(ErrorCode::Corrupt, "the names section has bytes after its last entry");
    }
    return Status::Success();
}

}  // namespace engine_placeholder::asset
