#include "target_transform.h"

#include "asset/texture.h"
#include "cstr/mem_ops.h"

namespace vse::asset
{

namespace
{

Status TextureTransform(const TransformContext& context, ByteSpan blob, Vector<byte>* out)
{
    return RetargetTextureBlob(*context.types, blob, context.target, out);
}

}  // namespace

TargetTransforms TargetTransforms::Builtin()
{
    TargetTransforms transforms;
    transforms.Register(types::kTexture, &TextureTransform);
    return transforms;
}

void TargetTransforms::Register(ResourceType type, TargetTransformFn function)
{
    for (usize i = 0; i < entries_.Size(); ++i)
    {
        if (entries_[i].type == type)
        {
            entries_[i].function = function;
            return;
        }
    }
    entries_.Push(Entry{type, function});
}

TargetTransformFn TargetTransforms::Find(ResourceType type) const
{
    for (usize i = 0; i < entries_.Size(); ++i)
    {
        if (entries_[i].type == type)
        {
            return entries_[i].function;
        }
    }
    return nullptr;
}

Status RetagBlob(ByteSpan blob, u32 target, Vector<byte>* out)
{
    return SetBlobTarget(blob, target, out);
}

Status TransformBlob(const TargetTransforms& transforms, const TransformContext& context, ByteSpan blob,
                     Vector<byte>* out)
{
    BlobHeaderInfo before;
    ENGINE_TRY(PeekBlobHeader(blob, &before));
    if (before.target == context.target)
    {
        out->Resize(blob.Size());
        MemCopy(out->Data(), blob.Data(), blob.Size());
        return Status::Success();
    }
    BlobReader source;
    ENGINE_TRY(source.Open(blob, *context.types));
    TargetTransformFn function = transforms.Find(before.type);
    if (function != nullptr)
    {
        ENGINE_TRY(function(context, blob, out));
    }
    else
    {
        ENGINE_TRY(RetagBlob(blob, context.target, out));
    }
    // Deterministic: the same input gives the same bytes (a transform that depends on addresses or the clock is
    // refused).
    if (function != nullptr)
    {
        Vector<byte> second;
        ENGINE_TRY(function(context, blob, &second));
        if (second.Size() != out->Size() || MemCompare(second.Data(), out->Data(), out->Size()) != 0)
        {
            return Status::Make(ErrorCode::Invalid,
                                "the transform is not deterministic: two runs gave different bytes");
        }
    }
    // The rules: a current-version blob of the same type and id, for the target, with the same references and a hash
    // that holds.
    BlobReader result;
    ENGINE_TRY(result.Open(ByteSpan(out->Data(), out->Size()), *context.types));
    ENGINE_TRY(result.Verify());
    if (result.Type() != source.Type() || result.Id() != source.Id())
    {
        return Status::Make(ErrorCode::Invalid, "the transform changed the type or the id of the blob");
    }
    if (result.Target() != context.target)
    {
        return Status::Make(ErrorCode::Invalid, "the transform did not lay the blob out for the requested target");
    }
    bool same = result.DependencyCount() == source.DependencyCount();
    for (usize i = 0; same && i < source.DependencyCount(); ++i)
    {
        const Dependency a = source.DependencyAt(i);
        const Dependency b = result.DependencyAt(i);
        same = a.id == b.id && a.type == b.type && a.type_version == b.type_version && a.role == b.role;
    }
    if (!same)
    {
        return Status::Make(ErrorCode::Invalid, "the transform changed the dependencies of the blob");
    }
    return Status::Success();
}

}  // namespace vse::asset
