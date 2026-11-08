#include "rhi/backends/backends.h"

#include "rhi/null/null_device.h"
#include "rhi/vulkan/vulkan_backend.h"

namespace engine_placeholder::rhi {

bool RegisterCompiledBackends() {
    // The table of backends this build has. Order matters only between backends of equal auto rank.
    bool ok = RegisterNullBackend();
    ok = RegisterVulkanBackend() && ok;
    return ok;
}

} // namespace engine_placeholder::rhi
