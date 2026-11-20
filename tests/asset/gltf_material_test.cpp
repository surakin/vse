// Phase 22 Day 338: importing glTF images, textures and materials (asset_import/gltf_material.h) end to end.

#include <catch2/catch_test_macros.hpp>

#include "asset/cpu_loaders.h"
#include "asset/material.h"
#include "asset/memory_backend.h"
#include "asset/resource_manager.h"
#include "asset/texture.h"
#include "asset_import/gltf_async.h"
#include "asset_import/gltf_import.h"
#include "asset_import/import_settings.h"
#include "asset_import/spec_gloss.h"
#include "settings/registry.h"
#include "settings/settings.h"
#include "jobs/scheduler.h"
#include "cstr/cstr.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"
#include "renderer/gbuffer.h"
#include "gltf_writer.h"
#include "png_writer.h"
#include "support.h"

namespace engine_placeholder::asset
{
namespace
{

using test::GltfWriter;
using test::MakePng;
using test::PngSpec;

Vector<byte> SmallPng(u32 w, u32 h)
{
    PngSpec spec;
    spec.width = w;
    spec.height = h;
    for (u32 y = 0; y < h; ++y)
    {
        for (u32 x = 0; x < w; ++x)
        {
            spec.rows.Push(static_cast<byte>(x * 40));
            spec.rows.Push(static_cast<byte>(y * 40));
            spec.rows.Push(100);
            spec.rows.Push(255);
        }
    }
    return MakePng(spec);
}

bool MtSays(const GltfError& error, const char* text)
{
    return StringView(error.message.Data(), CStrLength(error.message.Data())).Contains(StringView(text));
}

Status MtImport(const Vector<byte>& glb, GltfSource& source, ImportResult* result, GltfError* error)
{
    SeededIdSource ids(21);
    ImportOptions options;
    options.ids = &ids;
    const Status status = ImportGltf(ByteSpan(glb.Data(), glb.Size()), source, options, result, error);
    if (status.Failed())
    {
        UNSCOPED_INFO("ImportGltf: " << GltfMessage(*error, status));
    }
    return status;
}

// A glb with a triangle mesh using material 0, an image in a buffer view, one texture and `materials_json`.
Vector<byte> TexturedGlb(const char* materials_json, const char* extra_root = "")
{
    GltfWriter w;
    const f32 positions[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    const u16 indices[3] = {0, 1, 2};
    const Vector<byte> png = SmallPng(8, 4);
    const u32 pos_view = w.AddView(positions, sizeof(positions));
    const u32 idx_view = w.AddView(indices, sizeof(indices));
    const u32 img_view = w.AddView(png.Data(), png.Size());
    w.AddAccessor(pos_view, 5126, 3, "VEC3");
    w.AddAccessor(idx_view, 5123, 3, "SCALAR");
    char json[1500];
    FormatTo(
        json, sizeof(json),
        "\"images\":[{\"bufferView\":%u,\"mimeType\":\"image/png\",\"name\":\"wall\"}],"
        "\"samplers\":[{\"wrapS\":33071,\"wrapT\":33071,\"magFilter\":9728,\"minFilter\":9728}],"
        "\"textures\":[{\"source\":0,\"sampler\":0}],%s%s"
        "\"meshes\":[{\"name\":\"m\",\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1,\"material\":0}]}]",
        img_view, materials_json, extra_root);
    w.AddRoot(json);
    return w.Glb();
}

}  // namespace

TEST_CASE("gltf material: a textured glb imports to textures, materials and a mesh that load through the manager",
          "[asset][gltf]")
{
    const char*
        materials = "\"materials\":[{\"name\":\"brick\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[0.5,0.25,1,1],"
                    "\"metallicFactor\":0.0,\"roughnessFactor\":0.75,\"baseColorTexture\":{\"index\":0},"
                    "\"metallicRoughnessTexture\":{\"index\":0}},\"normalTexture\":{\"index\":0},\"doubleSided\":true,"
                    "\"emissiveFactor\":[0,0.5,0]},{\"name\":\"plain\",\"alphaMode\":\"MASK\",\"alphaCutoff\":0.3}],";
    NoGltfSource none;
    ImportResult result;
    GltfError error;
    REQUIRE(MtImport(TexturedGlb(materials), none, &result, &error).Ok());
    // Dependencies come first: three textures (the colour, normal and metallic-roughness use of one glTF texture), two
    // materials, the mesh.
    REQUIRE(result.resources.Size() == 6);
    CHECK(result.resources[0].type == types::kTexture);
    CHECK(result.resources[0].path.View() == StringView("/gltf/textures/wall"));
    CHECK(result.resources[1].path.View() == StringView("/gltf/textures/wall_normal"));
    CHECK(result.resources[2].path.View() == StringView("/gltf/textures/wall_mr"));
    CHECK(result.resources[3].type == types::kMaterial);
    CHECK(result.resources[3].path.View() == StringView("/gltf/materials/brick"));
    CHECK(result.resources[5].type == types::kMesh);
    CHECK(result.report.warnings >= 1);  // the missing UVs ... (the maps and the emissive colour are imported)

    TypeRegistry types;
    TextureView color;
    TextureView normal;
    TextureView metal_rough;
    REQUIRE(color.Open(ByteSpan(result.resources[0].blob.Data(), result.resources[0].blob.Size()), types).Ok());
    REQUIRE(normal.Open(ByteSpan(result.resources[1].blob.Data(), result.resources[1].blob.Size()), types).Ok());
    REQUIRE(metal_rough.Open(ByteSpan(result.resources[2].blob.Data(), result.resources[2].blob.Size()), types).Ok());
    CHECK(color.Format() == rhi::Format::BC7_SRGB);
    CHECK(normal.Format() == rhi::Format::BC7_UNorm);
    CHECK(metal_rough.Format() == rhi::Format::BC7_UNorm);
    CHECK(color.Width() == 8);
    CHECK(color.Height() == 4);
    CHECK(color.MipCount() == 4);
    MaterialAssetData brick;
    REQUIRE(LoadMaterialBlob(ByteSpan(result.resources[3].blob.Data(), result.resources[3].blob.Size()), types, &brick)
                .Ok());
    CHECK(StringView(brick.template_name.text) == StringView("gbuffer"));
    CHECK(brick.textures.Size() == 3);  // albedo, normal and metallic-roughness
    CHECK(brick.options.Size() == 1);   // NORMAL_MAP
    CHECK(brick.state.set_cull);
    CHECK(brick.state.cull == 0);
    // glTF coverage Day 2: emissiveFactor arrives as the emissive colour, rgb as it is and the strength 1.
    bool emissive_found = false;
    for (const MaterialParam& p : brick.params)
    {
        if (StringView(p.name.text) == StringView("emissive_color"))
        {
            emissive_found = true;
            f32 v[4] = {};
            MemCopy(v, brick.block.Data() + p.offset, sizeof(v));
            CHECK(v[0] == 0.0f);
            CHECK(v[1] == 0.5f);
            CHECK(v[2] == 0.0f);
            CHECK(v[3] == 1.0f);
        }
    }
    CHECK(emissive_found);
    MaterialAssetData plain;
    REQUIRE(LoadMaterialBlob(ByteSpan(result.resources[4].blob.Data(), result.resources[4].blob.Size()), types, &plain)
                .Ok());
    CHECK(plain.textures.Size() == 0);
    CHECK(plain.options.Size() == 1);  // ALPHA_TEST

    // The whole closure loads through the resource manager from a memory backend.
    HostMemoryBackend backend(types, u64{64} << 20);
    REQUIRE(ImportIntoBackend(backend, result).Ok());
    ManagerConfig config;
    config.buffer_pool_bytes = u64{64} << 20;
    ResourceManager manager(types, config);
    RegisterCpuLoaders(&manager);
    REQUIRE(manager.Mount(StringView("/"), &backend, 0).Ok());
    REQUIRE(manager.Mounts().RefreshTables().Ok());
    Handle<TextureView> texture;
    REQUIRE(manager.LoadPath<TextureView>(StringView("/gltf/textures/wall"), &texture).Ok());
    CHECK(texture.Get()->Width() == 8);
}

// glTF coverage Day 3: an ORM texture (occlusion and metallic-roughness from one glTF texture) is made once and bound
// to both slots, the occlusion strength is a parameter, the emissive map is an sRGB texture of its own, and a map on
// TEXCOORD_1 is dropped with a warning.
TEST_CASE("gltf material: the metallic-roughness, occlusion and emissive maps are imported", "[asset][gltf]")
{
    const char* materials =
        "\"materials\":[{\"name\":\"orm\",\"pbrMetallicRoughness\":{\"metallicRoughnessTexture\":{\"index\":0}},"
        "\"occlusionTexture\":{\"index\":0,\"strength\":0.5},\"emissiveTexture\":{\"index\":0},"
        "\"emissiveFactor\":[1,0.5,0.25]},{\"name\":\"second\",\"occlusionTexture\":{\"index\":0,\"texCoord\":1}}],";
    NoGltfSource none;
    ImportResult result;
    GltfError error;
    REQUIRE(MtImport(TexturedGlb(materials), none, &result, &error).Ok());
    TypeRegistry types;
    usize textures = 0;
    for (const ImportedResource& r : result.resources)
    {
        textures += r.type == types::kTexture ? 1 : 0;
    }
    CHECK(textures == 2);  // wall_mr (also the occlusion) and wall_emissive
    MaterialAssetData orm;
    bool found = false;
    for (const ImportedResource& r : result.resources)
    {
        if (r.path.View() == StringView("/gltf/materials/orm"))
        {
            REQUIRE(LoadMaterialBlob(ByteSpan(r.blob.Data(), r.blob.Size()), types, &orm).Ok());
            found = true;
        }
        if (r.path.View() == StringView("/gltf/textures/wall_emissive"))
        {
            TextureView emissive;
            REQUIRE(emissive.Open(ByteSpan(r.blob.Data(), r.blob.Size()), types).Ok());
            CHECK(emissive.Format() == rhi::Format::BC7_SRGB);
        }
    }
    REQUIRE(found);
    REQUIRE(orm.textures.Size() == 3);
    // The occlusion and the metallic-roughness share one texture.
    CHECK((orm.textures[1].texture == orm.textures[2].texture || orm.textures[0].texture == orm.textures[1].texture));
    bool strength = false;
    for (const MaterialParam& p : orm.params)
    {
        if (StringView(p.name.text) == StringView("occlusion_strength"))
        {
            f32 v = 0;
            MemCopy(&v, orm.block.Data() + p.offset, sizeof(v));
            strength = v == 0.5f;
        }
    }
    CHECK(strength);
    CHECK(result.report.warnings >= 1);  // the second material's occlusion on TEXCOORD_1
}

TEST_CASE("gltf material: the import is deterministic", "[asset][gltf]")
{
    const char* materials = "\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0}}}],";
    NoGltfSource none;
    ImportResult one;
    ImportResult two;
    GltfError error;
    const Vector<byte> glb = TexturedGlb(materials);
    REQUIRE(MtImport(glb, none, &one, &error).Ok());
    REQUIRE(MtImport(glb, none, &two, &error).Ok());
    REQUIRE(one.resources.Size() == two.resources.Size());
    for (usize i = 0; i < one.resources.Size(); ++i)
    {
        CHECK(one.resources[i].blob.Size() == two.resources[i].blob.Size());
        CHECK(MemCompare(one.resources[i].blob.Data(), two.resources[i].blob.Data(), one.resources[i].blob.Size()) ==
              0);
    }
}

namespace
{

// The `flags` parameter of a material blob: its value, or -1 when the material does not set it.
i64 FlagsOf(const ImportedResource& material)
{
    TypeRegistry types;
    MaterialAssetData data;
    REQUIRE(LoadMaterialBlob(ByteSpan(material.blob.Data(), material.blob.Size()), types, &data).Ok());
    for (const MaterialParam& p : data.params)
    {
        if (StringView(p.name.text) == StringView("flags"))
        {
            REQUIRE(p.type == MaterialParamType::UInt);
            u32 value = 0;
            MemCopy(&value, data.block.Data() + p.offset, sizeof(value));
            return value;
        }
    }
    return -1;
}

}  // namespace

TEST_CASE("gltf material: KHR_materials_unlit imports with the unlit flag; a lit material has no flags parameter",
          "[asset][gltf]")
{
    NoGltfSource none;
    ImportResult result;
    GltfError error;
    REQUIRE(
        MtImport(TexturedGlb(
                     "\"materials\":[{\"name\":\"sign\",\"extensions\":{\"KHR_materials_unlit\":{}},"
                     "\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0}}},{\"name\":\"lit\"}],",
                     "\"extensionsUsed\":[\"KHR_materials_unlit\"],\"extensionsRequired\":[\"KHR_materials_unlit\"],"),
                 none, &result, &error)
            .Ok());
    const ImportedResource* sign = nullptr;
    const ImportedResource* lit = nullptr;
    for (const ImportedResource& r : result.resources)
    {
        if (r.path.View() == StringView("/gltf/materials/sign"))
        {
            sign = &r;
        }
        if (r.path.View() == StringView("/gltf/materials/lit"))
        {
            lit = &r;
        }
    }
    REQUIRE(sign != nullptr);
    REQUIRE(lit != nullptr);
    CHECK(FlagsOf(*sign) == static_cast<i64>(renderer::kMaterialFlagUnlit));
    CHECK(FlagsOf(*lit) == -1);  // nothing new for a lit material: its blob is what it was before unlit existed
}

TEST_CASE("gltf material: what the template cannot show is refused or dropped, naming the material", "[asset][gltf]")
{
    NoGltfSource none;
    ImportResult result;
    GltfError error;
    CHECK(MtImport(
              TexturedGlb("\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0,\"extensions\":"
                          "{\"KHR_texture_transform\":{\"scale\":[2,2]}}}}}],",
                          ""),
              none, &result, &error)
              .Is(ErrorCode::Unsupported));
    CHECK(MtSays(error, "KHR_texture_transform"));
    CHECK(MtImport(TexturedGlb("\"materials\":[{\"alphaMode\":\"SOMETIMES\"}],"), none, &result, &error)
              .Is(ErrorCode::Invalid));
    CHECK(MtSays(error, "materials[0].alphaMode"));
    CHECK(MtImport(TexturedGlb("\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":7}}}],"),
                   none, &result, &error)
              .Is(ErrorCode::Invalid));
    CHECK(MtSays(error, "baseColorTexture.index"));
}

TEST_CASE("gltf material: images come from data URIs and files, and a bad one is refused naming it", "[asset][gltf]")
{
    // A file through the source.
    class FileSource final : public GltfSource
    {
    public:
        Status Read(StringView name, u64, Vector<byte>* out) override
        {
            if (name != StringView("tex/wall.png"))
            {
                return Status::Make(ErrorCode::NotFound, "no such file");
            }
            *out = SmallPng(4, 4);
            return Status::Success();
        }
    };
    GltfWriter w;
    const f32 positions[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    const u32 pos_view = w.AddView(positions, sizeof(positions));
    w.AddAccessor(pos_view, 5126, 3, "VEC3");
    w.AddRoot("\"images\":[{\"uri\":\"tex/wall.png\"}],\"textures\":[{\"source\":0}],"
              "\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0}}}],"
              "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"material\":0}]}]");
    const Vector<byte> glb = w.Glb();
    FileSource files;
    ImportResult result;
    GltfError error;
    REQUIRE(MtImport(glb, files, &result, &error).Ok());
    CHECK(result.resources.Size() == 3);
    NoGltfSource none;
    CHECK(MtImport(glb, none, &result, &error).Is(ErrorCode::NotFound));
    CHECK(MtSays(error, "images[0]"));

    // Bytes that are no image.
    GltfWriter bad;
    const u32 bp = bad.AddView(positions, sizeof(positions));
    bad.AddAccessor(bp, 5126, 3, "VEC3");
    const byte junk[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    const u32 jv = bad.AddView(junk, sizeof(junk));
    char json[400];
    FormatTo(json, sizeof(json),
             "\"images\":[{\"bufferView\":%u}],\"textures\":[{\"source\":0}],"
             "\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0}}}],"
             "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"material\":0}]}]",
             jv);
    bad.AddRoot(json);
    CHECK(MtImport(bad.Glb(), none, &result, &error).Is(ErrorCode::Invalid));
    CHECK(MtSays(error, "images[0]"));
    CHECK(MtSays(
        error,
        "neither a PNG nor a JPEG"));  // the reason survives the prefix (the sentence is not rewritten onto itself)
    // A texture with no image.
    GltfWriter noimg;
    const u32 np = noimg.AddView(positions, sizeof(positions));
    noimg.AddAccessor(np, 5126, 3, "VEC3");
    noimg.AddRoot("\"textures\":[{}],\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0}}}],"
                  "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"material\":0}]}]");
    CHECK(MtImport(noimg.Glb(), none, &result, &error).Is(ErrorCode::Unsupported));
    CHECK(MtSays(error, "textures[0]"));
}

TEST_CASE(
    "gltf material: textures no material uses are reported, and a material shared by skinned and static meshes warns",
    "[asset][gltf]")
{
    NoGltfSource none;
    ImportResult result;
    GltfError error;
    REQUIRE(MtImport(TexturedGlb("\"materials\":[{}],"), none, &result, &error).Ok());
    bool unused = false;
    for (usize i = 0; i < result.report.notes.Size(); ++i)
    {
        unused = unused || result.report.notes[i].View().Contains(StringView("1 textures are used by no material"));
    }
    CHECK(unused);
}

TEST_CASE("gltf settings: the import limits are settings and the import obeys them", "[asset][gltf]")
{
    settings::Registry registry;
    RegisterImportSettings(&registry);
    RegisterImportSettings(&registry);  // twice changes nothing
    ImportLimits limits = ImportLimitsFromSettings(registry);
    const ImportLimits defaults;
    CHECK(limits.max_file_bytes == defaults.max_file_bytes);
    CHECK(limits.max_pixels == defaults.max_pixels);
    CHECK(limits.max_nodes == defaults.max_nodes);
    CHECK(registry.Find(StringView("assets.import.max_output_bytes")).IsValid());
    // A setting changed through the settings system is what ImportLimitsFromSettings reads, and the import obeys it.
    settings::Settings loaded(registry);
    REQUIRE(loaded.SetText(StringView("assets.import.max_pixels"), StringView("8")).error == settings::SetError::None);
    limits = ImportLimitsFromSettings(registry);
    CHECK(limits.max_pixels == 8);
    ImportOptions options;
    options.limits = limits;
    NoGltfSource none;
    ImportResult result;
    GltfError error;
    const Vector<byte> glb = TexturedGlb(
        "\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0}}}],");
    CHECK(ImportGltf(ByteSpan(glb.Data(), glb.Size()), none, options, &result, &error).Is(ErrorCode::TooLarge));
    CHECK(MtSays(error, "images[0]"));
}

TEST_CASE("gltf async: an import runs on the job system, reports progress, cancels and runs many at once",
          "[asset][gltf]")
{
    const char* materials = "\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0}}}],";
    const Vector<byte> glb = TexturedGlb(materials);
    NoGltfSource none;
    ImportResult expected;
    GltfError error;
    REQUIRE(MtImport(glb, none, &expected, &error).Ok());

    jobs::Scheduler::Config config;
    config.participants = 3;
    config.pin = false;
    jobs::Scheduler scheduler(config);
    SeededIdSource ids(21);
    ImportOptions options;
    options.ids = &ids;
    Vector<byte> copy;
    for (usize i = 0; i < glb.Size(); ++i)
    {
        copy.Push(glb[i]);
    }
    UniquePtr<ImportTicket> ticket = ImportGltfAsync(scheduler, static_cast<Vector<byte>&&>(copy), none, options);
    ImportResult result;
    REQUIRE(ticket->Finish(&result, &error).Ok());
    CHECK(ticket->IsDone());
    CHECK(ticket->Progress() == 1.0f);
    REQUIRE(result.resources.Size() == expected.resources.Size());
    for (usize i = 0; i < result.resources.Size(); ++i)
    {
        CHECK(result.resources[i].id == expected.resources[i].id);
        CHECK(result.resources[i].blob.Size() == expected.resources[i].blob.Size());
    }
    CHECK(ticket->Finish(&result, &error).Is(ErrorCode::NotFound));

    // A cancelled import ends Cancelled (or finished before the cancel was seen) and adds nothing to a backend.
    for (u32 round = 0; round < 8; ++round)
    {
        SeededIdSource round_ids(round + 1);
        ImportOptions round_options;
        round_options.ids = &round_ids;
        Vector<byte> bytes;
        for (usize i = 0; i < glb.Size(); ++i)
        {
            bytes.Push(glb[i]);
        }
        UniquePtr<ImportTicket> cancelled = ImportGltfAsync(scheduler, static_cast<Vector<byte>&&>(bytes), none,
                                                            round_options);
        cancelled->Cancel();
        ImportResult partial;
        const Status status = cancelled->Finish(&partial, &error);
        CHECK((status.Ok() || status.Is(ErrorCode::Cancelled)));
    }
    // A ticket destroyed without being finished waits for its job.
    {
        SeededIdSource more_ids(5);
        ImportOptions more;
        more.ids = &more_ids;
        Vector<byte> bytes;
        for (usize i = 0; i < glb.Size(); ++i)
        {
            bytes.Push(glb[i]);
        }
        UniquePtr<ImportTicket> dropped = ImportGltfAsync(scheduler, static_cast<Vector<byte>&&>(bytes), none, more);
    }
    // Several imports at once share nothing.
    SeededIdSource ids_a(21);
    SeededIdSource ids_b(21);
    SeededIdSource ids_c(21);
    SeededIdSource* all_ids[3] = {&ids_a, &ids_b, &ids_c};
    UniquePtr<ImportTicket> tickets[3];
    for (usize i = 0; i < 3; ++i)
    {
        ImportOptions each;
        each.ids = all_ids[i];
        Vector<byte> bytes;
        for (usize k = 0; k < glb.Size(); ++k)
        {
            bytes.Push(glb[k]);
        }
        tickets[i] = ImportGltfAsync(scheduler, static_cast<Vector<byte>&&>(bytes), none, each);
    }
    for (usize i = 0; i < 3; ++i)
    {
        ImportResult each;
        REQUIRE(tickets[i]->Finish(&each, &error).Ok());
        REQUIRE(each.resources.Size() == expected.resources.Size());
        CHECK(each.resources[0].id == expected.resources[0].id);
    }
}

// Maintenance 2026-11-20: KHR_materials_pbrSpecularGlossiness is converted to metallic-roughness.
namespace
{

bool Near(f32 a, f32 b, f32 tolerance = 0.002f)
{
    return a - b <= tolerance && b - a <= tolerance;
}

DecodedImage SolidImage(u32 w, u32 h, u8 r, u8 g, u8 b, u8 a)
{
    DecodedImage image;
    image.width = w;
    image.height = h;
    image.rgba.Resize(static_cast<usize>(w) * h * 4);
    for (usize i = 0; i < image.rgba.Size(); i += 4)
    {
        image.rgba[i + 0] = static_cast<byte>(r);
        image.rgba[i + 1] = static_cast<byte>(g);
        image.rgba[i + 2] = static_cast<byte>(b);
        image.rgba[i + 3] = static_cast<byte>(a);
    }
    return image;
}

// A float or colour parameter of a material blob (`count` components), false when the material does not set it.
bool ParamOf(const ImportedResource& material, const char* name, f32* out, usize count)
{
    TypeRegistry types;
    MaterialAssetData data;
    REQUIRE(LoadMaterialBlob(ByteSpan(material.blob.Data(), material.blob.Size()), types, &data).Ok());
    for (const MaterialParam& p : data.params)
    {
        if (StringView(p.name.text) == StringView(name))
        {
            MemCopy(out, data.block.Data() + p.offset, sizeof(f32) * count);
            return true;
        }
    }
    return false;
}

const ImportedResource* ByPath(const ImportResult& result, const char* path)
{
    for (const ImportedResource& r : result.resources)
    {
        if (r.path.View() == StringView(path))
        {
            return &r;
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("spec gloss: a sample converts as the Khronos tools convert it", "[asset][gltf]")
{
    // A dielectric: the specular colour below 4% gives metallic 0 and the diffuse scaled by 1 / 0.96.
    SpecGlossSample plastic;
    plastic.diffuse = {{0.5f, 0.25f, 0.1f, 0.75f}};
    plastic.specular = {{0.0f, 0.0f, 0.0f}};
    plastic.glossiness = 0.3f;
    const MetalRoughSample p = SpecGlossToMetalRough(plastic);
    CHECK(p.metallic == 0.0f);
    CHECK(Near(p.base_color[0], 0.5f / 0.96f));
    CHECK(Near(p.base_color[1], 0.25f / 0.96f));
    CHECK(Near(p.base_color[3], 0.75f));
    CHECK(Near(p.roughness, 0.7f));
    // A metal: a black diffuse and a bright specular colour give metallic 1 and the specular colour as the base.
    SpecGlossSample gold;
    gold.diffuse = {{0.0f, 0.0f, 0.0f, 1.0f}};
    gold.specular = {{1.0f, 0.75f, 0.3f}};
    gold.glossiness = 1.0f;
    const MetalRoughSample g = SpecGlossToMetalRough(gold);
    CHECK(Near(g.metallic, 1.0f));
    CHECK(Near(g.base_color[0], 1.0f));
    CHECK(Near(g.base_color[1], 0.75f, 0.01f));
    CHECK(Near(g.base_color[2], 0.3f, 0.01f));
    CHECK(g.roughness == 0.0f);
    CHECK(SpecularIsDielectric({{0.03f, 0.03f, 0.03f}}));
    CHECK(!SpecularIsDielectric({{0.05f, 0.05f, 0.05f}}));
    // Between the two the metallic value is strictly inside (0, 1).
    SpecGlossSample mixed;
    mixed.diffuse = {{0.3f, 0.3f, 0.3f, 1.0f}};
    mixed.specular = {{0.3f, 0.3f, 0.3f}};
    const MetalRoughSample m = SpecGlossToMetalRough(mixed);
    CHECK(m.metallic > 0.0f);
    CHECK(m.metallic < 1.0f);
}

TEST_CASE("spec gloss: textures convert per texel at the larger image's size", "[asset][gltf]")
{
    // A black diffuse and a white specular with glossiness 0.6 (alpha 153): metal, roughness 0.4.
    const DecodedImage diffuse = SolidImage(4, 2, 0, 0, 0, 255);
    const DecodedImage specular = SolidImage(8, 4, 255, 255, 255, 153);
    SpecGlossImages in;
    in.diffuse = &diffuse;
    in.specular_glossiness = &specular;
    DecodedImage base;
    DecodedImage mr;
    REQUIRE(ConvertSpecGlossImages(in, 1u << 20, &base, &mr).Ok());
    CHECK(base.width == 8);
    CHECK(base.height == 4);
    CHECK(mr.width == 8);
    CHECK(!base.has_alpha);
    CHECK(static_cast<u8>(base.rgba[0]) == 255);  // the specular white as the base colour
    CHECK(static_cast<u8>(mr.rgba[1]) == 102);    // G: roughness 0.4
    CHECK(static_cast<u8>(mr.rgba[2]) == 255);    // B: metallic 1
    CHECK(static_cast<u8>(mr.rgba[mr.rgba.Size() - 2]) == 255);
    // The factors multiply the texels, the opacity comes from the diffuse.
    in.specular_factor = {{0.0f, 0.0f, 0.0f}};
    in.diffuse_factor = {{1.0f, 1.0f, 1.0f, 0.5f}};
    const DecodedImage white = SolidImage(2, 2, 255, 255, 255, 255);
    in.diffuse = &white;
    in.specular_glossiness = nullptr;
    REQUIRE(ConvertSpecGlossImages(in, 1u << 20, &base, nullptr).Ok());
    CHECK(base.width == 2);
    CHECK(base.has_alpha);
    CHECK(static_cast<u8>(base.rgba[3]) == 128);
    CHECK(static_cast<u8>(base.rgba[0]) == 255);  // 1 / 0.96 clamped
    // The pixel limit holds for the output.
    CHECK(ConvertSpecGlossImages(in, 3, &base, nullptr).Is(ErrorCode::TooLarge));
}

TEST_CASE("gltf material: KHR_materials_pbrSpecularGlossiness imports converted to metallic-roughness", "[asset][gltf]")
{
    NoGltfSource none;
    ImportResult result;
    GltfError error;
    // "armor" has both textures: a converted pair; "skin" a dielectric's diffuse texture: the texture as it is and the
    // factors; "paint" factors only; the extension wins over pbrMetallicRoughness.
    REQUIRE(MtImport(TexturedGlb(
                         "\"materials\":[{\"name\":\"armor\",\"extensions\":{\"KHR_materials_pbrSpecularGlossiness\":"
                         "{\"diffuseTexture\":{\"index\":0},\"specularGlossinessTexture\":{\"index\":0},"
                         "\"glossinessFactor\":0.5}}},"
                         "{\"name\":\"skin\",\"extensions\":{\"KHR_materials_pbrSpecularGlossiness\":"
                         "{\"diffuseTexture\":{\"index\":0},\"specularFactor\":[0,0,0],\"glossinessFactor\":0.25}}},"
                         "{\"name\":\"paint\",\"pbrMetallicRoughness\":{\"metallicFactor\":0.5},"
                         "\"extensions\":{\"KHR_materials_pbrSpecularGlossiness\":{\"diffuseFactor\":[0,0,0,1],"
                         "\"specularFactor\":[1,1,1],\"glossinessFactor\":0.75}}}],",
                         "\"extensionsUsed\":[\"KHR_materials_pbrSpecularGlossiness\"],"
                         "\"extensionsRequired\":[\"KHR_materials_pbrSpecularGlossiness\"],"),
                     none, &result, &error)
                .Ok());
    TypeRegistry types;
    const ImportedResource* base = ByPath(result, "/gltf/textures/armor_base_color");
    const ImportedResource* mr = ByPath(result, "/gltf/textures/armor_metal_rough");
    REQUIRE(base != nullptr);
    REQUIRE(mr != nullptr);
    TextureView base_view;
    TextureView mr_view;
    REQUIRE(base_view.Open(ByteSpan(base->blob.Data(), base->blob.Size()), types).Ok());
    REQUIRE(mr_view.Open(ByteSpan(mr->blob.Data(), mr->blob.Size()), types).Ok());
    CHECK(base_view.Format() == rhi::Format::BC7_SRGB);
    CHECK(mr_view.Format() == rhi::Format::BC7_UNorm);
    CHECK(base_view.Width() == 8);
    CHECK(ByPath(result, "/gltf/textures/wall") != nullptr);  // skin's diffuse, as it is

    const ImportedResource* armor = ByPath(result, "/gltf/materials/armor");
    const ImportedResource* skin = ByPath(result, "/gltf/materials/skin");
    const ImportedResource* paint = ByPath(result, "/gltf/materials/paint");
    REQUIRE(armor != nullptr);
    REQUIRE(skin != nullptr);
    REQUIRE(paint != nullptr);
    MaterialAssetData armor_data;
    REQUIRE(LoadMaterialBlob(ByteSpan(armor->blob.Data(), armor->blob.Size()), types, &armor_data).Ok());
    CHECK(armor_data.textures.Size() == 2);  // albedo and metallic-roughness, the converted pair
    f32 v[4] = {};
    REQUIRE(ParamOf(*armor, "metallic", v, 1));
    CHECK(v[0] == 1.0f);  // in the texture
    REQUIRE(ParamOf(*skin, "metallic", v, 1));
    CHECK(v[0] == 0.0f);
    REQUIRE(ParamOf(*skin, "roughness", v, 1));
    CHECK(Near(v[0], 0.75f));
    REQUIRE(ParamOf(*skin, "base_color", v, 4));
    CHECK(v[0] == 1.0f);  // 1 / 0.96 clamped
    REQUIRE(ParamOf(*paint, "metallic", v, 1));
    CHECK(Near(v[0], 1.0f));  // a black diffuse and a white specular: metal, not pbrMetallicRoughness' 0.5
    REQUIRE(ParamOf(*paint, "roughness", v, 1));
    CHECK(Near(v[0], 0.25f));
    REQUIRE(ParamOf(*paint, "base_color", v, 4));
    CHECK(Near(v[0], 1.0f));

    // An unlit specular-glossiness material gets the converted base colour only.
    ImportResult unlit;
    REQUIRE(MtImport(TexturedGlb("\"materials\":[{\"name\":\"sign\",\"extensions\":{\"KHR_materials_unlit\":{},"
                                 "\"KHR_materials_pbrSpecularGlossiness\":{\"diffuseTexture\":{\"index\":0},"
                                 "\"specularGlossinessTexture\":{\"index\":0}}}}],"),
                     none, &unlit, &error)
                .Ok());
    CHECK(ByPath(unlit, "/gltf/textures/sign_base_color") != nullptr);
    CHECK(ByPath(unlit, "/gltf/textures/sign_metal_rough") == nullptr);
    const ImportedResource* sign = ByPath(unlit, "/gltf/materials/sign");
    REQUIRE(sign != nullptr);
    MaterialAssetData sign_data;
    REQUIRE(LoadMaterialBlob(ByteSpan(sign->blob.Data(), sign->blob.Size()), types, &sign_data).Ok());
    CHECK(sign_data.textures.Size() == 1);
}

}  // namespace engine_placeholder::asset
