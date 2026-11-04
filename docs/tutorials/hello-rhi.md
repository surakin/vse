# Tutorial: Clear a Window with the RHI

**Goal:** Run a frame loop that clears the window to an animated color, first on the null device (no GPU) and then
on a real one with the `rhi_demo` app.

**You need:** a built tree (see getting-started). A GPU and a display are needed only for step 5.

## Steps

1. Create a device. `CreateNullDevice()` needs nothing; the Vulkan backend's `VulkanDevice::Create` takes a
   `DeviceCreateInfo` whose `window` is the window to present to.
2. Create a swapchain with `device->CreateSwapchain(SwapchainDesc{})`.
3. Each frame: `BeginFrame`, `Acquire`, record a command list, `Submit`, `EndFrame`, `Present`.
4. Record the clear. The image starts in no particular state, so move it to `ColorAttachment`, run a pass whose
   color attachment has `LoadOp::Clear`, and move it to `Present`.
5. Run it for real: `ENGINE_WINDOW_BACKEND=wayland ./build/game/rhi_demo/rhi_demo` (or `x11`). Set
   `RHI_DEMO_FRAMES=300` to stop after 300 frames, and `ENGINE_RHI_VALIDATION=1` to turn on the validation layer.
6. Shut down in order: `WaitIdle`, `DestroySwapchain`, then the device.

## Complete code

The loop below runs on the null device, which checks every rule the Vulkan backend relies on and counts what it
catches, so it also shows what a mistake looks like. The Vulkan version is [main.cpp](../../game/rhi_demo/main.cpp);
only the device creation and window handling differ.

```cpp compile standalone run
#include <cstdio>

#include "rhi/null/null_device.h"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;

int main() {
    NullDevice* device = CreateNullDevice();
    Swapchain* swapchain = device->CreateSwapchain(SwapchainDesc{});

    for (int frame = 0; frame < 6; ++frame) {
        device->BeginFrame();
        if (swapchain->Acquire() != SwapchainStatus::Ok) {
            device->EndFrame();
            continue;
        }
        const TextureHandle image = swapchain->CurrentTexture();

        CommandList* list = device->CreateCommandList();
        list->Begin();
        list->Barrier(image, ResourceState::Undefined, ResourceState::ColorAttachment);

        RenderingDesc pass;
        pass.color_count = 1;
        pass.color[0].texture = image;
        pass.color[0].load = LoadOp::Clear;
        pass.color[0].clear_color[0] = static_cast<f32>(frame) / 6.0f;  // red ramps up
        list->BeginRendering(pass);
        list->EndRendering();

        list->Barrier(image, ResourceState::ColorAttachment, ResourceState::Present);
        list->End();
        device->Submit(&list, 1);
        device->EndFrame();
        swapchain->Present();
    }

    device->WaitIdle();
    device->DestroySwapchain(swapchain);
    std::printf("frames: %llu, errors: %u\n", static_cast<unsigned long long>(device->FrameNumber()), device->ErrorCount());
    DestroyDevice(device);
}
```

## Expected output

```output
frames: 6, errors: 0
```

`rhi_demo` logs its average frame time once a second, for example `rhi_demo: 59 frames, 17.0 ms/frame` on a 60 Hz
display. It leaves the loop when you close the window.

## Where next

- [The RHI guide](../guide/rhi.md): the frame loop, resource states and the swapchain's resize and minimize rules.
- [Open a Window](hello-window.md): the platform layer under the swapchain.
