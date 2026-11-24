#include "cstr/cstr.h"
#include "cstr/mem_ops.h"
#include "renderer/gbuffer.h"

#include "algorithm.h"

#include "lang/scalar.h"
#include "cstr/varargs.h"

#include "lang/new.h"

#include "fullscreen.vert.dxil.inc"
#include "gbuffer.frag.perm.inc"
#include "gbuffer.vert.perm.inc"
#include "gbuffer_debug.frag.dxil.inc"
#include "morph.comp.refl.inc"  // the push block's size and offsets, checked below
#include "log/assert.h"
#include "renderer/renderer_log.h"
#include "cstr/format.h"

namespace vse::renderer
{

namespace
{

const char* FormatLabel(rhi::Format format)
{
    switch (format)
    {
    case rhi::Format::RGBA8_SRGB: return "RGBA8_SRGB";
    case rhi::Format::RG16_Float: return "RG16_Float";
    case rhi::Format::RGBA8_UNorm: return "RGBA8_UNorm";
    case rhi::Format::D32_Float: return "D32_Float";
    default: return "unknown";
    }
}

f32 SignNotZero(f32 v)
{
    return v >= 0.0f ? 1.0f : -1.0f;
}

// The location of the per-vertex color attribute in ColorVertexLayout.
constexpr u32 kColorLocation = 4;

bool HasColor(const VertexLayoutData& layout)
{
    for (u32 i = 0; i < layout.attribute_count; ++i)
    {
        if (layout.attributes[i].location == kColorLocation &&
            layout.attributes[i].format == rhi::VertexFormat::UNorm8x4)
        {
            return true;
        }
    }
    return false;
}

}  // namespace

bool CheckGBufferFormats(const rhi::Device* device, char* error, usize error_capacity)
{
    struct Need
    {
        rhi::Format format;
        rhi::TextureUsage usage;
        const char* usage_name;
    };
    const Need needs[] = {
        {kGBufferAlbedoFormat, rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled,
         "color attachment and sampled"},
        {kGBufferNormalFormat, rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled,
         "color attachment and sampled"},
        {kGBufferMaterialFormat, rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled,
         "color attachment and sampled"},
        {kGBufferEmissiveFormat, rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled,
         "color attachment and sampled"},
        {kGBufferDepthFormat, rhi::TextureUsage::DepthAttachment | rhi::TextureUsage::Sampled,
         "depth attachment and sampled"},
    };
    // The geometry pass writes four colors at once; D3D12 and Metal allow 8 (4 on some older mobile GPUs), so this
    // only fails on those.
    const PipelineTarget target = GBufferPipelineTarget();
    if (device->Caps().max_color_attachments < target.color_count)
    {
        if (error != nullptr && error_capacity > 0)
        {
            FormatTo(error, error_capacity,
                     "G-buffer: the device allows %u color attachments and the geometry pass writes %u",
                     device->Caps().max_color_attachments, target.color_count);
        }
        return false;
    }
    for (const Need& need : needs)
    {
        if (!device->FormatSupport(need.format, need.usage))
        {
            if (error != nullptr && error_capacity > 0)
            {
                FormatTo(error, error_capacity, "G-buffer: the device does not support format %s as %s",
                         FormatLabel(need.format), need.usage_name);
            }
            return false;
        }
    }
    if (error != nullptr && error_capacity > 0)
    {
        error[0] = '\0';
    }
    return true;
}

PipelineTarget GBufferPipelineTarget()
{
    PipelineTarget t;
    t.color_formats[0] = kGBufferAlbedoFormat;
    t.color_formats[1] = kGBufferNormalFormat;
    t.color_formats[2] = kGBufferMaterialFormat;
    t.color_formats[3] = kGBufferEmissiveFormat;
    t.color_count = 4;
    t.depth_format = kGBufferDepthFormat;
    return t;
}

Vec2 OctEncode(const Vec3& n)
{
    const f32 sum = Abs(n.x) + Abs(n.y) + Abs(n.z);
    f32 px = n.x / sum;
    f32 py = n.y / sum;
    if (n.z < 0.0f)
    {
        const f32 nx = (1.0f - Abs(py)) * SignNotZero(px);
        const f32 ny = (1.0f - Abs(px)) * SignNotZero(py);
        px = nx;
        py = ny;
    }
    return Vec2(px, py);
}

Vec3 OctDecode(const Vec2& f)
{
    f32 x = f.x;
    f32 y = f.y;
    const f32 z = 1.0f - Abs(x) - Abs(y);
    if (z < 0.0f)
    {
        const f32 nx = (1.0f - Abs(y)) * SignNotZero(x);
        const f32 ny = (1.0f - Abs(x)) * SignNotZero(y);
        x = nx;
        y = ny;
    }
    const f32 len = Sqrt(x * x + y * y + z * z);
    return Vec3(x / len, y / len, z / len);
}

f32 SrgbToLinear(f32 c)
{
    return c <= 0.04045f ? c / 12.92f : Pow((c + 0.055f) / 1.055f, 2.4f);
}

f32 LinearizeDepth(f32 depth, f32 near_plane, f32 far_plane)
{
    return near_plane * far_plane / (far_plane - depth * (far_plane - near_plane));
}

void FitCameraDepthRange(const RenderScene& scene, Camera* camera, f32 min_near)
{
    ENGINE_ASSERT(camera != nullptr && min_near > 0.0f,
                  "FitCameraDepthRange: camera is null or min_near is not positive");
    const Vec3 forward = camera->Forward();
    f32 nearest = 0.0f;
    f32 farthest = 0.0f;
    bool any = false;
    for (u32 slot = 0; slot < scene.SlotCount(); ++slot)
    {
        if (!scene.SlotAlive(slot))
        {
            continue;
        }
        const BoundingSphere& s = scene.ObjectAt(slot).bounds.sphere;
        const Vec3 d = s.center - camera->position;
        const f32 along = d.x * forward.x + d.y * forward.y + d.z * forward.z;
        const f32 lo = along - s.radius;
        const f32 hi = along + s.radius;
        if (!any || lo < nearest)
        {
            nearest = lo;
        }
        if (!any || hi > farthest)
        {
            farthest = hi;
        }
        any = true;
    }
    if (!any)
    {
        return;
    }
    camera->near_plane = nearest > min_near ? nearest : min_near;
    camera->far_plane = farthest > camera->near_plane * 2.0f ? farthest : camera->near_plane * 2.0f;
}

namespace
{

// The metallic-roughness and occlusion maps (glTF coverage Day 1) are linear; unset, they get the white default texture
// (by name: renderer::MaterialSystem), so the scalar parameters alone decide, as before the maps existed.
constexpr TemplateTextureSlot kGBufferSlots[5] = {
    {"albedo_tex", SlotShape::Texture2D, SlotColorSpace::Srgb},
    {"normal_tex", SlotShape::Texture2D, SlotColorSpace::Linear},
    {"metallic_roughness_tex", SlotShape::Texture2D, SlotColorSpace::Linear},
    {"occlusion_tex", SlotShape::Texture2D, SlotColorSpace::Linear},
    {"emissive_tex", SlotShape::Texture2D, SlotColorSpace::Srgb}};

constexpr TemplateParamDefault kGBufferParamDefaults[10] = {
    {"base_color", {1, 1, 1, 1}},          {"roughness", {0.5f, 0, 0, 0}},      {"metallic", {0, 0, 0, 0}},
    {"emissive_color", {0, 0, 0, 1}},      {"alpha_cutoff", {0.5f, 0, 0, 0}},   {"occlusion", {1, 0, 0, 0}},
    {"albedo_uv", {1, 1, 0, 0}},           {"albedo_uv_outside", {0, 0, 0, 0}}, {"occlusion_strength", {1, 0, 0, 0}},
    {"emissive_from_albedo", {0, 0, 0, 0}}};

const GBufferTemplateInfo kGBufferTemplateInfos[5] = {
    {"gbuffer", false, false, &shaders::kGbufferFragPermutations, kGBufferSlots, 5, kGBufferParamDefaults, 10},
    {"gbuffer_color", false, true, &shaders::kGbufferFragPermutations, kGBufferSlots, 5, kGBufferParamDefaults, 10},
    {"gbuffer_skinned", true, false, &shaders::kGbufferFragPermutations, kGBufferSlots, 5, kGBufferParamDefaults, 10},
    {"gbuffer_skinned_color", true, true, &shaders::kGbufferFragPermutations, kGBufferSlots, 5, kGBufferParamDefaults,
     10},
    {"gbuffer_instanced", false, true, &shaders::kGbufferFragPermutations, kGBufferSlots, 5, kGBufferParamDefaults, 10},
};

}  // namespace

const GBufferTemplateInfo* FindGBufferTemplateInfo(const char* name)
{
    if (name == nullptr)
    {
        return nullptr;
    }
    for (const GBufferTemplateInfo& info : kGBufferTemplateInfos)
    {
        if (CStrCompare(info.name, name) == 0)
        {
            return &info;
        }
    }
    return nullptr;
}

GBufferTemplates CreateGBufferTemplates(MaterialSystem* materials)
{
    ENGINE_ASSERT(materials != nullptr, "CreateGBufferTemplates: materials is null");
    // The emissive map is white when unset (its colour comes from `emissive_color`, black by default), not the black
    // its name would pick.
    const TextureDefault defaults[5] = {{"albedo_tex", DefaultTexture::White},
                                        {"normal_tex", DefaultTexture::FlatNormal},
                                        {"metallic_roughness_tex", DefaultTexture::White},
                                        {"occlusion_tex", DefaultTexture::White},
                                        {"emissive_tex", DefaultTexture::White}};
    TemplateId ids[5];  // plain, colored, skinned, skinned_colored, instanced
    for (u32 variant = 0; variant < 5; ++variant)
    {
        const bool instanced = variant == 4;
        const bool colored = !instanced && (variant & 1) != 0;
        const bool skinned = !instanced && (variant & 2) != 0;
        PermutationKey vkey;
        ReflectionError error;
        const char* names[2];
        u32 name_count = 0;
        if (colored)
        {
            names[name_count++] = "VERTEX_COLOR";
        }
        if (skinned)
        {
            names[name_count++] = "SKINNED";
        }
        if (instanced)
        {
            names[name_count++] = "INSTANCED";
        }
        const PermutationModule* vertex = nullptr;
        if (!MakePermutationKey(shaders::kGbufferVertPermutations, name_count != 0 ? names : nullptr, name_count, &vkey,
                                &error))
        {
            RenderLog(LogVerbosity::Error, "G-buffer: vertex shader permutation: %s", error.message);
        }
        else
        {
            vertex = FindPermutation(shaders::kGbufferVertPermutations, vkey, &error);
            if (vertex == nullptr)
            {
                RenderLog(LogVerbosity::Error, "G-buffer: vertex shader permutation: %s", error.message);
            }
        }
        TemplateId id;
        if (vertex != nullptr)
        {
            MaterialTemplateDesc d;
            static const char* const kNames[5] = {"gbuffer", "gbuffer_color", "gbuffer_skinned",
                                                  "gbuffer_skinned_color", "gbuffer_instanced"};
            d.name = kNames[variant];
            d.vertex = *vertex;
            d.fragment_permutations = &shaders::kGbufferFragPermutations;
            d.state.cull = rhi::CullMode::Back;
            d.state.depth_test = true;
            d.state.depth_write = true;
            d.state.depth_compare = rhi::CompareOp::Less;
            d.texture_defaults = defaults;
            d.texture_default_count = 5;
            d.skinned = skinned;
            d.instanced = instanced;
            id = materials->CreateTemplate(d);
        }
        if (!id.IsValid() && instanced)
        {
            RenderLog(LogVerbosity::Warn, "G-buffer: the instanced template could not be created; instanced meshes "
                                          "will not draw");
            continue;
        }
        if (!id.IsValid() && skinned)
        {
            // A device without bytecode for the skinned modules (a format not minted yet) still draws rigid meshes.
            RenderLog(LogVerbosity::Warn,
                      "G-buffer: the %s template could not be created; skinned meshes will not draw",
                      colored ? "skinned color" : "skinned");
            continue;
        }
        if (!id.IsValid())
        {
            for (u32 i = 0; i < variant; ++i)
            {
                if (ids[i].IsValid())
                {
                    materials->DestroyTemplate(ids[i]);
                }
            }
            return GBufferTemplates{};
        }
        ids[variant] = id;
    }
    GBufferTemplates out;
    out.plain = ids[0];
    out.colored = ids[1];
    out.skinned = ids[2];
    out.skinned_colored = ids[3];
    out.instanced = ids[4];
    return out;
}

MaterialId CreateGBufferMaterial(MaterialSystem* materials, const GBufferTemplates& templates,
                                 const GBufferOptions& options, const char* name)
{
    ENGINE_ASSERT(materials != nullptr, "CreateGBufferMaterial: materials is null");
    if (options.instanced && (options.skinned || options.vertex_color))
    {
        RenderLog(LogVerbosity::Error, "G-buffer: an instanced material takes its colour from the instance's tint; "
                                       "it cannot also be skinned or use vertex colors");
        return MaterialId{};
    }
    const TemplateId tmpl = options.instanced ? templates.instanced
                            : options.skinned ? (options.vertex_color ? templates.skinned_colored : templates.skinned)
                                              : (options.vertex_color ? templates.colored : templates.plain);
    if (!tmpl.IsValid())
    {
        RenderLog(LogVerbosity::Error, "G-buffer: the %s%s template is invalid", options.skinned ? "skinned " : "",
                  options.vertex_color ? "vertex color" : "plain");
        return MaterialId{};
    }
    const char* names[3];
    u32 count = 0;
    if (options.normal_map)
    {
        names[count++] = "NORMAL_MAP";
    }
    if (options.alpha_test)
    {
        names[count++] = "ALPHA_TEST";
    }
    if (options.vertex_color || options.instanced)
    {
        names[count++] = "VERTEX_COLOR";
    }
    PermutationKey key;
    ReflectionError error;
    if (!MakePermutationKey(shaders::kGbufferFragPermutations, names, count, &key, &error))
    {
        RenderLog(LogVerbosity::Error, "G-buffer: fragment shader permutation: %s", error.message);
        return MaterialId{};
    }
    const MaterialId id = materials->CreateMaterial(tmpl, key, name != nullptr ? name : "gbuffer");
    if (!id.IsValid())
    {
        return MaterialId{};
    }
    ApplyGBufferDefaults(materials, id);
    return id;
}

void ApplyGBufferDefaults(MaterialSystem* materials, MaterialId id)
{
    ENGINE_ASSERT(materials != nullptr, "ApplyGBufferDefaults: materials is null");
    materials->SetVec4(id, "base_color", Vec4(1, 1, 1, 1));
    materials->SetFloat(id, "roughness", 0.5f);
    materials->SetFloat(id, "metallic", 0.0f);
    materials->SetVec4(id, "emissive_color", Vec4(0, 0, 0, 1));
    materials->SetFloat(id, "alpha_cutoff", 0.5f);
    materials->SetFloat(id, "occlusion", 1.0f);
    materials->SetUInt(id, "flags", 0);
    materials->SetVec4(id, "albedo_uv", Vec4(1, 1, 0, 0));
    materials->SetVec4(id, "albedo_uv_outside", Vec4(0, 0, 0, 0));
    materials->SetFloat(id, "occlusion_strength", 1.0f);
    materials->SetFloat(id, "emissive_from_albedo", 0.0f);
}

// ---- GBufferPass ----

struct GBufferPass::SortEntry
{
    u64 key = 0;
    u32 slot = 0;
    rhi::PipelineHandle pipeline;
    rhi::BindGroupHandle material_group;
    const GpuMesh* mesh = nullptr;
    u32 joint_offset = kNoJoints;
    u32 first_index = 0;
    u32 index_count = 0;
    u32 morph_job = 0xFFFFFFFFu;  // GBufferPass::kNoMorphJob
};

namespace
{

// The frame's morph buffer stays below this; an object past it draws its base shape (counted).
constexpr u64 kMaxMorphBytes = u64{256} << 20;

// The word offset of the f32 attribute at `location` in binding 0 (Validate checked the formats).
u32 AttributeWord(const VertexLayoutData& layout, u32 location)
{
    for (u32 i = 0; i < layout.attribute_count; ++i)
    {
        if (layout.attributes[i].binding == 0 && layout.attributes[i].location == location)
        {
            return layout.attributes[i].offset / 4;
        }
    }
    return 0;
}

}  // namespace

GBufferPass::GBufferPass(rhi::Device* device, MaterialSystem* materials, FrameResources* frame)
    : materials_(materials), frame_(frame)
{
    ENGINE_ASSERT(device != nullptr && materials != nullptr && frame != nullptr,
                  "GBufferPass: device, materials or frame is null");
    const u32 cap = frame->MaxObjects();
    if (!draws_.TryResize(cap) || !slots_.TryResize(cap) || !joint_offsets_.TryResize(cap) ||
        !sort_scratch_.TryResize(cap) || !order_.TryResize(cap) || !order_scratch_.TryReserve(cap))
    {
        RenderLog(LogVerbosity::Error, "GBufferPass: out of memory for %u objects", cap);
        draws_.Reset();
        slots_.Reset();
        joint_offsets_.Reset();
        sort_scratch_.Reset();
        order_.Reset();
        order_scratch_.Reset();
    }
    else
    {
        storage_ok_ = true;
    }
}

GBufferPass::~GBufferPass() = default;

void GBufferPass::Reject(const char* fmt, ...)
{
    ++stats_.rejected;
    if (error_[0] != '\0')
    {
        return;  // the first message is kept; the rest are counted
    }
    VarArgs args;
    ENGINE_VA_START(args, fmt);
    VFormatTo(error_, sizeof(error_), fmt, args);
    ENGINE_VA_END(args);
}

bool GBufferPass::Prepare(const RenderScene& scene, const Frustum& frustum)
{
    stats_ = GBufferStats{};
    error_[0] = '\0';
    draw_count_ = 0;
    added_ = false;
    if (!storage_ok_)
    {
        FormatTo(error_, sizeof(error_), "G-buffer: the pass could not allocate its draw list");
        return false;
    }
    const u32 cap = frame_->MaxObjects();

    // The per-frame groups are cached by the material system; creating them is rare.
    frame_group_ = materials_->FrameBindGroup(frame_->FrameRegion());
    object_group_ = materials_->ObjectBindGroup(frame_->ObjectRegion());
    joint_group_ = rhi::BindGroupHandle{};     // made on the first skinned object of the frame
    instance_group_ = rhi::BindGroupHandle{};  // made on the first instanced object of the frame
    morph_jobs_.Clear();
    morph_bytes_ = 0;
    morph_buffer_ = GraphResource{};
    object_index_stream_ = materials_
                               ->ObjectIndexStream();  // valid only where shaders read the index from it (Direct3D 12)
    if (object_index_stream_.IsValid() && cap > kMaxObjectIndices)
    {
        // Draw i reads entry i of the stream: a frame with more objects than it holds would read past it (Vulkan has no
        // such limit).
        FormatTo(error_, sizeof(error_),
                 "G-buffer: %u objects per frame is more than the object index stream holds (%u)", cap,
                 kMaxObjectIndices);
        return false;
    }
    if (!frame_group_.IsValid() || !object_group_.IsValid())
    {
        FormatTo(error_, sizeof(error_), "G-buffer: the frame or object bind group could not be created");
        return false;
    }

    // Cull into a scratch array sized for the scene; the pass's own arrays hold at most FrameResources::MaxObjects().
    const u32 scene_slots = scene.SlotCount();
    u32* visible_slots = slots_.Data();
    if (scene_slots > cap)
    {
        if (cull_slots_.Size() < scene_slots && !cull_slots_.TryResize(scene_slots))
        {
            FormatTo(error_, sizeof(error_), "G-buffer: out of memory for the cull list");
            return false;
        }
        visible_slots = cull_slots_.Data();
    }
    const u32 visible = scene.Cull(frustum, visible_slots, scene_slots);
    stats_.visible = visible;

    SortEntry* entries = sort_scratch_.Data();
    u32 entry_count = 0;
    struct PaletteShare
    {
        const f32* matrices = nullptr;
        u32 count = 0;
        u32 offset = kNoJoints;
    };
    constexpr u32 kPaletteCache = 8;
    PaletteShare palette_cache[kPaletteCache];
    u32 palette_next = 0;
    const PipelineTarget target = GBufferPipelineTarget();
    for (u32 i = 0; i < visible; ++i)
    {
        const u32 slot = visible_slots[i];
        const RenderObject& object = scene.ObjectAt(slot);
        if (entry_count == cap)
        {
            Reject("G-buffer: more visible objects (%u) than the frame resources hold (%u)", visible, cap);
            continue;
        }
        const GpuMesh* mesh = object.mesh;
        if (mesh == nullptr || !mesh->Valid())
        {
            Reject("G-buffer: the object in slot %u has no mesh", slot);
            continue;
        }
        if (!mesh->Ready())
        {
            Reject(
                "G-buffer: the mesh of the object in slot %u is not prepared: record GpuMesh::RecordPrepare and call MarkReady after the submit",
                slot);
            continue;
        }
        const MaterialId material = materials_->MaterialAtIndex(object.material);
        if (!material.IsValid())
        {
            Reject("G-buffer: the object in slot %u uses material %u, which does not exist", slot, object.material);
            continue;
        }
        const bool mesh_color = HasColor(mesh->Layout());
        const bool material_color = materials_->ReadsVertexInput(material, kColorLocation);
        if (mesh_color != material_color)
        {
            if (mesh_color)
            {
                Reject(
                    "G-buffer: the object in slot %u has a mesh with vertex colors but material %u does not use them (create the material with GBufferOptions::vertex_color)",
                    slot, object.material);
            }
            else
            {
                Reject("G-buffer: the object in slot %u has a mesh without vertex colors but material %u expects them",
                       slot, object.material);
            }
            continue;
        }
        const PipelineLookup pipeline = materials_->GetPipeline(material, target, mesh->Layout(), mesh->Topology(),
                                                                mesh->HasPrimitiveRestart());
        if (!pipeline.pipeline.IsValid())
        {
            Reject("G-buffer: no pipeline could be built for the object in slot %u with material %u", slot,
                   object.material);
            continue;
        }
        u32 joint_offset = kNoJoints;
        if (mesh->Skinned())
        {
            if (object.joint_matrices == nullptr || object.joint_count == 0)
            {
                Reject("G-buffer: the skinned object in slot %u has no joint palette", slot);
                continue;
            }
            if (object.joint_count < mesh->SkinJointCount())
            {
                // The shader would read the matrices of the next object (or last frame's) for the missing joints.
                Reject(
                    "G-buffer: the skinned object in slot %u has a palette of fewer matrices than its mesh has joints",
                    slot);
                continue;
            }
            if (!joint_group_.IsValid())
            {
                joint_group_ = materials_->JointBindGroup(frame_->JointRegion());
                if (!joint_group_.IsValid())
                {
                    Reject("G-buffer: the joint palette bind group could not be created");
                    continue;
                }
            }
            // The objects of one entity (one per material slot) name the same palette: it is written once.
            u32 cached = kPaletteCache;
            for (u32 c = 0; c < kPaletteCache; ++c)
            {
                if (palette_cache[c].matrices == object.joint_matrices && palette_cache[c].count == object.joint_count)
                {
                    cached = c;
                    break;
                }
            }
            if (cached != kPaletteCache)
            {
                joint_offset = palette_cache[cached].offset;
            }
            else
            {
                joint_offset = frame_->WriteJoints(object.joint_matrices, object.joint_count);
                if (joint_offset != kNoJoints)
                {
                    palette_cache[palette_next] = PaletteShare{object.joint_matrices, object.joint_count, joint_offset};
                    palette_next = (palette_next + 1) % kPaletteCache;
                }
            }
            if (joint_offset == kNoJoints)
            {
                ++stats_.skinned_dropped;  // the budget is full: this object is left out whole
                continue;
            }
        }
        // Morph targets (glTF coverage Day 7): the morph pass writes this object's vertices; the slots of one entity
        // name the same mesh and weights and share one job.
        u32 morph_job = kNoMorphJob;
        if (mesh->MorphTargetCount() != 0 && object.morph_weights != nullptr && object.morph_weight_count != 0 &&
            mesh->MorphBuffer().IsValid() && materials_->MorphPipeline().IsValid())
        {
            for (usize j = 0; j < morph_jobs_.Size(); ++j)
            {
                if (morph_jobs_[j].mesh == mesh && morph_jobs_[j].weights == object.morph_weights)
                {
                    morph_job = static_cast<u32>(j);
                    break;
                }
            }
            if (morph_job == kNoMorphJob)
            {
                // The largest |weight| first (ties by target), zero weights skipped, at most kMaxActiveMorphTargets.
                const u32 count = object.morph_weight_count < mesh->MorphTargetCount() ? object.morph_weight_count
                                                                                       : mesh->MorphTargetCount();
                // The push block as the CPU fills it: eight header words, then three words per active target, in
                // exactly the block shaders/morph.comp declares and the pipeline binds (160 bytes against 128 until
                // glTF coverage Day 14).
                static_assert(sizeof(MorphJob::push) == shaders::kMorphCompPushSize, "the morph push block's size");
                static_assert(MaterialSystem::kMorphPushBytes == shaders::kMorphCompPushSize, "the morph push range");
                static_assert(shaders::kMorphCompPushTargetsOffset == 8 * 4, "the morph push block's header words");
                static_assert(8 + 3 * kMaxActiveMorphTargets <= shaders::kMorphCompPushSize / 4, "the targets fit");
                Array<u32, kMaxActiveMorphTargets> chosen = {};
                u32 chosen_count = 0;
                u32 nonzero = 0;
                for (u32 t = 0; t < count; ++t)
                {
                    const f32 w = object.morph_weights[t];
                    if (!(w != 0.0f) || !IsFinite(w))
                    {
                        continue;
                    }
                    ++nonzero;
                    u32 at = chosen_count;
                    while (at > 0 && Abs(object.morph_weights[chosen[at - 1]]) < Abs(w))
                    {
                        --at;
                    }
                    if (at >= kMaxActiveMorphTargets)
                    {
                        continue;
                    }
                    const u32 last = chosen_count < kMaxActiveMorphTargets ? chosen_count : kMaxActiveMorphTargets - 1;
                    for (u32 k = last; k > at; --k)
                    {
                        chosen[k] = chosen[k - 1];
                    }
                    chosen[at] = t;
                    chosen_count = chosen_count < kMaxActiveMorphTargets ? chosen_count + 1 : chosen_count;
                }
                stats_.morph_targets_dropped += nonzero - chosen_count;
                const u32 stride_words = mesh->Layout().stride / 4;
                const u64 bytes = static_cast<u64>(mesh->VertexCount()) * mesh->Layout().stride;
                if (chosen_count != 0 && morph_bytes_ + bytes <= kMaxMorphBytes)
                {
                    MorphJob job;
                    job.mesh = mesh;
                    job.weights = object.morph_weights;
                    job.out_offset = morph_bytes_;
                    job.push[0] = mesh->VertexCount();
                    job.push[1] = stride_words;
                    job.push[2] = AttributeWord(mesh->Layout(), 0);
                    job.push[3] = AttributeWord(mesh->Layout(), 1);
                    job.push[4] = AttributeWord(mesh->Layout(), 2);
                    job.push[5] = mesh->VertexCount() * stride_words;
                    job.push[6] = static_cast<u32>(morph_bytes_ / 4);
                    job.push[7] = chosen_count;
                    for (u32 k = 0; k < chosen_count; ++k)
                    {
                        const MeshMorphRange range = mesh->MorphTarget(chosen[k]);
                        const f32 w = object.morph_weights[chosen[k]];
                        u32 bits = 0;
                        MemCopy(&bits, &w, 4);
                        // Three words per target (shaders/morph.comp): eight fill the 32 words, 128 bytes, exactly.
                        job.push[8 + k * 3] = range.first_delta;
                        job.push[8 + k * 3 + 1] = range.delta_count;
                        job.push[8 + k * 3 + 2] = bits;
                    }
                    morph_bytes_ += bytes;
                    morph_job = static_cast<u32>(morph_jobs_.Size());
                    morph_jobs_.Push(job);
                    ++stats_.morphed_objects;
                }
                else if (chosen_count != 0)
                {
                    stats_.morph_targets_dropped += chosen_count;  // the budget is full: the base shape
                }
            }
        }
        SortEntry& e = entries[entry_count++];
        e.joint_offset = joint_offset;
        e.morph_job = morph_job;
        e.key = MakeDrawSortKey(pipeline.index, material.index, mesh->SortId());
        e.slot = slot;
        e.pipeline = pipeline.pipeline;
        e.material_group = materials_->MaterialBindGroup(material);
        e.mesh = mesh;
        e.first_index = object.first_index;
        e.index_count = object.index_count;
        DrawOrder& o = order_[entry_count - 1];
        o.key = e.key;
        o.tie = (u64{object.first_index} << 32) | object.index_count;  // first index, then count
        o.slot = slot;
        o.entry = entry_count - 1;
    }

    // The draw order: by key, then first index, index count and slot (a total order: the slot breaks every tie, so the
    // order is the same on every platform). The engine's own merge sort over 24-byte proxies, whose comparison is
    // three integer compares: the 56-byte entries are not moved, and the loops below read them in the proxies' order.
    // It replaced the C library's qsort, whose speed is the platform's; a merge wins here because few distinct keys
    // make most comparisons fall through to the tie-breaks, and it makes fewer of them (container audit sort
    // follow-up).
    StableSort(
        Span<DrawOrder>(order_.Data(), entry_count),
        [](const DrawOrder& x, const DrawOrder& y)
        {
            if (x.key != y.key)
            {
                return x.key < y.key;
            }
            if (x.tie != y.tie)
            {
                return x.tie < y.tie;
            }
            return x.slot < y.slot;
        },
        order_scratch_);
    for (u32 i = 0; i < entry_count; ++i)
    {
        const SortEntry& e = entries[order_[i].entry];
        slots_[i] = e.slot;
        joint_offsets_[i] = e.joint_offset;
    }
    if (stats_.skinned_dropped > 0)
    {
        RenderLog(
            LogVerbosity::Warn,
            "G-buffer: the joint palette is full (%u matrices): %u skinned objects were not drawn this frame; raise renderer.joint_palette_bytes",
            frame_->MaxJointMatrices(), stats_.skinned_dropped);
    }
    frame_->WriteObjects(scene, slots_.Data(), entry_count, joint_offsets_.Data());

    // Runs of the same material and mesh become one instanced draw; the object data is in the same order, so the
    // instances of a run are consecutive entries.
    rhi::PipelineHandle last_pipeline;
    rhi::BindGroupHandle last_group;
    for (u32 i = 0; i < entry_count; ++i)
    {
        const SortEntry& e = entries[order_[i].entry];
        if (draw_count_ > 0)
        {
            Draw& prev = draws_[draw_count_ - 1];
            if (prev.mesh == e.mesh && prev.material_group == e.material_group && prev.pipeline == e.pipeline &&
                prev.first_index == e.first_index && prev.index_count == e.index_count &&
                prev.morph_job == kNoMorphJob && e.morph_job == kNoMorphJob)
            {
                ++prev.instance_count;
                continue;
            }
        }
        Draw& d = draws_[draw_count_++];
        d.pipeline = e.pipeline;
        d.material_group = e.material_group;
        d.mesh = e.mesh;
        d.first_instance = i;
        d.instance_count = 1;
        d.first_index = e.first_index;
        d.index_count = e.index_count;
        d.skinned = e.mesh->Skinned();
        d.morph_job = e.morph_job;
        if (!(d.pipeline == last_pipeline))
        {
            ++stats_.pipeline_binds;
        }
        if (!(d.material_group == last_group))
        {
            ++stats_.material_binds;
        }
        last_pipeline = d.pipeline;
        last_group = d.material_group;
    }
    PrepareInstanced(scene, frustum, target);
    stats_.drawn_objects = entry_count;
    stats_.draws = draw_count_;
    return stats_.rejected == 0;
}

void GBufferPass::PrepareInstanced(const RenderScene& scene, const Frustum& frustum, const PipelineTarget& target)
{
    const u32 cap = frame_->MaxObjects();
    rhi::PipelineHandle last_pipeline;
    rhi::BindGroupHandle last_group;
    if (draw_count_ > 0)
    {
        last_pipeline = draws_[draw_count_ - 1].pipeline;
        last_group = draws_[draw_count_ - 1].material_group;
    }
    for (u32 slot = 0; slot < scene.InstancedSlotCount(); ++slot)
    {
        if (!scene.InstancedSlotAlive(slot))
        {
            continue;
        }
        const InstancedObject& obj = scene.InstancedAt(slot);
        if (obj.tree.Get() != nullptr)
        {
            if ((obj.flags & ObjectFlags::kHidden) == 0 && obj.tree->NodeCount() != 0)
            {
                PrepareTree(slot, obj, frustum, target, &last_pipeline, &last_group);
            }
            continue;
        }
        if ((obj.flags & ObjectFlags::kHidden) != 0 || obj.set->Count() == 0)
        {
            continue;
        }
        const GpuMesh* mesh = obj.mesh;
        if (!mesh->Valid() || !mesh->Ready())
        {
            Reject("G-buffer: the mesh of the instanced object in slot %u is missing or not prepared", slot);
            continue;
        }
        if (mesh->Skinned() || HasColor(mesh->Layout()) || mesh->MorphTargetCount() != 0)
        {
            Reject("G-buffer: the instanced object in slot %u has a skinned, morphed or vertex-colored mesh", slot);
            continue;
        }
        const MaterialId material = materials_->MaterialAtIndex(obj.material);
        if (!material.IsValid() || !materials_->IsInstanced(material))
        {
            Reject("G-buffer: the instanced object in slot %u needs a material made with GBufferOptions::instanced",
                   slot);
            continue;
        }
        if (object_index_stream_.IsValid())
        {
            Reject("G-buffer: instanced meshes are not drawn on this backend yet (slot %u)", slot);
            continue;
        }
        const PipelineLookup pipeline = materials_->GetPipeline(material, target, mesh->Layout(), mesh->Topology(),
                                                                mesh->HasPrimitiveRestart());
        if (!pipeline.pipeline.IsValid() || draw_count_ >= cap)
        {
            Reject("G-buffer: no pipeline or draw could be made for the instanced object in slot %u", slot);
            continue;
        }
        if (!instance_group_.IsValid())
        {
            instance_group_ = materials_->InstanceBindGroup(frame_->InstanceRegion());
            if (!instance_group_.IsValid())
            {
                Reject("G-buffer: the instance bind group could not be created");
                continue;
            }
        }
        // Cull in the set's own space: the planes and the camera are carried into it, so the records are read as
        // stored.
        const Quaternion inverse = obj.rotation.Conjugate();
        const f32 inverse_scale = 1.0f / obj.scale;
        InstanceCullParams params;
        for (u32 i = 0; i < 6; ++i)
        {
            const Plane& p = frustum.planes[i];
            params.frustum.planes[i].normal = inverse.RotateVector(p.normal);
            params.frustum.planes[i].d = (p.normal.Dot(obj.position) + p.d) * inverse_scale;
        }
        params.render_origin = Vec3d(0, 0, 0);
        params.camera_position = inverse.RotateVector(view_position_ - obj.position) * inverse_scale;
        params.mesh_radius = mesh->GetBounds().sphere.radius;
        params.max_distance = obj.max_distance * inverse_scale;
        params.min_size_ratio = obj.min_size_ratio;
        CullInstances(*obj.set.Get(), params, scheduler_, &cull_result_);
        const InstanceCullStats& cs = cull_result_.stats;
        stats_.instance_chunks_tested += cs.chunks_tested;
        stats_.instance_chunks_skipped += cs.chunks_skipped;
        stats_.instance_chunks_accepted += cs.chunks_accepted;
        stats_.instances_tested += cs.instances_tested;
        if (cs.instances_emitted == 0)
        {
            continue;
        }
        // Compose each visible instance with the set's transform and write it: one contiguous range, one draw.
        constexpr u32 kBatch = 256;
        GpuInstance batch[kBatch];
        u32 filled = 0;
        u32 first = kNoInstances;
        u32 written = 0;
        bool full = false;
        // The frame's buffer takes this many more; the rest of the visible instances is counted as dropped.
        const u32 room = frame_->MaxInstances() - frame_->InstancesWritten();
        const u32 limit = cs.instances_emitted < room ? cs.instances_emitted : room;
        u32 produced = 0;
        for (const InstanceRun& run : cull_result_.runs)
        {
            const InstanceChunkInfo info = obj.set->Chunk(run.chunk);
            const Span<const InstanceRecord> records = obj.set->Records(run.chunk);
            const Span<const InstanceExtra> extras = obj.set->Extras(run.chunk);
            for (u32 k = 0; k < run.count && produced < limit; ++k)
            {
                ++produced;
                const InstanceRecord& r = records[run.first + k];
                const Vec3d local = info.origin + Vec3d(r.position);
                const Vec3 world = obj.position +
                                   obj.rotation.RotateVector(Vec3(static_cast<f32>(local.x), static_cast<f32>(local.y),
                                                                  static_cast<f32>(local.z)) *
                                                             obj.scale);
                const Quaternion q = obj.rotation * r.rotation;
                GpuInstance& g = batch[filled++];
                g.position[0] = world.x;
                g.position[1] = world.y;
                g.position[2] = world.z;
                g.rotation[0] = q.x;
                g.rotation[1] = q.y;
                g.rotation[2] = q.z;
                g.rotation[3] = q.w;
                g.scale[0] = r.scale.x * obj.scale;
                g.scale[1] = r.scale.y * obj.scale;
                g.scale[2] = r.scale.z * obj.scale;
                if (extras.Size() != 0)
                {
                    const InstanceExtra& e = extras[run.first + k];
                    g.tint[0] = e.tint.x;
                    g.tint[1] = e.tint.y;
                    g.tint[2] = e.tint.z;
                    g.tint[3] = e.tint.w;
                    g.custom = e.custom;
                }
                else
                {
                    g.tint[0] = g.tint[1] = g.tint[2] = g.tint[3] = 1.0f;
                    g.custom = 0;
                }
                g.pad = 0;
                if (filled == kBatch)
                {
                    const u32 at = frame_->WriteInstances(batch, filled);
                    if (at == kNoInstances)
                    {
                        full = true;
                        break;
                    }
                    first = first == kNoInstances ? at : first;
                    written += filled;
                    filled = 0;
                }
            }
            if (full || produced == limit)
            {
                break;
            }
        }
        if (!full && filled != 0)
        {
            const u32 at = frame_->WriteInstances(batch, filled);
            if (at == kNoInstances)
            {
                full = true;
            }
            else
            {
                first = first == kNoInstances ? at : first;
                written += filled;
            }
        }
        stats_.instances_visible += written;
        if (written < cs.instances_emitted)
        {
            stats_.instances_dropped += cs.instances_emitted - written;
        }
        if (written == 0)
        {
            continue;
        }
        Draw& d = draws_[draw_count_++];
        d.pipeline = pipeline.pipeline;
        d.material_group = materials_->MaterialBindGroup(material);
        d.mesh = mesh;
        d.first_instance = first;
        d.instance_count = written;
        d.first_index = 0;
        d.index_count = 0;
        d.skinned = false;
        d.instanced = true;
        d.morph_job = kNoMorphJob;
        if (!(d.pipeline == last_pipeline))
        {
            ++stats_.pipeline_binds;
        }
        if (!(d.material_group == last_group))
        {
            ++stats_.material_binds;
        }
        last_pipeline = d.pipeline;
        last_group = d.material_group;
        ++stats_.instanced_sets;
    }
    if (stats_.instances_dropped > 0)
    {
        RenderLog(LogVerbosity::Warn,
                  "G-buffer: the instance buffer is full (%u instances): %u instances were not drawn",
                  frame_->MaxInstances(), stats_.instances_dropped);
    }
}

void GBufferPass::PrepareTree(u32 slot, const InstancedObject& obj, const Frustum& frustum,
                              const PipelineTarget& target, rhi::PipelineHandle* last_pipeline,
                              rhi::BindGroupHandle* last_group)
{
    if (object_index_stream_.IsValid())
    {
        Reject("G-buffer: instanced meshes are not drawn on this backend yet (slot %u)", slot);
        return;
    }
    const InstanceTree& tree = *obj.tree.Get();
    // Cull in the tree's own space, like a set: the planes and the camera are carried into it.
    const Quaternion inverse = obj.rotation.Conjugate();
    const f32 inverse_scale = 1.0f / obj.scale;
    TreeCullParams params;
    for (u32 i = 0; i < 6; ++i)
    {
        const Plane& p = frustum.planes[i];
        params.frustum.planes[i].normal = inverse.RotateVector(p.normal);
        params.frustum.planes[i].d = (p.normal.Dot(obj.position) + p.d) * inverse_scale;
    }
    params.render_origin = Vec3d(0, 0, 0);
    params.camera_position = inverse.RotateVector(view_position_ - obj.position) * inverse_scale;
    while (tree_lod_.Size() <= slot)
    {
        tree_lod_.Push(TreeLodState());
    }
    CullInstanceTree(tree, params, &tree_lod_[slot], scheduler_, &tree_cull_);
    stats_.instance_tree_nodes += tree_cull_.stats.nodes_emitted;
    if (tree_cull_.stats.nodes_emitted == 0)
    {
        return;
    }
    bool drew = false;
    Vector<InstanceRecord> records;
    Vector<InstanceExtra> extras;
    for (const TreeDrawRun& run : tree_cull_.runs)
    {
        const GpuMesh* mesh = run.mesh < kMaxSceneTreeMeshes ? obj.tree_meshes[run.mesh] : nullptr;
        if (mesh == nullptr || !mesh->Valid() || !mesh->Ready())
        {
            Reject("G-buffer: tree mesh %u of the instanced object in slot %u is missing or not prepared", run.mesh,
                   slot);
            continue;
        }
        if (mesh->Skinned() || HasColor(mesh->Layout()) || mesh->MorphTargetCount() != 0)
        {
            Reject("G-buffer: tree mesh %u of the instanced object in slot %u is skinned, morphed or vertex-colored",
                   run.mesh, slot);
            continue;
        }
        const u32 material_index = run.material_slot < kMaxSceneTreeMeshes ? obj.tree_materials[run.material_slot] : 0;
        const MaterialId material = materials_->MaterialAtIndex(material_index);
        if (!material.IsValid() || !materials_->IsInstanced(material))
        {
            Reject("G-buffer: material slot %u of the instanced tree in slot %u needs a material made with "
                   "GBufferOptions::instanced",
                   run.material_slot, slot);
            continue;
        }
        const PipelineLookup pipeline = materials_->GetPipeline(material, target, mesh->Layout(), mesh->Topology(),
                                                                mesh->HasPrimitiveRestart());
        if (!pipeline.pipeline.IsValid() || draw_count_ >= frame_->MaxObjects())
        {
            Reject("G-buffer: no pipeline or draw could be made for a run of the instanced tree in slot %u", slot);
            continue;
        }
        if (!instance_group_.IsValid())
        {
            instance_group_ = materials_->InstanceBindGroup(frame_->InstanceRegion());
            if (!instance_group_.IsValid())
            {
                Reject("G-buffer: the instance bind group could not be created");
                continue;
            }
        }
        CompactTreeNodes(tree, Span<const TreeVisibleNode>(tree_cull_.nodes.Data() + run.first, run.count),
                         Vec3d(0, 0, 0), &records, &extras);
        constexpr u32 kBatch = 256;
        GpuInstance batch[kBatch];
        u32 filled = 0;
        u32 first = kNoInstances;
        u32 written = 0;
        bool full = false;
        const u32 room = frame_->MaxInstances() - frame_->InstancesWritten();
        const u32 limit = records.Size() < room ? static_cast<u32>(records.Size()) : room;
        for (u32 k = 0; k < limit && !full; ++k)
        {
            const InstanceRecord& r = records[k];
            const Vec3 world = obj.position + obj.rotation.RotateVector(r.position * obj.scale);
            const Quaternion q = obj.rotation * r.rotation;
            const InstanceExtra& e = extras[k];
            GpuInstance& g = batch[filled++];
            g.position[0] = world.x;
            g.position[1] = world.y;
            g.position[2] = world.z;
            g.rotation[0] = q.x;
            g.rotation[1] = q.y;
            g.rotation[2] = q.z;
            g.rotation[3] = q.w;
            g.scale[0] = r.scale.x * obj.scale;
            g.scale[1] = r.scale.y * obj.scale;
            g.scale[2] = r.scale.z * obj.scale;
            g.tint[0] = e.tint.x;
            g.tint[1] = e.tint.y;
            g.tint[2] = e.tint.z;
            g.tint[3] = e.tint.w;
            g.custom = e.custom;
            g.pad = 0;
            if (filled == kBatch || k + 1 == limit)
            {
                const u32 at = frame_->WriteInstances(batch, filled);
                if (at == kNoInstances)
                {
                    full = true;
                    break;
                }
                first = first == kNoInstances ? at : first;
                written += filled;
                filled = 0;
            }
        }
        stats_.instances_visible += written;
        if (written < records.Size())
        {
            stats_.instances_dropped += static_cast<u32>(records.Size()) - written;
        }
        if (written == 0)
        {
            continue;
        }
        Draw& d = draws_[draw_count_++];
        d.pipeline = pipeline.pipeline;
        d.material_group = materials_->MaterialBindGroup(material);
        d.mesh = mesh;
        d.first_instance = first;
        d.instance_count = written;
        d.first_index = 0;
        d.index_count = 0;
        d.skinned = false;
        d.instanced = true;
        d.morph_job = kNoMorphJob;
        if (!(d.pipeline == *last_pipeline))
        {
            ++stats_.pipeline_binds;
        }
        if (!(d.material_group == *last_group))
        {
            ++stats_.material_binds;
        }
        *last_pipeline = d.pipeline;
        *last_group = d.material_group;
        ++stats_.instance_tree_runs;
        drew = true;
    }
    if (drew)
    {
        ++stats_.instance_trees;
    }
}

GBufferTargets GBufferPass::AddToGraph(RenderGraph* graph, u32 width, u32 height)
{
    ENGINE_ASSERT(graph != nullptr && width > 0 && height > 0,
                  "GBufferPass::AddToGraph: graph is null or the size is zero");
    ENGINE_ASSERT(
        !added_,
        "GBufferPass::AddToGraph: called twice since the last Prepare; one pass object serves one view per frame");
    added_ = true;
    rhi::TextureDesc td;
    td.width = width;
    td.height = height;
    td.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled | extra_usage_;
    td.format = kGBufferAlbedoFormat;
    td.debug_name = "gbuffer_albedo";
    targets_.albedo = graph->CreateTexture("gbuffer_albedo", td);
    td.format = kGBufferNormalFormat;
    td.debug_name = "gbuffer_normal";
    targets_.normal = graph->CreateTexture("gbuffer_normal", td);
    td.format = kGBufferMaterialFormat;
    td.debug_name = "gbuffer_material";
    targets_.material = graph->CreateTexture("gbuffer_material", td);
    td.format = kGBufferEmissiveFormat;
    td.debug_name = "gbuffer_emissive";
    targets_.emissive = graph->CreateTexture("gbuffer_emissive", td);
    td.format = kGBufferDepthFormat;
    td.usage = rhi::TextureUsage::DepthAttachment | rhi::TextureUsage::Sampled | extra_usage_;
    td.debug_name = "gbuffer_depth";
    targets_.depth = graph->CreateTexture("gbuffer_depth", td);
    width_ = width;
    height_ = height;
    // The morph pass (glTF coverage Day 7) writes the morphed objects' vertices before the G-buffer pass reads them.
    if (!morph_jobs_.Empty())
    {
        rhi::BufferDesc bd;
        bd.size = morph_bytes_;
        bd.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::Vertex;
        bd.location = rhi::MemoryLocation::GpuOnly;
        bd.debug_name = "morph_vertices";
        morph_buffer_ = graph->CreateBuffer("morph_vertices", bd);
        graph->AddPass("morph", &GBufferPass::RecordMorph, this).Write(morph_buffer_, GraphUsage::StorageWrite);
    }
    PassBuilder pass = graph->AddPass("gbuffer", &GBufferPass::Record, this);
    pass.Write(targets_.albedo, GraphUsage::ColorAttachment)
        .Write(targets_.normal, GraphUsage::ColorAttachment)
        .Write(targets_.material, GraphUsage::ColorAttachment)
        .Write(targets_.emissive, GraphUsage::ColorAttachment)
        .Write(targets_.depth, GraphUsage::DepthAttachment);
    if (!morph_jobs_.Empty())
    {
        pass.Read(morph_buffer_, GraphUsage::VertexBuffer);
    }
    return targets_;
}

void GBufferPass::RecordMorph(const PassExecuteContext& context, void* user)
{
    const GBufferPass* self = static_cast<const GBufferPass*>(user);
    rhi::CommandList* list = context.list;
    rhi::Device* device = self->materials_->Device();
    const rhi::BufferHandle out = context.graph->ResolvedBuffer(self->morph_buffer_);
    const rhi::PipelineHandle pipeline = self->materials_->MorphPipeline();
    if (!pipeline.IsValid() || !out.IsValid())
    {
        return;
    }
    list->PushDebugLabel("morph");
    list->SetPipeline(pipeline);
    for (usize j = 0; j < self->morph_jobs_.Size(); ++j)
    {
        const MorphJob& job = self->morph_jobs_[j];
        rhi::BindGroupEntry e[2];
        e[0].binding = 0;
        e[0].buffer = job.mesh->MorphBuffer();
        e[1].binding = 1;
        e[1].buffer = out;
        rhi::BindGroupDesc gd;
        gd.layout = self->materials_->MorphLayout();
        gd.entries = rhi::Span<rhi::BindGroupEntry>(e, 2);
        gd.transient = true;
        const rhi::BindGroupHandle group = device->CreateBindGroup(gd);
        if (!group.IsValid())
        {
            continue;  // the object draws whatever the buffer holds: logged by the device
        }
        list->SetBindGroup(0, group);
        list->SetPushConstants(0, job.push.Data(), MaterialSystem::kMorphPushBytes);
        list->Dispatch((job.mesh->VertexCount() + 63) / 64, 1, 1);
    }
    list->PopDebugLabel();
}

void GBufferPass::Record(const PassExecuteContext& context, void* user)
{
    const GBufferPass* self = static_cast<const GBufferPass*>(user);
    rhi::CommandList* list = context.list;
    rhi::RenderingDesc rd;
    rd.color_count = 4;
    rd.color[0].texture = context.graph->ResolvedTexture(self->targets_.albedo);
    rd.color[1].texture = context.graph->ResolvedTexture(self->targets_.normal);
    rd.color[2].texture = context.graph->ResolvedTexture(self->targets_.material);
    rd.color[3].texture = context.graph->ResolvedTexture(self->targets_.emissive);
    for (u32 i = 0; i < 4; ++i)
    {
        rd.color[i].load = rhi::LoadOp::Clear;
        rd.color[i].store = rhi::StoreOp::Store;
        rd.color[i].clear_color[0] = 0.0f;
        rd.color[i].clear_color[1] = 0.0f;
        rd.color[i].clear_color[2] = 0.0f;
        rd.color[i].clear_color[3] = 0.0f;
    }
    rd.depth.texture = context.graph->ResolvedTexture(self->targets_.depth);
    rd.depth.load = rhi::LoadOp::Clear;
    rd.depth.store = rhi::StoreOp::Store;
    rd.depth.clear_depth = 1.0f;
    list->BeginRendering(rd);
    list->SetViewport(
        rhi::Viewport{0.0f, 0.0f, static_cast<f32>(self->width_), static_cast<f32>(self->height_), 0.0f, 1.0f});
    list->SetScissor(rhi::ScissorRect{0, 0, self->width_, self->height_});
    if (self->draw_count_ > 0)
    {
        list->SetBindGroup(kFrameSet, self->frame_group_);
        list->SetBindGroup(kObjectSet, self->object_group_);
    }
    rhi::PipelineHandle pipeline;
    rhi::BindGroupHandle group;
    u32 index_slot = 0;  // the slot the object index stream is bound at (0: not bound)
    for (u32 i = 0; i < self->draw_count_; ++i)
    {
        const Draw& d = self->draws_[i];
        if (!(d.pipeline == pipeline))
        {
            list->SetPipeline(d.pipeline);
            pipeline = d.pipeline;
            group = rhi::BindGroupHandle{};  // a new pipeline layout: rebind the material group
            if (d.skinned)
            {
                list->SetBindGroup(MaterialSystem::kJointSet, self->joint_group_);
            }
            if (d.instanced)
            {
                list->SetBindGroup(MaterialSystem::kInstanceSet, self->instance_group_);
            }
        }
        if (!d.instanced && self->object_index_stream_.IsValid() &&
            index_slot != MaterialSystem::ObjectIndexSlotFor(d.skinned))
        {
            // Draw i is issued with first_instance = i; Direct3D 12 fetches this instance-rate stream at that index.
            // It follows the mesh's streams, so a skinned mesh (two streams) has it one slot later.
            index_slot = MaterialSystem::ObjectIndexSlotFor(d.skinned);
            list->SetVertexBuffer(index_slot, self->object_index_stream_, 0);
        }
        if (!(d.material_group == group))
        {
            list->SetBindGroup(kMaterialSet, d.material_group);
            group = d.material_group;
        }
        if (d.morph_job != kNoMorphJob)
        {
            d.mesh->DrawWithVertices(list, context.graph->ResolvedBuffer(self->morph_buffer_),
                                     self->morph_jobs_[d.morph_job].out_offset, d.instance_count, d.first_instance,
                                     d.first_index, d.index_count);
        }
        else
        {
            d.mesh->Draw(list, d.instance_count, d.first_instance, d.first_index, d.index_count);
        }
    }
    list->EndRendering();
}

// ---- GBufferDebugView ----

GBufferDebugView::GBufferDebugView(rhi::Device* device) : device_(device)
{
    ENGINE_ASSERT(device != nullptr, "GBufferDebugView: device is null");
    Build();
}

void GBufferDebugView::Build()
{
    rhi::Device* device = device_;
    vertex_ = rhi::CreateShaderFromSet(*device, shaders::kFullscreenVertShader, rhi::ShaderStage::Vertex);
    fragment_ = rhi::CreateShaderFromSet(*device, shaders::kGbufferDebugFragShader, rhi::ShaderStage::Fragment);
    rhi::SamplerDesc smp;
    smp.mag_filter = rhi::FilterMode::Nearest;
    smp.min_filter = rhi::FilterMode::Nearest;
    smp.mip_filter = rhi::FilterMode::Nearest;
    smp.address_u = rhi::AddressMode::ClampToEdge;
    smp.address_v = rhi::AddressMode::ClampToEdge;
    sampler_ = device->CreateSampler(smp);
    rhi::BindingDesc bindings[6];
    for (u32 i = 0; i < 4; ++i)
    {
        bindings[i] = {i, rhi::BindingType::SampledTexture, rhi::ShaderStage::Fragment, 1};
    }
    bindings[4] = {4, rhi::BindingType::Sampler, rhi::ShaderStage::Fragment, 1};
    bindings[5] = {5, rhi::BindingType::SampledTexture, rhi::ShaderStage::Fragment, 1};
    rhi::BindGroupLayoutDesc ld;
    ld.entries = rhi::Span<rhi::BindingDesc>(bindings, 6);
    layout_ = device->CreateBindGroupLayout(ld);
}

GBufferDebugView::~GBufferDebugView()
{
    registration_.Reset();
    Release();
}

void GBufferDebugView::Release()
{
    if (device_ == nullptr)
    {
        return;
    }
    if (pipeline_.IsValid())
    {
        device_->DestroyPipeline(pipeline_);
    }
    if (layout_.IsValid())
    {
        device_->DestroyBindGroupLayout(layout_);
    }
    if (sampler_.IsValid())
    {
        device_->DestroySampler(sampler_);
    }
    if (fragment_.IsValid())
    {
        device_->DestroyShader(fragment_);
    }
    if (vertex_.IsValid())
    {
        device_->DestroyShader(vertex_);
    }
    pipeline_ = {};
    layout_ = {};
    sampler_ = {};
    fragment_ = {};
    vertex_ = {};
    pipeline_format_ = rhi::Format::Undefined;  // the pipeline is made again by the next AddToGraph
}

bool GBufferDebugView::Recreate(rhi::Device* device)
{
    device_ = device;
    Build();
    if (!Valid())
    {
        RenderLog(LogVerbosity::Error, "GBufferDebugView: recreating the shaders, sampler or layout failed");
        Release();
        return false;
    }
    return true;
}

bool GBufferDebugView::Valid() const
{
    return vertex_.IsValid() && fragment_.IsValid() && sampler_.IsValid() && layout_.IsValid();
}

void GBufferDebugView::AddToGraph(RenderGraph* graph, const GBufferTargets& targets, GBufferLayer layer,
                                  GraphResource output, rhi::Format output_format, f32 near_plane, f32 far_plane)
{
    ENGINE_ASSERT(graph != nullptr && output_format != rhi::Format::Undefined,
                  "GBufferDebugView::AddToGraph: graph is null or the output format is undefined");
    targets_ = targets;
    output_ = output;
    output_format_ = output_format;
    if (layer == GBufferLayer::WorldPosition)
    {
        ENGINE_ASSERT(has_inverse_view_projection_,
                      "GBufferDebugView::AddToGraph: the WorldPosition layer needs SetInverseViewProjection first");
    }
    has_inverse_view_projection_ = false;
    // The pipeline is built here, on the frame's thread, not inside the recording callback.
    if (pipeline_format_ != output_format_)
    {
        if (pipeline_.IsValid())
        {
            device_->DestroyPipeline(pipeline_);
        }
        rhi::GraphicsPipelineDesc gp;
        gp.vertex_shader = vertex_;
        gp.fragment_shader = fragment_;
        gp.cull_mode = rhi::CullMode::None;
        gp.color_targets[0].format = output_format_;
        gp.color_target_count = 1;
        gp.bind_group_layouts[0] = layout_;
        gp.bind_group_layout_count = 1;
        gp.push_constant_bytes = sizeof(Params);
        gp.push_constant_stages = rhi::ShaderStage::Fragment;
        gp.debug_name = "gbuffer_debug";
        pipeline_ = device_->CreateGraphicsPipeline(gp);
        pipeline_format_ = pipeline_.IsValid() ? output_format_ : rhi::Format::Undefined;
    }
    params_.layer = static_cast<u32>(layer);
    params_.near_plane = near_plane;
    params_.far_plane = far_plane;
    graph->AddPass("gbuffer_debug", &GBufferDebugView::Record, this)
        .Read(targets.albedo, GraphUsage::Sampled)
        .Read(targets.normal, GraphUsage::Sampled)
        .Read(targets.material, GraphUsage::Sampled)
        .Read(targets.depth, GraphUsage::Sampled)
        .Read(targets.emissive, GraphUsage::Sampled)
        .Write(output, GraphUsage::ColorAttachment);
}

void GBufferDebugView::Record(const PassExecuteContext& context, void* user)
{
    GBufferDebugView* self = static_cast<GBufferDebugView*>(user);
    rhi::Device* device = self->device_;
    const rhi::TextureHandle out = context.graph->ResolvedTexture(self->output_);
    const rhi::TextureDesc& gd0 = context.graph->TextureDescOf(self->targets_.albedo);
    if (!self->pipeline_.IsValid())
    {
        RenderLog(LogVerbosity::Error,
                  "GBufferDebugView: the pipeline for the output format could not be created; nothing is drawn");
        return;
    }
    rhi::BindGroupEntry e[6];
    e[0].binding = 0;
    e[0].texture = context.graph->ResolvedTexture(self->targets_.albedo);
    e[1].binding = 1;
    e[1].texture = context.graph->ResolvedTexture(self->targets_.normal);
    e[2].binding = 2;
    e[2].texture = context.graph->ResolvedTexture(self->targets_.material);
    e[3].binding = 3;
    e[3].texture = context.graph->ResolvedTexture(self->targets_.depth);
    e[4].binding = 4;
    e[4].sampler = self->sampler_;
    e[5].binding = 5;
    e[5].texture = context.graph->ResolvedTexture(self->targets_.emissive);
    rhi::BindGroupDesc gd;
    gd.layout = self->layout_;
    gd.entries = rhi::Span<rhi::BindGroupEntry>(e, 6);
    gd.transient = true;
    const rhi::BindGroupHandle group = device->CreateBindGroup(gd);
    rhi::RenderingDesc rd;
    rd.color_count = 1;
    rd.color[0].texture = out;
    rd.color[0].load = rhi::LoadOp::Clear;
    context.list->BeginRendering(rd);
    context.list->SetPipeline(self->pipeline_);
    context.list->SetViewport(
        rhi::Viewport{0.0f, 0.0f, static_cast<f32>(gd0.width), static_cast<f32>(gd0.height), 0.0f, 1.0f});
    context.list->SetScissor(rhi::ScissorRect{0, 0, gd0.width, gd0.height});
    context.list->SetBindGroup(0, group);
    context.list->SetPushConstants(0, &self->params_, sizeof(Params));
    context.list->Draw(3, 1, 0, 0);
    context.list->EndRendering();
}

}  // namespace vse::renderer
