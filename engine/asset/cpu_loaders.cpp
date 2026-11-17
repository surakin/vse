#include "asset/cpu_loaders.h"

namespace engine_placeholder::asset
{
namespace
{

Status LoadSkeleton(const LoadContext& context, SkeletonView* out, void*)
{
    return out->Open(context.Bytes(), context.Types());
}

Status LoadAudioClip(const LoadContext& context, AudioClipView* out, void*)
{
    return out->Open(context.Buffer(), context.Types());
}

Status LoadClip(const LoadContext& context, ClipView* out, void*)
{
    ENGINE_TRY(out->Open(context.Bytes(), context.Types()));
    const SkeletonView* skeleton = context.Dependency<SkeletonView>(out->Skeleton());
    if (skeleton == nullptr)
    {
        return Status::Make(ErrorCode::DependencyTypeMismatch,
                            "the clip's skeleton is not among its loaded dependencies");
    }
    if (out->Bind(*skeleton).Failed())
    {
        return Status::Make(ErrorCode::DependencyTypeMismatch,
                            "the clip was made for another skeleton than the one loaded (id, hash or joint count)");
    }
    return Status::Success();
}

Status LoadMesh(const LoadContext& context, MeshView* out, void*)
{
    ENGINE_TRY(out->Open(context.Bytes(), context.Types()));
    if (out->Skinned())
    {
        const SkeletonView* skeleton = context.Dependency<SkeletonView>(out->Skeleton());
        if (skeleton == nullptr)
        {
            return Status::Make(ErrorCode::DependencyTypeMismatch,
                                "the mesh's skeleton is not among its loaded dependencies");
        }
        ENGINE_TRY(out->CheckSkeleton(*skeleton));
    }
    return Status::Success();
}

Status LoadTexture(const LoadContext& context, TextureView* out, void*)
{
    return out->Open(context.Bytes(), context.Types());
}

Status LoadMaterial(const LoadContext& context, MaterialAssetData* out, void*)
{
    return LoadMaterialBlob(context.Bytes(), context.Types(), out);
}

Status LoadLevel(const LoadContext& context, LevelSummary* out, void*)
{
    return ReadLevelSummary(context.Bytes(), context.Types(), out);
}

}  // namespace

void RegisterSkeletonLoader(ResourceManager* manager)
{
    manager->RegisterLoader<SkeletonView>(&LoadSkeleton);
}

void RegisterCpuLoaders(ResourceManager* manager)
{
    manager->RegisterLoader<SkeletonView>(&LoadSkeleton);
    manager->RegisterLoader<AudioClipView>(&LoadAudioClip);
    manager->RegisterLoader<ClipView>(&LoadClip);
    manager->RegisterLoader<MeshView>(&LoadMesh);
    manager->RegisterLoader<TextureView>(&LoadTexture);
    manager->RegisterLoader<MaterialAssetData>(&LoadMaterial);
    manager->RegisterLoader<LevelSummary>(&LoadLevel);
}

}  // namespace engine_placeholder::asset
