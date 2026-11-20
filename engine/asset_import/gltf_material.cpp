#include "asset_import/gltf_material.h"

#include "asset/material.h"
#include "asset/texture_mint.h"
#include "asset_import/gltf_mesh.h"
#include "asset_import/gltf_names.h"
#include "asset_import/image_decode.h"
#include "asset_import/material_source.h"
#include "asset_import/spec_gloss.h"
#include "containers/array.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"
#include "cstr/number.h"
#include "lang/scalar.h"
#include "lang/status_try.h"

namespace engine_placeholder::asset
{

namespace
{

constexpr u32 kUseColor = 0;       // sRGB base color
constexpr u32 kUseNormal = 1;      // linear normal map
constexpr u32 kUseMetalRough = 2;  // linear metallic-roughness map (glTF coverage Day 3)
constexpr u32 kUseOcclusion = 3;   // linear occlusion map
constexpr u32 kUseEmissive = 4;    // sRGB emissive map
constexpr u32 kUses = 5;
// The textures converted from a specular-glossiness material (maintenance 2026-11-20): one pair per material, so their
// `texture` field is the material's index, not a glTF texture's.
constexpr u32 kUseSgColor = 5;       // sRGB base colour converted from diffuse and specular-glossiness
constexpr u32 kUseSgMetalRough = 6;  // linear metallic-roughness converted from them

// The manifest's key of a (texture, use) pair. The base colour and the normal map keep the key they had before the maps
// existed (`texture * 2 + use`), so a re-import with an older manifest keeps their ids; the maps get keys of their own,
// and a converted specular-glossiness pair keys by its material's index.
u32 TextureKey(u32 texture, u32 use)
{
    if (use >= kUseSgColor)
    {
        return 0x60000000u + texture * 2 + (use - kUseSgColor);
    }
    return use < kUseMetalRough ? texture * 2 + use : 0x40000000u + texture * 3 + (use - kUseMetalRough);
}

// The suffix of a texture's resource name for its use.
const char* TextureSuffix(u32 use)
{
    switch (use)
    {
    case kUseNormal: return "_normal";
    case kUseMetalRough: return "_mr";
    case kUseOcclusion: return "_occlusion";
    case kUseEmissive: return "_emissive";
    case kUseSgColor: return "_base_color";
    case kUseSgMetalRough: return "_metal_rough";
    default: return "";
    }
}

// Whether a use's texture is sRGB (the colours) or linear data.
bool UseIsSrgb(u32 use)
{
    return use == kUseColor || use == kUseEmissive || use == kUseSgColor;
}

Status RefuseMaterial(GltfError* error, StringView where, StringView what, ErrorCode code = ErrorCode::Invalid)
{
    error->Set(where, what);
    return Status::Make(code, "a glTF material is refused");
}

// A texture reference of a material: which texture, or none.
struct TextureRef
{
    bool present = false;
    u32 texture = 0;
};

// The importer's view of one glTF material.
struct MaterialInfo
{
    StringView name;
    f64 base_color[4] = {1, 1, 1, 1};
    f64 metallic = 1.0;
    f64 roughness = 1.0;
    Array<f64, 4> emissive_color = {{0.0, 0.0, 0.0, 1.0}};  // emissiveFactor's rgb, the strength in a
    bool emissive = false;                                  // written only when the factor is not black
    bool unlit = false;  // KHR_materials_unlit: the `flags` parameter gets the unlit bit
    f64 alpha_cutoff = 0.5;
    u8 alpha_mode = 0;  // 0 opaque, 1 mask, 2 blend
    bool double_sided = false;
    bool skinned = false;
    bool used_static = false;
    bool used_colored = false;    // by a mesh with COLOR_0 (glTF coverage Day 5): the colour template
    bool used_uncolored = false;  // by a mesh without
    TextureRef color;
    TextureRef normal;
    TextureRef metal_rough;        // pbrMetallicRoughness.metallicRoughnessTexture
    TextureRef occlusion;          // occlusionTexture
    TextureRef emissive_map;       // emissiveTexture
    f64 occlusion_strength = 1.0;  // occlusionTexture.strength
    // KHR_materials_pbrSpecularGlossiness, converted (maintenance 2026-11-20). With `sg_textures` the base colour and
    // the metallic-roughness are textures made from `sg_diffuse` and `sg_specular` and the factors; without, the
    // conversion went into the factors above and `color` (a dielectric's diffuse texture as it is).
    bool sg_textures = false;
    TextureRef sg_diffuse;
    TextureRef sg_specular;
    Array<f32, 4> sg_diffuse_factor = {{1.0f, 1.0f, 1.0f, 1.0f}};
    Array<f32, 3> sg_specular_factor = {{1.0f, 1.0f, 1.0f}};
    f32 sg_glossiness = 1.0f;
};

Status ReadTextureRef(const JsonValue& info, u64 texture_count, TextureRef* out, ImportReport* report,
                      GltfError* error);

// Reads a material's KHR_materials_pbrSpecularGlossiness and converts it: into the factors when they alone decide
// (no texture, or a diffuse texture with a specular colour below the dielectric 4%, where the base colour is the
// diffuse scaled), else into a pair of textures made from the two images.
Status ReadSpecGloss(const JsonValue& sg, u64 texture_count, MaterialInfo* out, ImportReport* report, GltfError* error)
{
    const JsonValue diffuse = sg.Get("diffuseFactor");
    if (diffuse.Exists())
    {
        if (!diffuse.IsArray() || diffuse.Size() != 4)
        {
            return RefuseMaterial(error, diffuse.Path(), StringView("is not an array of four numbers"));
        }
        for (usize k = 0; k < 4; ++k)
        {
            f64 v = 0;
            ENGINE_TRY(diffuse.At(k).AsDouble(&v, error));
            out->sg_diffuse_factor[k] = static_cast<f32>(v);
        }
    }
    const JsonValue specular = sg.Get("specularFactor");
    if (specular.Exists())
    {
        if (!specular.IsArray() || specular.Size() != 3)
        {
            return RefuseMaterial(error, specular.Path(), StringView("is not an array of three numbers"));
        }
        for (usize k = 0; k < 3; ++k)
        {
            f64 v = 0;
            ENGINE_TRY(specular.At(k).AsDouble(&v, error));
            out->sg_specular_factor[k] = static_cast<f32>(v);
        }
    }
    if (sg.Get("glossinessFactor").Exists())
    {
        f64 v = 0;
        ENGINE_TRY(sg.Get("glossinessFactor").AsDouble(&v, error));
        out->sg_glossiness = static_cast<f32>(v);
    }
    ENGINE_TRY(ReadTextureRef(sg.Get("diffuseTexture"), texture_count, &out->sg_diffuse, report, error));
    ENGINE_TRY(ReadTextureRef(sg.Get("specularGlossinessTexture"), texture_count, &out->sg_specular, report, error));
    SpecGlossSample factors;
    factors.diffuse = out->sg_diffuse_factor;
    factors.specular = out->sg_specular_factor;
    factors.glossiness = out->sg_glossiness;
    if (!out->sg_specular.present && (!out->sg_diffuse.present || SpecularIsDielectric(out->sg_specular_factor)))
    {
        // With a metallic value of 0 the base colour is linear in the diffuse (the diffuse times a scale, clamped), so
        // a diffuse texture stays as it is and the factors' conversion is the factor (a white texel clamps at the same
        // 1).
        const MetalRoughSample converted = SpecGlossToMetalRough(factors);
        for (u32 c = 0; c < 4; ++c)
        {
            out->base_color[c] = converted.base_color[c];
        }
        out->metallic = converted.metallic;
        out->roughness = converted.roughness;
        out->color = out->sg_diffuse;
        return Status::Success();
    }
    out->sg_textures = true;  // the factors are in the textures
    out->metallic = 1.0;
    out->roughness = 1.0;
    return Status::Success();
}

// The textures a material binds, by use. KHR_materials_unlit ignores everything but the base colour (and the normal map
// it always had), so an unlit material binds none of the maps. The occlusion of an ORM texture (the same texture as the
// metallic-roughness map, both linear) is not made twice: it is listed here as absent and bound to the
// metallic-roughness texture (`SharesMetalRough`).
void MaterialRefs(const MaterialInfo& info, TextureRef refs[kUses])
{
    refs[kUseColor] = info.color;
    refs[kUseNormal] = info.normal;
    refs[kUseMetalRough] = info.unlit ? TextureRef() : info.metal_rough;
    refs[kUseOcclusion] = info.unlit ? TextureRef() : info.occlusion;
    refs[kUseEmissive] = info.unlit ? TextureRef() : info.emissive_map;
    if (refs[kUseOcclusion].present && refs[kUseMetalRough].present &&
        refs[kUseOcclusion].texture == refs[kUseMetalRough].texture)
    {
        refs[kUseOcclusion].present = false;
    }
}

bool SharesMetalRough(const MaterialInfo& info)
{
    return !info.unlit && info.occlusion.present && info.metal_rough.present &&
           info.occlusion.texture == info.metal_rough.texture;
}

Status ReadTextureRef(const JsonValue& info, u64 texture_count, TextureRef* out, ImportReport* report, GltfError* error)
{
    out->present = false;
    if (!info.Exists())
    {
        return Status::Success();
    }
    u32 index = 0;
    ENGINE_TRY(info.RequiredIndex("index", &index, texture_count, error));
    u64 tex_coord = 0;
    bool present = false;
    ENGINE_TRY(info.OptionalUnsigned("texCoord", &tex_coord, 1000, &present, error));
    if (tex_coord != 0)
    {
        AddImportWarning(report, info.Path(), StringView("texture dropped: the meshes carry TEXCOORD_0 only"));
        return Status::Success();
    }
    const JsonValue extensions = info.Get("extensions");
    const JsonValue transform = extensions.Get("KHR_texture_transform");
    if (transform.Exists())
    {
        f64 offset[2] = {0, 0};
        f64 scale[2] = {1, 1};
        f64 rotation = 0;
        const JsonValue o = transform.Get("offset");
        const JsonValue s = transform.Get("scale");
        for (usize k = 0; k < 2; ++k)
        {
            if (o.Exists())
            {
                ENGINE_TRY(o.At(k).AsDouble(&offset[k], error));
            }
            if (s.Exists())
            {
                ENGINE_TRY(s.At(k).AsDouble(&scale[k], error));
            }
        }
        if (transform.Get("rotation").Exists())
        {
            ENGINE_TRY(transform.Get("rotation").AsDouble(&rotation, error));
        }
        if (offset[0] != 0 || offset[1] != 0 || scale[0] != 1 || scale[1] != 1 || rotation != 0)
        {
            return RefuseMaterial(error, transform.Path(),
                                  StringView("is a texture transform; the standard material template has none"),
                                  ErrorCode::Unsupported);
        }
    }
    out->present = true;
    out->texture = index;
    return Status::Success();
}

Status ReadMaterial(const JsonValue& material, u64 texture_count, MaterialInfo* out, ImportReport* report,
                    GltfError* error)
{
    bool has_name = false;
    ENGINE_TRY(material.OptionalString("name", &out->name, &has_name, error));
    if (!has_name)
    {
        out->name = StringView();
    }
    const JsonValue extensions = material.Get("extensions");
    // KHR_materials_unlit: the base colour (factor and texture) is shown as it is; the extension says the material's
    // other properties are ignored, and the renderer's unlit flag ignores them (renderer::kMaterialFlagUnlit).
    out->unlit = extensions.Get("KHR_materials_unlit").Exists();
    const JsonValue spec_gloss = extensions.Get("KHR_materials_pbrSpecularGlossiness");
    const JsonValue pbr = material.Get("pbrMetallicRoughness");
    if (spec_gloss.Exists())
    {
        // The extension wins over pbrMetallicRoughness, which a file may carry as a fallback.
        ENGINE_TRY(ReadSpecGloss(spec_gloss, texture_count, out, report, error));
    }
    else if (pbr.Exists())
    {
        const JsonValue factor = pbr.Get("baseColorFactor");
        if (factor.Exists())
        {
            if (!factor.IsArray() || factor.Size() != 4)
            {
                return RefuseMaterial(error, factor.Path(), StringView("is not an array of four numbers"));
            }
            for (usize k = 0; k < 4; ++k)
            {
                ENGINE_TRY(factor.At(k).AsDouble(&out->base_color[k], error));
            }
        }
        if (pbr.Get("metallicFactor").Exists())
        {
            ENGINE_TRY(pbr.Get("metallicFactor").AsDouble(&out->metallic, error));
        }
        if (pbr.Get("roughnessFactor").Exists())
        {
            ENGINE_TRY(pbr.Get("roughnessFactor").AsDouble(&out->roughness, error));
        }
        ENGINE_TRY(ReadTextureRef(pbr.Get("baseColorTexture"), texture_count, &out->color, report, error));
        ENGINE_TRY(
            ReadTextureRef(pbr.Get("metallicRoughnessTexture"), texture_count, &out->metal_rough, report, error));
    }
    ENGINE_TRY(ReadTextureRef(material.Get("normalTexture"), texture_count, &out->normal, report, error));
    if (material.Get("normalTexture").Get("scale").Exists())
    {
        f64 normal_scale = 1.0;
        ENGINE_TRY(material.Get("normalTexture").Get("scale").AsDouble(&normal_scale, error));
        if (normal_scale != 1.0 && out->normal.present)
        {
            AddImportWarning(report, material.Get("normalTexture").Path(),
                             StringView("normalTexture.scale dropped: the template has no normal scale"));
        }
    }
    ENGINE_TRY(ReadTextureRef(material.Get("occlusionTexture"), texture_count, &out->occlusion, report, error));
    if (material.Get("occlusionTexture").Get("strength").Exists())
    {
        ENGINE_TRY(material.Get("occlusionTexture").Get("strength").AsDouble(&out->occlusion_strength, error));
    }
    ENGINE_TRY(ReadTextureRef(material.Get("emissiveTexture"), texture_count, &out->emissive_map, report, error));
    const JsonValue emissive = material.Get("emissiveFactor");
    if (emissive.Exists())
    {
        for (usize k = 0; k < 3; ++k)
        {
            ENGINE_TRY(emissive.At(k).AsDouble(&out->emissive_color[k], error));
            out->emissive = out->emissive || out->emissive_color[k] > 0.0;
        }
        const JsonValue strength_value = extensions.Get("KHR_materials_emissive_strength").Get("emissiveStrength");
        if (strength_value.Exists())
        {
            ENGINE_TRY(strength_value.AsDouble(&out->emissive_color[3], error));
        }
    }
    StringView mode("OPAQUE");
    bool has_mode = false;
    ENGINE_TRY(material.OptionalString("alphaMode", &mode, &has_mode, error));
    if (mode == StringView("MASK"))
    {
        out->alpha_mode = 1;
    }
    else if (mode == StringView("BLEND"))
    {
        out->alpha_mode = 2;
    }
    else if (mode != StringView("OPAQUE"))
    {
        return RefuseMaterial(error, material.Get("alphaMode").Path(), StringView("is not OPAQUE, MASK or BLEND"));
    }
    if (material.Get("alphaCutoff").Exists())
    {
        ENGINE_TRY(material.Get("alphaCutoff").AsDouble(&out->alpha_cutoff, error));
    }
    ENGINE_TRY(material.OptionalBool("doubleSided", &out->double_sided, error));
    return Status::Success();
}

// The bytes of an image: a `data:` URI, a file through the source, or a buffer view.
Status ReadImageBytes(const GltfDocument& document, GltfSource& source, const ImportLimits& limits,
                      const JsonValue& image, Vector<byte>* out, GltfError* error)
{
    StringView uri;
    bool has_uri = false;
    ENGINE_TRY(image.OptionalString("uri", &uri, &has_uri, error));
    if (has_uri)
    {
        if (uri.StartsWith(StringView("data:")))
        {
            const usize comma = uri.Find(StringView(";base64,"));
            if (comma == uri.Length())
            {
                return RefuseMaterial(error, image.Get("uri").Path(), StringView("is a data: URI that is not base64"));
            }
            return DecodeBase64(uri.Substring(comma + 8), limits.max_buffer_bytes, out, error);
        }
        String<HeapAllocator> name(HeapAllocator::Default());
        const Status cleaned = CleanRelativeUri(uri, &name, error);
        if (cleaned.Failed())
        {
            const GltfError inner = *error;  // the sentence is rewritten into the same buffer
            error->Set(image.Path(), StringView(inner.message.Data()));
            return cleaned;
        }
        const Status read = source.Read(name.View(), limits.max_file_bytes, out);
        if (read.Failed())
        {
            error->Set(image.Path(), StringView("could not be read from its file"));
        }
        return read;
    }
    u32 view_index = 0;
    ENGINE_TRY(image.RequiredIndex("bufferView", &view_index, document.views.Size(), error));
    const GltfBufferView& view = document.views[view_index];
    const Vector<byte>& buffer = document.buffers[view.buffer];
    if (!out->TryResize(static_cast<usize>(view.length)))
    {
        return Status::Make(ErrorCode::LimitExceeded, "out of memory");
    }
    MemCopy(out->Data(), buffer.Data() + view.offset, static_cast<usize>(view.length));
    return Status::Success();
}

}  // namespace

Status ImportMaterials(const GltfDocument& document, const GltfNodeGraph& graph, GltfSource& source,
                       const ImportOptions& options, const TypeRegistry& types, ImportResult* result, GltfError* error)
{
    const JsonValue materials = document.root.Get("materials");
    const JsonValue textures = document.root.Get("textures");
    const JsonValue images = document.root.Get("images");
    const JsonValue samplers = document.root.Get("samplers");
    const usize material_count = materials.Size();
    if (material_count == 0)
    {
        if (textures.Size() != 0)
        {
            char what[96];
            FormatTo(what, sizeof(what), "%u textures are used by no material and are not imported",
                     static_cast<unsigned>(textures.Size()));
            AddImportWarning(&result->report, textures.Path(), StringView(what, CStrLength(what)));
        }
        return Status::Success();
    }
    Vector<MaterialInfo> infos;
    for (usize m = 0; m < material_count; ++m)
    {
        MaterialInfo info;
        ENGINE_TRY(ReadMaterial(materials.At(m), textures.Size(), &info, &result->report, error));
        infos.Push(info);
    }
    // Materials of skinned meshes use the skinned template; one that static meshes use too gets a warning.
    const JsonValue meshes = document.root.Get("meshes");
    Vector<bool> mesh_skinned;
    mesh_skinned.Resize(meshes.Size());
    for (usize n = 0; n < graph.nodes.Size(); ++n)
    {
        if (graph.nodes[n].mesh != kNoGltfIndex && graph.nodes[n].skin != kNoGltfIndex)
        {
            mesh_skinned[graph.nodes[n].mesh] = true;
        }
    }
    for (usize mesh = 0; mesh < meshes.Size(); ++mesh)
    {
        const JsonValue primitives = meshes.At(mesh).Get("primitives");
        for (usize p = 0; p < primitives.Size(); ++p)
        {
            const JsonValue index = primitives.At(p).Get("material");
            u32 material = 0;
            if (index.Exists() && index.AsIndex(&material, material_count, error).Ok())
            {
                (mesh_skinned[mesh] ? infos[material].skinned : infos[material].used_static) = true;
                (GltfMeshHasColors(meshes.At(mesh)) ? infos[material].used_colored
                                                    : infos[material].used_uncolored) = true;
            }
        }
    }
    for (usize m = 0; m < material_count; ++m)
    {
        if (infos[m].skinned && infos[m].used_static)
        {
            AddImportWarning(
                &result->report, materials.At(m).Path(),
                StringView("is used by skinned and by static meshes: the skinned template is used for both"));
        }
    }
    // The textures to make: (texture, use) pairs some material needs.
    Vector<u32> needed[kUses];
    for (usize m = 0; m < material_count; ++m)
    {
        TextureRef refs[kUses];
        MaterialRefs(infos[m], refs);
        for (u32 use = 0; use < kUses; ++use)
        {
            if (!refs[use].present)
            {
                continue;
            }
            bool have = false;
            for (usize k = 0; k < needed[use].Size(); ++k)
            {
                have = have || needed[use][k] == refs[use].texture;
            }
            if (!have)
            {
                needed[use].Push(refs[use].texture);
            }
        }
    }
    {
        Vector<bool> referenced;
        referenced.Resize(textures.Size());
        usize count = 0;
        for (u32 use = 0; use < kUses; ++use)
        {
            for (usize k = 0; k < needed[use].Size(); ++k)
            {
                count += referenced[needed[use][k]] ? 0 : 1;
                referenced[needed[use][k]] = true;
            }
        }
        for (usize m = 0; m < material_count; ++m)
        {
            const Array<const TextureRef*, 2> refs = {{&infos[m].sg_diffuse, &infos[m].sg_specular}};
            for (const TextureRef* ref : refs)
            {
                if (infos[m].sg_textures && ref->present)
                {
                    count += referenced[ref->texture] ? 0 : 1;
                    referenced[ref->texture] = true;
                }
            }
        }
        if (count < textures.Size())
        {
            char what[96];
            FormatTo(what, sizeof(what), "%u textures are used by no material and are not imported",
                     static_cast<unsigned>(textures.Size() - count));
            AddImportWarning(&result->report, textures.Path(), StringView(what, CStrLength(what)));
        }
    }
    struct MadeTexture
    {
        MadeTexture() : path(HeapAllocator::Default()) {}
        u32 texture;
        u32 use;
        ResourceId id;
        usize resource;  // its index in result->resources
        String<HeapAllocator> path;
    };
    Vector<MadeTexture> made;
    Vector<ManifestObject> objects;
    Vector<String<HeapAllocator>> name_store;
    Vector<u32> order_texture;
    Vector<u32> order_use;
    for (u32 use = 0; use < kUses; ++use)
    {
        for (usize k = 0; k < needed[use].Size(); ++k)
        {
            order_texture.Push(needed[use][k]);
            order_use.Push(use);
        }
    }
    // The converted pairs of the specular-glossiness materials, after the glTF textures (an unlit material shows no
    // metallic-roughness, so it gets none).
    for (usize m = 0; m < material_count; ++m)
    {
        if (infos[m].sg_textures)
        {
            order_texture.Push(static_cast<u32>(m));
            order_use.Push(kUseSgColor);
            if (!infos[m].unlit)
            {
                order_texture.Push(static_cast<u32>(m));
                order_use.Push(kUseSgMetalRough);
            }
        }
    }
    for (usize i = 0; i < order_texture.Size(); ++i)
    {
        if (order_use[i] >= kUseSgColor)
        {
            String<HeapAllocator> raw(HeapAllocator::Default(), infos[order_texture[i]].name);
            if (raw.Length() == 0)
            {
                char text[24];
                FormatTo(text, sizeof(text), "material%u", static_cast<unsigned>(order_texture[i]));
                raw = String<HeapAllocator>(HeapAllocator::Default(), StringView(text, CStrLength(text)));
            }
            raw.Append(StringView(TextureSuffix(order_use[i])));
            name_store.Push(static_cast<String<HeapAllocator>&&>(raw));
            continue;
        }
        const JsonValue texture = textures.At(order_texture[i]);
        StringView name;
        bool has_name = false;
        ENGINE_TRY(texture.OptionalString("name", &name, &has_name, error));
        if (!has_name)
        {
            u32 source_index = 0;
            if (texture.Get("source").Exists() &&
                texture.Get("source").AsIndex(&source_index, images.Size(), error).Ok())
            {
                ENGINE_TRY(images.At(source_index).OptionalString("name", &name, &has_name, error));
            }
        }
        String<HeapAllocator> raw(HeapAllocator::Default(), has_name ? name : StringView());
        if (raw.Length() == 0)
        {
            char text[24];
            FormatTo(text, sizeof(text), "texture%u", static_cast<unsigned>(order_texture[i]));
            raw = String<HeapAllocator>(HeapAllocator::Default(), StringView(text, CStrLength(text)));
        }
        raw.Append(StringView(TextureSuffix(order_use[i])));
        name_store.Push(static_cast<String<HeapAllocator>&&>(raw));
    }
    for (usize i = 0; i < order_texture.Size(); ++i)
    {
        objects.Push(ManifestObject{TextureKey(order_texture[i], order_use[i]), name_store[i].View()});
    }
    Vector<ResourceId> texture_ids;
    AssignIds(options, ImportKind::Texture, Span<const ManifestObject>(objects.Data(), objects.Size()), &texture_ids);
    Vector<String<HeapAllocator>> segments;
    for (usize i = 0; i < name_store.Size(); ++i)
    {
        String<HeapAllocator> segment(HeapAllocator::Default());
        CleanPathSegment(name_store[i].View(), &segment);
        segments.Push(static_cast<String<HeapAllocator>&&>(segment));
    }
    MakeNamesUnique(&segments);
    const String<HeapAllocator> prefix = ImportPathPrefix(options);
    // Decode each image once; mint each texture for its use.
    Vector<usize> last_use;  // per image: the last texture (in `order_texture`) that reads it
    last_use.Resize(images.Size());
    // The glTF textures an entry reads: its own, or a converted pair's two.
    const auto sources_of = [&](usize i, Array<u32, 2>* out) -> u32
    {
        if (order_use[i] < kUseSgColor)
        {
            (*out)[0] = order_texture[i];
            return 1;
        }
        const MaterialInfo& info = infos[order_texture[i]];
        u32 n = 0;
        const Array<const TextureRef*, 2> refs = {{&info.sg_diffuse, &info.sg_specular}};
        for (const TextureRef* ref : refs)
        {
            if (ref->present)
            {
                (*out)[n++] = ref->texture;
            }
        }
        return n;
    };
    for (usize i = 0; i < order_texture.Size(); ++i)
    {
        Array<u32, 2> sources = {{0, 0}};
        const u32 n = sources_of(i, &sources);
        for (u32 k = 0; k < n; ++k)
        {
            u32 source_index = 0;
            if (textures.At(sources[k]).Get("source").Exists() &&
                textures.At(sources[k]).Get("source").AsIndex(&source_index, images.Size(), error).Ok())
            {
                last_use[source_index] = i;
            }
        }
    }
    Vector<DecodedImage> decoded;
    Vector<bool> have_image;
    decoded.Resize(images.Size());
    have_image.Resize(images.Size());
    // Decodes the image of glTF texture `t` once; `*image_index` names it in `decoded`.
    const auto decode_texture = [&](u32 t, u32* image_index) -> Status
    {
        const JsonValue texture = textures.At(t);
        if (!texture.Get("source").Exists())
        {
            return RefuseMaterial(error, texture.Path(), StringView("has no source image"), ErrorCode::Unsupported);
        }
        ENGINE_TRY(texture.RequiredIndex("source", image_index, images.Size(), error));
        const JsonValue image = images.At(*image_index);
        if (!have_image[*image_index])
        {
            Vector<byte> bytes;
            ENGINE_TRY(ReadImageBytes(document, source, options.limits, image, &bytes, error));
            const Status decoded_ok = DecodePixels(ByteSpan(bytes.Data(), bytes.Size()), options.limits.max_pixels,
                                                   &decoded[*image_index], error);
            if (decoded_ok.Failed())
            {
                const GltfError inner = *error;  // the sentence is rewritten into the same buffer
                error->Set(image.Path(), StringView(inner.message.Data()));
                return decoded_ok;
            }
            if (decoded[*image_index].reduced_depth)
            {
                AddImportWarning(&result->report, image.Path(), StringView("16 bits per channel reduced to 8"));
            }
            have_image[*image_index] = true;
        }
        return Status::Success();
    };
    DecodedImage converted_mr;  // a converted pair's metallic-roughness, made with its base colour for the next entry
    for (usize i = 0; i < order_texture.Size(); ++i)
    {
        const bool converted = order_use[i] >= kUseSgColor;
        // Where a failure is reported: the glTF texture, or the converted material.
        const JsonValue texture = converted ? materials.At(order_texture[i]) : textures.At(order_texture[i]);
        Array<u32, 2> sources = {{0, 0}};
        const u32 source_count = sources_of(i, &sources);
        Array<u32, 2> image_indices = {{0, 0}};
        for (u32 k = 0; k < source_count; ++k)
        {
            ENGINE_TRY(decode_texture(sources[k], &image_indices[k]));
        }
        DecodedImage converted_pixels;
        if (order_use[i] == kUseSgMetalRough)
        {
            converted_pixels = static_cast<DecodedImage&&>(converted_mr);  // made with the base colour, just before
            converted_mr = DecodedImage();
        }
        else if (converted)
        {
            const MaterialInfo& info = infos[order_texture[i]];
            SpecGlossImages inputs;
            u32 k = 0;
            inputs.diffuse = info.sg_diffuse.present ? &decoded[image_indices[k++]] : nullptr;
            inputs.specular_glossiness = info.sg_specular.present ? &decoded[image_indices[k++]] : nullptr;
            inputs.diffuse_factor = info.sg_diffuse_factor;
            inputs.specular_factor = info.sg_specular_factor;
            inputs.glossiness_factor = info.sg_glossiness;
            if (inputs.diffuse != nullptr && inputs.specular_glossiness != nullptr &&
                (inputs.diffuse->width != inputs.specular_glossiness->width ||
                 inputs.diffuse->height != inputs.specular_glossiness->height))
            {
                AddImportWarning(&result->report, texture.Path(),
                                 StringView("the diffuse and specular-glossiness textures differ in size: the smaller "
                                            "is resampled for the conversion"));
            }
            // The metallic-roughness entry, when there is one, follows at once (an unlit material has none).
            const bool with_mr = i + 1 < order_use.Size() && order_use[i + 1] == kUseSgMetalRough;
            const Status made_ok = ConvertSpecGlossImages(inputs, options.limits.max_pixels, &converted_pixels,
                                                          with_mr ? &converted_mr : nullptr);
            if (made_ok.Failed())
            {
                error->Set(texture.Path(), StringView(made_ok.Message()));
                return made_ok;
            }
        }
        const u32 image_index = image_indices[0];
        const DecodedImage& pixels = converted ? converted_pixels : decoded[image_index];
        TextureMintSpec spec;
        spec.format = UseIsSrgb(order_use[i]) ? rhi::Format::BC7_SRGB : rhi::Format::BC7_UNorm;
        spec.srgb = UseIsSrgb(order_use[i]);
        spec.drop_alpha = !((order_use[i] == kUseColor || order_use[i] == kUseSgColor) && pixels.has_alpha);
        spec.alpha = spec.drop_alpha ? MipAlpha::Ignore : MipAlpha::Straight;
        ImportedResource resource;
        resource.id = texture_ids[i];
        resource.type = types::kTexture;
        resource.path = String<HeapAllocator>(HeapAllocator::Default(), prefix.View());
        resource.path.Append(StringView("/textures/"));
        resource.path.Append(segments[i].View());
        const ByteSpan layer(pixels.rgba.Data(), pixels.rgba.Size());
        const Status minted = MintTextureFromPixels(types, resource.id, spec, pixels.width, pixels.height,
                                                    Span<const ByteSpan>(&layer, 1), &resource.blob);
        if (minted.Failed())
        {
            error->Set(texture.Path(), StringView(minted.Message()));
            return minted;
        }
        for (u32 k = 0; k < source_count; ++k)
        {
            if (last_use[image_indices[k]] == i)
            {
                decoded[image_indices[k]] = DecodedImage();  // its last texture: the pixels are not kept
            }
        }
        if (options.cancel != nullptr && options.cancel->Load(MemoryOrder::Acquire))
        {
            error->Set(StringView(), StringView("the import was cancelled"));
            return Status::Make(ErrorCode::Cancelled, "the import was cancelled");
        }
        result->report.output_bytes += resource.blob.Size();
        if (result->report.output_bytes > options.limits.max_output_bytes)
        {
            return RefuseMaterial(error, texture.Path(),
                                  StringView("takes the import past the limit on the bytes it may make"),
                                  ErrorCode::TooLarge);
        }
        MadeTexture record;
        record.texture = order_texture[i];
        record.use = order_use[i];
        record.id = resource.id;
        record.resource = result->resources.Size();
        record.path = String<HeapAllocator>(HeapAllocator::Default(), resource.path.View());
        made.Push(static_cast<MadeTexture&&>(record));
        result->manifest.Add(ImportKind::Texture, TextureKey(order_texture[i], order_use[i]), name_store[i].View(),
                             resource.id);
        result->resources.Push(static_cast<ImportedResource&&>(resource));
    }
    // The materials.
    // A material that coloured and uncoloured meshes both use gets a twin on the colour template, `<name>_color`, with
    // the manifest key kGltfColorTwinKey + its index; one only coloured meshes use is itself on the colour template.
    Vector<String<HeapAllocator>> twin_names;
    Vector<ManifestObject> material_objects;
    for (usize m = 0; m < material_count; ++m)
    {
        material_objects.Push(ManifestObject{static_cast<u32>(m), infos[m].name});
        String<HeapAllocator> twin(HeapAllocator::Default(), infos[m].name);
        twin.Append(StringView("_color"));
        twin_names.Push(static_cast<String<HeapAllocator>&&>(twin));
    }
    for (usize m = 0; m < material_count; ++m)
    {
        if (infos[m].used_colored && infos[m].used_uncolored)
        {
            material_objects.Push(ManifestObject{kGltfColorTwinKey + static_cast<u32>(m), twin_names[m].View()});
        }
    }
    Vector<ResourceId> assigned;
    AssignIds(options, ImportKind::Material,
              Span<const ManifestObject>(material_objects.Data(), material_objects.Size()), &assigned);
    Vector<ResourceId> material_ids;
    Vector<ResourceId> twin_ids;
    for (usize m = 0; m < material_count; ++m)
    {
        material_ids.Push(assigned[m]);
        twin_ids.Push(ResourceId());
    }
    for (usize k = material_count; k < material_objects.Size(); ++k)
    {
        twin_ids[material_objects[k].index - kGltfColorTwinKey] = assigned[k];
    }
    Vector<String<HeapAllocator>> material_segments;
    for (usize m = 0; m < material_count; ++m)
    {
        String<HeapAllocator> segment(HeapAllocator::Default());
        CleanPathSegment(infos[m].name, &segment);
        if (segment.Length() == 0)
        {
            char text[24];
            FormatTo(text, sizeof(text), "material%u", static_cast<unsigned>(m));
            segment = String<HeapAllocator>(HeapAllocator::Default(), StringView(text, CStrLength(text)));
        }
        material_segments.Push(static_cast<String<HeapAllocator>&&>(segment));
    }
    MakeNamesUnique(&material_segments);
    // The resolver the material minter asks for each texture path.
    class Resolver final : public MaterialReferenceResolver
    {
    public:
        Resolver(const ImportResult* result, const Vector<MadeTexture>* made) : result_(result), made_(made) {}
        Status Resolve(StringView path, ResourceId* id, ByteSpan* blob) const override
        {
            for (usize i = 0; i < made_->Size(); ++i)
            {
                if ((*made_)[i].path.View() == path)
                {
                    const ImportedResource& r = result_->resources[(*made_)[i].resource];
                    *id = r.id;
                    *blob = ByteSpan(r.blob.Data(), r.blob.Size());
                    return Status::Success();
                }
            }
            return Status::Make(ErrorCode::NotFound, "no such texture in this import");
        }

    private:
        const ImportResult* result_;
        const Vector<MadeTexture>* made_;
    };
    for (usize m = 0; m < material_count; ++m)
    {
        for (u32 variant = 0; variant < 2; ++variant)
        {
            const MaterialInfo& info = infos[m];
            const bool colored = variant == 1;
            const bool twin = info.used_colored && info.used_uncolored;
            if (colored ? !info.used_colored : (info.used_colored && !info.used_uncolored))
            {
                continue;  // this variant is not used
            }
            const ResourceId material_id = colored && twin ? twin_ids[m] : material_ids[m];
            String<HeapAllocator> text(HeapAllocator::Default());
            char id[17];
            FormatResourceId(material_id, id);
            char line[256];
            const char* template_name = info.skinned ? (colored ? "gbuffer_skinned_color" : "gbuffer_skinned")
                                                     : (colored ? "gbuffer_color" : "gbuffer");
            FormatTo(line, sizeof(line), "id %s\ntemplate %s\n", id, template_name);
            text.Append(StringView(line, CStrLength(line)));
            if (info.normal.present)
            {
                text.Append(StringView("option NORMAL_MAP\n"));
            }
            if (info.alpha_mode == 1)
            {
                text.Append(StringView("option ALPHA_TEST\n"));
            }
            // The numbers are parsed back by MintMaterial: FormatGeneral writes '.' whatever the host's LC_NUMERIC is.
            const auto append_float = [&text](f64 value)
            {
                char number[32];
                const usize length = FormatGeneral(number, sizeof(number), value, 9);
                text.Append(' ');
                text.Append(StringView(number, length));
            };
            text.Append(StringView("param base_color color"));
            for (u32 c = 0; c < 4; ++c)
            {
                append_float(info.base_color[c]);
            }
            const Array<const char*, 3> scalar_names = {"roughness", "metallic", "alpha_cutoff"};
            const Array<f64, 3> scalars = {info.roughness, info.metallic, info.alpha_cutoff};
            for (u32 k = 0; k < 3; ++k)
            {
                text.Append(StringView("\nparam "));
                text.Append(StringView(scalar_names[k]));
                text.Append(StringView(" float"));
                append_float(scalars[k]);
            }
            if (info.emissive)
            {
                // glTF coverage Day 2: the emissive colour as it is (rgb, the strength in a), not its largest
                // component.
                text.Append(StringView("\nparam emissive_color color"));
                for (u32 c = 0; c < 4; ++c)
                {
                    append_float(info.emissive_color[c]);
                }
            }
            if (info.unlit)
            {
                // Written only for an unlit material, so the blobs of the materials that imported before do not change.
                text.Append(StringView("\nparam flags uint 1"));
            }
            else if (info.occlusion.present && info.occlusion_strength != 1.0)
            {
                // glTF coverage Day 3: the occlusion map's strength (1, the default, is the template's own).
                text.Append(StringView("\nparam occlusion_strength float"));
                append_float(info.occlusion_strength);
            }
            text.Append('\n');
            TextureRef refs[kUses];
            MaterialRefs(info, refs);
            u32 made_use[kUses] = {kUseColor, kUseNormal, kUseMetalRough, kUseOcclusion, kUseEmissive};
            if (SharesMetalRough(info))
            {
                refs[kUseOcclusion] = info.occlusion;  // bound to the metallic-roughness texture, made once
                made_use[kUseOcclusion] = kUseMetalRough;
            }
            const char* slots[kUses] = {"albedo_tex", "normal_tex", "metallic_roughness_tex", "occlusion_tex",
                                        "emissive_tex"};
            u32 first_texture = 0xFFFFFFFFu;
            for (u32 use = 0; use < kUses; ++use)
            {
                if (!refs[use].present)
                {
                    continue;
                }
                for (usize k = 0; k < made.Size(); ++k)
                {
                    if (made[k].texture == refs[use].texture && made[k].use == made_use[use])
                    {
                        FormatTo(line, sizeof(line), "texture %s %s\n", slots[use], made[k].path.CStr());
                        text.Append(StringView(line, CStrLength(line)));
                    }
                }
                first_texture = first_texture == 0xFFFFFFFFu ? refs[use].texture : first_texture;
            }
            if (info.sg_textures)
            {
                for (usize k = 0; k < made.Size(); ++k)
                {
                    if (made[k].use >= kUseSgColor && made[k].texture == m)
                    {
                        FormatTo(line, sizeof(line), "texture %s %s\n",
                                 made[k].use == kUseSgColor ? "albedo_tex" : "metallic_roughness_tex",
                                 made[k].path.CStr());
                        text.Append(StringView(line, CStrLength(line)));
                    }
                }
                // The sampler of the diffuse texture, else of the specular-glossiness one.
                first_texture = info.sg_diffuse.present ? info.sg_diffuse.texture : info.sg_specular.texture;
            }
            // The sampler of the first texture the material uses.
            if (first_texture != 0xFFFFFFFFu)
            {
                const JsonValue texture = textures.At(first_texture);
                u32 sampler_index = 0;
                const JsonValue sampler_value = texture.Get("sampler");
                if (sampler_value.Exists())
                {
                    ENGINE_TRY(sampler_value.AsIndex(&sampler_index, samplers.Size(), error));
                    const JsonValue sampler = samplers.At(sampler_index);
                    u64 wrap_s = 10497;
                    u64 wrap_t = 10497;
                    u64 mag = 9729;
                    u64 min = 9987;
                    bool present = false;
                    ENGINE_TRY(sampler.OptionalUnsigned("wrapS", &wrap_s, 100000, &present, error));
                    ENGINE_TRY(sampler.OptionalUnsigned("wrapT", &wrap_t, 100000, &present, error));
                    ENGINE_TRY(sampler.OptionalUnsigned("magFilter", &mag, 100000, &present, error));
                    ENGINE_TRY(sampler.OptionalUnsigned("minFilter", &min, 100000, &present, error));
                    if (wrap_s != wrap_t)
                    {
                        AddImportWarning(&result->report, sampler.Path(),
                                         StringView("wrapT differs from wrapS: the sampler uses wrapS for both"));
                    }
                    const char* address = wrap_s == 33071 ? "clamp" : (wrap_s == 33648 ? "mirror" : "repeat");
                    const bool nearest = mag == 9728 && (min == 9728 || min == 9984 || min == 9986);
                    FormatTo(line, sizeof(line), "sampler linear_sampler filter %s address %s\n",
                             nearest ? "nearest" : "linear", address);
                    text.Append(StringView(line, CStrLength(line)));
                }
            }
            if (info.double_sided)
            {
                text.Append(StringView("state cull none\n"));
            }
            if (info.alpha_mode == 2)
            {
                text.Append(StringView("state alpha_blend on\nstate depth_write off\n"));
            }
            ImportedResource resource;
            resource.id = material_id;
            resource.type = types::kMaterial;
            resource.path = String<HeapAllocator>(HeapAllocator::Default(), prefix.View());
            resource.path.Append(StringView("/materials/"));
            resource.path.Append(material_segments[m].View());
            if (colored && twin)
            {
                resource.path.Append(StringView("_color"));
            }
            const Resolver resolver(result, &made);
            MaterialSourceError mint_error;
            const Status minted = MintMaterial(text.View(), resolver, types, &resource.blob, &mint_error);
            if (minted.Failed())
            {
                error->Set(materials.At(m).Path(), StringView(mint_error.message.Data()));
                return Status::Make(ErrorCode::Unsupported, "the material does not fit the template");
            }
            result->report.output_bytes += resource.blob.Size();
            if (result->report.output_bytes > options.limits.max_output_bytes)
            {
                return RefuseMaterial(error, materials.At(m).Path(),
                                      StringView("takes the import past the limit on the bytes it may make"),
                                      ErrorCode::TooLarge);
            }
            result->manifest.Add(ImportKind::Material,
                                 colored && twin ? kGltfColorTwinKey + static_cast<u32>(m) : static_cast<u32>(m),
                                 colored && twin ? twin_names[m].View() : infos[m].name, resource.id);
            result->resources.Push(static_cast<ImportedResource&&>(resource));
        }
    }
    return Status::Success();
}

}  // namespace engine_placeholder::asset
