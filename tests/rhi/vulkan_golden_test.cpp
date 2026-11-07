#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <catch2/catch_test_macros.hpp>

#include "io/file.h"
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
using gpu_test::CompareOptions;
using gpu_test::Image;

namespace {

Image Solid(unsigned w, unsigned h, unsigned char r, unsigned char g,
            unsigned char b) {
  Image img;
  img.width = w;
  img.height = h;
  img.rgba.resize(static_cast<size_t>(w) * h * 4);
  for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
    img.rgba[i * 4 + 0] = r;
    img.rgba[i * 4 + 1] = g;
    img.rgba[i * 4 + 2] = b;
    img.rgba[i * 4 + 3] = 255;
  }
  return img;
}

// A half-and-half image: the left half is `a`, the right half `b`.
Image Split(unsigned w, unsigned h, unsigned char a, unsigned char b) {
  Image img = Solid(w, h, a, a, a);
  for (unsigned y = 0; y < h; ++y) {
    for (unsigned x = w / 2; x < w; ++x) {
      unsigned char *p = &img.rgba[(static_cast<size_t>(y) * w + x) * 4];
      p[0] = p[1] = p[2] = b;
    }
  }
  return img;
}

constexpr u32 kSize = 64;
constexpr float kClip[3][2] = {{0.0f, -0.8f}, {0.8f, 0.8f}, {-0.8f, 0.8f}};
constexpr float kColor[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};

// The color the triangle has at the center of pixel (x, y): barycentric
// interpolation of the vertex colors. Returns false when the pixel center lies
// outside the triangle.
bool ExpectedColor(u32 x, u32 y, float out[3]) {
  float p[3][2];
  for (int i = 0; i < 3; ++i) {
    p[i][0] = (kClip[i][0] + 1.0f) * 0.5f * kSize;
    p[i][1] = (kClip[i][1] + 1.0f) * 0.5f * kSize;
  }
  const float px = static_cast<float>(x) + 0.5f;
  const float py = static_cast<float>(y) + 0.5f;
  const float det = (p[1][1] - p[2][1]) * (p[0][0] - p[2][0]) +
                    (p[2][0] - p[1][0]) * (p[0][1] - p[2][1]);
  const float l0 = ((p[1][1] - p[2][1]) * (px - p[2][0]) +
                    (p[2][0] - p[1][0]) * (py - p[2][1])) /
                   det;
  const float l1 = ((p[2][1] - p[0][1]) * (px - p[2][0]) +
                    (p[0][0] - p[2][0]) * (py - p[2][1])) /
                   det;
  const float l2 = 1.0f - l0 - l1;
  if (l0 < 0 || l1 < 0 || l2 < 0) {
    return false;
  }
  for (int c = 0; c < 3; ++c) {
    out[c] = l0 * kColor[0][c] + l1 * kColor[1][c] + l2 * kColor[2][c];
  }
  return true;
}

// Owns the device and everything the triangle scene creates, so a failed REQUIRE still cleans up (the GPU is
// idled first, then the resources are destroyed, then the device).
struct TriangleScene {
    gpu_test::GpuFixture fx;
    VulkanDevicePtr device;
    ShaderHandle vs;
    ShaderHandle fs;
    PipelineHandle pipeline;
    BufferHandle vb;

    TriangleScene() {
        fx.RequireValidation();
        DeviceCreateInfo info;
        info.validation = ValidationSetting::On;
        info.sync_validation = true;
        device = VulkanDevice::Create(info);
        if (device == nullptr) {
            SKIP("no suitable Vulkan device");
        }
    }
    ~TriangleScene() {
        if (device == nullptr) {
            return;
        }
        device->WaitIdle();
        device->DestroyBuffer(vb);
        device->DestroyPipeline(pipeline);
        device->DestroyShader(vs);
        device->DestroyShader(fs);
        device.Reset();
    }
};

// Renders the red-green-blue triangle on a dark gray background into `image`.
bool RenderTriangle(Image* image) {
    TriangleScene scene;
    VulkanDevice* device = scene.device.Get();
    ShaderDesc sd;
    sd.stage = ShaderStage::Vertex;
    sd.words = shaders::kTriangleVertSpv;
    sd.word_count = shaders::kTriangleVertSpvWordCount;
    scene.vs = device->CreateShader(sd);
    sd.stage = ShaderStage::Fragment;
    sd.words = shaders::kTriangleFragSpv;
    sd.word_count = shaders::kTriangleFragSpvWordCount;
    scene.fs = device->CreateShader(sd);

    VertexBindingDesc binding = {20, false};
    VertexAttributeDesc attributes[2] = {{0, 0, VertexFormat::Float2, 0}, {1, 0, VertexFormat::Float3, 8}};
    GraphicsPipelineDesc gp;
    gp.vertex_shader = scene.vs;
    gp.fragment_shader = scene.fs;
    gp.vertex_bindings = rhi::Span<VertexBindingDesc>(&binding, 1);
    gp.vertex_attributes = rhi::Span<VertexAttributeDesc>(attributes, 2);
    gp.cull_mode = CullMode::None;
    gp.color_targets[0].format = Format::RGBA8_UNorm;
    gp.color_target_count = 1;
    scene.pipeline = device->CreateGraphicsPipeline(gp);
    REQUIRE(scene.pipeline.IsValid());

    // The vertices go to a device-local buffer through the staging path.
    float vertices[3 * 5];
    for (int i = 0; i < 3; ++i) {
        vertices[i * 5 + 0] = kClip[i][0];
        vertices[i * 5 + 1] = kClip[i][1];
        vertices[i * 5 + 2] = kColor[i][0];
        vertices[i * 5 + 3] = kColor[i][1];
        vertices[i * 5 + 4] = kColor[i][2];
    }
    BufferDesc bd;
    bd.size = sizeof(vertices);
    bd.usage = BufferUsage::Vertex | BufferUsage::CopyDst;
    bd.location = MemoryLocation::GpuOnly;
    scene.vb = device->CreateBuffer(bd);
    REQUIRE(scene.vb.IsValid());
    REQUIRE(device->UploadBuffer(scene.vb, 0, vertices, sizeof(vertices)).ok);

    gpu_test::RenderTarget target(device, kSize, kSize);
    REQUIRE(target.Valid());
    device->BeginFrame();
    CommandList* list = device->CreateCommandList();
    list->Begin();
    list->Barrier(scene.vb, ResourceState::CopyDst, ResourceState::VertexBuffer);
    list->Barrier(target.Texture(), ResourceState::Undefined, ResourceState::ColorAttachment);
    RenderingDesc pass;
    pass.color_count = 1;
    pass.color[0].texture = target.Texture();
    pass.color[0].clear_color[0] = pass.color[0].clear_color[1] = pass.color[0].clear_color[2] = 0.2f;
    pass.color[0].clear_color[3] = 1.0f;
    list->BeginRendering(pass);
    list->SetPipeline(scene.pipeline);
    list->SetViewport(Viewport{0, 0, static_cast<f32>(kSize), static_cast<f32>(kSize), 0.0f, 1.0f});
    list->SetScissor(ScissorRect{0, 0, kSize, kSize});
    list->SetVertexBuffer(0, scene.vb, 0);
    list->Draw(3, 1, 0, 0);
    list->EndRendering();
    list->End();
    CommandList* lists[] = {list};
    const bool submitted = device->Submit(lists, 1) != 0;
    device->EndFrame();
    REQUIRE(submitted);
    device->WaitIdle();
    return target.Readback(image); // `target` is destroyed before `scene` (reverse order), while the device lives
}

} // namespace

TEST_CASE("Image comparison applies the interior and edge tolerances",
          "[rhi][golden][image]") {
  const Image gray = Solid(16, 16, 100, 100, 100);
  CompareOptions options;

  SECTION("identical images match") {
    CHECK(gpu_test::Compare(gray, gray, options, "unit_same").same);
  }
  SECTION("a difference within the tolerance matches, beyond it fails") {
    CHECK(gpu_test::Compare(gray, Solid(16, 16, 102, 98, 101), options,
                            "unit_within")
              .same);
    const gpu_test::CompareResult r = gpu_test::Compare(
        gray, Solid(16, 16, 103, 100, 100), options, "unit_beyond");
    CHECK_FALSE(r.same);
    CHECK(r.interior_misses == 256);
    CHECK(r.message.find("diff image") != std::string::npos);
    gpu_test::Image diff;
    CHECK(gpu_test::ReadPpm(gpu_test::DiffPath("unit_beyond").c_str(), &diff));
  }
  SECTION("a differing size is reported, not compared") {
    const gpu_test::CompareResult r = gpu_test::Compare(
        gray, Solid(8, 16, 100, 100, 100), options, "unit_size");
    CHECK_FALSE(r.same);
    CHECK(r.message.find("size differs") != std::string::npos);
  }
  SECTION("edge pixels get the wider tolerance") {
    const Image golden =
        Split(16, 16, 0, 200); // the two columns at the seam are edge pixels
    Image actual = golden;
    for (unsigned y = 0; y < 16; ++y) {
      actual.rgba[(static_cast<size_t>(y) * 16 + 7) * 4] =
          40; // seam pixel off by 40: inside edge_tolerance 64
    }
    CHECK(gpu_test::Compare(golden, actual, options, "unit_edge").same);
    for (unsigned y = 0; y < 16; ++y) {
      actual.rgba[(static_cast<size_t>(y) * 16 + 7) * 4] =
          100; // off by 100 on every seam pixel: too many misses
    }
    CHECK_FALSE(
        gpu_test::Compare(golden, actual, options, "unit_edge_bad").same);
    // The same error away from any edge is an interior miss.
    actual = golden;
    actual.rgba[(static_cast<size_t>(8) * 16 + 2) * 4] = 40;
    CHECK_FALSE(
        gpu_test::Compare(golden, actual, options, "unit_edge_interior").same);
  }
}

TEST_CASE("PPM files round-trip and malformed ones are refused",
          "[rhi][golden][image]") {
  const Image img = Split(8, 4, 10, 240);
  REQUIRE(gpu_test::WritePpm(gpu_test::TempPath("rhi_ppm_roundtrip.ppm").c_str(), img));
  Image back;
  REQUIRE(gpu_test::ReadPpm(gpu_test::TempPath("rhi_ppm_roundtrip.ppm").c_str(), &back));
  CHECK(back.width == 8);
  CHECK(back.height == 4);
  CHECK(back.rgba == img.rgba);
  CHECK_FALSE(gpu_test::ReadPpm(gpu_test::TempPath("rhi_no_such_file.ppm").c_str(), &back));
  const char bad[] = "P6\n4 4\n255\nshort";
  REQUIRE(engine_placeholder::io::WriteFile(engine_placeholder::io::Path(engine_placeholder::StringView(gpu_test::TempPath("rhi_ppm_bad.ppm").c_str())), bad, sizeof(bad) - 1).Ok());
  CHECK_FALSE(gpu_test::ReadPpm(gpu_test::TempPath("rhi_ppm_bad.ppm").c_str(), &back));
}

TEST_CASE("The triangle renders offscreen and matches its golden image",
          "[rhi][vulkan][gpu][golden]") {
  Image rendered;
  const bool rendered_ok = RenderTriangle(&rendered); // a SKIP inside must not be nested in an assertion
  REQUIRE(rendered_ok);
  REQUIRE(rendered.width == kSize);

  // Independent of the golden: the background is the clear color, and interior
  // pixels have the color the vertex colors interpolate to at the pixel center.
  // A wrong golden cannot hide a broken pipeline from these.
  const unsigned char *corner = rendered.Pixel(1, 1);
  CHECK(std::abs(corner[0] - 51) <= 1); // 0.2 * 255
  CHECK(std::abs(corner[1] - 51) <= 1);
  CHECK(std::abs(corner[2] - 51) <= 1);
  const u32 probes[][2] = {{32, 14}, {32, 40}, {50, 54}, {14, 54},
                           {32, 30}, {40, 48}, {24, 48}};
  for (const auto &probe : probes) {
    float want[3];
    REQUIRE(ExpectedColor(probe[0], probe[1], want));
    const unsigned char *got = rendered.Pixel(probe[0], probe[1]);
    for (int c = 0; c < 3; ++c) {
      INFO("pixel " << probe[0] << "," << probe[1] << " channel " << c);
      CHECK(std::abs(got[c] - static_cast<int>(want[c] * 255.0f + 0.5f)) <= 3);
    }
  }
  // The pixel just outside the triangle's left edge at mid-height is
  // background, not triangle.
  CHECK(std::abs(rendered.Pixel(6, 40)[0] - 51) <= 1);

  if (gpu_test::UpdatingGoldens()) {
    REQUIRE(gpu_test::WritePpm(gpu_test::GoldenPath("triangle.ppm").c_str(),
                               rendered));
    WARN("ENGINE_RHI_UPDATE_GOLDEN=1: tests/rhi/golden/triangle.ppm was "
         "rewritten; inspect it before committing");
    return;
  }
  Image golden;
  REQUIRE(
      gpu_test::ReadPpm(gpu_test::GoldenPath("triangle.ppm").c_str(), &golden));
  const gpu_test::CompareResult result =
      gpu_test::Compare(golden, rendered, CompareOptions{}, "triangle");
  INFO(result.message);
  CHECK(result.same);
}
