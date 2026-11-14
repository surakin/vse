#include "lang/limits.h"
#include "lang/traits.h"
#include "cstr/cstr.h"

#include <catch2/catch_test_macros.hpp>

#include "rhi/caps.h"
#include "rhi/desc.h"
#include "rhi/format.h"
#include "rhi/handle.h"
#include "rhi/enums.h"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;

namespace
{

// True when `reason` is non-null and starts with `prefix` (the descriptor name plus the start of the message).
bool Rejects(const char* reason, const char* prefix)
{
    return reason != nullptr && CStrCompareN(reason, prefix, CStrLength(prefix)) == 0;
}

}  // namespace

TEST_CASE("Default handles are invalid and compare equal", "[rhi][handle]")
{
    BufferHandle a;
    BufferHandle b;
    CHECK_FALSE(a.IsValid());
    CHECK(a == b);
    CHECK(a.index == 0);
    CHECK(a.generation == 0);
}

TEST_CASE("Handles compare by index and generation", "[rhi][handle]")
{
    BufferHandle a{3, 1};
    BufferHandle same{3, 1};
    BufferHandle other_index{4, 1};
    BufferHandle other_generation{3, 2};
    CHECK(a.IsValid());
    CHECK(a == same);
    CHECK(a != other_index);
    CHECK(a != other_generation);         // a stale copy of a recycled slot is not equal
    CHECK(BufferHandle{0, 1}.IsValid());  // index 0 is a legal slot; validity is the generation
    CHECK_FALSE(BufferHandle{5, 0}.IsValid());
}

TEST_CASE("Handle kinds are distinct types", "[rhi][handle]")
{
    static_assert(!IsSame<BufferHandle, TextureHandle>);
    static_assert(!IsSame<ShaderHandle, PipelineHandle>);
    static_assert(sizeof(BufferHandle) == 8);
    SUCCEED();
}

TEST_CASE("FormatInfo matches hand-computed layouts for every format", "[rhi][format]")
{
    struct Expect
    {
        Format f;
        u8 bytes;
        u8 bw;
        u8 bh;
        u8 channels;
        bool depth;
        bool stencil;
        bool srgb;
        bool compressed;
    };
    const Expect expected[] = {
        {Format::R8_UNorm, 1, 1, 1, 1, false, false, false, false},
        {Format::RG8_UNorm, 2, 1, 1, 2, false, false, false, false},
        {Format::RGBA8_UNorm, 4, 1, 1, 4, false, false, false, false},
        {Format::RGBA8_SRGB, 4, 1, 1, 4, false, false, true, false},
        {Format::BGRA8_UNorm, 4, 1, 1, 4, false, false, false, false},
        {Format::BGRA8_SRGB, 4, 1, 1, 4, false, false, true, false},
        {Format::R16_Float, 2, 1, 1, 1, false, false, false, false},
        {Format::RG16_Float, 4, 1, 1, 2, false, false, false, false},
        {Format::RGBA16_Float, 8, 1, 1, 4, false, false, false, false},
        {Format::R32_Float, 4, 1, 1, 1, false, false, false, false},
        {Format::RG32_Float, 8, 1, 1, 2, false, false, false, false},
        {Format::RGB32_Float, 12, 1, 1, 3, false, false, false, false},
        {Format::RGBA32_Float, 16, 1, 1, 4, false, false, false, false},
        {Format::R32_UInt, 4, 1, 1, 1, false, false, false, false},
        {Format::D32_Float, 4, 1, 1, 1, true, false, false, false},
        {Format::D24_UNorm_S8_UInt, 4, 1, 1, 2, true, true, false, false},
        {Format::D32_Float_S8_UInt, 8, 1, 1, 2, true, true, false, false},
        {Format::BC1_UNorm, 8, 4, 4, 4, false, false, false, true},
        {Format::BC1_SRGB, 8, 4, 4, 4, false, false, true, true},
        {Format::BC3_UNorm, 16, 4, 4, 4, false, false, false, true},
        {Format::BC3_SRGB, 16, 4, 4, 4, false, false, true, true},
        {Format::BC5_UNorm, 16, 4, 4, 2, false, false, false, true},
        {Format::BC7_UNorm, 16, 4, 4, 4, false, false, false, true},
        {Format::BC7_SRGB, 16, 4, 4, 4, false, false, true, true},
        {Format::BC4_UNorm, 8, 4, 4, 1, false, false, false, true},
    };
    // Every format except Undefined is covered exactly once.
    CHECK(sizeof(expected) / sizeof(expected[0]) == static_cast<usize>(Format::Count) - 1);
    for (const Expect& e : expected)
    {
        INFO(FormatInfo(e.f).name);
        const FormatProperties& p = FormatInfo(e.f);
        CHECK(p.bytes_per_block == e.bytes);
        CHECK(p.block_width == e.bw);
        CHECK(p.block_height == e.bh);
        CHECK(p.channels == e.channels);
        CHECK(p.is_depth == e.depth);
        CHECK(p.has_stencil == e.stencil);
        CHECK(p.is_srgb == e.srgb);
        CHECK(p.is_compressed == e.compressed);
    }
    CHECK(FormatInfo(Format::Undefined).bytes_per_block == 0);
}

TEST_CASE("Format names are unique and non-empty", "[rhi][format]")
{
    for (usize i = 0; i < static_cast<usize>(Format::Count); ++i)
    {
        const char* a = FormatInfo(static_cast<Format>(i)).name;
        REQUIRE(a != nullptr);
        CHECK(a[0] != '\0');
        for (usize j = 0; j < i; ++j)
        {
            CHECK(CStrCompare(a, FormatInfo(static_cast<Format>(j)).name) != 0);
        }
    }
}

TEST_CASE("Row and slice sizes round partial blocks up", "[rhi][format]")
{
    CHECK(FormatRowBytes(Format::RGBA8_UNorm, 64) == 256);
    CHECK(FormatSliceBytes(Format::RGBA8_UNorm, 64, 3) == 768);
    // BC1: 4x4 blocks of 8 bytes. 5 texels wide needs 2 blocks; 5 texels high needs 2 block rows.
    CHECK(FormatRowBytes(Format::BC1_UNorm, 5) == 16);
    CHECK(FormatSliceBytes(Format::BC1_UNorm, 5, 5) == 32);
    CHECK(FormatSliceBytes(Format::BC7_UNorm, 4, 4) == 16);
    CHECK(FormatRowBytes(Format::RGBA8_UNorm, 0) == 0);
    // Wide rows do not overflow 32 bits.
    CHECK(FormatRowBytes(Format::RGBA32_Float, 0xFFFFFFFFu) == 0xFFFFFFFFull * 16);
}

TEST_CASE("Flag operators combine, mask and test", "[rhi][flags]")
{
    BufferUsage v = BufferUsage::Vertex | BufferUsage::CopyDst;
    CHECK(Has(v, BufferUsage::Vertex));
    CHECK(Has(v, BufferUsage::CopyDst));
    CHECK_FALSE(Has(v, BufferUsage::Index));
    CHECK_FALSE(Has(v, BufferUsage::Vertex | BufferUsage::Index));  // all of the flags must be present
    CHECK(Has(v, BufferUsage::None));
    CHECK(Any(v & BufferUsage::Vertex));
    CHECK_FALSE(Any(v & BufferUsage::Uniform));
    CHECK_FALSE(Any(BufferUsage::None));
    CHECK((ShaderStage::Vertex | ShaderStage::Fragment) == ShaderStage::AllGraphics);
    CHECK((ShaderStage::AllGraphics | ShaderStage::Compute) == ShaderStage::All);
    CHECK((ColorWriteMask::R | ColorWriteMask::G | ColorWriteMask::B | ColorWriteMask::A) == ColorWriteMask::All);
}

TEST_CASE("VertexFormatSize covers every format", "[rhi][types]")
{
    CHECK(VertexFormatSize(VertexFormat::Float1) == 4);
    CHECK(VertexFormatSize(VertexFormat::Float2) == 8);
    CHECK(VertexFormatSize(VertexFormat::Float3) == 12);
    CHECK(VertexFormatSize(VertexFormat::Float4) == 16);
    CHECK(VertexFormatSize(VertexFormat::UInt3) == 12);
    CHECK(VertexFormatSize(VertexFormat::UNorm8x4) == 4);
    CHECK(VertexFormatSize(VertexFormat::Count) == 0);
    for (u8 i = 0; i < static_cast<u8>(VertexFormat::Count); ++i)
    {
        CHECK(VertexFormatSize(static_cast<VertexFormat>(i)) > 0);
    }
}

TEST_CASE("The packed vertex formats have the sizes, widths and alignments of their type", "[rhi][types]")
{
    struct Row
    {
        VertexFormat format;
        u32 size;
        u32 components;
        bool integer;
        u32 alignment;
    };
    const Row rows[] = {
        {VertexFormat::Float16x2, 4, 2, false, 4}, {VertexFormat::Float16x4, 8, 4, false, 4},
        {VertexFormat::UNorm16x2, 4, 2, false, 4}, {VertexFormat::UNorm16x4, 8, 4, false, 4},
        {VertexFormat::SNorm16x2, 4, 2, false, 4}, {VertexFormat::SNorm16x4, 8, 4, false, 4},
        {VertexFormat::SNorm8x4, 4, 4, false, 4},  {VertexFormat::UNorm8x4, 4, 4, false, 4},
        {VertexFormat::UInt8x4, 4, 4, true, 4},    {VertexFormat::UInt16x4, 8, 4, true, 4},
        {VertexFormat::Float3, 12, 3, false, 4},   {VertexFormat::UInt2, 8, 2, true, 4},
    };
    for (const Row& r : rows)
    {
        INFO("format " << static_cast<u32>(r.format));
        CHECK(VertexFormatSize(r.format) == r.size);
        CHECK(VertexFormatComponents(r.format) == r.components);
        CHECK(VertexFormatIsInteger(r.format) == r.integer);
        CHECK(VertexFormatAlignment(r.format) == r.alignment);
    }
    CHECK(VertexFormatComponents(VertexFormat::Count) == 0);
    CHECK(VertexFormatAlignment(VertexFormat::Count) == 0);
    // Every format has non-zero size, width and alignment, and a size that is a multiple of its alignment.
    for (u8 i = 0; i < static_cast<u8>(VertexFormat::Count); ++i)
    {
        const VertexFormat f = static_cast<VertexFormat>(i);
        CHECK(VertexFormatComponents(f) > 0);
        CHECK(VertexFormatSize(f) % VertexFormatAlignment(f) == 0);
    }
}

TEST_CASE("A vertex attribute feeds a shader input of the same kind and width", "[rhi][types]")
{
    // Float-class storage feeds float inputs of the same width; integer storage feeds uint inputs.
    CHECK(VertexFormatFeedsInput(VertexFormat::UNorm8x4, VertexFormat::Float4));
    CHECK(VertexFormatFeedsInput(VertexFormat::SNorm8x4, VertexFormat::Float4));
    CHECK(VertexFormatFeedsInput(VertexFormat::Float16x2, VertexFormat::Float2));
    CHECK(VertexFormatFeedsInput(VertexFormat::UNorm16x4, VertexFormat::Float4));
    CHECK(VertexFormatFeedsInput(VertexFormat::SNorm16x2, VertexFormat::Float2));
    CHECK(VertexFormatFeedsInput(VertexFormat::UInt8x4, VertexFormat::UInt4));
    CHECK(VertexFormatFeedsInput(VertexFormat::UInt16x4, VertexFormat::UInt4));
    CHECK(VertexFormatFeedsInput(VertexFormat::Float3, VertexFormat::Float3));
    // A different width, or a float attribute for a uint input and the reverse, does not.
    CHECK_FALSE(VertexFormatFeedsInput(VertexFormat::UNorm8x4, VertexFormat::Float3));
    CHECK_FALSE(VertexFormatFeedsInput(VertexFormat::Float16x2, VertexFormat::Float4));
    CHECK_FALSE(VertexFormatFeedsInput(VertexFormat::UInt8x4, VertexFormat::Float4));
    CHECK_FALSE(VertexFormatFeedsInput(VertexFormat::UNorm8x4, VertexFormat::UInt4));
    CHECK_FALSE(VertexFormatFeedsInput(VertexFormat::UInt16x4, VertexFormat::UInt3));
    CHECK_FALSE(VertexFormatFeedsInput(VertexFormat::Count, VertexFormat::Float4));
    CHECK_FALSE(VertexFormatFeedsInput(VertexFormat::Float4, VertexFormat::Count));
}

TEST_CASE("MaxMipLevels is floor(log2(max extent)) + 1", "[rhi][desc]")
{
    CHECK(MaxMipLevels(1, 1) == 1);
    CHECK(MaxMipLevels(2, 2) == 2);
    CHECK(MaxMipLevels(256, 256) == 9);
    CHECK(MaxMipLevels(255, 255) == 8);
    CHECK(MaxMipLevels(256, 1) == 9);
    CHECK(MaxMipLevels(1, 1, 64) == 7);
    CHECK(MaxMipLevels(0, 4) == 0);
}

TEST_CASE("DeviceCaps defaults are the guaranteed minimums", "[rhi][caps]")
{
    DeviceCaps caps;
    CHECK(caps.max_bind_groups == kMaxBindGroups);
    CHECK(caps.max_push_constant_bytes == kMaxPushConstantBytes);
    CHECK_FALSE(caps.supports_bindless);
    CHECK(FormatInfo(caps.depth_format).is_depth);
}

TEST_CASE("BufferDesc validation", "[rhi][desc]")
{
    BufferDesc d;
    d.size = 256;
    d.usage = BufferUsage::Vertex;
    CHECK(d.Validate() == nullptr);

    BufferDesc zero = d;
    zero.size = 0;
    CHECK(Rejects(zero.Validate(), "BufferDesc: size is zero"));

    BufferDesc none = d;
    none.usage = BufferUsage::None;
    CHECK(Rejects(none.Validate(), "BufferDesc: no usage"));

    BufferDesc unknown = d;
    unknown.usage = static_cast<BufferUsage>(1u << 20);
    CHECK(Rejects(unknown.Validate(), "BufferDesc: unknown usage"));

    BufferDesc readback = d;
    readback.location = MemoryLocation::GpuToCpu;
    CHECK(Rejects(readback.Validate(), "BufferDesc: GpuToCpu"));
    readback.usage = BufferUsage::CopyDst;
    CHECK(readback.Validate() == nullptr);

    BufferDesc upload = d;
    upload.location = MemoryLocation::CpuToGpu;
    upload.usage = BufferUsage::CopySrc | BufferUsage::Uniform;
    CHECK(upload.Validate() == nullptr);
}

TEST_CASE("TextureDesc validation", "[rhi][desc]")
{
    TextureDesc d;
    d.width = 64;
    d.height = 64;
    d.format = Format::RGBA8_UNorm;
    d.usage = TextureUsage::Sampled | TextureUsage::CopyDst;
    CHECK(d.Validate() == nullptr);

    SECTION("zero extent")
    {
        d.width = 0;
        CHECK(Rejects(d.Validate(), "TextureDesc: extent"));
    }
    SECTION("2D with depth")
    {
        d.depth = 4;
        CHECK(Rejects(d.Validate(), "TextureDesc: 2D textures must have depth 1"));
    }
    SECTION("3D array")
    {
        d.dimension = TextureDimension::Tex3D;
        d.depth = 4;
        d.array_layers = 2;
        CHECK(Rejects(d.Validate(), "TextureDesc: 3D textures cannot be arrays"));
        d.array_layers = 1;
        CHECK(d.Validate() == nullptr);
    }
    SECTION("cube is reserved")
    {
        d.dimension = TextureDimension::Cube;
        CHECK(Rejects(d.Validate(), "TextureDesc: cube"));
    }
    SECTION("undefined format")
    {
        d.format = Format::Undefined;
        CHECK(Rejects(d.Validate(), "TextureDesc: format is undefined"));
    }
    SECTION("too many mips")
    {
        d.mip_levels = 7;  // 64x64 has 7 levels
        CHECK(d.Validate() == nullptr);
        d.mip_levels = 8;
        CHECK(Rejects(d.Validate(), "TextureDesc: mip_levels exceeds"));
        d.mip_levels = 0;
        CHECK(Rejects(d.Validate(), "TextureDesc: mip_levels is zero"));
    }
    SECTION("multisampling")
    {
        d.samples = 4;
        CHECK(Rejects(d.Validate(), "TextureDesc: multisampling"));
    }
    SECTION("no usage")
    {
        d.usage = TextureUsage::None;
        CHECK(Rejects(d.Validate(), "TextureDesc: no usage"));
    }
    SECTION("depth format used as color")
    {
        d.format = Format::D32_Float;
        d.usage = TextureUsage::ColorAttachment;
        CHECK(Rejects(d.Validate(), "TextureDesc: color attachment requires"));
    }
    SECTION("depth attachment needs a depth format")
    {
        d.usage = TextureUsage::DepthAttachment;
        CHECK(Rejects(d.Validate(), "TextureDesc: depth attachment requires"));
        d.format = Format::D32_Float;
        CHECK(d.Validate() == nullptr);
    }
    SECTION("compressed formats cannot be rendered to or stored")
    {
        d.format = Format::BC1_UNorm;
        CHECK(d.Validate() == nullptr);
        d.usage = TextureUsage::ColorAttachment;
        CHECK(Rejects(d.Validate(), "TextureDesc: color attachment requires"));
        d.usage = TextureUsage::Storage;
        CHECK(Rejects(d.Validate(), "TextureDesc: storage usage requires"));
    }
    SECTION("compressed 3D")
    {
        d.format = Format::BC7_UNorm;
        d.dimension = TextureDimension::Tex3D;
        d.depth = 4;
        CHECK(Rejects(d.Validate(), "TextureDesc: compressed 3D"));
    }
}

TEST_CASE("SamplerDesc validation", "[rhi][desc]")
{
    SamplerDesc d;
    CHECK(d.Validate() == nullptr);
    d.max_anisotropy = 16.0f;
    CHECK(d.Validate() == nullptr);
    d.max_anisotropy = 0.5f;
    CHECK(Rejects(d.Validate(), "SamplerDesc: max_anisotropy"));
    d.max_anisotropy = 17.0f;
    CHECK(Rejects(d.Validate(), "SamplerDesc: max_anisotropy"));
    d.max_anisotropy = Limits<f32>::QuietNaN();  // NaN must not slip through the range check
    CHECK(Rejects(d.Validate(), "SamplerDesc: max_anisotropy"));

    SamplerDesc garbage;
    garbage.mag_filter = static_cast<FilterMode>(9);
    CHECK(Rejects(garbage.Validate(), "SamplerDesc: unknown filter"));
    garbage = SamplerDesc{};
    garbage.address_w = static_cast<AddressMode>(9);
    CHECK(Rejects(garbage.Validate(), "SamplerDesc: unknown address"));
    garbage = SamplerDesc{};
    garbage.border_color = static_cast<BorderColor>(9);
    CHECK(Rejects(garbage.Validate(), "SamplerDesc: unknown border"));

    SamplerDesc lod;
    lod.min_lod = 4.0f;
    lod.max_lod = 2.0f;
    CHECK(Rejects(lod.Validate(), "SamplerDesc: max_lod"));
    lod.min_lod = -1.0f;
    CHECK(Rejects(lod.Validate(), "SamplerDesc: min_lod"));
}

TEST_CASE("ShaderDesc validation", "[rhi][desc]")
{
    const u32 words[8] = {0x07230203, 0x00010000, 0, 1, 0, 0, 0, 0};
    ShaderDesc d;
    d.stage = ShaderStage::Vertex;
    d.SetCode(SpirvBlob(words, 8));
    CHECK(d.Validate() == nullptr);

    ShaderDesc multi = d;
    multi.stage = ShaderStage::AllGraphics;
    CHECK(Rejects(multi.Validate(), "ShaderDesc: stage"));
    ShaderDesc nostage = d;
    nostage.stage = ShaderStage::None;
    CHECK(Rejects(nostage.Validate(), "ShaderDesc: stage"));
    ShaderDesc nowords = d;
    nowords.bytes = nullptr;
    CHECK(Rejects(nowords.Validate(), "ShaderDesc: no bytecode"));
    ShaderDesc tiny = d;
    tiny.size = 4 * sizeof(u32);
    CHECK(Rejects(tiny.Validate(), "ShaderDesc: module is smaller"));
    ShaderDesc ragged = d;
    ragged.size = 6 * sizeof(u32) + 2;
    CHECK(Rejects(ragged.Validate(), "ShaderDesc: SPIR-V size is not a whole number"));
    ShaderDesc misaligned = d;
    misaligned.bytes = reinterpret_cast<const u8*>(words) + 2;
    misaligned.size = 6 * sizeof(u32);
    CHECK(Rejects(misaligned.Validate(), "ShaderDesc: SPIR-V bytecode is not 4-byte aligned"));
    ShaderDesc noentry = d;
    noentry.entry = "";
    CHECK(Rejects(noentry.Validate(), "ShaderDesc: entry"));
    noentry.entry = nullptr;
    CHECK(Rejects(noentry.Validate(), "ShaderDesc: entry"));
    ShaderDesc longname = d;
    longname.entry = "0123456789012345678901234567890123456789012345678901234567890123";  // 64 bytes
    CHECK(Rejects(longname.Validate(), "ShaderDesc: entry point name is longer"));
    longname.entry = "012345678901234567890123456789012345678901234567890123456789012";  // 63 bytes
    CHECK(longname.Validate() == nullptr);
}

TEST_CASE("ShaderDesc validation checks a DXIL container's tag and size and refuses MSL", "[rhi][desc]")
{
    // A DXBC container header: tag, 16-byte digest, version 1.0, total size 40, one part; then 8 bytes of part data.
    u8 dxil[40] = {'D', 'X', 'B', 'C'};
    dxil[20] = 1;
    dxil[24] = 40;
    dxil[28] = 1;
    ShaderDesc d;
    d.stage = ShaderStage::Fragment;
    d.SetCode(DxilBlob(dxil, sizeof(dxil)));
    CHECK(d.format == ShaderFormat::Dxil);
    CHECK(d.Validate() == nullptr);

    ShaderDesc short_header = d;
    short_header.size = 31;
    CHECK(Rejects(short_header.Validate(), "ShaderDesc: module is smaller than a DXIL container header"));
    ShaderDesc wrong_size = d;
    wrong_size.size = 36;
    CHECK(Rejects(wrong_size.Validate(), "ShaderDesc: DXIL size differs"));
    u8 bad_tag[40];
    for (usize i = 0; i < sizeof(bad_tag); ++i)
    {
        bad_tag[i] = dxil[i];
    }
    bad_tag[0] = 'Q';
    ShaderDesc not_dxbc = d;
    not_dxbc.bytes = bad_tag;
    CHECK(Rejects(not_dxbc.Validate(), "ShaderDesc: DXIL bytecode does not start with the DXBC"));
    // SPIR-V words are not DXIL: the format decides which checks apply.
    const u32 words[8] = {0x07230203, 0x00010000, 0, 1, 0, 0, 0, 0};
    ShaderDesc spirv_as_dxil = d;
    spirv_as_dxil.bytes = words;
    spirv_as_dxil.size = sizeof(words);
    CHECK(Rejects(spirv_as_dxil.Validate(), "ShaderDesc: DXIL bytecode does not start with the DXBC"));

    // A Metal library (Phase 21 Day 299): the MTLB tag, a header, and an executable library (type byte 0).
    u8 lib[40] = {'M', 'T', 'L', 'B'};  // 40 bytes: truncated by 4 it still has a whole header
    lib[16] = sizeof(lib);              // the file size the header records (u64 at 16)
    ShaderDesc metallib = d;
    metallib.format = ShaderFormat::MetalLib;
    metallib.bytes = lib;
    metallib.size = sizeof(lib);
    metallib.entry = "main_0";
    CHECK(metallib.Validate() == nullptr);
    metallib.size = 16;
    CHECK(Rejects(metallib.Validate(), "ShaderDesc: module is smaller than a Metal library header"));
    metallib.size = sizeof(lib) - 4;  // a truncated library
    CHECK(Rejects(metallib.Validate(), "ShaderDesc: Metal library size differs from the size its header records"));
    metallib.size = sizeof(lib);
    lib[10] = 2;  // a dynamic library
    CHECK(Rejects(metallib.Validate(), "ShaderDesc: Metal library is not an executable library"));
    lib[0] = 'X';
    CHECK(Rejects(metallib.Validate(), "ShaderDesc: Metal library does not start with the MTLB tag"));
    CHECK(StringView(ShaderFormatName(ShaderFormat::Spirv)) == "spirv");
    CHECK(StringView(ShaderFormatName(ShaderFormat::Dxil)) == "dxil");
    CHECK(StringView(ShaderFormatName(ShaderFormat::MetalLib)) == "metallib");
}

TEST_CASE("ShaderBlobSet picks the blob of a format and is empty where a format was not minted", "[rhi][desc]")
{
    static const u32 words[6] = {0x07230203, 0x00010000, 0, 1, 0, 0};
    static const u8 dxil[4] = {'D', 'X', 'B', 'C'};
    ShaderBlobSet set{"sample.frag", SpirvBlob(words, 6), {}};
    CHECK(set.For(ShaderFormat::Spirv).size == 24);
    CHECK(set.For(ShaderFormat::Spirv).format == ShaderFormat::Spirv);
    CHECK(set.For(ShaderFormat::Dxil).Empty());
    CHECK(set.For(ShaderFormat::Dxil).format == ShaderFormat::Dxil);
    CHECK(set.For(ShaderFormat::MetalLib).Empty());
    set.dxil = DxilBlob(dxil, 4);
    CHECK(set.For(ShaderFormat::Dxil).bytes == dxil);
    static const u8 lib[4] = {'M', 'T', 'L', 'B'};
    set.metallib = MetalLibBlob(lib, 4, "main_0");
    CHECK(set.For(ShaderFormat::MetalLib).bytes == lib);
    CHECK(StringView(set.For(ShaderFormat::MetalLib).entry) == "main_0");  // the entry travels with the blob
    ShaderDesc from_set;
    from_set.SetCode(set.For(ShaderFormat::MetalLib));
    CHECK(StringView(from_set.entry) == "main_0");
    from_set.SetCode(set.For(ShaderFormat::Spirv));  // a blob without an entry is `main`, whatever was set before
    CHECK(StringView(from_set.entry) == "main");
}

TEST_CASE("BindGroupLayoutDesc validation", "[rhi][desc]")
{
    BindingDesc entries[2];
    entries[0] = {0, BindingType::UniformBuffer, ShaderStage::Vertex, 1};
    entries[1] = {1, BindingType::SampledTexture, ShaderStage::Fragment, 1};
    BindGroupLayoutDesc d;
    d.entries = rhi::Span<BindingDesc>(entries);
    CHECK(d.Validate() == nullptr);

    BindGroupLayoutDesc empty;
    CHECK(empty.Validate() == nullptr);  // an empty layout is legal

    BindingDesc dup[2] = {entries[0], entries[0]};
    BindGroupLayoutDesc dups;
    dups.entries = rhi::Span<BindingDesc>(dup);
    CHECK(Rejects(dups.Validate(), "BindGroupLayoutDesc: duplicate"));

    BindingDesc bad = entries[0];
    bad.stages = ShaderStage::None;
    BindGroupLayoutDesc nostages;
    nostages.entries = rhi::Span<BindingDesc>(&bad, 1);
    CHECK(Rejects(nostages.Validate(), "BindGroupLayoutDesc: binding stages"));

    bad = entries[0];
    bad.count = 0;
    BindGroupLayoutDesc zero;
    zero.entries = rhi::Span<BindingDesc>(&bad, 1);
    CHECK(Rejects(zero.Validate(), "BindGroupLayoutDesc: binding count"));

    bad = entries[0];
    bad.count = 4;  // arrays of uniform buffers are not supported
    BindGroupLayoutDesc arr;
    arr.entries = rhi::Span<BindingDesc>(&bad, 1);
    CHECK(Rejects(arr.Validate(), "BindGroupLayoutDesc: only sampled"));
    bad.type = BindingType::SampledTexture;
    CHECK(arr.Validate() == nullptr);

    BindingDesc huge = {0, BindingType::Sampler, ShaderStage::Fragment, kMaxBindingArraySize + 1};
    BindGroupLayoutDesc huge_desc;
    huge_desc.entries = rhi::Span<BindingDesc>(&huge, 1);
    CHECK(Rejects(huge_desc.Validate(), "BindGroupLayoutDesc: binding array is too large"));
    huge.count = kMaxBindingArraySize;
    CHECK(huge_desc.Validate() == nullptr);

    BindingDesc many[kMaxBindingsPerLayout + 1];
    for (u32 i = 0; i < kMaxBindingsPerLayout + 1; ++i)
    {
        many[i] = {i, BindingType::UniformBuffer, ShaderStage::Vertex, 1};
    }
    BindGroupLayoutDesc too_many;
    too_many.entries = rhi::Span<BindingDesc>(many);
    CHECK(Rejects(too_many.Validate(), "BindGroupLayoutDesc: too many"));
}

namespace
{

GraphicsPipelineDesc GoodPipeline(VertexBindingDesc* binding, VertexAttributeDesc* attrs)
{
    binding[0] = {24, false};
    attrs[0] = {0, 0, VertexFormat::Float3, 0};
    attrs[1] = {1, 0, VertexFormat::Float3, 12};
    GraphicsPipelineDesc d;
    d.vertex_shader = ShaderHandle{1, 1};
    d.fragment_shader = ShaderHandle{2, 1};
    d.vertex_bindings = rhi::Span<VertexBindingDesc>(binding, 1);
    d.vertex_attributes = rhi::Span<VertexAttributeDesc>(attrs, 2);
    d.color_targets[0].format = Format::BGRA8_SRGB;
    d.color_target_count = 1;
    return d;
}

}  // namespace

TEST_CASE("GraphicsPipelineDesc validation", "[rhi][desc]")
{
    VertexBindingDesc binding[1];
    VertexAttributeDesc attrs[2];
    GraphicsPipelineDesc d = GoodPipeline(binding, attrs);
    CHECK(d.Validate() == nullptr);

    SECTION("missing vertex shader")
    {
        d.vertex_shader = ShaderHandle{};
        CHECK(Rejects(d.Validate(), "GraphicsPipelineDesc: vertex shader"));
    }
    SECTION("color targets need a fragment shader")
    {
        d.fragment_shader = ShaderHandle{};
        CHECK(Rejects(d.Validate(), "GraphicsPipelineDesc: color targets require"));
        d.color_target_count = 0;
        d.depth_format = Format::D32_Float;
        CHECK(d.Validate() == nullptr);  // depth-only pipeline
    }
    SECTION("no attachments")
    {
        d.color_target_count = 0;
        CHECK(Rejects(d.Validate(), "GraphicsPipelineDesc: no attachments"));
    }
    SECTION("too many color targets")
    {
        d.color_target_count = kMaxColorAttachments + 1;
        CHECK(Rejects(d.Validate(), "GraphicsPipelineDesc: too many color targets"));
    }
    SECTION("bad target formats")
    {
        d.color_targets[0].format = Format::D32_Float;
        CHECK(Rejects(d.Validate(), "GraphicsPipelineDesc: color target format"));
        d.color_targets[0].format = Format::BC1_UNorm;
        CHECK(Rejects(d.Validate(), "GraphicsPipelineDesc: color target format"));
        d.color_targets[0].format = Format::Undefined;
        CHECK(Rejects(d.Validate(), "GraphicsPipelineDesc: color target format"));
    }
    SECTION("depth state without a depth format")
    {
        d.depth_test = true;
        CHECK(Rejects(d.Validate(), "GraphicsPipelineDesc: depth state"));
        d.depth_format = Format::D32_Float;
        CHECK(d.Validate() == nullptr);
        d.depth_format = Format::RGBA8_UNorm;
        CHECK(Rejects(d.Validate(), "GraphicsPipelineDesc: depth_format"));
    }
    SECTION("multisampling")
    {
        d.samples = 2;
        CHECK(Rejects(d.Validate(), "GraphicsPipelineDesc: multisampling"));
    }
    SECTION("vertex layout")
    {
        binding[0].stride = 0;
        CHECK(Rejects(d.Validate(), "GraphicsPipelineDesc: vertex binding stride"));
        binding[0].stride = 24;

        attrs[1].binding = 1;  // no such binding
        CHECK(Rejects(d.Validate(), "GraphicsPipelineDesc: vertex attribute references"));
        attrs[1].binding = 0;

        attrs[1].offset = 16;  // 16 + 12 > 24
        CHECK(Rejects(d.Validate(), "GraphicsPipelineDesc: vertex attribute extends"));
        attrs[1].offset = 12;

        attrs[1].format = VertexFormat::UNorm16x4;  // 8 bytes at offset 12: fits the stride, and 12 is 4-aligned
        CHECK(d.Validate() == nullptr);
        attrs[1].offset = 14;  // a multiple of the 16-bit component size, not of 4: D3D12 refuses it
        CHECK(Rejects(d.Validate(), "GraphicsPipelineDesc: vertex attribute offset is not aligned"));
        attrs[1].format = VertexFormat::SNorm8x4;
        attrs[1].offset = 13;  // an 8-bit format is held to 4 bytes too
        CHECK(Rejects(d.Validate(), "GraphicsPipelineDesc: vertex attribute offset is not aligned"));
        attrs[1].format = VertexFormat::Float3;
        attrs[1].offset = 12;

        attrs[1].location = 0;  // duplicate of attrs[0]
        CHECK(Rejects(d.Validate(), "GraphicsPipelineDesc: duplicate vertex attribute"));
        attrs[1].location = 1;

        attrs[1].format = VertexFormat::Count;
        CHECK(Rejects(d.Validate(), "GraphicsPipelineDesc: unknown vertex format"));
        attrs[1].format = VertexFormat::Float3;
        CHECK(d.Validate() == nullptr);
    }
    SECTION("a binding no attribute reads is legal")
    {
        VertexBindingDesc two[2] = {{24, false}, {16, true}};
        d.vertex_bindings = rhi::Span<VertexBindingDesc>(two);
        CHECK(d.Validate() == nullptr);
    }
    SECTION("garbage formats are rejected, not asserted on")
    {
        d.color_targets[0].format = static_cast<Format>(200);
        CHECK(Rejects(d.Validate(), "GraphicsPipelineDesc: color target format"));
        d.color_targets[0].format = Format::BGRA8_SRGB;
        d.depth_format = static_cast<Format>(200);
        CHECK(Rejects(d.Validate(), "GraphicsPipelineDesc: depth_format"));
    }
    SECTION("primitive restart needs a strip topology")
    {
        d.primitive_restart = true;
        CHECK(Rejects(d.Validate(), "GraphicsPipelineDesc: primitive_restart"));
        d.topology = PrimitiveTopology::LineList;
        CHECK(Rejects(d.Validate(), "GraphicsPipelineDesc: primitive_restart"));
        d.topology = PrimitiveTopology::TriangleStrip;
        CHECK(d.Validate() == nullptr);
        d.topology = PrimitiveTopology::LineStrip;
        CHECK(d.Validate() == nullptr);
    }
    SECTION("garbage enum values are rejected")
    {
        d.depth_compare = static_cast<CompareOp>(99);
        CHECK(Rejects(d.Validate(), "GraphicsPipelineDesc: unknown fixed-function"));
        d.depth_compare = CompareOp::Less;
        d.topology = static_cast<PrimitiveTopology>(50);
        CHECK(Rejects(d.Validate(), "GraphicsPipelineDesc: unknown fixed-function"));
        d.topology = PrimitiveTopology::TriangleList;
        d.color_targets[0].blend.src_color = static_cast<BlendFactor>(200);
        CHECK(Rejects(d.Validate(), "GraphicsPipelineDesc: unknown blend"));
        d.color_targets[0].blend.src_color = BlendFactor::One;
        d.color_targets[0].blend.alpha_op = static_cast<BlendOp>(77);
        CHECK(Rejects(d.Validate(), "GraphicsPipelineDesc: unknown blend"));
        d.color_targets[0].blend.alpha_op = BlendOp::Add;
        d.color_targets[0].write_mask = static_cast<ColorWriteMask>(0xFF);
        CHECK(Rejects(d.Validate(), "GraphicsPipelineDesc: unknown color write mask"));
        d.color_targets[0].write_mask = ColorWriteMask::R | ColorWriteMask::A;
        CHECK(d.Validate() == nullptr);
    }
    SECTION("layouts and push constants")
    {
        d.bind_group_layouts[0] = BindGroupLayoutHandle{1, 1};
        d.bind_group_layout_count = 1;
        CHECK(d.Validate() == nullptr);
        d.bind_group_layouts[0] = BindGroupLayoutHandle{};
        CHECK(Rejects(d.Validate(), "GraphicsPipelineDesc: bind group layout handle"));
        d.bind_group_layout_count = kMaxBindGroups + 1;
        CHECK(Rejects(d.Validate(), "GraphicsPipelineDesc: too many bind group layouts"));
        d.bind_group_layout_count = 0;

        d.push_constant_bytes = 6;
        CHECK(Rejects(d.Validate(), "GraphicsPipelineDesc: push_constant_bytes is not"));
        d.push_constant_bytes = 132;
        CHECK(Rejects(d.Validate(), "GraphicsPipelineDesc: push_constant_bytes exceeds"));
        d.push_constant_bytes = 64;
        CHECK(Rejects(d.Validate(), "GraphicsPipelineDesc: push constants need"));
        d.push_constant_stages = ShaderStage::Compute;
        CHECK(Rejects(d.Validate(), "GraphicsPipelineDesc: push constants need"));
        d.push_constant_stages = ShaderStage::AllGraphics;
        CHECK(d.Validate() == nullptr);
    }
}

TEST_CASE("ComputePipelineDesc validation", "[rhi][desc]")
{
    ComputePipelineDesc d;
    CHECK(Rejects(d.Validate(), "ComputePipelineDesc: compute shader"));
    d.shader = ShaderHandle{1, 1};
    CHECK(d.Validate() == nullptr);
    d.push_constant_bytes = 16;
    CHECK(d.Validate() == nullptr);
    d.push_constant_bytes = 130;
    CHECK(Rejects(d.Validate(), "ComputePipelineDesc: push_constant_bytes is not"));
    d.push_constant_bytes = 256;
    CHECK(Rejects(d.Validate(), "ComputePipelineDesc: push_constant_bytes exceeds"));
    d.push_constant_bytes = 0;
    d.bind_group_layout_count = 1;
    CHECK(Rejects(d.Validate(), "ComputePipelineDesc: bind group layout handle"));
    d.bind_group_layouts[0] = BindGroupLayoutHandle{2, 1};
    CHECK(d.Validate() == nullptr);
}
