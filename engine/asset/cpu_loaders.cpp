#include "asset/cpu_loaders.h"

namespace vse::asset
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

Status LoadScript(const LoadContext& context, ScriptView* out, void*)
{
    ENGINE_TRY(out->Open(context.Buffer(), context.Types()));
    for (usize i = 0; i < out->DependencyCount(); ++i)
    {
        if (context.Dependency<ScriptView>(out->DependencyAt(i)) == nullptr)
        {
            return Status::Make(ErrorCode::DependencyTypeMismatch,
                                "a script's required script is not among its loaded dependencies");
        }
    }
    return Status::Success();
}

Status LoadClip(const LoadContext& context, ClipView* out, void*)
{
    ENGINE_TRY(out->Open(context.Bytes(), context.Types()));
    if (out->Skeleton().IsNone())
    {
        return Status::Success();  // morph weights only (glTF coverage Day 9): no skeleton, no dependency
    }
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

Status LoadMeshCollider(const LoadContext& context, MeshColliderView* out, void*)
{
    return out->Open(context.Buffer(), context.Types());
}

Status LoadHeightMap(const LoadContext& context, HeightMapView* out, void*)
{
    return out->Open(context.Buffer(), context.Types());
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
    manager->RegisterLoader<ScriptView>(&LoadScript);
    manager->RegisterLoader<MeshView>(&LoadMesh);
    manager->RegisterLoader<TextureView>(&LoadTexture);
    manager->RegisterLoader<MaterialAssetData>(&LoadMaterial);
    manager->RegisterLoader<MeshColliderView>(&LoadMeshCollider);
    manager->RegisterLoader<HeightMapView>(&LoadHeightMap);
    manager->RegisterLoader<LevelSummary>(&LoadLevel);
}

}  // namespace vse::asset
