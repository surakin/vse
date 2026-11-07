#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <catch2/catch_test_macros.hpp>

#include "temp_dir.h"
#include "io/file.h"
#include "io/file_system.h"
#include "gpu_fixture.h"
#include "rhi/rhi.h"
#include "rhi/rhi_log.h"
#include "rhi/vulkan/vk_pipeline_policy.h"
#include "rhi/vulkan/vulkan_device.h"
#include "triangle.frag.spv.inc"
#include "triangle.vert.spv.inc"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;
using namespace engine_placeholder::rhi::vulkan;

namespace {

struct VulkanPipelineTestQuietLog {
    bool saved = LogRhi.Enabled();
    VulkanPipelineTestQuietLog() { LogRhi.SetEnabled(false); }
    ~VulkanPipelineTestQuietLog() { LogRhi.SetEnabled(saved); }
};

bool Contains(const char* text, const char* needle) { return text != nullptr && std::strstr(text, needle) != nullptr; }

PipelineCacheIdentity Identity() {
    PipelineCacheIdentity id;
    id.vendor_id = 0x10de;
    id.device_id = 0x2504;
    for (u8 i = 0; i < 16; ++i) id.uuid[i] = static_cast<u8>(0xa0 + i);
    return id;
}

// A well-formed version-one header for Identity(), followed by `extra` payload bytes.
std::vector<unsigned char> CacheBlob(usize extra = 8) {
    const PipelineCacheIdentity id = Identity();
    std::vector<unsigned char> b(32 + extra, 0x5a);
    auto put = [&](usize at, u32 v) {
        for (int i = 0; i < 4; ++i) b[at + i] = static_cast<unsigned char>(v >> (8 * i));
    };
    put(0, 32);
    put(4, 1);
    put(8, id.vendor_id);
    put(12, id.device_id);
    for (int i = 0; i < 16; ++i) b[16 + i] = id.uuid[i];
    return b;
}

} // namespace

TEST_CASE("Pipeline state converts to the matching Vulkan enums", "[rhi][vulkan][pipeline][policy]") {
    CHECK(ToVkTopology(PrimitiveTopology::TriangleList) == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    CHECK(ToVkTopology(PrimitiveTopology::PointList) == VK_PRIMITIVE_TOPOLOGY_POINT_LIST);
    CHECK(ToVkCullMode(CullMode::None) == VK_CULL_MODE_NONE);
    CHECK(ToVkCullMode(CullMode::Back) == VK_CULL_MODE_BACK_BIT);
    CHECK(ToVkFrontFace(FrontFace::Clockwise) == VK_FRONT_FACE_CLOCKWISE);
    CHECK(ToVkFrontFace(FrontFace::CounterClockwise) == VK_FRONT_FACE_COUNTER_CLOCKWISE);
    CHECK(ToVkCompareOp(CompareOp::LessEqual) == VK_COMPARE_OP_LESS_OR_EQUAL);
    CHECK(ToVkBlendFactor(BlendFactor::OneMinusSrcAlpha) == VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
    CHECK(ToVkBlendOp(BlendOp::ReverseSubtract) == VK_BLEND_OP_REVERSE_SUBTRACT);
    CHECK(ToVkVertexFormat(VertexFormat::Float3) == VK_FORMAT_R32G32B32_SFLOAT);
    CHECK(ToVkVertexFormat(VertexFormat::UNorm8x4) == VK_FORMAT_R8G8B8A8_UNORM);
    CHECK(ToVkIndexType(IndexType::UInt16) == VK_INDEX_TYPE_UINT16);
    CHECK(ToVkIndexType(IndexType::UInt32) == VK_INDEX_TYPE_UINT32);
    CHECK(ToVkShaderStages(ShaderStage::AllGraphics) == (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT));
    CHECK(ToVkShaderStages(ShaderStage::Compute) == VK_SHADER_STAGE_COMPUTE_BIT);
    CHECK(ToVkDescriptorType(BindingType::SampledTexture) == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
    CHECK(ToVkDescriptorType(BindingType::Sampler) == VK_DESCRIPTOR_TYPE_SAMPLER);
    CHECK(ToVkDescriptorType(BindingType::StorageTexture) == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    // Every color write mask bit is the Vulkan component bit: the backend casts it directly.
    CHECK(static_cast<u32>(ColorWriteMask::R) == VK_COLOR_COMPONENT_R_BIT);
    CHECK(static_cast<u32>(ColorWriteMask::G) == VK_COLOR_COMPONENT_G_BIT);
    CHECK(static_cast<u32>(ColorWriteMask::B) == VK_COLOR_COMPONENT_B_BIT);
    CHECK(static_cast<u32>(ColorWriteMask::A) == VK_COLOR_COMPONENT_A_BIT);
}

TEST_CASE("CheckPipelineCacheHeader accepts only a blob for this device and driver", "[rhi][vulkan][pipeline][policy]") {
    const PipelineCacheIdentity id = Identity();
    std::vector<unsigned char> blob = CacheBlob();
    CHECK(CheckPipelineCacheHeader(blob.data(), blob.size(), id) == nullptr);
    CHECK(CheckPipelineCacheHeader(nullptr, 0, id) != nullptr);
    CHECK(Contains(CheckPipelineCacheHeader(blob.data(), 31, id), "smaller"));
    SECTION("each header field is checked") {
        std::vector<unsigned char> b = blob;
        b[0] = 16; // header size below 32
        CHECK(Contains(CheckPipelineCacheHeader(b.data(), b.size(), id), "header size"));
        b = blob;
        b[4] = 2;
        CHECK(Contains(CheckPipelineCacheHeader(b.data(), b.size(), id), "version"));
        b = blob;
        b[8] ^= 1;
        CHECK(Contains(CheckPipelineCacheHeader(b.data(), b.size(), id), "vendor"));
        b = blob;
        b[12] ^= 1;
        CHECK(Contains(CheckPipelineCacheHeader(b.data(), b.size(), id), "device"));
        b = blob;
        b[31] ^= 1;
        CHECK(Contains(CheckPipelineCacheHeader(b.data(), b.size(), id), "driver"));
    }
    SECTION("the file is matched against the identity it is asked about") {
        PipelineCacheIdentity other = id;
        other.vendor_id = 0x1002;
        CHECK(CheckPipelineCacheHeader(blob.data(), blob.size(), other) != nullptr);
    }
}

TEST_CASE("The pipeline cache path and directory follow the documented order", "[rhi][vulkan][pipeline][policy]") {
    char path[256];
    REQUIRE(BuildPipelineCachePath("/tmp/c", Identity(), path, sizeof(path)));
    CHECK(std::strcmp(path, "/tmp/c/pipeline_cache_10de_2504_a0a1a2a3a4a5a6a7a8a9aaabacadaeaf.bin") == 0);
    REQUIRE(BuildPipelineCachePath("/tmp/c/", Identity(), path, sizeof(path)));
    CHECK(std::strstr(path, "/tmp/c/pipeline_cache_") == path); // no doubled slash
    char tiny[16];
    CHECK_FALSE(BuildPipelineCachePath("/tmp/c", Identity(), tiny, sizeof(tiny)));
    CHECK(tiny[0] == '\0');

    char dir[128];
    CHECK(ChoosePipelineCacheDir("/explicit", "/env", dir, sizeof(dir)) == PipelineCacheDirChoice::Chosen);
    CHECK(std::strcmp(dir, "/explicit") == 0);
    CHECK(ChoosePipelineCacheDir(nullptr, "/env", dir, sizeof(dir)) == PipelineCacheDirChoice::Chosen);
    CHECK(std::strcmp(dir, "/env") == 0);
    CHECK(ChoosePipelineCacheDir("", "", dir, sizeof(dir)) == PipelineCacheDirChoice::NotConfigured);
    CHECK(dir[0] == '\0'); // neither set: the caller falls back to the user's cache directory
    CHECK(ChoosePipelineCacheDir(nullptr, nullptr, dir, sizeof(dir)) == PipelineCacheDirChoice::NotConfigured);
    CHECK(ChoosePipelineCacheDir("/a/very/long/directory/name/that/does/not/fit", nullptr, tiny, sizeof(tiny)) == PipelineCacheDirChoice::TooLong);
    CHECK(tiny[0] == '\0');

    char app[32];
    REQUIRE(SanitizePipelineCacheAppName("my/app", app, sizeof(app)));
    CHECK(std::strcmp(app, "my_app") == 0);
    REQUIRE(SanitizePipelineCacheAppName(nullptr, app, sizeof(app)));
    CHECK(std::strcmp(app, "engine") == 0);
    REQUIRE(SanitizePipelineCacheAppName("", app, sizeof(app)));
    CHECK(std::strcmp(app, "engine") == 0);
    CHECK_FALSE(SanitizePipelineCacheAppName("an application name that is much too long for the buffer", app, 8));
    CHECK(app[0] == '\0');
}

namespace {

struct TriangleDevice {
    gpu_test::GpuFixture fx;
    VulkanDevicePtr device;
    ShaderHandle vs;
    ShaderHandle fs;

    explicit TriangleDevice(const char* cache_dir = nullptr, bool cache = true) {
        fx.RequireValidation();
        DeviceCreateInfo info;
        info.validation = ValidationSetting::On;
        info.sync_validation = true;
        info.pipeline_cache = cache;
        info.pipeline_cache_dir = cache_dir;
        device = VulkanDevice::Create(info);
        if (device == nullptr) {
            SKIP("no suitable Vulkan device");
        }
        ShaderDesc d;
        d.stage = ShaderStage::Vertex;
        d.words = shaders::kTriangleVertSpv;
        d.word_count = shaders::kTriangleVertSpvWordCount;
        vs = device->CreateShader(d);
        d.stage = ShaderStage::Fragment;
        d.words = shaders::kTriangleFragSpv;
        d.word_count = shaders::kTriangleFragSpvWordCount;
        fs = device->CreateShader(d);
    }
    ~TriangleDevice() {
        if (device != nullptr) {
            device->DestroyShader(vs);
            device->DestroyShader(fs);
            device->WaitIdle();
            device.Reset();
        }
    }

    GraphicsPipelineDesc Desc(Format target) const {
        static VertexBindingDesc binding = {20, false};
        static VertexAttributeDesc attributes[2] = {{0, 0, VertexFormat::Float2, 0}, {1, 0, VertexFormat::Float3, 8}};
        GraphicsPipelineDesc g;
        g.vertex_shader = vs;
        g.fragment_shader = fs;
        g.vertex_bindings = rhi::Span<VertexBindingDesc>(&binding, 1);
        g.vertex_attributes = rhi::Span<VertexAttributeDesc>(attributes, 2);
        g.cull_mode = CullMode::None;
        g.color_targets[0].format = target;
        g.color_target_count = 1;
        g.debug_name = "triangle";
        return g;
    }
};

} // namespace

TEST_CASE("The triangle pipeline compiles for the swapchain format and an offscreen format", "[rhi][vulkan][gpu][pipeline]") {
    TriangleDevice t;
    for (Format format : {Format::BGRA8_SRGB, Format::RGBA8_UNorm, Format::RGBA16_Float}) {
        if (!t.device->FormatSupport(format, TextureUsage::ColorAttachment)) {
            continue;
        }
        const PipelineHandle p = t.device->CreateGraphicsPipeline(t.Desc(format));
        CHECK(p.IsValid());
        t.device->DestroyPipeline(p);
    }
    t.device->BeginFrame();
    t.device->EndFrame();
    t.device->BeginFrame();
    t.device->EndFrame();
    t.device->WaitIdle();
    t.device->BeginFrame();
    CHECK(t.device->Counts().pipelines == 0);
    t.device->EndFrame();
}

TEST_CASE("Graphics pipeline creation refuses bad input before the driver sees it", "[rhi][vulkan][gpu][pipeline]") {
    TriangleDevice t;
    t.fx.ExpectErrors(0);
    VulkanPipelineTestQuietLog quiet;
    const Format target = Format::RGBA8_UNorm;
    SECTION("no vertex shader") {
        GraphicsPipelineDesc g = t.Desc(target);
        g.vertex_shader = ShaderHandle{};
        CHECK_FALSE(t.device->CreateGraphicsPipeline(g).IsValid());
    }
    SECTION("a destroyed shader") {
        ShaderDesc d;
        d.stage = ShaderStage::Vertex;
        d.words = shaders::kTriangleVertSpv;
        d.word_count = shaders::kTriangleVertSpvWordCount;
        ShaderHandle gone = t.device->CreateShader(d);
        t.device->DestroyShader(gone);
        t.device->BeginFrame();
        t.device->EndFrame();
        t.device->BeginFrame();
        t.device->EndFrame();
        t.device->WaitIdle();
        t.device->BeginFrame();
        GraphicsPipelineDesc g = t.Desc(target);
        g.vertex_shader = gone;
        CHECK_FALSE(t.device->CreateGraphicsPipeline(g).IsValid());
        t.device->EndFrame();
    }
    SECTION("stage mismatch") {
        GraphicsPipelineDesc g = t.Desc(target);
        g.vertex_shader = t.fs;
        CHECK_FALSE(t.device->CreateGraphicsPipeline(g).IsValid());
        g = t.Desc(target);
        g.fragment_shader = t.vs;
        CHECK_FALSE(t.device->CreateGraphicsPipeline(g).IsValid());
    }
    SECTION("a layout that does not exist") {
        GraphicsPipelineDesc g = t.Desc(target);
        g.bind_group_layouts[0] = BindGroupLayoutHandle{};
        g.bind_group_layout_count = 1;
        CHECK_FALSE(t.device->CreateGraphicsPipeline(g).IsValid());
    }
    SECTION("a color format the device cannot render to") {
        bool tried = false;
        for (u32 f = 1; f < static_cast<u32>(Format::Count); ++f) {
            const Format format = static_cast<Format>(f);
            if (!FormatInfo(format).is_depth && !FormatInfo(format).is_compressed && !t.device->FormatSupport(format, TextureUsage::ColorAttachment)) {
                GraphicsPipelineDesc g = t.Desc(format);
                CHECK_FALSE(t.device->CreateGraphicsPipeline(g).IsValid());
                tried = true;
                break;
            }
        }
        if (!tried) {
            WARN("every color format renders on this device; the unsupported-format path was not exercised");
        }
    }
    SECTION("limits and descriptor errors") {
        GraphicsPipelineDesc g = t.Desc(target);
        g.push_constant_bytes = 4096;
        g.push_constant_stages = ShaderStage::Vertex;
        CHECK_FALSE(t.device->CreateGraphicsPipeline(g).IsValid());
        g = t.Desc(target);
        g.samples = 4;
        CHECK_FALSE(t.device->CreateGraphicsPipeline(g).IsValid());
    }
    CHECK(t.device->Counts().pipelines == 0);
}

TEST_CASE("Equal bind group layouts share one descriptor set layout and pipelines share a pipeline layout", "[rhi][vulkan][gpu][pipeline]") {
    TriangleDevice t;
    BindingDesc entries[2] = {{0, BindingType::UniformBuffer, ShaderStage::Vertex, 1}, {1, BindingType::SampledTexture, ShaderStage::Fragment, 1}};
    BindGroupLayoutDesc ld;
    ld.entries = rhi::Span<BindingDesc>(entries, 2);
    const BindGroupLayoutHandle a = t.device->CreateBindGroupLayout(ld);
    const BindGroupLayoutHandle b = t.device->CreateBindGroupLayout(ld);
    REQUIRE(a.IsValid());
    REQUIRE(b.IsValid());
    CHECK_FALSE(a == b); // two handles...
    CHECK(t.device->Counts().bind_group_layouts == 2);
    CHECK(t.device->Counts().descriptor_set_layouts == 1); // ...one Vulkan object

    // The same bindings listed in another order are the same layout.
    BindingDesc swapped[2] = {entries[1], entries[0]};
    BindGroupLayoutDesc sd;
    sd.entries = rhi::Span<BindingDesc>(swapped, 2);
    const BindGroupLayoutHandle reordered = t.device->CreateBindGroupLayout(sd);
    REQUIRE(reordered.IsValid());
    CHECK(t.device->Counts().descriptor_set_layouts == 1);
    t.device->DestroyBindGroupLayout(reordered);

    entries[1].stages = ShaderStage::AllGraphics; // different stages: a different layout
    const BindGroupLayoutHandle c = t.device->CreateBindGroupLayout(ld);
    REQUIRE(c.IsValid());
    CHECK(t.device->Counts().descriptor_set_layouts == 2);

    GraphicsPipelineDesc g = t.Desc(Format::RGBA8_UNorm);
    g.bind_group_layouts[0] = a;
    g.bind_group_layout_count = 1;
    const PipelineHandle p1 = t.device->CreateGraphicsPipeline(g);
    g.bind_group_layouts[0] = b; // equal to `a`
    const PipelineHandle p2 = t.device->CreateGraphicsPipeline(g);
    REQUIRE(p1.IsValid());
    REQUIRE(p2.IsValid());
    VulkanDevice::PipelineInfo i1, i2;
    REQUIRE(t.device->LookupPipeline(p1, &i1));
    REQUIRE(t.device->LookupPipeline(p2, &i2));
    CHECK(i1.layout == i2.layout);
    CHECK(i1.layout_ids[0] == i2.layout_ids[0]);
    CHECK(t.device->Counts().pipeline_layouts == 1);

    g.bind_group_layouts[0] = c;
    const PipelineHandle p3 = t.device->CreateGraphicsPipeline(g);
    REQUIRE(p3.IsValid());
    VulkanDevice::PipelineInfo i3;
    REQUIRE(t.device->LookupPipeline(p3, &i3));
    CHECK(i3.layout != i1.layout);
    CHECK(t.device->Counts().pipeline_layouts == 2);

    // Push constants are part of the key.
    g.bind_group_layouts[0] = a;
    g.push_constant_bytes = 16;
    g.push_constant_stages = ShaderStage::Vertex;
    const PipelineHandle p4 = t.device->CreateGraphicsPipeline(g);
    REQUIRE(p4.IsValid());
    CHECK(t.device->Counts().pipeline_layouts == 3);

    t.device->DestroyPipeline(p1);
    t.device->DestroyPipeline(p2);
    t.device->DestroyPipeline(p3);
    t.device->DestroyPipeline(p4);
    t.device->DestroyBindGroupLayout(a);
    t.device->DestroyBindGroupLayout(b);
    t.device->DestroyBindGroupLayout(c);
    for (int i = 0; i < 3; ++i) {
        t.device->BeginFrame();
        t.device->EndFrame();
    }
    t.device->WaitIdle();
    t.device->BeginFrame();
    CHECK(t.device->Counts().bind_group_layouts == 0);
    CHECK(t.device->Counts().descriptor_set_layouts == 0);
    CHECK(t.device->Counts().pipelines == 0);
    CHECK(t.device->Counts().pipeline_layouts == 0); // freed with the last pipeline that used them
    t.device->EndFrame();
}

TEST_CASE("Creating and destroying many distinct layouts does not run out of pipeline layouts", "[rhi][vulkan][gpu][pipeline]") {
    TriangleDevice t;
    // More distinct layouts over time than the cache holds at once: each round's layouts are released.
    for (u32 round = 0; round < 40; ++round) {
        BindGroupLayoutHandle layouts[16];
        PipelineHandle pipelines[16];
        for (u32 i = 0; i < 16; ++i) {
            BindingDesc entry = {i + round, BindingType::UniformBuffer, ShaderStage::Vertex, 1};
            BindGroupLayoutDesc ld;
            ld.entries = rhi::Span<BindingDesc>(&entry, 1);
            layouts[i] = t.device->CreateBindGroupLayout(ld);
            REQUIRE(layouts[i].IsValid());
            GraphicsPipelineDesc g = t.Desc(Format::RGBA8_UNorm);
            g.bind_group_layouts[0] = layouts[i];
            g.bind_group_layout_count = 1;
            pipelines[i] = t.device->CreateGraphicsPipeline(g);
            REQUIRE(pipelines[i].IsValid());
        }
        for (u32 i = 0; i < 16; ++i) {
            t.device->DestroyPipeline(pipelines[i]);
            t.device->DestroyBindGroupLayout(layouts[i]);
        }
        t.device->BeginFrame();
        t.device->EndFrame();
        t.device->BeginFrame();
        t.device->EndFrame();
        t.device->WaitIdle();
    }
    t.device->BeginFrame();
    CHECK(t.device->Counts().pipeline_layouts == 0);
    CHECK(t.device->Counts().descriptor_set_layouts == 0);
    t.device->EndFrame();
}

TEST_CASE("The triangle pipeline draws a triangle into an offscreen target", "[rhi][vulkan][gpu][pipeline]") {
    TriangleDevice t;
    t.fx.ExpectErrors(0);
    constexpr u32 kSize = 64;
    TextureDesc td;
    td.width = kSize;
    td.height = kSize;
    td.format = Format::RGBA8_UNorm;
    td.usage = TextureUsage::ColorAttachment | TextureUsage::CopySrc;
    const TextureHandle target = t.device->CreateTexture(td);
    REQUIRE(target.IsValid());

    // One red-green-blue triangle covering the middle of the target, in clip space.
    const float vertices[3 * 5] = {
        0.0f, -0.8f, 1.0f, 0.0f, 0.0f, //
        0.8f, 0.8f,  1.0f, 0.0f, 0.0f, //
        -0.8f, 0.8f, 1.0f, 0.0f, 0.0f, //
    };
    BufferDesc bd;
    bd.size = sizeof(vertices);
    bd.usage = BufferUsage::Vertex;
    bd.location = MemoryLocation::CpuToGpu;
    const BufferHandle vb = t.device->CreateBuffer(bd);
    REQUIRE(vb.IsValid());
    void* mapped = t.device->MapBuffer(vb);
    REQUIRE(mapped != nullptr);
    std::memcpy(mapped, vertices, sizeof(vertices));
    t.device->UnmapBuffer(vb);

    const PipelineHandle pipeline = t.device->CreateGraphicsPipeline(t.Desc(Format::RGBA8_UNorm));
    REQUIRE(pipeline.IsValid());

    t.device->BeginFrame();
    CommandList* list = t.device->CreateCommandList();
    list->Begin();
    list->Barrier(target, ResourceState::Undefined, ResourceState::ColorAttachment);
    RenderingDesc pass;
    pass.color_count = 1;
    pass.color[0].texture = target;
    pass.color[0].clear_color[0] = 0.0f;
    pass.color[0].clear_color[1] = 0.0f;
    pass.color[0].clear_color[2] = 1.0f;
    pass.color[0].clear_color[3] = 1.0f;
    list->BeginRendering(pass);
    list->SetPipeline(pipeline);
    list->SetViewport(Viewport{0, 0, static_cast<f32>(kSize), static_cast<f32>(kSize), 0.0f, 1.0f});
    list->SetScissor(ScissorRect{0, 0, kSize, kSize});
    list->SetVertexBuffer(0, vb, 0);
    list->Draw(3, 1, 0, 0);
    list->EndRendering();
    list->End();
    CommandList* lists[] = {list};
    REQUIRE(t.device->Submit(lists, 1) != 0);
    t.device->EndFrame();

    std::vector<unsigned char> pixels(kSize * kSize * 4);
    REQUIRE(t.device->ReadbackTexture(target, 0, 0, pixels.data(), pixels.size(), ResourceState::ColorAttachment));
    auto at = [&](u32 x, u32 y) { return &pixels[(y * kSize + x) * 4]; };
    // The triangle's interior is red; the corner outside it keeps the blue clear color.
    CHECK(at(32, 40)[0] == 255);
    CHECK(at(32, 40)[2] == 0);
    CHECK(at(2, 2)[0] == 0);
    CHECK(at(2, 2)[2] == 255);

    t.device->DestroyPipeline(pipeline);
    t.device->DestroyBuffer(vb);
    t.device->DestroyTexture(target);
}

TEST_CASE("A cache directory that cannot be used never stops the device", "[rhi][vulkan][gpu][pipeline][cache]") {
    io_test::TempDir temp_dir;
    VulkanPipelineTestQuietLog quiet;
    SECTION("the cache directory is a file") {
        REQUIRE(io_test::WriteText(temp_dir / "blocked", "not a directory"));
        const engine_test::Text dir = engine_test::Concat(temp_dir.Native().View(), "/blocked");
        TriangleDevice t(dir.CStr());
        CHECK(t.device->CreateGraphicsPipeline(t.Desc(Format::RGBA8_UNorm)).IsValid());
        CHECK_FALSE(t.device->SavePipelineCache()); // reported with a warning, not a failure of the device
        CHECK(t.device->PipelineCache().saved_bytes == 0);
    }
    SECTION("the cache directory cannot be written") {
        if (::geteuid() == 0) {
            SUCCEED("root writes anywhere");
            return;
        }
        REQUIRE(io::CreateDirectory(temp_dir / "readonly").Ok());
        REQUIRE(::chmod(engine_test::Concat(temp_dir.Native().View(), "/readonly").CStr(), 0500) == 0);
        const engine_test::Text dir = engine_test::Concat(temp_dir.Native().View(), "/readonly/cache");
        TriangleDevice t(dir.CStr());
        CHECK(t.device->CreateGraphicsPipeline(t.Desc(Format::RGBA8_UNorm)).IsValid());
        CHECK_FALSE(t.device->SavePipelineCache());
    }
    SECTION("the path is too long to hold a cache file name") {
        const engine_test::Text long_name = engine_test::Repeated(600, 'd');
        const engine_test::Text dir = engine_test::Concat(temp_dir.Native().View(), "/", long_name.View());
        TriangleDevice t(dir.CStr());
        CHECK(t.device->PipelineCachePath()[0] == '\0'); // running without a cache
        CHECK(t.device->CreateGraphicsPipeline(t.Desc(Format::RGBA8_UNorm)).IsValid());
    }
}

TEST_CASE("The pipeline cache is written at shutdown, reused, and ignored when damaged", "[rhi][vulkan][gpu][pipeline][cache]") {
    io_test::TempDir temp_dir;
    char dir[512];
    REQUIRE(temp_dir.Native().Length() < sizeof(dir));
    std::snprintf(dir, sizeof(dir), "%s", temp_dir.Native().CStr()); // the TempDir removes everything, whatever a section left behind
    char file[512] = {};

    {
        TriangleDevice t(dir);
        const VulkanDevice::PipelineCacheStats s = t.device->PipelineCache();
        CHECK(s.enabled);
        CHECK(s.loaded_bytes == 0); // nothing there yet
        CHECK_FALSE(s.rejected);
        std::strncpy(file, t.device->PipelineCachePath(), sizeof(file) - 1);
        REQUIRE(file[0] != '\0');
        CHECK(std::strstr(file, dir) == file);
        const PipelineHandle p = t.device->CreateGraphicsPipeline(t.Desc(Format::RGBA8_UNorm));
        CHECK(p.IsValid());
        t.device->DestroyPipeline(p);
    } // saved here
    io::FileInfo st;
    REQUIRE(io::Stat(io::Path(StringView(file)), &st).Ok());
    const u64 first_size = st.size;
    CHECK(first_size > 32);

    {
        TriangleDevice t(dir);
        const VulkanDevice::PipelineCacheStats s = t.device->PipelineCache();
        CHECK(s.loaded_bytes == first_size); // reused
        CHECK_FALSE(s.rejected);
    }

    SECTION("the cache can be saved while the device runs") {
        {
            TriangleDevice t(dir);
            const PipelineHandle p = t.device->CreateGraphicsPipeline(t.Desc(Format::RGBA8_UNorm));
            REQUIRE(p.IsValid());
            REQUIRE(io::Remove(io::Path(StringView(file))).Ok());
            CHECK(t.device->SavePipelineCache());
            CHECK(io::Stat(io::Path(StringView(file)), &st).Ok());
            CHECK(t.device->PipelineCache().saved_bytes > 32);
            t.device->DestroyPipeline(p);
        }
    }
    SECTION("a cache path that is a directory is ignored, not read") {
        REQUIRE(io::Remove(io::Path(StringView(file))).Ok());
        REQUIRE(io::CreateDirectory(io::Path(StringView(file))).Ok());
        {
            TriangleDevice t(dir);
            CHECK(t.device->PipelineCache().loaded_bytes == 0);
            CHECK(t.device->CreateGraphicsPipeline(t.Desc(Format::RGBA8_UNorm)).IsValid());
        }
        REQUIRE(io::Remove(io::Path(StringView(file))).Ok());
        REQUIRE(io::Stat(io::Path(StringView(file)), &st).Is(io::Error::NotFound)); // saving into the directory failed, and nothing was left behind
    }
    SECTION("a truncated file is ignored and replaced") {
        REQUIRE(truncate(file, 10) == 0);
        {
            VulkanPipelineTestQuietLog quiet;
            TriangleDevice t(dir);
            const VulkanDevice::PipelineCacheStats s = t.device->PipelineCache();
            CHECK(s.rejected);
            CHECK(s.loaded_bytes == 0);
            CHECK(t.device->CreateGraphicsPipeline(t.Desc(Format::RGBA8_UNorm)).IsValid()); // still works
        }
        TriangleDevice again(dir);
        CHECK(again.device->PipelineCache().loaded_bytes > 32); // the good file was written back
        CHECK_FALSE(again.device->PipelineCache().rejected);
    }
    SECTION("a file for another device is ignored") {
        io::FilePtr f;
        REQUIRE(io::File::Open(io::Path(StringView(file)), io::OpenMode::ReadWrite, io::kOpenNone, &f).Ok());
        const unsigned char bogus[4] = {0xde, 0xad, 0xbe, 0xef};
        REQUIRE(f->Write(bogus, 4, 8).Ok()); // the vendor id
        REQUIRE(f->Close().Ok());
        VulkanPipelineTestQuietLog quiet;
        TriangleDevice t(dir);
        CHECK(t.device->PipelineCache().rejected);
        CHECK(t.device->PipelineCache().loaded_bytes == 0);
    }
    SECTION("garbage with a plausible header is survived") {
        io::FilePtr f;
        REQUIRE(io::File::Open(io::Path(StringView(file)), io::OpenMode::ReadWrite, io::kOpenNone, &f).Ok());
        const unsigned char junk[64] = {0xff};
        REQUIRE(f->Write(junk, sizeof(junk), 40).Ok());
        REQUIRE(f->Close().Ok());
        VulkanPipelineTestQuietLog quiet;
        TriangleDevice t(dir);
        CHECK(t.device->CreateGraphicsPipeline(t.Desc(Format::RGBA8_UNorm)).IsValid());
    }
    SECTION("an empty file is ignored") {
        REQUIRE(truncate(file, 0) == 0);
        VulkanPipelineTestQuietLog quiet;
        TriangleDevice t(dir);
        CHECK(t.device->PipelineCache().loaded_bytes == 0);
    }
    SECTION("disabling the cache leaves the file alone") {
        TriangleDevice t(dir, /*cache=*/false);
        CHECK_FALSE(t.device->PipelineCache().enabled);
        CHECK(t.device->PipelineCachePath()[0] == '\0');
    }
}
