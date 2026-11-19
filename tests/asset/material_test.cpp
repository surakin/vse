// The material format, the loader and the material minter (Phase 22 Day 326).

#include <catch2/catch_test_macros.hpp>

#include "asset/material.h"
#include "cstr/cstr.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"
#include "material_source.h"
#include "texture_source.h"

namespace engine_placeholder::asset
{

namespace
{

// Texture blobs under paths, as the minter's resolver.
class MtResolver final : public MaterialReferenceResolver
{
public:
    struct Entry
    {
        Array<char, 64> path = {};
        ResourceId id;
        Vector<byte> blob;
    };

    Status Resolve(StringView path, ResourceId* id, ByteSpan* blob) const override
    {
        for (usize i = 0; i < entries.Size(); ++i)
        {
            if (StringView(entries[i].path.Data(), CStrLength(entries[i].path.Data())) == path)
            {
                *id = entries[i].id;
                *blob = ByteSpan(entries[i].blob.Data(), entries[i].blob.Size());
                return Status::Success();
            }
        }
        return Status::Make(ErrorCode::NotFound, "no such texture");
    }

    Vector<Entry> entries;
};

// Mints a texture of `images` raw files (all 4x4) from `source` (the id line is prepended) and files it under `path`.
void MtAddTexture(MtResolver* r, const char* path, u32 id_low, const char* body, u32 images)
{
    char text[512];
    FormatTo(text, sizeof(text), "id 00000000000%05x\n%s", id_low, body);
    Vector<byte> pixels;
    pixels.Resize(4 * 4 * 4);
    for (usize i = 0; i < pixels.Size(); ++i)
    {
        pixels[i] = static_cast<byte>(i * 7);
    }
    Vector<Vector<byte>> files;
    Vector<ByteSpan> spans;
    for (u32 i = 0; i < images; ++i)
    {
        Vector<byte> raw;
        REQUIRE(BuildRawImage(4, 4, ByteSpan(pixels.Data(), pixels.Size()), 0, &raw).Ok());
        files.Push(static_cast<Vector<byte>&&>(raw));
    }
    for (usize i = 0; i < files.Size(); ++i)
    {
        spans.Push(ByteSpan(files[i].Data(), files[i].Size()));
    }
    TypeRegistry types;
    MtResolver::Entry e;
    FormatTo(e.path.Data(), e.path.Size(), "%s", path);
    REQUIRE(ParseResourceId(StringView(text + 3, 16), &e.id).Ok());
    TextureSourceError error;
    const Status s = MintTexture(StringView(text, CStrLength(text)), Span<const ByteSpan>(spans.Data(), spans.Size()),
                                 types, &e.blob, &error);
    INFO(error.message.Data());
    REQUIRE(s.Ok());
    r->entries.Push(static_cast<MtResolver::Entry&&>(e));
}

MtResolver MtMakeResolver()
{
    MtResolver r;
    MtAddTexture(&r, "albedo.texr", 0x101, "source a.raw\nformat rgba8\nsrgb true\n", 1);
    MtAddTexture(&r, "normal.texr", 0x102, "source n.raw\nformat rgba8\n", 1);
    MtAddTexture(&r, "other.texr", 0x103, "source o.raw\nformat rgba8\nsrgb true\n", 1);
    MtAddTexture(&r, "sky.texr", 0x104, "kind cube\nsource 1\nsource 2\nsource 3\nsource 4\nsource 5\nsource 6\n", 6);
    return r;
}

Status MtMint(const char* text, const MtResolver& r, Vector<byte>* out, MaterialSourceError* error, u32 target = 0)
{
    TypeRegistry types;
    return MintMaterial(StringView(text, CStrLength(text)), r, types, out, error, target);
}

constexpr const char* kMtId = "id 00000000000000a1\n";

Vector<byte> MtMintOk(const char* body, const MtResolver& r)
{
    char text[1024];
    FormatTo(text, sizeof(text), "%s%s", kMtId, body);
    Vector<byte> blob;
    MaterialSourceError error;
    const Status s = MtMint(text, r, &blob, &error);
    INFO(error.message.Data());
    REQUIRE(s.Ok());
    return blob;
}

f32 MtFloatAt(const MaterialAssetData& m, u32 offset)
{
    f32 v = 0;
    MemCopy(&v, m.block.Data() + offset, 4);
    return v;
}

}  // namespace

TEST_CASE("a skinnable and a non-skinnable material set the flag from the template", "[asset][material]")
{
    const MtResolver r = MtMakeResolver();
    TypeRegistry types;
    MaterialAssetData plain;
    MaterialAssetData skinned;
    const Vector<byte> a = MtMintOk("template gbuffer\n", r);
    const Vector<byte> b = MtMintOk("template gbuffer_skinned\n", r);
    REQUIRE(LoadMaterialBlob(ByteSpan(a.Data(), a.Size()), types, &plain).Ok());
    REQUIRE(LoadMaterialBlob(ByteSpan(b.Data(), b.Size()), types, &skinned).Ok());
    CHECK((plain.flags & kMaterialSkinnable) == 0);
    CHECK((skinned.flags & kMaterialSkinnable) != 0);
}

TEST_CASE("a valid material round-trips with std140 parameters and typed texture references", "[asset][material]")
{
    const MtResolver r = MtMakeResolver();
    const Vector<byte> blob = MtMintOk("template gbuffer\n"
                                       "option NORMAL_MAP\n"
                                       "param roughness float 0.25\n"
                                       "param base_color color 1 0.5 0.25 1\n"
                                       "texture albedo_tex albedo.texr\n"
                                       "texture normal_tex normal.texr\n"
                                       "sampler linear_sampler filter nearest address clamp anisotropy 8\n"
                                       "state cull none\n"
                                       "state alpha_blend on\n",
                                       r);
    TypeRegistry types;
    MaterialAssetData m;
    ResourceId id;
    REQUIRE(LoadMaterialBlob(ByteSpan(blob.Data(), blob.Size()), types, &m, &id).Ok());
    CHECK(id == ResourceId::FromValue(0xa1));
    CHECK(StringView(m.template_name.text, CStrLength(m.template_name.text)) == StringView("gbuffer"));
    CHECK(m.block.Size() == 96);  // the std140 block (the albedo_uv and albedo_uv_outside vectors at 48 and 64, the
                                  // occlusion strength at 80, the block rounded to 16 bytes)
    CHECK(MtFloatAt(m, 16) == 0.25f);
    CHECK(MtFloatAt(m, 20) == 0.0f);  // the template's default metallic
    CHECK(MtFloatAt(m, 4) == 0.5f);   // base_color.g
    CHECK(MtFloatAt(m, 28) == 0.5f);  // the default alpha_cutoff
    CHECK(MtFloatAt(m, 48) == 1.0f);  // the default albedo_uv: scale 1, 1, offset 0, 0
    CHECK(MtFloatAt(m, 52) == 1.0f);
    CHECK(MtFloatAt(m, 56) == 0.0f);
    CHECK(MtFloatAt(m, 60) == 0.0f);
    CHECK(MtFloatAt(m, 76) == 0.0f);  // albedo_uv_outside: alpha 0, no mask
    CHECK(MtFloatAt(m, 80) == 1.0f);  // the default occlusion_strength (glTF coverage Day 1)
    CHECK(m.options.Size() == 1);
    CHECK(m.params.Size() == 2);
    REQUIRE(m.textures.Size() == 2);
    CHECK(m.textures[0].texture == ResourceId::FromValue(0x101));
    CHECK(m.textures[1].texture == ResourceId::FromValue(0x102));
    REQUIRE(m.samplers.Size() == 1);
    CHECK(m.samplers[0].min_filter == MaterialFilter::Nearest);
    CHECK(m.samplers[0].address_u == MaterialAddress::ClampToEdge);
    CHECK(m.samplers[0].max_anisotropy == 8);
    CHECK(m.state.set_cull);
    CHECK(m.state.cull == 0);
    CHECK(m.state.set_alpha_blend);
    CHECK(m.state.alpha_blend);
    CHECK_FALSE(m.state.set_depth_test);
}

// glTF coverage Day 1: the metallic-roughness and occlusion maps are slots of every G-buffer template, with no
// permutation option, and take linear textures.
TEST_CASE("a material binds linear metallic-roughness and occlusion maps without an option", "[asset][material]")
{
    const MtResolver r = MtMakeResolver();
    const Vector<byte> blob = MtMintOk("template gbuffer\n"
                                       "param occlusion_strength float 0.5\n"
                                       "texture metallic_roughness_tex normal.texr\n"
                                       "texture occlusion_tex normal.texr\n",
                                       r);
    TypeRegistry types;
    MaterialAssetData m;
    REQUIRE(LoadMaterialBlob(ByteSpan(blob.Data(), blob.Size()), types, &m).Ok());
    REQUIRE(m.textures.Size() == 2);
    CHECK(StringView(m.textures[0].slot.text, CStrLength(m.textures[0].slot.text)) ==
          StringView("metallic_roughness_tex"));
    CHECK(StringView(m.textures[1].slot.text, CStrLength(m.textures[1].slot.text)) == StringView("occlusion_tex"));
    CHECK(MtFloatAt(m, 80) == 0.5f);
}

TEST_CASE("a material with three textures lists three dependencies", "[asset][material]")
{
    MtResolver r = MtMakeResolver();
    const Vector<byte> blob = MtMintOk("template gbuffer\n"
                                       "option NORMAL_MAP\n"
                                       "texture albedo_tex albedo.texr\n"
                                       "texture normal_tex normal.texr\n",
                                       r);
    TypeRegistry types;
    BlobReader reader;
    REQUIRE(reader.Open(ByteSpan(blob.Data(), blob.Size()), types).Ok());
    CHECK(reader.DependencyCount() == 2);
    // Three bindings, one of a slot the template does not have (the blob builder does not check slots); a repeated
    // texture is one dependency.
    MaterialAssetData data;
    data.template_name = MaterialName();
    MakeMaterialName("gbuffer", 7, &data.template_name);
    for (u32 i = 0; i < 3; ++i)
    {
        static const char* const kSlots[3] = {"albedo_tex", "normal_tex", "extra_tex"};
        MaterialTextureBinding b;
        MakeMaterialName(kSlots[i], CStrLength(kSlots[i]), &b.slot);
        b.texture = ResourceId::FromValue(i == 2 ? 0x101 : 0x101 + i);
        data.textures.Push(b);
    }
    Vector<byte> out;
    REQUIRE(BuildMaterialBlob(types, ResourceId::FromValue(0x77), data, &out).Ok());
    BlobReader three;
    REQUIRE(three.Open(ByteSpan(out.Data(), out.Size()), types).Ok());
    CHECK(three.DependencyCount() == 2);
    data.textures[2].texture = ResourceId::FromValue(0x103);
    REQUIRE(BuildMaterialBlob(types, ResourceId::FromValue(0x77), data, &out).Ok());
    BlobReader distinct;
    REQUIRE(distinct.Open(ByteSpan(out.Data(), out.Size()), types).Ok());
    CHECK(distinct.DependencyCount() == 3);
}

TEST_CASE("invalid material sources are refused with the line", "[asset][material]")
{
    const MtResolver r = MtMakeResolver();
    struct Case
    {
        const char* text;
        u32 line;
        const char* fragment;
    };
    const Case cases[] = {
        {"template gbuffer\n", 1, "id line"},
        {"id 00000000000000a1\ntemplate nope\n", 2, "unknown template"},
        {"id 00000000000000a1\ntemplate gbuffer\nparam roughness vec2 1 2\n", 3, "wrong type"},
        {"id 00000000000000a1\ntemplate gbuffer\nparam shininess float 1\n", 3, "roughness"},
        {"id 00000000000000a1\ntemplate gbuffer\nparam roughness float abc\n", 3, "finite"},
        {"id 00000000000000a1\ntemplate gbuffer\nparam roughness float 1 2\n", 3, "number of values"},
        {"id 00000000000000a1\ntemplate gbuffer\ntexture albedo_tex missing.texr\n", 3, "does not exist"},
        {"id 00000000000000a1\ntemplate gbuffer\ntexture albedo_tex normal.texr\n", 3, "sRGB"},
        {"id 00000000000000a1\ntemplate gbuffer\ntexture albedo_tex sky.texr\n", 3, "2D texture"},
        {"id 00000000000000a1\ntemplate gbuffer\ntexture normal_tex normal.texr\n", 3, "no texture slot"},
        {"id 00000000000000a1\ntemplate gbuffer\ntexture metallic_roughness_tex albedo.texr\n", 3, "takes linear"},
        {"id 00000000000000a1\ntemplate gbuffer\ntexture occlusion_tex albedo.texr\n", 3, "takes linear"},
        {"id 00000000000000a1\ntemplate gbuffer\nsampler nothing filter linear\n", 3, "no sampler slot"},
        {"id 00000000000000a1\ntemplate gbuffer\noption BOGUS\n", 0, "BOGUS"},
        {"id 00000000000000a1\ntemplate gbuffer\nstate cull sideways\n", 3, "cull"},
        {"id 00000000000000a1\ntemplate gbuffer\nbogus 1\n", 3, "unknown record"},
        {"id 00000000000000a1\ntemplate gbuffer\nparam roughness float 1\nparam roughness float 2\n", 4, "twice"},
        {"id 00000000000000a1\ntemplate gbuffer\ntexture albedo_tex albedo.texr\ntexture albedo_tex albedo.texr\n", 4,
         "twice"},
        {"id 00000000000000a1\ntemplate gbuffer\nsampler linear_sampler filter linear\nsampler linear_sampler\n", 4,
         "described twice"},
    };
    for (const Case& c : cases)
    {
        Vector<byte> blob;
        MaterialSourceError error;
        const Status s = MtMint(c.text, r, &blob, &error);
        INFO(c.text);
        INFO(error.message.Data());
        CHECK(s.Failed());
        CHECK(error.line == c.line);
        CHECK(StringView(error.message.Data(), CStrLength(error.message.Data())).Contains(StringView(c.fragment)));
    }
}

TEST_CASE("the material minter is deterministic", "[asset][material]")
{
    const MtResolver r = MtMakeResolver();
    const char* body = "template gbuffer\nparam metallic float 1\ntexture albedo_tex albedo.texr\n";
    const Vector<byte> a = MtMintOk(body, r);
    const Vector<byte> b = MtMintOk(body, r);
    REQUIRE(a.Size() == b.Size());
    CHECK(MemCompare(a.Data(), b.Data(), a.Size()) == 0);
}

TEST_CASE("the loader refuses hostile material blobs", "[asset][material]")
{
    const MtResolver r = MtMakeResolver();
    const Vector<byte> good = MtMintOk("template gbuffer\ntexture albedo_tex albedo.texr\n", r);
    TypeRegistry types;
    MaterialAssetData m;
    REQUIRE(LoadMaterialBlob(ByteSpan(good.Data(), good.Size()), types, &m).Ok());
    // Every truncation and every single-byte flip must come back as a status, never a crash.
    for (usize n = 0; n < good.Size(); ++n)
    {
        CHECK(LoadMaterialBlob(ByteSpan(good.Data(), n), types, &m).Failed());
    }
    // A flipped byte must come back as a status or a different valid material, never a crash; the bytes the loader
    // checks (the container header, the material header's reserved bytes and counts) are refused.
    ByteSpan header;
    BlobReader reader;
    REQUIRE(reader.Open(ByteSpan(good.Data(), good.Size()), types).Ok());
    REQUIRE(reader.FindSection(16, &header));
    const usize header_offset = static_cast<usize>(header.Data() - good.Data());
    for (usize i = 0; i < good.Size(); ++i)
    {
        Vector<byte> bad;
        bad.Resize(good.Size());
        MemCopy(bad.Data(), good.Data(), good.Size());
        bad[i] = static_cast<byte>(bad[i] ^ 0xff);
        const Status s = LoadMaterialBlob(ByteSpan(bad.Data(), bad.Size()), types, &m);
        const bool container_header = i < 16 || (i >= 40 && i < 48);
        const bool reserved = i >= header_offset + 33 && i < header_offset + 48;
        const bool counts = i >= header_offset + 4 && i < header_offset + 24;
        if (container_header || reserved || counts)
        {
            INFO("flipped byte " << i);
            CHECK(s.Failed());
        }
    }
}

TEST_CASE("a blob of another version is refused naming remint", "[asset][material]")
{
    const MtResolver r = MtMakeResolver();
    const Vector<byte> good = MtMintOk("template gbuffer\n", r);
    TypeRegistry types;
    // A blob written by an older version of the type: the type-version field (offset 12) holds 0.
    Vector<byte> v0;
    v0.Resize(good.Size());
    MemCopy(v0.Data(), good.Data(), good.Size());
    v0[12] = 0;
    v0[13] = 0;
    v0[14] = 0;
    v0[15] = 0;
    MaterialAssetData out;
    const Status s = LoadMaterialBlob(ByteSpan(v0.Data(), v0.Size()), types, &out);
    CHECK(s.Failed());
    CHECK(s.Code() == ErrorCode::UnsupportedVersion);
    CHECK(StringView(s.Message(), CStrLength(s.Message())).Contains(StringView("remint")));
}

TEST_CASE("the builder refuses out-of-range data", "[asset][material]")
{
    TypeRegistry types;
    MaterialAssetData data;
    MakeMaterialName("gbuffer", 7, &data.template_name);
    data.block.Resize(32);
    MaterialParam p;
    MakeMaterialName("roughness", 9, &p.name);
    p.offset = 30;
    p.size = 4;
    data.params.Push(p);
    Vector<byte> out;
    CHECK(BuildMaterialBlob(types, ResourceId::FromValue(0x5), data, &out).Failed());
    data.params[0].offset = 16;
    CHECK(BuildMaterialBlob(types, ResourceId::FromValue(0x5), data, &out).Ok());
    CHECK(BuildMaterialBlob(types, ResourceId::None(), data, &out).Failed());
}

}  // namespace engine_placeholder::asset
