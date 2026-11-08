// The parts of Dx12Device that later days of Phase 13 write (resources on Day 165, descriptors on Day 166, pipelines on Day 167,
// command lists on Day 168, the swapchain on Day 169). Until then each logs that it is not there yet and fails as the RHI contract
// allows (an invalid handle, null, false). Each day moves its functions out of this file.

#include "rhi/dx12/dx12_device.h"

namespace engine_placeholder::rhi::dx12 {

BufferHandle Dx12Device::CreateBuffer(const BufferDesc&) {
    NotYet("CreateBuffer", 165);
    return {};
}
void Dx12Device::DestroyBuffer(BufferHandle) {}
TextureHandle Dx12Device::CreateTexture(const TextureDesc&) {
    NotYet("CreateTexture", 165);
    return {};
}
void Dx12Device::DestroyTexture(TextureHandle) {}
void* Dx12Device::MapBuffer(BufferHandle) {
    NotYet("MapBuffer", 165);
    return nullptr;
}
void Dx12Device::UnmapBuffer(BufferHandle) {}
UploadTicket Dx12Device::UploadBuffer(BufferHandle, u64, const void*, u64) {
    NotYet("UploadBuffer", 165);
    return UploadTicket{0, false};
}
bool Dx12Device::ReadbackBuffer(BufferHandle, u64, void*, u64) {
    NotYet("ReadbackBuffer", 165);
    return false;
}
UploadTicket Dx12Device::UploadTexture(TextureHandle, u32, u32, const void*, u32) {
    NotYet("UploadTexture", 165);
    return UploadTicket{0, false};
}
bool Dx12Device::ReadbackTexture(TextureHandle, u32, u32, void*, u64, ResourceState) {
    NotYet("ReadbackTexture", 165);
    return false;
}
bool Dx12Device::FormatSupport(Format, TextureUsage) const { return false; }
bool Dx12Device::CanGenerateMips(Format) const { return false; }
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
