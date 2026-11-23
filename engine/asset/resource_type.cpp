#include "asset/resource_type.h"

namespace vse::asset
{

void ResourceType::Spell(char (&out)[5]) const
{
    for (u32 i = 0; i < 4; ++i)
    {
        const u8 c = static_cast<u8>((tag_ >> (8 * i)) & 0xff);
        out[i] = (c >= 0x20 && c < 0x7f) ? static_cast<char>(c) : '?';
    }
    out[4] = '\0';
}

TypeRegistry::TypeRegistry()
{
    types_.Push(TypeInfo{types::kMesh, 1, "mesh"});
    types_.Push(TypeInfo{types::kTexture, 1, "texture"});
    types_.Push(TypeInfo{types::kMaterial, 1, "material"});
    types_.Push(TypeInfo{types::kLevel, 2, "level"});
    types_.Push(TypeInfo{types::kSkeleton, 1, "skeleton"});
    types_.Push(TypeInfo{types::kAnimation, 1, "animation"});
    types_.Push(TypeInfo{types::kNames, 1, "names"});
    types_.Push(TypeInfo{types::kHull, 1, "hull"});
    types_.Push(TypeInfo{types::kMeshCollider, 1, "mesh_collider"});
    types_.Push(TypeInfo{types::kAudioClip, 1, "audio_clip"});
    types_.Push(TypeInfo{types::kScript, 1, "script"});
    types_.Push(TypeInfo{types::kHeightMap, 1, "height_map"});
}

Status TypeRegistry::Register(ResourceType type, u32 version, const char* name)
{
    if (type.IsNone() || version == 0)
    {
        return Status::Make(ErrorCode::Invalid, "a resource type needs a non-null tag and a version above 0");
    }
    if (Find(type, nullptr))
    {
        return Status::Make(ErrorCode::DuplicateId, "the resource type is registered already");
    }
    types_.Push(TypeInfo{type, version, name});
    return Status::Success();
}

bool TypeRegistry::Find(ResourceType type, TypeInfo* out) const
{
    for (usize i = 0; i < types_.Size(); ++i)
    {
        if (types_[i].type == type)
        {
            if (out != nullptr)
            {
                *out = types_[i];
            }
            return true;
        }
    }
    return false;
}

}  // namespace vse::asset
