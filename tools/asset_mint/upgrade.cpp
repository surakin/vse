#include "upgrade.h"

#include "cstr/format.h"
#include "cstr/mem_ops.h"

namespace engine_placeholder::asset
{

void UpgradeChains::Register(ResourceType type, u32 from_version, UpgradeFn step)
{
    for (usize i = 0; i < entries_.Size(); ++i)
    {
        if (entries_[i].type == type && entries_[i].from_version == from_version)
        {
            entries_[i].step = step;
            return;
        }
    }
    entries_.Push(Entry{type, from_version, step});
}

UpgradeFn UpgradeChains::Find(ResourceType type, u32 from_version) const
{
    for (usize i = 0; i < entries_.Size(); ++i)
    {
        if (entries_[i].type == type && entries_[i].from_version == from_version)
        {
            return entries_[i].step;
        }
    }
    return nullptr;
}

Status UpgradeBlob(const UpgradeChains& chains, const TypeRegistry& types, ByteSpan blob, Vector<byte>* out)
{
    BlobHeaderInfo info;
    ENGINE_TRY(PeekBlobHeader(blob, &info));
    TypeInfo type;
    if (!types.Find(info.type, &type))
    {
        return Status::Make(ErrorCode::TypeMismatch, "the blob's type is not registered");
    }
    if (info.type_version > type.current_version)
    {
        return Status::Make(ErrorCode::UnsupportedVersion, "the blob is of a newer version than this tool knows");
    }
    out->Resize(blob.Size());
    MemCopy(out->Data(), blob.Data(), blob.Size());
    u32 version = info.type_version;
    while (version < type.current_version)
    {
        const UpgradeFn step = chains.Find(info.type, version);
        if (step == nullptr)
        {
            return Status::Make(ErrorCode::UnsupportedVersion, "no upgrade step from this version of the type");
        }
        Vector<byte> next;
        ENGINE_TRY(step(types, ByteSpan(out->Data(), out->Size()), &next));
        BlobHeaderInfo made;
        if (PeekBlobHeader(ByteSpan(next.Data(), next.Size()), &made).Failed() || made.type != info.type ||
            made.id != info.id || made.type_version != version + 1)
        {
            return Status::Make(ErrorCode::Invalid, "an upgrade step did not make the next version of the same blob");
        }
        *out = static_cast<Vector<byte>&&>(next);
        version += 1;
    }
    return Status::Success();
}

}  // namespace engine_placeholder::asset
