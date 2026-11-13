#include "asset/pak_backend.h"

#include "io/file_system.h"

namespace engine_placeholder::asset
{
namespace
{

class PakEntryReader : public ResourceReader
{
public:
    PakEntryReader(const PakReader* reader, const PakEntry& entry) : reader_(reader), entry_(entry) {}
    u64 Size() const override { return entry_.size; }
    Status ReadAt(u64 offset, void* buffer, usize length) override
    {
        return reader_->ReadEntry(entry_, offset, buffer, length);
    }

private:
    const PakReader* reader_;
    PakEntry entry_;
};

}  // namespace

PakBackend::PakBackend(PakReader reader, const TypeRegistry& types) : reader_(Move(reader)), types_(&types) {}

Status PakBackend::FromFile(io::FilePtr file, const TypeRegistry& types, u32 target, UniquePtr<PakBackend>* out)
{
    PakReader reader;
    ENGINE_TRY(PakReader::Open(Move(file), types, target, &reader));
    *out = MakeUnique<PakBackend>(Move(reader), types);
    if (!*out)
    {
        return Status::Make(ErrorCode::Other, "memory for the backend ran out");
    }
    return Status::Success();
}

Status PakBackend::Open(const io::Path& path, const TypeRegistry& types, u32 target, UniquePtr<PakBackend>* out)
{
    io::FilePtr file;
    const io::Status s = io::File::Open(path, io::OpenMode::Read, io::kOpenNone, &file);
    if (s.Is(io::Error::NotFound))
    {
        return Status::Make(ErrorCode::NotFound, "the pak file does not exist");
    }
    if (s.Failed())
    {
        return Status::Make(ErrorCode::Io, "the pak file could not be opened");
    }
    return FromFile(Move(file), types, target, out);
}

Status PakBackend::CheckBase() const
{
    const PakInfo& info = reader_.Info();
    if (info.has_base && (base_ == nullptr || base_->Info().identity != info.base))
    {
        return Status::Make(ErrorCode::MissingDependency,
                            "the base pak of this patch pak is not mounted with the named hash");
    }
    return Status::Success();
}

Status PakBackend::Stat(ResourceId id, ResourceInfo* out)
{
    ENGINE_TRY(CheckBase());
    PakEntry entry;
    if (!reader_.Find(id, &entry))
    {
        return Status::Make(ErrorCode::NotFound, "the pak does not hold the resource");
    }
    out->size = entry.size;
    out->hash = entry.hash;
    out->has_hash = true;
    out->modified_ns = 0;
    out->type = entry.type;
    return Status::Success();
}

Status PakBackend::Open(ResourceId id, UniquePtr<ResourceReader>* out)
{
    ENGINE_TRY(CheckBase());
    PakEntry entry;
    if (!reader_.Find(id, &entry))
    {
        return Status::Make(ErrorCode::NotFound, "the pak does not hold the resource");
    }
    u8 header[kBlobHeaderBytes];
    ENGINE_TRY(reader_.ReadEntry(entry, 0, header, sizeof(header)));
    BlobHeaderInfo info;
    ENGINE_TRY(PeekBlobHeader(ByteSpan(header, sizeof(header)), &info));
    if (info.id != id)
    {
        return Status::Make(ErrorCode::Corrupt, "the pak index and the blob disagree on the resource id");
    }
    UniquePtr<PakEntryReader> reader = MakeUnique<PakEntryReader>(&reader_, entry);
    if (!reader)
    {
        return Status::Make(ErrorCode::Other, "memory for the reader ran out");
    }
    *out = Move(reader);
    return Status::Success();
}

Status PakBackend::ReadTable(ResourceTable* out)
{
    ENGINE_TRY(CheckBase());
    if (!reader_.Info().has_names)
    {
        return Status::Success();
    }
    return reader_.ReadNames(*types_, out);
}

}  // namespace engine_placeholder::asset
