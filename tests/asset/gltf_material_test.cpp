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
    // Dependencies come first: two textures (color and normal use of one glTF texture), two materials, the mesh.
    REQUIRE(result.resources.Size() == 5);
    CHECK(result.resources[0].type == types::kTexture);
    CHECK(result.resources[0].path.View() == StringView("/gltf/textures/wall"));
    CHECK(result.resources[1].path.View() == StringView("/gltf/textures/wall_normal"));
    CHECK(result.resources[2].type == types::kMaterial);
    CHECK(result.resources[2].path.View() == StringView("/gltf/materials/brick"));
    CHECK(result.resources[4].type == types::kMesh);
    CHECK(result.report.warnings >= 2);  // metallicRoughnessTexture, the missing UVs ... (the emissive colour is exact)

    TypeRegistry types;
    TextureView color;
    TextureView normal;
    REQUIRE(color.Open(ByteSpan(result.resources[0].blob.Data(), result.resources[0].blob.Size()), types).Ok());
    REQUIRE(normal.Open(ByteSpan(result.resources[1].blob.Data(), result.resources[1].blob.Size()), types).Ok());
    CHECK(color.Format() == rhi::Format::BC7_SRGB);
    CHECK(normal.Format() == rhi::Format::BC7_UNorm);
    CHECK(color.Width() == 8);
    CHECK(color.Height() == 4);
    CHECK(color.MipCount() == 4);
    MaterialAssetData brick;
    REQUIRE(LoadMaterialBlob(ByteSpan(result.resources[2].blob.Data(), result.resources[2].blob.Size()), types, &brick)
                .Ok());
    CHECK(StringView(brick.template_name.text) == StringView("gbuffer"));
    CHECK(brick.textures.Size() == 2);
    CHECK(brick.options.Size() == 1);  // NORMAL_MAP
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
    REQUIRE(LoadMaterialBlob(ByteSpan(result.resources[3].blob.Data(), result.resources[3].blob.Size()), types, &plain)
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

}  // namespace engine_placeholder::asset
