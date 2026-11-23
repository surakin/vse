#include "asset/gpu_loaders.h"

#include "cstr/format.h"
#include "renderer/shader_permutation.h"

namespace vse::asset
{

GpuMaterial::~GpuMaterial()
{
    if (materials != nullptr && id.IsValid() && materials->IsAlive(id))
    {
        materials->DestroyMaterial(id);
    }
}

namespace
{

Status DeviceRefused(const char* message)
{
    return Status::Make(ErrorCode::Other, message);
}

Status LoadGpuMesh(const LoadContext& context, GpuMeshAsset* out, void* user)
{
    GpuAssetContext* gpu = static_cast<GpuAssetContext*>(user);
    MeshView& view = out->view;
    ENGINE_TRY(view.Open(context.Bytes(), context.Types()));
    out->bytes = context.Buffer();
    const byte* base = context.Bytes().Data();
    if (view.Skinned())
    {
        const SkeletonView* skeleton = context.Dependency<SkeletonView>(view.Skeleton());
        if (skeleton == nullptr)
        {
            return Status::Make(ErrorCode::DependencyTypeMismatch,
                                "the mesh's skeleton is not among its loaded dependencies");
        }
        ENGINE_TRY(view.CheckSkeleton(*skeleton));
    }
    // The layout: the attributes of binding 0 as the blob stores them, then the skin stream of a skinned mesh.
    renderer::VertexLayoutData layout;
    for (u32 i = 0; i < view.AttributeCount(); ++i)
    {
        const MeshAttribute a = view.Attribute(i);
        if (a.binding != 0)
        {
            continue;
        }
        if (layout.attribute_count >= rhi::kMaxVertexAttributes)
        {
            return Status::Make(ErrorCode::Corrupt, "the mesh has more vertex attributes than the RHI allows");
        }
        rhi::VertexAttributeDesc& d = layout.attributes[layout.attribute_count++];
        d.location = a.location;
        d.binding = 0;
        d.format = a.format;
        d.offset = a.offset;
    }
    layout.stride = view.VertexStride();
    if (view.Skinned())
    {
        layout = renderer::SkinnedVertexLayout(layout);
    }
    const MeshLod lod = view.Lod(0);
    const u32 width = view.IndexBytesPerIndex();
    renderer::Bounds bounds;
    const Array<f32, 3> lo = view.BoundsMin();
    const Array<f32, 3> hi = view.BoundsMax();
    bounds.box.min = Vec3(lo[0], lo[1], lo[2]);
    bounds.box.max = Vec3(hi[0], hi[1], hi[2]);
    bounds.sphere.center = Vec3((lo[0] + hi[0]) * 0.5f, (lo[1] + hi[1]) * 0.5f, (lo[2] + hi[2]) * 0.5f);
    const Vec3 half((hi[0] - lo[0]) * 0.5f, (hi[1] - lo[1]) * 0.5f, (hi[2] - lo[2]) * 0.5f);
    bounds.sphere.radius = Sqrt(half.x * half.x + half.y * half.y + half.z * half.z);
    const byte* indices = view.IndexBytes().Data() + static_cast<usize>(lod.first_index) * width;
    renderer::MeshGeometry& geometry = out->geometry;
    geometry.layout = layout;
    geometry.bytes = context.Buffer();
    geometry.vertex_offset = static_cast<u64>(view.VertexBytes().Data() - base);
    geometry.vertex_count = view.VertexCount();
    geometry.index_type = width == 2 ? rhi::IndexType::UInt16 : rhi::IndexType::UInt32;
    geometry.index_offset = static_cast<u64>(indices - base);
    geometry.index_count = lod.index_count;
    geometry.topology = rhi::PrimitiveTopology::TriangleList;
    geometry.bounds = bounds;
    // The material slots become index ranges of the first level of detail (slots lie in the blob's whole index list).
    if (view.SlotCount() != 0 && view.SlotCount() <= renderer::kMaxMeshRanges)
    {
        bool inside = true;
        for (u32 i = 0; i < view.SlotCount() && inside; ++i)
        {
            const MeshSlot slot = view.Slot(i);
            inside = slot.first_index >= lod.first_index && slot.index_count != 0 && slot.index_count % 3 == 0 &&
                     (slot.first_index - lod.first_index) % 3 == 0 &&
                     slot.first_index - lod.first_index <= lod.index_count &&
                     slot.index_count <= lod.index_count - (slot.first_index - lod.first_index);
        }
        if (inside)
        {
            for (u32 i = 0; i < view.SlotCount(); ++i)
            {
                const MeshSlot slot = view.Slot(i);
                geometry.ranges[i] = renderer::MeshRange{slot.first_index - lod.first_index, slot.index_count};
            }
            geometry.range_count = view.SlotCount();
        }
    }
    geometry.skin_offset = view.Skinned() ? static_cast<u64>(view.SkinBytes().Data() - base) : 0;
    geometry.skin_joint_count = view.Skinned() ? view.SkinJointCount() : 0;
    if (view.Skinned() && view.JointBoundsBytes().Size() == static_cast<usize>(view.SkinJointCount()) * 32)
    {
        geometry.joint_bounds_offset = static_cast<u64>(view.JointBoundsBytes().Data() - base);
        geometry.has_joint_bounds = true;  // the skinned object is bounded by its pose (2026-11-20)
    }
    // Morph targets (glTF coverage Day 7): the delta section is uploaded where it is; the runs of the targets are
    // small.
    if (view.MorphTargetCount() != 0)
    {
        geometry.morph_delta_offset = view.MorphDeltaCount() != 0
                                          ? static_cast<u64>(view.MorphDeltaBytes().Data() - base)
                                          : 0;
        geometry.morph_delta_count = view.MorphDeltaCount();
        geometry.morph_target_count = view.MorphTargetCount();
        for (u32 t = 0; t < view.MorphTargetCount(); ++t)
        {
            const MeshMorphTargetInfo info = view.MorphTarget(t);
            geometry.morph_targets[t] = renderer::MeshMorphRange{info.first_delta, info.delta_count};
        }
    }
    if (const char* refused = geometry.Validate())
    {
        return Status::Make(ErrorCode::Corrupt, refused);
    }
    char name[40];
    char hex[17];
    FormatResourceId(context.Id(), hex);
    FormatTo(name, sizeof(name), "mesh %s", hex);
    out->mesh = MakeShared<renderer::GpuMesh>();
    if (!out->mesh->CreateFromGeometry(gpu->device, geometry, gpu->resources, name))
    {
        return DeviceRefused("the device refused the mesh buffers, or the mesh could not register for a reset");
    }
    return Status::Success();
}

Status LoadGpuTexture(const LoadContext& context, GpuTextureAsset* out, void* user)
{
    GpuAssetContext* gpu = static_cast<GpuAssetContext*>(user);
    ENGINE_TRY(out->view.Open(context.Bytes(), context.Types()));
    out->bytes = context.Buffer();
    const byte* base = context.Bytes().Data();
    Vector<renderer::TextureLevelSpan> levels;
    if (!levels.TryReserve(static_cast<usize>(out->view.Layers()) * out->view.MipCount()))
    {
        return Status::Make(ErrorCode::LimitExceeded, "out of memory for the level list");
    }
    for (u32 layer = 0; layer < out->view.Layers(); ++layer)
    {
        for (u32 mip = 0; mip < out->view.MipCount(); ++mip)
        {
            const TextureLevel level = out->view.Level(layer, mip);
            renderer::TextureLevelSpan span;
            span.offset = static_cast<u64>(level.bytes.Data() - base);
            span.bytes = level.bytes.Size();
            span.row_pitch = level.row_pitch;
            span.mip = mip;
            span.layer = layer;
            levels.Push(span);
        }
    }
    char name[40];
    char hex[17];
    FormatResourceId(context.Id(), hex);
    FormatTo(name, sizeof(name), "texture %s", hex);
    out->texture = MakeShared<renderer::GpuTexture>();
    if (!out->texture->CreateFromSharedLevels(gpu->device, out->view.Desc(rhi::TextureUsage::Sampled), context.Buffer(),
                                              Span<const renderer::TextureLevelSpan>(levels.Data(), levels.Size()),
                                              gpu->resources, name))
    {
        return DeviceRefused("the device refused the texture");
    }
    return Status::Success();
}

rhi::FilterMode FilterOf(MaterialFilter f)
{
    return f == MaterialFilter::Nearest ? rhi::FilterMode::Nearest : rhi::FilterMode::Linear;
}

rhi::AddressMode AddressOf(MaterialAddress a)
{
    switch (a)
    {
    case MaterialAddress::Repeat: return rhi::AddressMode::Repeat;
    case MaterialAddress::ClampToEdge: return rhi::AddressMode::ClampToEdge;
    case MaterialAddress::MirrorRepeat: return rhi::AddressMode::MirrorRepeat;
    }
    return rhi::AddressMode::Repeat;
}

f32 FloatAt(const byte* p)
{
    f32 v;
    MemCopy(&v, p, sizeof(v));
    return v;
}

Status LoadGpuMaterial(const LoadContext& context, GpuMaterialAsset* asset, void* user)
{
    asset->material = MakeShared<GpuMaterial>();
    GpuMaterial* out = asset->material.Get();
    GpuAssetContext* gpu = static_cast<GpuAssetContext*>(user);
    MaterialAssetData data;
    ENGINE_TRY(LoadMaterialBlob(context.Bytes(), context.Types(), &data));
    const renderer::GBufferTemplateInfo* info = renderer::FindGBufferTemplateInfo(data.template_name.text);
    if (info == nullptr)
    {
        return Status::Make(ErrorCode::Corrupt, "the material names a template the renderer does not have");
    }
    renderer::TemplateId tmpl = info->skinned
                                    ? (info->vertex_color ? gpu->templates.skinned_colored : gpu->templates.skinned)
                                    : (info->vertex_color ? gpu->templates.colored : gpu->templates.plain);
    if (!tmpl.IsValid())
    {
        return DeviceRefused("the renderer has no such template (the G-buffer shaders did not match)");
    }
    const char* options[kMaterialMaxOptions];
    for (usize i = 0; i < data.options.Size(); ++i)
    {
        options[i] = data.options[i].text;
    }
    renderer::PermutationKey key;
    renderer::ReflectionError reflection_error;
    if (!renderer::MakePermutationKey(*info->fragment_permutations, options, static_cast<u32>(data.options.Size()),
                                      &key, &reflection_error))
    {
        return Status::Make(ErrorCode::Corrupt, "the material names a permutation option the template does not have");
    }
    char name[40];
    char hex[17];
    FormatResourceId(context.Id(), hex);
    FormatTo(name, sizeof(name), "material %s", hex);
    out->materials = gpu->materials;
    out->flags = data.flags;
    out->id = gpu->materials->CreateMaterial(tmpl, key, name);
    if (!out->id.IsValid())
    {
        return DeviceRefused("the material system refused the material");
    }
    renderer::ApplyGBufferDefaults(gpu->materials, out->id);  // what the blob does not name keeps the template's value
    for (usize i = 0; i < data.params.Size(); ++i)
    {
        const MaterialParam& p = data.params[i];
        const byte* at = data.block.Data() + p.offset;
        bool ok = false;
        switch (p.type)
        {
        case MaterialParamType::Float: ok = gpu->materials->SetFloat(out->id, p.name.text, FloatAt(at)); break;
        case MaterialParamType::Int:
        {
            i32 v;
            MemCopy(&v, at, sizeof(v));
            ok = gpu->materials->SetInt(out->id, p.name.text, v);
            break;
        }
        case MaterialParamType::UInt:
        {
            u32 v;
            MemCopy(&v, at, sizeof(v));
            ok = gpu->materials->SetUInt(out->id, p.name.text, v);
            break;
        }
        case MaterialParamType::Vec2:
            ok = gpu->materials->SetVec2(out->id, p.name.text, Vec2(FloatAt(at), FloatAt(at + 4)));
            break;
        case MaterialParamType::Vec3:
        case MaterialParamType::Color3:
            ok = gpu->materials->SetVec3(out->id, p.name.text, Vec3(FloatAt(at), FloatAt(at + 4), FloatAt(at + 8)));
            break;
        case MaterialParamType::Vec4:
        case MaterialParamType::Color4:
            ok = gpu->materials->SetVec4(out->id, p.name.text,
                                         Vec4(FloatAt(at), FloatAt(at + 4), FloatAt(at + 8), FloatAt(at + 12)));
            break;
        case MaterialParamType::Mat4:
        {
            Mat4 m;
            for (u32 c = 0; c < 4; ++c)
            {
                m.col[c] = Vec4(FloatAt(at + 16 * c), FloatAt(at + 16 * c + 4), FloatAt(at + 16 * c + 8),
                                FloatAt(at + 16 * c + 12));
            }
            ok = gpu->materials->SetMat4(out->id, p.name.text, m);
            break;
        }
        }
        if (!ok)
        {
            return Status::Make(ErrorCode::Corrupt, "the material sets a parameter the template does not have");
        }
    }
    for (usize i = 0; i < data.textures.Size(); ++i)
    {
        const GpuTextureAsset* texture = context.Dependency<GpuTextureAsset>(data.textures[i].texture);
        if (texture == nullptr)
        {
            return Status::Make(ErrorCode::DependencyTypeMismatch,
                                "a material's texture is not loaded as a GPU texture");
        }
        if (!gpu->materials->SetTexture(out->id, data.textures[i].slot.text, texture->texture))
        {
            return Status::Make(ErrorCode::Corrupt, "the material binds a texture slot the template does not have");
        }
    }
    for (usize i = 0; i < data.samplers.Size(); ++i)
    {
        const MaterialSampler& s = data.samplers[i];
        rhi::SamplerDesc desc;
        desc.min_filter = FilterOf(s.min_filter);
        desc.mag_filter = FilterOf(s.mag_filter);
        desc.mip_filter = FilterOf(s.mip_filter);
        desc.address_u = AddressOf(s.address_u);
        desc.address_v = AddressOf(s.address_v);
        desc.max_anisotropy = static_cast<f32>(s.max_anisotropy);
        renderer::GpuSamplerPtr sampler = MakeShared<renderer::GpuSampler>();
        if (!sampler->Create(gpu->device, desc, gpu->resources, s.slot.text))
        {
            return DeviceRefused("the device refused a sampler");
        }
        if (!gpu->materials->SetSampler(out->id, s.slot.text, sampler))
        {
            return Status::Make(ErrorCode::Corrupt, "the material binds a sampler slot the template does not have");
        }
    }
    return Status::Success();
}

}  // namespace

Status RegisterGpuFallbacks(ResourceManager* manager)
{
    const TypeRegistry& types = manager->Types();
    // The unit cube.
    renderer::MeshData cube;
    if (renderer::MakeCubeMesh(1.0f, &cube) != nullptr)
    {
        return Status::Make(ErrorCode::Other, "the fallback cube could not be made");
    }
    Vector<MeshVertex> vertices;
    for (u32 i = 0; i < cube.VertexCount(); ++i)
    {
        const renderer::StandardVertex* v = static_cast<const renderer::StandardVertex*>(cube.Vertices()) + i;
        MeshVertex out;
        for (u32 k = 0; k < 3; ++k)
        {
            out.position[k] = v->position[k];
            out.normal[k] = v->normal[k];
        }
        for (u32 k = 0; k < 4; ++k)
        {
            out.tangent[k] = v->tangent[k];
        }
        out.uv[0] = v->uv[0];
        out.uv[1] = v->uv[1];
        vertices.Push(out);
    }
    Vector<u32> indices;
    for (u32 i = 0; i < cube.IndexCount(); ++i)
    {
        indices.Push(cube.IndexAt(i));
    }
    MeshBuildInput mesh_input;
    mesh_input.vertices = Span<const MeshVertex>(vertices.Data(), vertices.Size());
    mesh_input.indices = Span<const u32>(indices.Data(), indices.Size());
    Vector<byte> mesh_blob;
    ENGINE_TRY(BuildMeshBlob(types, ResourceId::FromValue(kFallbackMeshId), mesh_input, &mesh_blob));
    ENGINE_TRY(manager->RegisterFallback(types::kMesh, ByteSpan(mesh_blob.Data(), mesh_blob.Size())));

    // The checker: 8 x 8 texels, magenta and black, one level.
    Vector<byte> texels;
    for (u32 y = 0; y < 8; ++y)
    {
        for (u32 x = 0; x < 8; ++x)
        {
            const bool magenta = ((x / 2 + y / 2) & 1) == 0;
            texels.Push(magenta ? 255 : 0);
            texels.Push(0);
            texels.Push(magenta ? 255 : 0);
            texels.Push(255);
        }
    }
    const ByteSpan level(texels.Data(), texels.Size());
    TextureBuildInput texture_input;
    texture_input.format = rhi::Format::RGBA8_SRGB;
    texture_input.width = 8;
    texture_input.height = 8;
    texture_input.subresources = Span<const ByteSpan>(&level, 1);
    Vector<byte> texture_blob;
    ENGINE_TRY(BuildTextureBlob(types, ResourceId::FromValue(kFallbackTextureId), texture_input, &texture_blob));
    ENGINE_TRY(manager->RegisterFallback(types::kTexture, ByteSpan(texture_blob.Data(), texture_blob.Size())));

    // The material: the G-buffer template with a magenta base color.
    MaterialAssetData material;
    ENGINE_TRY(Status(MakeMaterialName("gbuffer", 7, &material.template_name)
                          ? Status::Success()
                          : Status::Make(ErrorCode::Other, "the fallback template name does not fit")));
    material.block.Resize(16);
    const f32 magenta[4] = {1.0f, 0.0f, 1.0f, 1.0f};
    MemCopy(material.block.Data(), magenta, sizeof(magenta));
    MaterialParam param;
    if (!MakeMaterialName("base_color", 10, &param.name))
    {
        return Status::Make(ErrorCode::Other, "the fallback parameter name does not fit");
    }
    param.type = MaterialParamType::Color4;
    param.offset = 0;
    param.size = 16;
    material.params.Push(param);
    Vector<byte> material_blob;
    ENGINE_TRY(BuildMaterialBlob(types, ResourceId::FromValue(kFallbackMaterialId), material, &material_blob));
    return manager->RegisterFallback(types::kMaterial, ByteSpan(material_blob.Data(), material_blob.Size()));
}

void RegisterGpuLoaders(ResourceManager* manager, GpuAssetContext* context)
{
    manager->RegisterLoader<GpuMeshAsset>(&LoadGpuMesh, context);
    manager->RegisterLoader<GpuTextureAsset>(&LoadGpuTexture, context);
    manager->RegisterLoader<GpuMaterialAsset>(&LoadGpuMaterial, context);
}

bool MakeHeightTexture(rhi::Device* device, const HeightMapView& view, renderer::DeviceResources* resources,
                       renderer::GpuTexturePtr* out)
{
    out->Reset();
    if (device == nullptr || view.Bytes() == nullptr || view.MipCount() == 0)
    {
        return false;
    }
    rhi::TextureDesc desc;
    desc.width = view.Header().width;
    desc.height = view.Header().height;
    desc.mip_levels = view.MipCount();
    desc.format = rhi::Format::R16_UNorm;
    desc.usage = rhi::TextureUsage::Sampled;
    Vector<renderer::TextureLevelSpan> levels;
    if (!levels.TryReserve(view.MipCount()))
    {
        return false;
    }
    for (u32 mip = 0; mip < view.MipCount(); ++mip)
    {
        renderer::TextureLevelSpan span;
        span.offset = view.MipOffset(mip);
        span.bytes = static_cast<u64>(view.MipWidth(mip)) * view.MipHeight(mip) * 2;
        span.mip = mip;
        levels.Push(span);
    }
    renderer::GpuTexturePtr texture = MakeShared<renderer::GpuTexture>();
    if (!texture->CreateFromSharedLevels(device, desc, view.Bytes(),
                                         Span<const renderer::TextureLevelSpan>(levels.Data(), levels.Size()),
                                         resources, "height map"))
    {
        return false;
    }
    *out = texture;
    return true;
}

bool MakeTerrainSource(rhi::Device* device, const HeightMapView& view, renderer::DeviceResources* resources,
                       renderer::TerrainSourcePtr* out)
{
    out->Reset();
    if (device == nullptr || view.Bytes() == nullptr || view.MipCount() == 0)
    {
        return false;
    }
    renderer::TerrainSourcePtr source = MakeShared<renderer::TerrainSource>();
    renderer::TerrainTilesDesc desc;
    view.MakeTilesDesc(&desc);
    if (!source->tiles.Build(desc) || !MakeHeightTexture(device, view, resources, &source->height))
    {
        return false;
    }
    source->samples_x = view.Header().width;
    source->samples_z = view.Header().height;
    source->cell_size = view.Header().cell_size;
    source->range_min = view.Header().range_min;
    source->range_max = view.Header().range_max;
    *out = source;
    return true;
}

}  // namespace vse::asset
