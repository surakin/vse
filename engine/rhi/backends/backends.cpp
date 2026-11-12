#include "rhi/backends/backends.h"

#include "rhi/null/null_device.h"
#if ENGINE_RHI_HAVE_VULKAN
#include "rhi/vulkan/vulkan_backend.h"
#endif
#if ENGINE_RHI_HAVE_DX12
#include "rhi/dx12/dx12_backend.h"
#endif

namespace engine_placeholder::rhi
{

bool RegisterCompiledBackends()
{
    // The table of backends this build has. Order matters only between backends of equal auto rank.
    bool ok = RegisterNullBackend();
#if ENGINE_RHI_HAVE_VULKAN  // the build defines it where the rhi_vulkan library exists (not macOS: Phase 26 decision 8)
    ok = RegisterVulkanBackend() && ok;
#endif
#if ENGINE_RHI_HAVE_DX12  // the build defines it where the rhi_dx12 library exists (Windows)
    ok = RegisterDx12Backend() && ok;
#endif
    return ok;
}

}  // namespace engine_placeholder::rhi
