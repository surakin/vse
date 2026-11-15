#include "asset_import/gltf_material.h"

#include "asset/material.h"
#include "asset/texture_mint.h"
#include "asset_import/gltf_names.h"
#include "asset_import/image_decode.h"
#include "asset_import/material_source.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"
#include "lang/scalar.h"
#include "lang/status_try.h"

namespace engine_placeholder::asset
{

namespace
{

constexpr u32 kUseColor = 0;   // sRGB base color
constexpr u32 kUseNormal = 1;  // linear normal map
constexpr u32 kUses = 2;

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
    f64 emissive_scale = 0.0;
    f64 alpha_cutoff = 0.5;
    u8 alpha_mode = 0;  // 0 opaque, 1 mask, 2 blend
    bool double_sided = false;
    bool skinned = false;
    bool used_static = false;
    TextureRef color;
    TextureRef normal;
};

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
    if (extensions.Get("KHR_materials_unlit").Exists())
    {
        return RefuseMaterial(error, extensions.Get("KHR_materials_unlit").Path(),
                              StringView("is KHR_materials_unlit, which the engine's materials cannot show"),
                              ErrorCode::Unsupported);
    }
    const JsonValue pbr = material.Get("pbrMetallicRoughness");
    if (pbr.Exists())
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
        if (pbr.Get("metallicRoughnessTexture").Exists())
        {
            AddImportWarning(
                report, pbr.Get("metallicRoughnessTexture").Path(),
                StringView("metallicRoughnessTexture dropped: the template takes scalar metallic and roughness"));
        }
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
    if (material.Get("occlusionTexture").Exists())
    {
        AddImportWarning(report, material.Get("occlusionTexture").Path(),
                         StringView("occlusionTexture dropped: the template has no occlusion map"));
    }
    if (material.Get("emissiveTexture").Exists())
    {
        AddImportWarning(report, material.Get("emissiveTexture").Path(),
                         StringView("emissiveTexture dropped: the template has no emissive map"));
    }
    const JsonValue emissive = material.Get("emissiveFactor");
    if (emissive.Exists())
    {
        f64 largest = 0;
        for (usize k = 0; k < 3; ++k)
        {
            f64 v = 0;
            ENGINE_TRY(emissive.At(k).AsDouble(&v, error));
            largest = v > largest ? v : largest;
        }
        f64 strength = 1.0;
        const JsonValue strength_value = extensions.Get("KHR_materials_emissive_strength").Get("emissiveStrength");
        if (strength_value.Exists())
        {
            ENGINE_TRY(strength_value.AsDouble(&strength, error));
        }
        out->emissive_scale = largest * strength;
        if (largest > 0)
        {
            AddImportWarning(
                report, emissive.Path(),
                StringView(
                    "emissiveFactor approximated by its largest component: the template has an emissive scale only"));
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
        const TextureRef refs[kUses] = {infos[m].color, infos[m].normal};
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
    for (usize i = 0; i < order_texture.Size(); ++i)
    {
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
        if (order_use[i] == kUseNormal)
        {
            raw.Append(StringView("_normal"));
        }
        name_store.Push(static_cast<String<HeapAllocator>&&>(raw));
    }
    for (usize i = 0; i < order_texture.Size(); ++i)
    {
        objects.Push(ManifestObject{order_texture[i] * kUses + order_use[i], name_store[i].View()});
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
    for (usize i = 0; i < order_texture.Size(); ++i)
    {
        u32 source_index = 0;
        if (textures.At(order_texture[i]).Get("source").Exists() &&
            textures.At(order_texture[i]).Get("source").AsIndex(&source_index, images.Size(), error).Ok())
        {
            last_use[source_index] = i;
        }
    }
    Vector<DecodedImage> decoded;
    Vector<bool> have_image;
    decoded.Resize(images.Size());
    have_image.Resize(images.Size());
    for (usize i = 0; i < order_texture.Size(); ++i)
    {
        const JsonValue texture = textures.At(order_texture[i]);
        u32 image_index = 0;
        if (!texture.Get("source").Exists())
        {
            return RefuseMaterial(error, texture.Path(), StringView("has no source image"), ErrorCode::Unsupported);
        }
        ENGINE_TRY(texture.RequiredIndex("source", &image_index, images.Size(), error));
        const JsonValue image = images.At(image_index);
        if (!have_image[image_index])
        {
            Vector<byte> bytes;
            ENGINE_TRY(ReadImageBytes(document, source, options.limits, image, &bytes, error));
            const Status decoded_ok = DecodePixels(ByteSpan(bytes.Data(), bytes.Size()), options.limits.max_pixels,
                                                   &decoded[image_index], error);
            if (decoded_ok.Failed())
            {
                const GltfError inner = *error;  // the sentence is rewritten into the same buffer
                error->Set(image.Path(), StringView(inner.message.Data()));
                return decoded_ok;
            }
            if (decoded[image_index].reduced_depth)
            {
                AddImportWarning(&result->report, image.Path(), StringView("16 bits per channel reduced to 8"));
            }
            have_image[image_index] = true;
        }
        const DecodedImage& pixels = decoded[image_index];
        TextureMintSpec spec;
        spec.format = order_use[i] == kUseColor ? rhi::Format::BC7_SRGB : rhi::Format::BC7_UNorm;
        spec.srgb = order_use[i] == kUseColor;
        spec.drop_alpha = !(order_use[i] == kUseColor && pixels.has_alpha);
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
        if (last_use[image_index] == i)
        {
            decoded[image_index] = DecodedImage();  // its last texture: the pixels are not kept
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
        result->manifest.Add(ImportKind::Texture, order_texture[i] * kUses + order_use[i], name_store[i].View(),
                             resource.id);
        result->resources.Push(static_cast<ImportedResource&&>(resource));
    }
    // The materials.
    Vector<ManifestObject> material_objects;
    for (usize m = 0; m < material_count; ++m)
    {
        material_objects.Push(ManifestObject{static_cast<u32>(m), infos[m].name});
    }
    Vector<ResourceId> material_ids;
    AssignIds(options, ImportKind::Material,
              Span<const ManifestObject>(material_objects.Data(), material_objects.Size()), &material_ids);
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
        const MaterialInfo& info = infos[m];
        String<HeapAllocator> text(HeapAllocator::Default());
        char id[17];
        FormatResourceId(material_ids[m], id);
        char line[256];
        FormatTo(line, sizeof(line), "id %s\ntemplate %s\n", id, info.skinned ? "gbuffer_skinned" : "gbuffer");
        text.Append(StringView(line, CStrLength(line)));
        if (info.normal.present)
        {
            text.Append(StringView("option NORMAL_MAP\n"));
        }
        if (info.alpha_mode == 1)
        {
            text.Append(StringView("option ALPHA_TEST\n"));
        }
        FormatTo(line, sizeof(line),
                 "param base_color color %.9g %.9g %.9g %.9g\nparam roughness float %.9g\nparam metallic float %.9g\n"
                 "param emissive_scale float %.9g\nparam alpha_cutoff float %.9g\n",
                 info.base_color[0], info.base_color[1], info.base_color[2], info.base_color[3], info.roughness,
                 info.metallic, info.emissive_scale, info.alpha_cutoff);
        text.Append(StringView(line, CStrLength(line)));
        const TextureRef refs[kUses] = {info.color, info.normal};
        const char* slots[kUses] = {"albedo_tex", "normal_tex"};
        u32 first_texture = 0xFFFFFFFFu;
        for (u32 use = 0; use < kUses; ++use)
        {
            if (!refs[use].present)
            {
                continue;
            }
            for (usize k = 0; k < made.Size(); ++k)
            {
                if (made[k].texture == refs[use].texture && made[k].use == use)
                {
                    FormatTo(line, sizeof(line), "texture %s %s\n", slots[use], made[k].path.CStr());
                    text.Append(StringView(line, CStrLength(line)));
                }
            }
            first_texture = first_texture == 0xFFFFFFFFu ? refs[use].texture : first_texture;
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
        resource.id = material_ids[m];
        resource.type = types::kMaterial;
        resource.path = String<HeapAllocator>(HeapAllocator::Default(), prefix.View());
        resource.path.Append(StringView("/materials/"));
        resource.path.Append(material_segments[m].View());
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
        result->manifest.Add(ImportKind::Material, static_cast<u32>(m), infos[m].name, resource.id);
        result->resources.Push(static_cast<ImportedResource&&>(resource));
    }
    return Status::Success();
}

}  // namespace engine_placeholder::asset
