// A planted Metal validation error (Phase 26 Day 316): commits a command buffer whose blit encoder was never ended,
// which Metal's API validation (MTL_DEBUG_LAYER=1 with MTL_DEBUG_LAYER_ERROR_MODE=assert) stops with a failed
// assertion. The ctest metal_validation_planted expects that assertion: it shows a validation error fails a GPU test
// run with the same environment. Without validation the program reports so and exits 3, which the ctest does not accept
// either.
#include "os/console.h"
#include "os/os.h"
#include "rhi/backends/backends.h"
#include "rhi/metal/metal_device.h"
#include "rhi/rhi.h"

using namespace engine_placeholder;

int main()
{
    rhi::RegisterCompiledBackends();
    rhi::DeviceCreateInfo info;
    info.backend = rhi::Backend::Metal;
    info.application_name = "metal_validation_planted";
    rhi::DevicePtr device = rhi::CreateDevice(info);
    if (!device)
    {
        os::PrintError("metal_validation_planted: no Metal device\n");
        return 2;
    }
    rhi::metal::MetalDevice& metal = static_cast<rhi::metal::MetalDevice&>(*device);
    if (!metal.ValidationEnabled())
    {
        os::PrintError("metal_validation_planted: Metal's validation is off (MTL_DEBUG_LAYER is not set)\n");
        return 3;
    }
    @autoreleasepool
    {
        id<MTLCommandBuffer> buffer = [metal.Queue() commandBuffer];
        id<MTLBlitCommandEncoder> blit = [buffer blitCommandEncoder];
        os::Print("metal_validation_planted: committing with encoder %p still open\n", (__bridge void*)blit);
        [buffer commit];  // the planted error: the encoder was never ended
        [buffer waitUntilCompleted];
    }
    os::Print("metal_validation_planted: the planted call was not caught\n");
    return 0;
}
