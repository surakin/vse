// The parts of Dx12Device that later days of Phase 13 write (descriptors on Day 166, pipelines on Day 167,
// command lists on Day 168, the swapchain on Day 169). Until then each logs that it is not there yet and fails as the RHI contract
// allows (an invalid handle, null, false). Each day moves its functions out of this file.

#include "rhi/dx12/dx12_device.h"

namespace engine_placeholder::rhi::dx12 {

SamplerHandle Dx12Device::CreateSampler(const SamplerDesc&) {
    NotYet("CreateSampler", 166);
    return {};
}
void Dx12Device::DestroySampler(SamplerHandle) {}
BindGroupLayoutHandle Dx12Device::CreateBindGroupLayout(const BindGroupLayoutDesc&) {
    NotYet("CreateBindGroupLayout", 166);
    return {};
}
void Dx12Device::DestroyBindGroupLayout(BindGroupLayoutHandle) {}
BindGroupHandle Dx12Device::CreateBindGroup(const BindGroupDesc&) {
    NotYet("CreateBindGroup", 166);
    return {};
}
void Dx12Device::DestroyBindGroup(BindGroupHandle) {}
ShaderHandle Dx12Device::CreateShader(const ShaderDesc&) {
    NotYet("CreateShader", 167);
    return {};
}
void Dx12Device::DestroyShader(ShaderHandle) {}
PipelineHandle Dx12Device::CreateGraphicsPipeline(const GraphicsPipelineDesc&) {
    NotYet("CreateGraphicsPipeline", 167);
    return {};
}
PipelineHandle Dx12Device::CreateComputePipeline(const ComputePipelineDesc&) {
    NotYet("CreateComputePipeline", 167);
    return {};
}
void Dx12Device::DestroyPipeline(PipelineHandle) {}
CommandList* Dx12Device::CreateCommandList() {
    NotYet("CreateCommandList", 168);
    return nullptr;
}
Swapchain* Dx12Device::CreateSwapchain(const SwapchainDesc&) {
    NotYet("CreateSwapchain", 169);
    return nullptr;
}
void Dx12Device::DestroySwapchain(Swapchain*) {}
bool Dx12Device::RecreateSurface(const os::Window*) {
    NotYet("RecreateSurface", 169);
    return false;
}

} // namespace engine_placeholder::rhi::dx12
