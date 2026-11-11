#include <catch2/catch_test_macros.hpp>

#include "fill.comp.spv.inc"
#include "gpu_fixture.h"
#include "rhi/rhi.h"
#include "rhi/rhi_log.h"
#include "rhi/vulkan/vulkan_device.h"
#include "triangle.vert.spv.inc"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;
using namespace engine_placeholder::rhi::vulkan;

namespace
{

struct VulkanComputeTestQuietLog
{
    bool saved = LogRhi.Enabled();
    VulkanComputeTestQuietLog() { LogRhi.SetEnabled(false); }
    ~VulkanComputeTestQuietLog() { LogRhi.SetEnabled(saved); }
};

struct ComputeDevice
{
    gpu_test::GpuFixture fx;
    VulkanDevicePtr device;

    ComputeDevice()
    {
        fx.RequireValidation();
        DeviceCreateInfo info;
        info.validation = ValidationSetting::On;
        info.sync_validation = true;
        info.pipeline_cache = false;
        device = VulkanDevice::Create(info);  // no window: compute and offscreen only
        if (device == nullptr)
        {
            SKIP("no suitable Vulkan device");
        }
    }
    ~ComputeDevice()
    {
        if (device != nullptr)
        {
            device->WaitIdle();
            device.Reset();
        }
    }

    ShaderHandle Shader(const u32* words, usize count)
    {
        ShaderDesc d;
        d.stage = ShaderStage::Compute;
        d.SetCode(SpirvBlob(words, count));
        return device->CreateShader(d);
    }

    BindGroupLayoutHandle StorageLayout(u32 buffers)
    {
        BindingDesc entries[2] = {{0, BindingType::StorageBuffer, ShaderStage::Compute, 1},
                                  {1, BindingType::StorageBuffer, ShaderStage::Compute, 1}};
        BindGroupLayoutDesc ld;
        ld.entries = rhi::Span<BindingDesc>(entries, buffers);
        return device->CreateBindGroupLayout(ld);
    }
};

}  // namespace

TEST_CASE("Compute pipeline creation refuses bad input", "[rhi][vulkan][gpu][compute]")
{
    ComputeDevice t;
    t.fx.ExpectErrors(0);
    VulkanComputeTestQuietLog quiet;
    const ShaderHandle cs = t.Shader(shaders::kFillCompSpv, shaders::kFillCompSpvWordCount);
    const BindGroupLayoutHandle one = t.StorageLayout(1);

    ComputePipelineDesc cp;
    CHECK_FALSE(t.device->CreateComputePipeline(cp).IsValid());  // no shader

    ShaderDesc vd;
    vd.stage = ShaderStage::Vertex;  // a compute module declared as a vertex shader is refused by the module check
    vd.SetCode(SpirvBlob(shaders::kFillCompSpv, shaders::kFillCompSpvWordCount));
    CHECK_FALSE(t.device->CreateShader(vd).IsValid());

    // A shader of another stage is not a compute shader.
    ShaderDesc vertex;
    vertex.stage = ShaderStage::Vertex;
    vertex.SetCode(SpirvBlob(shaders::kTriangleVertSpv, shaders::kTriangleVertSpvWordCount));
    const ShaderHandle vs = t.device->CreateShader(vertex);
    REQUIRE(vs.IsValid());
    cp.shader = vs;
    CHECK_FALSE(t.device->CreateComputePipeline(cp).IsValid());
    t.device->DestroyShader(vs);

    cp.shader = cs;
    cp.bind_group_layouts[0] = one;
    cp.bind_group_layout_count = 1;
    cp.push_constant_bytes = 6;  // not a multiple of 4
    CHECK_FALSE(t.device->CreateComputePipeline(cp).IsValid());
    cp.push_constant_bytes = 4;

    t.device->DestroyBindGroupLayout(one);
    t.device->WaitIdle();
    t.device->BeginFrame();
    t.device->EndFrame();
    t.device->WaitIdle();
    CHECK_FALSE(t.device->CreateComputePipeline(cp).IsValid());  // the layout is gone
    t.device->DestroyShader(cs);
}
