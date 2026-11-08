// The parts of Dx12Device that later days of Phase 13 write (command lists on Day 168, the swapchain on Day 169). Until then each logs that it is not there yet and fails as the RHI contract
// allows (an invalid handle, null, false). Each day moves its functions out of this file.

#include "rhi/dx12/dx12_device.h"

namespace engine_placeholder::rhi::dx12 {

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
