// Phase 5 Day 78: triangle strips on a real device with validation and synchronization validation: a quad as a plain
// strip, as an indexed strip, two strips in one indexed draw split by a restart index, and the same two strips joined by
// degenerate triangles on a pipeline without restart. Back-face culling is on, so a wrong winding alternation would drop
// the second triangle of a strip.
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gpu_fixture.h"
#include "image_compare.h"
#include "render_target.h"
#include "rhi/rhi.h"
#include "rhi/vulkan/vulkan_device.h"
#include "triangle.frag.spv.inc"
#include "triangle.vert.spv.inc"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;
using namespace engine_placeholder::rhi::vulkan;
using gpu_test::Image;

namespace {

constexpr u32 kVulkanStripTestSize = 32;

struct Vertex {
    float x, y, r, g, b;
};

// Two quads, each as the strip (top-left, bottom-left, top-right, bottom-right) in clip space (+Y is down on screen); the
// first triangle is counter-clockwise on screen, a front face for the pipeline's default winding.
// The left quad covers x -0.9..-0.1, the right quad x 0.1..0.9, both y -0.5..0.5.
void Quad(float x0, float x1, std::vector<Vertex>* out) {
    out->push_back({x0, -0.5f, 1, 0, 0});
    out->push_back({x0, 0.5f, 1, 0, 0});
    out->push_back({x1, -0.5f, 1, 0, 0});
    out->push_back({x1, 0.5f, 1, 0, 0});
}

struct StripScene {
    gpu_test::GpuFixture fx;
    VulkanDevicePtr device;
    ShaderHandle vs, fs;
    PipelineHandle plain, restart;
    BufferHandle vb, ib;

    StripScene() {
        fx.RequireValidation();
        DeviceCreateInfo info;
        info.validation = ValidationSetting::On;
        info.sync_validation = true;
        info.pipeline_cache = false;
        device = VulkanDevice::Create(info);
        if (device == nullptr) SKIP("no suitable Vulkan device");
        ShaderDesc sd;
        sd.stage = ShaderStage::Vertex;
        sd.words = shaders::kTriangleVertSpv;
        sd.word_count = shaders::kTriangleVertSpvWordCount;
        vs = device->CreateShader(sd);
        sd.stage = ShaderStage::Fragment;
        sd.words = shaders::kTriangleFragSpv;
        sd.word_count = shaders::kTriangleFragSpvWordCount;
        fs = device->CreateShader(sd);
        VertexBindingDesc binding = {sizeof(Vertex), false};
        VertexAttributeDesc attributes[2] = {{0, 0, VertexFormat::Float2, 0}, {1, 0, VertexFormat::Float3, 8}};
        GraphicsPipelineDesc gp;
        gp.vertex_shader = vs;
        gp.fragment_shader = fs;
        gp.vertex_bindings = rhi::Span<VertexBindingDesc>(&binding, 1);
        gp.vertex_attributes = rhi::Span<VertexAttributeDesc>(attributes, 2);
        gp.topology = PrimitiveTopology::TriangleStrip;
        gp.cull_mode = CullMode::Back;
        gp.color_targets[0].format = Format::RGBA8_UNorm;
        gp.color_target_count = 1;
        plain = device->CreateGraphicsPipeline(gp);
        gp.primitive_restart = true;
        restart = device->CreateGraphicsPipeline(gp);
    }
    ~StripScene() {
        if (device == nullptr) return;
        device->WaitIdle();
        if (vb.IsValid()) device->DestroyBuffer(vb);
        if (ib.IsValid()) device->DestroyBuffer(ib);
        device->DestroyPipeline(plain);
        device->DestroyPipeline(restart);
        device->DestroyShader(vs);
        device->DestroyShader(fs);
        device.Reset();
    }

    void Upload(const std::vector<Vertex>& vertices, const u16* indices, u32 index_count) {
        BufferDesc bd;
        bd.size = vertices.size() * sizeof(Vertex);
        bd.usage = BufferUsage::Vertex | BufferUsage::CopyDst;
        bd.location = MemoryLocation::GpuOnly;
        vb = device->CreateBuffer(bd);
        REQUIRE(vb.IsValid());
        REQUIRE(device->UploadBuffer(vb, 0, vertices.data(), bd.size).ok);
        if (index_count != 0) {
            bd.size = index_count * sizeof(u16);
            bd.usage = BufferUsage::Index | BufferUsage::CopyDst;
            ib = device->CreateBuffer(bd);
            REQUIRE(ib.IsValid());
            REQUIRE(device->UploadBuffer(ib, 0, indices, bd.size).ok);
        }
    }

    // Draws with `pipeline`, indexed when `index_count` is non-zero, and reads the target back.
    Image Draw(PipelineHandle pipeline, u32 vertex_count, u32 index_count) {
        gpu_test::RenderTarget target(device.Get(), kVulkanStripTestSize, kVulkanStripTestSize);
        REQUIRE(target.Valid());
        device->BeginFrame();
        CommandList* list = device->CreateCommandList();
        list->Begin();
        list->Barrier(vb, ResourceState::CopyDst, ResourceState::VertexBuffer);
        if (index_count != 0) list->Barrier(ib, ResourceState::CopyDst, ResourceState::IndexBuffer);
        list->Barrier(target.Texture(), ResourceState::Undefined, ResourceState::ColorAttachment);
        RenderingDesc pass;
        pass.color_count = 1;
        pass.color[0].texture = target.Texture();
        pass.color[0].clear_color[0] = pass.color[0].clear_color[1] = pass.color[0].clear_color[2] = 0.2f;
        pass.color[0].clear_color[3] = 1.0f;
        list->BeginRendering(pass);
        list->SetPipeline(pipeline);
        list->SetViewport(Viewport{0, 0, static_cast<f32>(kVulkanStripTestSize), static_cast<f32>(kVulkanStripTestSize), 0.0f, 1.0f});
        list->SetScissor(ScissorRect{0, 0, kVulkanStripTestSize, kVulkanStripTestSize});
        list->SetVertexBuffer(0, vb, 0);
        if (index_count != 0) {
            list->SetIndexBuffer(ib, 0, IndexType::UInt16);
            list->DrawIndexed(index_count, 1, 0, 0, 0);
        } else {
            list->Draw(vertex_count, 1, 0, 0);
        }
        list->EndRendering();
        list->End();
        CommandList* lists[] = {list};
        REQUIRE(device->Submit(lists, 1) != 0);
        device->EndFrame();
        device->WaitIdle();
        Image image;
        REQUIRE(target.Readback(&image));
        return image;
    }
};

bool IsRed(const Image& image, u32 x, u32 y) {
    const unsigned char* p = &image.rgba[(static_cast<size_t>(y) * image.width + x) * 4];
    return p[0] > 200 && p[1] < 50 && p[2] < 50;
}

bool IsBackground(const Image& image, u32 x, u32 y) {
    const unsigned char* p = &image.rgba[(static_cast<size_t>(y) * image.width + x) * 4];
    return p[0] > 40 && p[0] < 70 && p[1] > 40 && p[1] < 70; // 0.2 * 255 = 51
}

// Pixel columns of the quads: x -0.9..-0.1 is columns 1.6..14.4, x 0.1..0.9 is 17.6..30.4; rows 8..24.
// Samples near the two opposite corners of each quad lie in different triangles of the strip.
void CheckQuad(const Image& image, bool left, bool expect_filled) {
    const u32 x0 = left ? 3 : 19;
    const u32 x1 = left ? 12 : 28;
    const u32 points[4][2] = {{x0, 10}, {x1, 21}, {x0, 21}, {x1, 10}};
    for (const auto& p : points) {
        if (expect_filled) CHECK(IsRed(image, p[0], p[1]));
        else CHECK(IsBackground(image, p[0], p[1]));
    }
}

} // namespace

TEST_CASE("A quad drawn as a four-vertex strip fills both triangles with back-face culling on", "[rhi][vulkan][gpu][strip]") {
    StripScene s;
    s.fx.ExpectErrors(0);
    std::vector<Vertex> v;
    Quad(-0.9f, -0.1f, &v);
    s.Upload(v, nullptr, 0);
    const Image image = s.Draw(s.plain, 4, 0);
    CheckQuad(image, true, true);
    CHECK(IsBackground(image, 16, 16)); // between the quad and the edge of the target
}

TEST_CASE("An indexed strip draws the same quad", "[rhi][vulkan][gpu][strip]") {
    StripScene s;
    s.fx.ExpectErrors(0);
    std::vector<Vertex> v;
    Quad(-0.9f, -0.1f, &v);
    const u16 indices[4] = {0, 1, 2, 3};
    s.Upload(v, indices, 4);
    CheckQuad(s.Draw(s.plain, 0, 4), true, true);
}

TEST_CASE("A restart index splits one indexed draw into two strips", "[rhi][vulkan][gpu][strip]") {
    StripScene s;
    s.fx.ExpectErrors(0);
    std::vector<Vertex> v;
    Quad(-0.9f, -0.1f, &v);
    Quad(0.1f, 0.9f, &v);
    const u16 indices[9] = {0, 1, 2, 3, 0xFFFF, 4, 5, 6, 7};
    s.Upload(v, indices, 9);
    const Image image = s.Draw(s.restart, 0, 9);
    CheckQuad(image, true, true);
    CheckQuad(image, false, true);
    // Without the restart the strip would continue through the gap and fill it; with it the gap is empty.
    CHECK(IsBackground(image, 16, 16));
    CHECK(IsBackground(image, 15, 12));
    CHECK(IsBackground(image, 16, 20));
}

TEST_CASE("Degenerate triangles join two strips on a pipeline without restart", "[rhi][vulkan][gpu][strip]") {
    StripScene s;
    s.fx.ExpectErrors(0);
    std::vector<Vertex> v;
    Quad(-0.9f, -0.1f, &v);
    Quad(0.1f, 0.9f, &v);
    // 0 1 2 3 | 3 4 (the join) | 4 5 6 7: the second strip must start at an even position, so 3 4 plus one more 4 would be
    // needed only if the first strip had odd length; it has four vertices.
    const u16 indices[10] = {0, 1, 2, 3, 3, 4, 4, 5, 6, 7};
    s.Upload(v, indices, 10);
    const Image image = s.Draw(s.plain, 0, 10);
    CheckQuad(image, true, true);
    CheckQuad(image, false, true);
    CHECK(IsBackground(image, 16, 16));
    CHECK(IsBackground(image, 15, 12));
}
