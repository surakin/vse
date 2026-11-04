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

## Draw a triangle

The clear needs no shaders. A triangle needs two (GLSL in `shaders/`, compiled to SPIR-V and committed as
`*.spv.inc`, see [Shaders](../guide/rhi-shaders.md)), a pipeline that describes the vertex layout and the target
format, and a vertex buffer.

1. Create the two shader modules from `shaders::kTriangleVertSpv` and `shaders::kTriangleFragSpv`.
2. Describe the vertices: one binding of 20 bytes (two floats of position, three of color), attributes at offsets 0
   and 8. Create the pipeline with the target's format (`swapchain->ImageFormat()`, or the offscreen format).
3. Create a `GpuOnly` vertex buffer with `BufferUsage::Vertex | CopyDst` and fill it with `UploadBuffer`; the buffer
   is left in `CopyDst`, so the first command list moves it to `VertexBuffer` with a barrier (outside a pass).
4. In the pass: `SetPipeline`, `SetViewport`, `SetScissor`, `SetVertexBuffer`, `Draw(3, 1, 0, 0)`.

The same steps run on the null device, which checks the draw against the pipeline and the pass:

```cpp compile standalone run
#include <cstdio>

#include "rhi/null/null_device.h"
#include "triangle.frag.spv.inc"
#include "triangle.vert.spv.inc"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;

int main() {
    NullDevice* device = CreateNullDevice();

    ShaderDesc sd;
    sd.stage = ShaderStage::Vertex;
    sd.words = shaders::kTriangleVertSpv;
    sd.word_count = shaders::kTriangleVertSpvWordCount;
    const ShaderHandle vs = device->CreateShader(sd);
    sd.stage = ShaderStage::Fragment;
    sd.words = shaders::kTriangleFragSpv;
    sd.word_count = shaders::kTriangleFragSpvWordCount;
    const ShaderHandle fs = device->CreateShader(sd);

    VertexBindingDesc binding = {20, false};
    VertexAttributeDesc attributes[2] = {{0, 0, VertexFormat::Float2, 0}, {1, 0, VertexFormat::Float3, 8}};
    GraphicsPipelineDesc gp;
    gp.vertex_shader = vs;
    gp.fragment_shader = fs;
    gp.vertex_bindings = Span<VertexBindingDesc>(&binding, 1);
    gp.vertex_attributes = Span<VertexAttributeDesc>(attributes, 2);
    gp.cull_mode = CullMode::None;
    gp.color_targets[0].format = Format::RGBA8_UNorm;
    gp.color_target_count = 1;
    const PipelineHandle pipeline = device->CreateGraphicsPipeline(gp);

    const f32 vertices[3 * 5] = {
        0.0f,  -0.8f, 1.0f, 0.0f, 0.0f, // top, red
        0.8f,  0.8f,  0.0f, 1.0f, 0.0f, // bottom right, green
        -0.8f, 0.8f,  0.0f, 0.0f, 1.0f, // bottom left, blue
    };
    BufferDesc bd;
    bd.size = sizeof(vertices);
    bd.usage = BufferUsage::Vertex | BufferUsage::CopyDst;
    bd.location = MemoryLocation::GpuOnly;
    const BufferHandle vb = device->CreateBuffer(bd);
    device->UploadBuffer(vb, 0, vertices, sizeof(vertices));

    TextureDesc td;
    td.width = 64;
    td.height = 64;
    td.format = Format::RGBA8_UNorm;
    td.usage = TextureUsage::ColorAttachment | TextureUsage::CopySrc;
    const TextureHandle target = device->CreateTexture(td);

    device->BeginFrame();
    CommandList* list = device->CreateCommandList();
    list->Begin();
    list->Barrier(vb, ResourceState::CopyDst, ResourceState::VertexBuffer);
    list->Barrier(target, ResourceState::Undefined, ResourceState::ColorAttachment);
    RenderingDesc pass;
    pass.color_count = 1;
    pass.color[0].texture = target;
    list->BeginRendering(pass);
    list->SetPipeline(pipeline);
    list->SetViewport(Viewport{0, 0, 64, 64, 0.0f, 1.0f});
    list->SetScissor(ScissorRect{0, 0, 64, 64});
    list->SetVertexBuffer(0, vb, 0);
    list->Draw(3, 1, 0, 0);
    list->EndRendering();
    list->End();
    device->Submit(&list, 1);
    device->EndFrame();

    device->WaitIdle();
    std::printf("draws: ok, errors: %u\n", device->ErrorCount());
    DestroyDevice(device);
}
```

```output
draws: ok, errors: 0
```

`rhi_demo` started with this triangle and now draws a textured, lit, depth-tested cube the same way (see
[The RHI guide](../guide/rhi.md#depth-indexed-draws-and-the-3d-conventions)); `rhi_demo --screenshot out.ppm [--frames N]` renders its scene offscreen at
256x256 without a window and writes a PPM, which is how the golden-image test in
[Building and Testing](../guide/building-and-testing.md#golden-image-rendering-tests) is exercised by hand.

## Add a texture

A texture needs its pixels on the GPU, optionally a chain of smaller copies (mips) so that it looks right when
drawn small, and a sampler that says how to read it.

1. Create the texture with `Sampled | CopyDst | CopySrc` usage and a full mip chain (`MaxMipLevels`).
2. `UploadTexture` level 0. The texture is then in `ShaderRead`.
3. Record `GenerateMips` in a command list and submit it (check `CanGenerateMips` first).
4. Create a sampler (trilinear, anisotropy 16) and put the texture and sampler in a bind group together with the
   uniform buffer the shader reads; the fragment shader samples with `texture(sampler2D(tex, smp), uv)`.

The null device accepts the same calls and checks them:

```cpp compile standalone run
#include <cstdio>
#include <vector>

#include "rhi/null/null_device.h"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;

int main() {
    NullDevice* device = CreateNullDevice();

    // An 8x8 checkerboard, 4 bytes per texel.
    std::vector<unsigned char> pixels(8 * 8 * 4);
    for (u32 y = 0; y < 8; ++y) {
        for (u32 x = 0; x < 8; ++x) {
            const unsigned char v = ((x + y) & 1) != 0 ? 255 : 0;
            unsigned char* p = &pixels[(y * 8 + x) * 4];
            p[0] = p[1] = p[2] = v;
            p[3] = 255;
        }
    }

    TextureDesc td;
    td.width = 8;
    td.height = 8;
    td.format = Format::RGBA8_SRGB;
    td.mip_levels = MaxMipLevels(8, 8);
    td.usage = TextureUsage::Sampled | TextureUsage::CopyDst | TextureUsage::CopySrc;
    const TextureHandle texture = device->CreateTexture(td);
    device->UploadTexture(texture, 0, 0, pixels.data(), 0);

    device->BeginFrame();
    CommandList* list = device->CreateCommandList();
    list->Begin();
    list->GenerateMips(texture);
    list->End();
    device->Submit(&list, 1);
    device->EndFrame();

    SamplerDesc sd;
    sd.max_anisotropy = 16.0f;
    const SamplerHandle sampler = device->CreateSampler(sd);

    BindingDesc bindings[2] = {{1, BindingType::SampledTexture, ShaderStage::Fragment, 1}, {2, BindingType::Sampler, ShaderStage::Fragment, 1}};
    BindGroupLayoutDesc ld;
    ld.entries = Span<BindingDesc>(bindings, 2);
    const BindGroupLayoutHandle layout = device->CreateBindGroupLayout(ld);
    BindGroupEntry entries[2];
    entries[0].binding = 1;
    entries[0].texture = texture;
    entries[1].binding = 2;
    entries[1].sampler = sampler;
    BindGroupDesc gd;
    gd.layout = layout;
    gd.entries = Span<BindGroupEntry>(entries, 2);
    const BindGroupHandle group = device->CreateBindGroup(gd);

    device->WaitIdle();
    std::printf("mips: %u, group: %s, errors: %u\n", td.mip_levels, group.IsValid() ? "ok" : "invalid", device->ErrorCount());
    DestroyDevice(device);
}
```

```output
mips: 4, group: ok, errors: 0
```

`rhi_demo` does this with a checkerboard stored in `game/rhi_demo/assets/checker.rtex` (all mip levels in the file;
`game/common/textures.h` reads it) and draws it on the cube. `rhi_demo --screenshot out.ppm` writes the scene
offscreen; the offscreen target is linear, so it looks darker than the window.

## Light it

The last step is shading. The vertex shader sends the normal (turned by the normal matrix) and the uv on; the
fragment shader computes, in linear light, `albedo * (ambient + light_color * max(dot(N, L), 0))` for one directional
light. Both shaders read the same uniform block (model-view-projection, model, normal matrix, the light), which the
application rewrites every frame in a `UniformRing` slice and binds through a transient bind group - so moving the
light is just writing different numbers. `rhi_demo` orbits the light around the cube and the camera with the arrow
keys; `shaders/lit.frag` has the math in its header, and `tests/rhi/vulkan_lit_cube_test.cpp` checks it against pixels.
Run it: `./build/game/rhi_demo/rhi_demo --validation`; see `game/rhi_demo/README.md` for the options.

## Where next

- [The RHI guide](../guide/rhi.md): the frame loop, resource states and the swapchain's resize and minimize rules.
- [rhi_demo](../../game/rhi_demo/README.md): the finished demo and its options.
- [Open a Window](hello-window.md): the platform layer under the swapchain.
