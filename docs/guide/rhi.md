# The Render Hardware Interface (RHI)

Status: written.

**Audience:** engine and renderer programmers. Read [Standard types](foundation-types.md) and [Containers](foundation-containers.md) first. If you only want to
draw things with materials, start with the [renderer guide](renderer.md): the renderer sits on top of the RHI and hides most of this page. You come here
when you write a renderer pass, a tool or a test that talks to the GPU directly.

The GPU is a second computer with its own memory, and you talk to it through a driver API (Vulkan, Direct3D 12, Metal) that is long, explicit and
different on each platform. The **RHI** (Render Hardware Interface) is the engine's one small interface over those APIs: you create buffers, textures and
pipelines, record drawing commands into a list, submit the list, and show the result in a window. Code above the RHI never names Vulkan or Direct3D, so the
same code runs on every backend, and the `null` backend runs it with no GPU at all, which is how most tests work.

## At a glance

| I need | Use | Notes |
|---|---|---|
| a device (the GPU connection) | `rhi::CreateDevice(DeviceCreateInfo)` | call `rhi::RegisterCompiledBackends()` once first; `Backend::Null` needs no GPU |
| a buffer or texture | `device->CreateBuffer` / `CreateTexture` | returns a handle; an invalid handle means failure (see the log) |
| to fill a GPU-only buffer or texture | `UploadBuffer` / `UploadTexture` | then `Barrier` to the state the first use needs |
| to draw or copy | `device->CreateCommandList()`, record, `Submit` | one list per thread; valid for one frame |
| to order GPU work | `list->Barrier(resource, before, after)` | you state the states; the RHI does not track them for you |
| a window to draw into | `device->CreateSwapchain`, then `Acquire` / `Present` | handle `Recreated` and `Minimized` |
| shaders | [Shaders](rhi-shaders.md), `CreateShader` | compiled offline; the RHI takes bytecode |
| to rebuild everything on a setting change | `rhi::DeviceHost` and `DeviceObserver` | see "Lifecycle and reset" |

## Where to start reading

This page is long because it is also the reference for the backends. You do not need all of it.

1. **Read first, in order:** Overview, Words you will meet, Handles and descriptors, Devices, command lists and lifetimes, Rules & contracts, Common mistakes.
2. **Then, to draw something:** Backends and the registry, Creating a device, Uploading data, Recording and submitting, Presenting to a window, The frame loop,
   Pipelines, Binding resources. The [hello-rhi tutorial](../tutorials/hello-rhi.md) walks through exactly these.
3. **When you need it:** Textures and samplers, Depth and indexed draws, Textures in draws, Compute, Choosing a GPU, Presentation, Lifecycle and reset.
4. **Contributors and curious readers only:** GPU memory, Vulkan backend notes, Validation layers (worth reading if you debug), Vulkan on Windows, The DX12 backend, The Metal backend,
   Performance notes.

## Source of truth

- `engine/rhi/*.h`
- `engine/rhi/vulkan/`, `engine/rhi/dx12/` and `engine/rhi/metal/` (backend internals)
- `docs/superpowers/specs/2026-11-04-rhi-design.md` (design decisions D1-D8)

## API reference

Full reference: [The Render Hardware Interface](../reference/rhi.md).

- [[rhi::LogRhi]] — the RHI log channel
- [[rhi::Handle]] — the generation-checked handle every GPU resource is referred to by
- [[rhi::Format]] and [[rhi::FormatInfo]] — pixel formats and their layout table
- [[rhi::BufferDesc]], [[rhi::TextureDesc]], [[rhi::SamplerDesc]], [[rhi::ShaderDesc]],
  [[rhi::BindGroupLayoutDesc]], [[rhi::GraphicsPipelineDesc]], [[rhi::ComputePipelineDesc]] — creation descriptors
- [[rhi::DeviceCaps]] — device limits
- [[rhi::Device]] — resource creation, mapping, submission and frames
- [[rhi::CommandList]] — recording commands
- [[rhi::Swapchain]] — the presentable images of a window
- [[rhi::HandlePool]] — slot storage with generation checks and deferred release
- [[rhi::NullDevice]] and [[rhi::CreateNullDevice]] — the validating no-GPU backend
- [[rhi::CreateDevice]] and [[rhi::DeviceCreateInfo]] — creating a device for a backend
- [[rhi::ValidateSpirv]] — the shader module sanity check; see [Shaders](rhi-shaders.md)
- [[rhi::UniformRing]] — per-frame uniform data
- [[rhi::RegisterMetalBackend]] — the Metal backend's registration (macOS)
- [[rhi::vulkan::VulkanDevice]] — the Vulkan device
- [[rhi::vulkan::SelectPhysicalDevice]] and [[rhi::vulkan::PlanQueues]] — GPU choice and queue planning
- [[rhi::vulkan::MemoryAllocator]] — the GPU memory suballocator, with [[rhi::vulkan::MemoryStats]]
- [[rhi::Device::UploadBuffer]], [[rhi::UploadTicket]] and [[rhi::Device::ReadbackBuffer]] — moving bytes to and from buffers
- [[rhi::RingAllocator]] — the staging ring behind uploads
- [[rhi::vulkan::LoadVulkanLoader]] — opens the Vulkan loader at runtime
- [[rhi::vulkan::VkGlobalFns]], [[rhi::vulkan::VkInstanceFns]], [[rhi::vulkan::VkDeviceFns]] — the function-pointer tables
- [[rhi::vulkan::VulkanInstance]] and [[rhi::vulkan::VulkanInstanceDesc]] — the instance, its extensions and validation
- [[rhi::vulkan::GetValidationCounts]] and [[rhi::vulkan::SetValidationCapture]] — validation message counters and capture hook
- [[rhi::vulkan::VkResultName]], [[rhi::vulkan::ClassifyVkResult]] — readable and classified `VkResult`s

## Guide

### Overview

The RHI is the only way the engine talks to the GPU. Code above it - the renderer, the demo, tools - holds
`Device`, `CommandList` and `Swapchain` objects and opaque, generation-checked handles; it never includes Vulkan
(Vulkan, Direct3D 12 and Metal are the backends built today). Where to read next:

| To do this | Read |
|---|---|
| make a device, resources, upload data | Creating a device, Uploading data, Textures and samplers |
| draw into a window | Presenting to a window, The frame loop |
| write and load shaders | [Shaders](rhi-shaders.md), Pipelines |
| give shaders buffers, textures, sampler | Binding resources, Textures in draws |
| 3D: depth, indices, projection | Depth, indexed draws and the 3D conventions |
| compute | Compute |
| see it all working | the [hello-rhi tutorial](../tutorials/hello-rhi.md) and `game/rhi_demo/README.md` |

The demo (`game/rhi_demo`) is the reference client: a textured, lit, depth-tested cube with an orbit camera, written
only against `rhi/*.h` plus the one backend header needed to create the device. Everything it builds on - the
projection, the cube mesh, the textures, the `UniformRing` - is renderer-side code, not part of the RHI.

### Words you will meet

| Word | Meaning |
|---|---|
| **backend** | the code that implements the RHI on one API: `Vulkan` (Linux, Windows), `Dx12` (Windows), `Metal` (macOS), `Null` (checks everything, draws nothing) |
| **handle** | an opaque eight-byte id for a GPU object; the all-zero value is invalid (next section) |
| **descriptor struct** | a plain struct (`BufferDesc`, `TextureDesc`, ...) that says what to create; not to be confused with a shader "descriptor" below |
| **command list** | a recording of GPU work (copies, draws, dispatches); nothing runs until you `Submit` it |
| **barrier** | a command that tells the GPU "this resource was used one way and will now be used another way", so it finishes the earlier work and changes the memory layout first |
| **resource state** | the way a resource is being used right now (`CopyDst`, `ShaderRead`, `ColorAttachment`, `Present`, ...); you name it in every barrier |
| **swapchain** | the queue of images that belong to a window; you draw into one, then present it to the screen |
| **frame in flight** | a frame the CPU has handed over that the GPU has not finished; the CPU may run one to three frames ahead |
| **fence** | a counter that grows when the GPU finishes a submission; waiting on a value waits for that work |
| **shader** | a program that runs on the GPU (vertex, fragment or compute); see [Shaders](rhi-shaders.md) |
| **pipeline** | (often "PSO", pipeline state object) the shaders plus all fixed state of a draw (vertex layout, blending, depth test, target formats), created once and bound per draw |
| **bind group** | a set of buffers, textures and samplers handed to shaders together (a Vulkan descriptor set, a Direct3D descriptor table); its **layout** says the shape |
| **push constants** | a few bytes sent with each draw, the cheapest way to pass small per-draw values |
| **pass (rendering)** | the span between `BeginRendering` and `EndRendering`, in which draws write into the attachments (color and depth textures) you name |
| **compute** | general GPU work with no drawing: a compute shader run over a grid of work groups with `Dispatch` |

### Handles and descriptors

Every GPU resource is referred to by an opaque handle: eight bytes, an index and a generation.
The all-zero value is the invalid handle. The device bumps a slot's generation when the resource is
destroyed, so an old copy of a handle compares unequal to the current one and the device rejects it
instead of touching freed memory. Each resource kind has its own handle type
([[rhi::BufferHandle]], [[rhi::TextureHandle]], ...), so passing a texture where a buffer is expected
does not compile.

Resources are created from plain descriptor structs with sensible defaults. Each descriptor has a
`Validate()` that checks it without a device and returns null for a good descriptor or a static string
starting with the descriptor name, for example `"TextureDesc: mip_levels exceeds the full chain for the
extent"`. The device calls it on every creation; call it yourself to report a problem early. Variable-
length parts of a descriptor (bindings, vertex attributes, shader words) are passed as a
[[rhi::Span]] that must stay valid only for the duration of the creation call.

Usage and stage sets are scoped `u32` enums with `|`, `&`, `Has` and `Any`; there are no raw integer
masks:

```cpp compile standalone run
#include <cstdio>

#include "rhi/format.h"
#include "rhi/handle.h"
#include "rhi/enums.h"

using namespace vse::rhi;

int main() {
BufferUsage usage = BufferUsage::Vertex | BufferUsage::CopyDst;
std::printf("vertex=%d index=%d\n", Has(usage, BufferUsage::Vertex), Has(usage, BufferUsage::Index));

BufferHandle none;
std::printf("default handle valid=%d\n", none.IsValid());

// A 5x5 BC1 image needs two 4x4 blocks per row and two block rows.
std::printf("bc1 5x5 = %llu bytes, rgba8 64 wide = %llu bytes per row\n",
            static_cast<unsigned long long>(FormatSliceBytes(Format::BC1_UNorm, 5, 5)),
            static_cast<unsigned long long>(FormatRowBytes(Format::RGBA8_UNorm, 64)));
}
```

```output
vertex=1 index=0
default handle valid=0
bc1 5x5 = 32 bytes, rgba8 64 wide = 256 bytes per row
```

### Devices, command lists and lifetimes

A [[rhi::Device]] creates every resource and returns handles. A creation function that cannot succeed
(a bad descriptor, an unsupported format, out of memory) logs the reason on `LogRhi` and returns the
invalid handle; it never aborts. `Destroy*` makes the handle invalid at once, but the memory is released
only when the GPU has finished the frames that could have used it.

Work is recorded into a [[rhi::CommandList]] obtained from `Device::CreateCommandList()`. Lists can be
recorded on different threads at the same time; one list belongs to one thread. A list is valid until the
next `Device::BeginFrame()`, and is submitted with `Device::Submit()` after `End()`.

Resource states are explicit. A texture starts in `ResourceState::Undefined`; before using it the way a
command needs, record `Barrier(texture, before, after)`. The same applies to `GpuOnly` buffers.
`CpuToGpu` and `GpuToCpu` buffers have a fixed state and take no barriers.

The null backend checks all of this without a GPU. It logs and counts each violation instead of
aborting, so a test can assert that a bad sequence is caught:

```cpp compile standalone run
#include <cstdio>

#include "rhi/null/null_device.h"
#include "rhi/rhi_log.h"

using namespace vse;
using namespace vse::rhi;

int main() {
    LogRhi.SetEnabled(false);  // keep the expected error out of the output
    NullDevicePtr device = CreateNullDevice();
    device->BeginFrame();

    BufferDesc desc;
    desc.size = 64;
    desc.usage = BufferUsage::CopySrc | BufferUsage::CopyDst;
    BufferHandle a = device->CreateBuffer(desc);
    BufferHandle b = device->CreateBuffer(desc);

    CommandList* list = device->CreateCommandList();
    list->Begin();
    list->Barrier(a, ResourceState::Undefined, ResourceState::CopySrc);
    list->CopyBuffer(a, b, 0, 0, 64);  // b was never moved to CopyDst
    list->End();
    device->Submit(&list, 1);

    std::printf("errors: %u\n", device->ErrorCount());
    device.Reset();
}
```

```output
errors: 1
```

### Rules & contracts

**The Vulkan boundary.** Nothing outside `engine/rhi/vulkan/` may name the Vulkan API: no `vk*`, `Vk*`, `VK_*` or
`PFN_vk*` identifier and no `vulkan` include. Gameplay, renderer and tool code create a device with
[[rhi::CreateDevice]] and talk to [[rhi::Device]] and [[rhi::CommandList]] only. The reverse holds too: the
backend-agnostic headers directly under `engine/rhi/` never include a Vulkan, Wayland, X11 or backend-directory
header. Both rules are checked by the `rhi_boundary_check` ctest (see "Building and Testing"); the one allowed
exception is the GPU driver probe, listed with its reason in `tools/rhi_boundary_check/allowlist.txt`.

**Programming errors abort; bad descriptions and the environment fail softly.** The RHI sorts every problem into one of
three kinds, and the headers say which with `Aborts:` and `Fails:` labels.

| Kind | Examples | What happens |
| --- | --- | --- |
| Programming error — a call the contract forbids | a destroyed or invalid handle passed to `DestroyBuffer`, `MapBuffer`, `UploadBuffer`, `Submit`; a range outside the buffer; `Submit` outside a frame; a list submitted twice; `Acquire` twice without `Present` | `ENGINE_ASSERT` aborts. The null backend counts it instead (`ErrorCount()`), so tests can provoke it. |
| Bad description — data that may come from an asset | an unsupported format or usage, a size beyond the device limits, a shader of the wrong stage, a layout that does not match its bind group | The `Create*` call logs the reason on `LogRhi` and returns an invalid handle. |
| Environment — the machine, not the code | no Vulkan loader or GPU, out of GPU memory, out of handles, a full staging ring, a lost device, a lost swapchain | Logged; the call returns an invalid handle, `false`, `0` or a `SwapchainStatus`. The application decides. |

The rule of thumb: operations *on an existing handle* assert, because a bad handle can only be a bug; operations that
*create* something return an invalid handle, because the description or the machine may be at fault and the caller
can recover. After a lost device, submissions are ignored, `Device::DeviceLost` reports it and destroying resources
and the device still works; a lost device is also recoverable: `DeviceHost::Recreate` replaces it (see "Lifecycle and reset", and the
renderer guide's "Device loss and recovery" for the automatic retry policy).

**Failure injection.** [[rhi::FaultInjection]] in [[rhi::DeviceCreateInfo]] makes a GPU backend (Vulkan, Direct3D 12, Metal) fail on purpose, so
the paths above are exercised without a broken machine: `lose_device_at_frame` reports the device lost at a chosen
frame, and `fail_allocations_from` makes the Nth device-memory allocation and every later one fail as if the GPU were
out of memory. `rhi_demo` exposes them as `--inject-device-loss FRAME` and `--inject-oom N`; a lost device ends the
demo with exit code 3 and a message, a failed allocation with exit code 1.

### Common mistakes

- **Using a handle after `Destroy*` and expecting the old object.** The handle is invalid the moment `Destroy*` returns,
  although the memory lives until the GPU is done with it. Asserts catch a second `Destroy`; a stale handle passed to a
  `Create*` description is logged and the call returns an invalid handle.
- **Forgetting that `Create*` can fail.** `CreateTexture`, `CreateBuffer` and the rest return an invalid handle when
  memory is short or the description is wrong (`IsValid()` tells). Operations on an invalid handle abort.
- **Stating the wrong `before` in a `Barrier`.** The RHI does not track states for you. `Undefined` is for "I do not
  care what was there"; using it for a resource whose contents you need discards them.
- **Barriers inside a render pass, or draws outside one.** Copies, barriers and dispatches go between passes; draws go
  inside. This is a Metal rule as much as a Vulkan one.
- **Padding.** A buffer-to-texture copy needs `DeviceCaps::buffer_texture_copy_*_alignment`; the default device
  (and Direct3D 12) wants 512-byte offsets and 256-byte rows, so pad rows to that rather than packing them tightly.
- **Writing the uniform ring after recording.** Write into a `UniformRing` slice, `Flush()` the ring before `Submit`,
  and never reuse a slice across frames.
- **Calling `BeginFrame`/`EndFrame` unbalanced.** `Submit` and `CreateCommandList` need an open frame; a second
  `BeginFrame` without `EndFrame` aborts.
- **Assuming the swapchain image lives on.** `CurrentTexture()` is valid until the next `Acquire` or `Resize`.
- **Tuning on a Debug build with validation on.** The numbers in "Performance notes" are Release without validation;
  validation costs more than the engine does.
- **Naming Vulkan outside `engine/rhi/vulkan/`.** The `rhi_boundary_check` ctest will fail the build.
- **Forgetting `rhi::RegisterCompiledBackends()`.** Without it no backend exists and `CreateDevice` fails with "backend is not registered".
- **Rendering after `Acquire` returned `Minimized`.** No image was acquired: skip the frame (but still pair `BeginFrame` with `EndFrame`).
  `Recreated` is not a failure; an image was acquired, but size-dependent resources such as a depth texture need rebuilding.
- **Keeping one copy of CPU-rewritten data.** A buffer the GPU may still be reading from last frame needs one copy per frame in flight; a `UniformRing` does this for you.
- **Using a texture before it is in the right state.** A sampled texture must be in `ShaderRead` while a draw runs; `UploadTexture` leaves it there, a render target does not.

### Platform notes

The backends each platform builds, besides the `null` test backend everywhere: Vulkan on Linux, Vulkan and Direct3D 12 on Windows (see "The DX12 backend" below), and Metal on
macOS (see "The Metal backend" below): macOS has no Vulkan (MoltenVK would be a third-party layer), so `rhi_vulkan` is not built there. The Metal backend is
built there and is what `Auto` picks. The tests that run once per GPU backend run through it; the Vulkan-only tests skip. The Vulkan backend needs Vulkan 1.3 with dynamic rendering, synchronization2 and timeline
semaphores, and presents through `VK_KHR_wayland_surface` or `VK_KHR_xlib_surface` on Linux and `VK_KHR_win32_surface`
on Windows, depending on the window ([[os::Window]]); "Vulkan on Windows" below has the Windows details. With the NVIDIA proprietary driver, ThreadSanitizer cannot run GPU tests (the driver's threads crash
inside its interceptors), and the X11 presentation path leaks inside a library the driver unloads; see "Building and
Testing" for how the sanitizer flavors cover this.

The portability review,
`docs/superpowers/specs/2026-11-04-rhi-portability-review.md`, lists for every concept how each backend implements it
and which shims they need; the points that shape how you write code today are the clip-space convention (Y down,
depth 0..1, with the flip in the projection), the copy alignment above, explicit resource states, and bind groups
created ahead of time rather than per draw call.

### Performance notes

Recorded by `rhi_bench` on 2026-11-04: NVIDIA GeForce RTX 3060, driver 615.78.08, AMD Ryzen 9 7950X3D, Linux, a Release
build, validation off, 512x512 offscreen target. Numbers vary a few percent run to run; they are a baseline for
comparing later work, not a promise, and are never asserted in CI.

| Measurement | Result |
|---|---|
| Draw recording, 1000 cubes (uniform-ring slice + transient bind group + pipeline + draw each) | 0.50-0.52 ms per frame (about 0.5 us per draw) |
| Draw recording, 10000 cubes | 6.2-6.4 ms per frame (about 0.63 us per draw); the GPU keeps up, wall time is within 5% of recording time |
| `Barrier`, recording cost | about 0.15-0.17 us (buffer), 0.17-0.21 us (texture) |
| Upload through the staging ring, 1 MiB x 256 and 64 MiB x 8 | 14-18 GiB/s (a memcpy plus one GPU copy; bound by host memory bandwidth) |
| Cube pipeline creation (shaders, pipeline, buffers) | 5.6-5.8 ms with the cache off, 4.9-5.0 ms on a cold cache, 3.6 ms on a warm one |

On macOS (Metal on the Intel Iris Plus 645 of a 2019 MacBook Pro, macOS 15.8.1, Release, validation off): 0.64-0.89 us per
draw (1000 and 10000 cubes, a transient bind group each), barriers 0.02-0.05 us (recorded, nothing to encode between Metal encoders), uploads
3.8 GiB/s (1 MiB) and 6.6 GiB/s (64 MiB), the cube pipeline 1.8-1.9 ms with or without the pipeline archive.

Use it with the design rules in mind. `Submit` of a whole frame costs microseconds, so the cost of a frame is recording.
Recording is thread-compatible per list, so scale by recording lists on several jobs. A profile of the 10000-draw case
(`perf record`) put the NVIDIA driver at about 12%, the device's resource mutex (`pthread_mutex_lock`, from
`CreateBindGroup` and the handle lookups behind `SetPipeline`/`SetVertexBuffer`) at about 11%, `CreateBindGroup`
itself at 8%, and the rest spread across the list's own methods. Per-draw transient bind groups are therefore the
first thing to drop when draw counts grow: put per-object data in one buffer and index it, or bind it once per group of
draws.

### Backends and the registry

A program has the backends it registered, and no others. Each backend library describes itself with a
[[rhi::BackendDescriptor]] (its [[rhi::Backend]] value, a name such as `vulkan`, a device factory, an adapter lister, an
availability probe, its shader format and how strongly `Auto` prefers it) and registers it with
[[rhi::RegisterBackend]]. [[rhi::CreateDevice]], [[rhi::EnumerateAdapters]] and [[rhi::DeviceHost]] live in the neutral `rhi`
library and reach backends only through this registry, so the renderer links `rhi` alone and never a backend.

Registration is one explicit call, never a global constructor: a program calls [[rhi::RegisterCompiledBackends]]
(`rhi/backends/backends.h`, library `rhi_backends`) at startup, before it creates a device. It registers the null backend everywhere,
Vulkan on Linux and Windows, Direct3D 12 on Windows and Metal on macOS (which has no Vulkan). Test binaries do it from a Catch2 listener
(`tests/support/register_backends.cpp`). Forgetting the call is not a silent "no GPU": the device creation fails with
`the vulkan backend is not registered in this program (link rhi_backends and call rhi::RegisterCompiledBackends ...)`.

`Backend::Auto`, the default of [[rhi::DeviceCreateInfo::backend]], picks the available registered backend with the highest
rank ([[rhi::PickAutoBackend]]): Direct3D 12 (rank 20) on Windows, Metal (30) on macOS, Vulkan (10) on Linux; the null backend has rank 0, so `Auto` never picks it. A named backend that is registered but
cannot run here (no Vulkan loader, for example) fails with the backend's own reason ([[rhi::ResolveBackend]]). The renderer's
`backend` setting (`auto`, `null`, `vulkan`, `dx12`, `metal`) goes through the same rule when the program starts; it is not changed at run time
(a change waits for the next start, see the render settings guide). `rhi::PickAutoBackend` never picks
null, and a GPU backend that is registered but fails is never replaced by null.

```cpp compile standalone run
#include <cstdio>

#include "rhi/backend_registry.h"
#include "rhi/backends/backends.h"
#include "rhi/rhi.h"

using namespace vse;

int main() {
    rhi::RegisterCompiledBackends();
    // Every platform has the null backend, and at least one GPU backend: Vulkan (spirv) on Linux and Windows,
    // Direct3D 12 (dxil) on Windows, Metal (metallib) on macOS.
    const rhi::BackendDescriptor* null_backend = rhi::FindBackend(rhi::Backend::Null);
    std::printf("%s takes %s\n", null_backend->name, rhi::ShaderFormatName(null_backend->shader_format));
    bool gpu = false;
    for (rhi::Backend which : {rhi::Backend::Vulkan, rhi::Backend::Dx12, rhi::Backend::Metal}) {
        gpu = gpu || rhi::FindBackend(which) != nullptr;
    }
    std::printf("a GPU backend is registered: %s\n", gpu ? "yes" : "no");
    return 0;
}
```
```output
null takes spirv
a GPU backend is registered: yes
```

### Creating a device

[[rhi::CreateDevice]] takes a [[rhi::DeviceCreateInfo]] and returns a device for the requested [[rhi::Backend]]
(`Null`, `Vulkan`, `Dx12`, `Metal`, or `Auto` for the preferred one), or null with the reason logged. The device is headless; a
presentation window is added with the swapchain.

For Vulkan, the GPU is chosen by [[rhi::vulkan::SelectPhysicalDevice]]. A GPU qualifies if it supports Vulkan 1.3,
dynamic rendering, synchronization2, timeline semaphores and has a graphics+compute queue family; qualifying GPUs
rank discrete, integrated, virtual, software, the earlier one winning a tie. The full ranking, with the reason
for each rejection, is logged on `LogRhi` at `Info`. Set `ENGINE_RHI_GPU` to a GPU index (`1`) or a part of its
name (`radeon`, case-insensitive) to choose another one; an override that matches nothing suitable fails device
creation rather than silently picking a different GPU.

The device uses one graphics+compute queue for everything, uploads included, so no queue-family ownership
transfers exist. [[rhi::vulkan::PlanQueues]] also records a dedicated transfer-only family when the GPU has one,
where an asynchronous transfer path would go later. `Submit`, `BeginFrame`, `EndFrame` and `WaitIdle` belong to
one submission thread (the queue is not externally synchronized beyond that rule). The device enables only the
features it uses: dynamic rendering, synchronization2, timeline semaphores and, where available, anisotropic
filtering. [[rhi::Device::Caps]] reports the limits it found.

### Choosing a GPU

[[rhi::EnumerateAdapters]] lists the GPUs a backend can use, and [[rhi::DeviceCreateInfo::adapter]] picks one. Each
[[rhi::AdapterInfo]] has a **stable id** (Vulkan builds it from the vendor id, the device id and the device UUID, never the
enumeration index), a name, a kind, device-local memory, whether it can present to the window you passed, and whether it
is suitable; an unsuitable adapter says why in `unsuitable_reason`. Save the id, not the index: it survives another GPU
being added.

The request is matched by [[rhi::MatchAdapter]]: an id first, then a LUID (Windows' locally unique adapter identifier, 16
hexadecimal digits, which names the same GPU to Vulkan and Direct3D 12 until the next reboot), then a decimal index, then a
case-insensitive part of the name. The order of precedence is `DeviceCreateInfo::adapter`, then the `ENGINE_RHI_GPU` environment variable (Vulkan), then
the ranking (discrete before integrated before virtual before software). A request that names nothing, or an adapter that
is not suitable, makes [[rhi::CreateDevice]] fail with a log line that names the reason. It never falls back to another
GPU: the caller decides whether to try the best one instead.

```cpp compile standalone run
#include <cstdio>

#include "rhi/backends/backends.h"
#include "rhi/rhi.h"

using namespace vse;

int main() {
    rhi::RegisterCompiledBackends();     // once, before any device
    rhi::DeviceCreateInfo info;
    info.backend = rhi::Backend::Null;   // the null backend lists two fake adapters, so this runs without a GPU
    rhi::AdapterList adapters;
    if (rhi::EnumerateAdapters(info, &adapters)) {
        for (u32 i = 0; i < adapters.count; ++i) {
            std::printf("%s: %s\n", adapters.items[i].id, adapters.items[i].name);
        }
    }
    info.adapter = "integrated";         // an id, an index or part of the name
    rhi::DevicePtr device = rhi::CreateDevice(info);
    if (device != nullptr) {
        std::printf("created on %s\n", device->Adapter().id);
    }
    return 0;
}
```
```output
null-discrete: Null Discrete GPU
null-integrated: Null Integrated GPU
created on null-integrated
```

`Device::Adapter` reports the adapter a device was created on. On a machine with one GPU everything above still works; the
list has one entry. Direct3D 12 and Metal map onto the same struct.

### Lifecycle and reset

A device can be destroyed and created again while the program runs: to change the adapter, turn validation on or off, or
recover from a lost device. [[rhi::DeviceHost]] owns the device and its swapchain and does the sequence. Everything that
holds device objects registers as a [[rhi::DeviceObserver]] with a **tier** (0 is created first and released last: samplers
and textures, then meshes, then layouts and shaders, then materials and pipelines, then passes), and then:

1. `Recreate` calls `OnDeviceReleasing` on every observer, high tier first, so each destroys what it made and keeps its
   CPU-side source (pixels, mesh data, descriptors);
2. the host calls `Device::Quiesce` (wait idle, drain deferred destruction), destroys the swapchain and the device (which
   saves the pipeline cache), and logs an error if any object is still alive (`Device::Resources` has the counts);
3. it creates the new device and swapchain, then calls `OnDeviceCreated` on every observer, low tier first, so each
   recreates and re-uploads.

A failed creation leaves the host empty and `LastError` says why; the caller rolls back with `Create` and the old
description. Handles of the old device are never valid on the new one: each device has a serial (`Device::Epoch`) that
seeds its handle generations, so a stale handle is rejected instead of aliasing a new resource. (The limit: a slot reused
more than 2^20 times, or more than 4096 devices in one process, could wrap into another device's range.)

Thread rule: `Recreate` runs on the thread that called `Create`, between frames. Called inside `BeginFrame`/`EndFrame` or on
another thread it aborts.

```cpp compile standalone run
#include <cstdio>

#include "rhi/backends/backends.h"
#include "rhi/device_host.h"

using namespace vse;

struct CubeOwner final : rhi::DeviceObserver {
    rhi::Device* device = nullptr;
    rhi::BufferHandle buffer;
    void OnDeviceReleasing(u32) override {
        device->DestroyBuffer(buffer);   // destroy what this owner made; its data lives on in the CPU
        buffer = rhi::BufferHandle{};
    }
    bool OnDeviceCreated(rhi::Device* d, u32) override {
        device = d;
        rhi::BufferDesc desc;
        desc.size = 64;
        desc.usage = rhi::BufferUsage::Uniform;
        desc.location = rhi::MemoryLocation::CpuToGpu;
        buffer = d->CreateBuffer(desc);
        return buffer.IsValid();
    }
    const char* ObserverName() const override { return "cube"; }
};

int main() {
    rhi::RegisterCompiledBackends(); // once, before any device
    CubeOwner owner;
    rhi::DeviceHost host;
    host.AddObserver(&owner, 1);
    rhi::DeviceCreateInfo info;
    info.backend = rhi::Backend::Null;
    host.Create(info, rhi::SwapchainDesc{});
    const u32 first = host.Epoch();
    host.Recreate(info, rhi::SwapchainDesc{});
    std::printf("new device: %s, live buffers: %zu, resets: %u\n", host.Epoch() != first ? "yes" : "no",
                host.GetDevice()->Resources().buffers, host.ResetCount());
    host.Destroy();
    return 0;
}
```
```output
new device: yes, live buffers: 1, resets: 1
```

### Presentation

[[rhi::Swapchain::Reconfigure]] changes the present mode, size, image format and HDR mode in one call, reusing the old
swapchain where the API allows (`oldSwapchain` on Vulkan). A request the surface cannot grant falls back with a log line, and the
result is readable: [[rhi::Swapchain::ActualPresentMode]], [[rhi::Swapchain::ImageFormat]], [[rhi::Swapchain::GetColorSpace]].
[[rhi::Swapchain::Capabilities]] lists what the surface offers (present modes, formats, HDR) so a settings screen can grey out what
is unavailable. `Resize` is `Reconfigure` with a new size. The images and textures from before a reconfigure are invalid; call it
between frames.

- **HDR:** `HdrMode::ScRgbLinear` is granted when the surface offers `R16G16B16A16_SFLOAT` in the extended-sRGB-linear color space
  (the engine asks for `VK_EXT_swapchain_colorspace` when it is installed); `HdrMode::Hdr10` needs a 10-bit format the RHI does
  not have yet and falls back. Neither has been seen on a real HDR display (see `KNOWN-ISSUES.md`).
- **Zero extent:** a window with no area gets no swapchain; the request is remembered and applied by the `Acquire` that sees the
  window again. No zero-sized image is ever created.
- **A format change** reaches the renderer through [[rhi::DeviceHost::ReconfigureSwapchain]], which calls
  `DeviceObserver::OnSwapchainChanged` only when the image format or color space changed, so only what was built against the
  old format (the tonemap pipeline) is rebuilt.
- **A lost surface** (`Swapchain` returns `Lost` because the window's native surface changed) is repaired by
  [[rhi::DeviceHost::RecreateSurface]]: it destroys the swapchain, asks the device for a new surface
  ([[rhi::Device::RecreateSurface]]) and creates the swapchain again; if the adapter cannot present to the new surface it falls
  back to a full reset.
- **Fullscreen** belongs to the window: see [[os::Window::SetFullscreen]]; the swapchain just follows the resize.

### Uploading data

Buffers in `GpuOnly` memory cannot be written by the CPU. [[rhi::Device::UploadBuffer]] copies bytes into one: it
stages them in a persistently mapped 16 MiB ring, records a copy on an internal command buffer, and submits it on
the graphics queue. The bytes have been read from your pointer when the call returns, so you may reuse it at once.
The returned [[rhi::UploadTicket]] holds a fence value; pass it to [[rhi::Device::WaitFence]] to wait for the GPU, or
ignore it, since the copy is ordered after everything submitted earlier and before everything submitted later. A
ticket with fence 0 means no wait is needed (the destination was CPU-visible and was written through its mapping).

An upload larger than a quarter of the ring is split into chunks, and when the ring is full the call waits for the
oldest chunk to finish and reuses its space. Uploads from several threads are serialized by the device. The buffer is
in state `CopyDst` afterwards; move it to the state its first use needs with `Barrier` in a command list, as for
any other write. A typical first use of a vertex buffer is:

1. create it with `Vertex | CopyDst` usage in `GpuOnly` memory;
2. `UploadBuffer` the vertex bytes;
3. record `Barrier(buffer, CopyDst, VertexBuffer)` before the first draw that reads it.

Because everything runs on one queue family, buffers are created with exclusive sharing and no queue-family
ownership transfers exist anywhere in the RHI.

[[rhi::Device::ReadbackBuffer]] is the opposite, for tests and tools: it waits for all earlier work, copies the range
through a temporary `GpuToCpu` buffer and returns the bytes. It stalls the GPU, so keep it out of frame loops.
`CpuToGpu` and `GpuToCpu` buffers can instead be mapped with `MapBuffer`; the mapping is persistent, so mapping costs
only bookkeeping, and non-coherent memory is flushed or invalidated for you.

**Deferred destruction.** `DestroyBuffer` makes the handle invalid at once but releases the memory only after the
frame it was destroyed in has completed on the GPU (a buffer destroyed between frames belongs to the next frame), so
a buffer used by work already submitted this frame is never freed under it. `BeginFrame` releases what has
completed; `WaitIdle` releases everything. Destroying the device frees any buffers still alive, with a warning.

### Textures and samplers

A texture is created in state `Undefined`; its contents are unknown until something writes them. The RHI does not
hide image layouts: you move a texture between [[rhi::ResourceState]]s with `Barrier`, and the Vulkan backend turns
each state into stages, accesses and a layout through one table (`GetBarrierScopes`). Ask
[[rhi::Device::FormatSupport]] whether a format works with the usages you want before relying on it; the device
answers from its format table, and `CreateTexture` refuses a combination it cannot do.

The easy path for static data is [[rhi::Device::UploadTexture]]. It writes one mip level of one array layer from
CPU memory, with an optional row pitch for data that is not tightly packed, and leaves the whole texture in
`ShaderRead`. The first upload requires the texture to be `Undefined` (or already `ShaderRead` for the next level
or layer), so upload every level you have, then use the texture. Block-compressed formats count rows in block rows.
The texture needs `Sampled` and `CopyDst` usage. A level bigger than the staging chunk is uploaded in row bands.

```
TextureDesc desc;
desc.width = 64; desc.height = 64; desc.mip_levels = 7;
desc.format = Format::RGBA8_UNorm;
desc.usage = TextureUsage::Sampled | TextureUsage::CopyDst | TextureUsage::CopySrc;
TextureHandle tex = device->CreateTexture(desc);
device->UploadTexture(tex, 0, 0, pixels, 0);      // level 0, tightly packed
// in a command list: fill the other levels with 2x2 averages
list->GenerateMips(tex);                           // ShaderRead -> ShaderRead
```

[[rhi::CommandList::GenerateMips]] halves level 0 into the rest with linear blits. It needs the usages above and a
format the device can blit (check [[rhi::Device::CanGenerateMips]]); for other formats upload precomputed mips.
[[rhi::Device::ReadbackTexture]] copies one level back for tests and tools and, like `ReadbackBuffer`, stalls.

**Cube textures.** `TextureDimension::Cube` makes a cube map: six square faces (`array_layers` is 6, `depth` is 1) in the
order +X, -X, +Y, -Y, +Z, -Z, each face an array layer, so `UploadTexture(tex, mip, face, ...)` fills one face of one level.
A cube can only be sampled and copied (no storage, no render target) and has no array form. A shader reads it as
`TextureCube`; the binding is a plain `SampledTexture` (the dimension belongs to the texture, not the layout). Check
`DeviceCaps::cube_textures` first: the Vulkan and Metal backends and the null device report true, and `CreateTexture` fails for a
cube on a device that reports false.

Samplers are cheap to describe and are shared: `CreateSampler` returns the same handle for equal descriptors and
counts references, so call `DestroySampler` once per `CreateSampler`. The anisotropy is clamped to what the device
supports, and the debug name does not take part in the comparison.

### Recording and submitting

Work reaches the GPU as command lists. [[rhi::Device::CreateCommandList]] hands the calling thread a list that is
valid for the current frame; record it with `Begin` ... `End`, then pass the ended lists to [[rhi::Device::Submit]]
in the order they must run. Lists record on any thread at the same time: the Vulkan backend keeps one
`VkCommandPool` per (thread, frame in flight), so recording takes no lock, and the pools of a frame are reset only
after the GPU has finished the frame that used them (three frames deep). A job-system participant uses its
participant index as its pool slot; other threads take one of 64 fallback slots and give it back when they exit.

```
device->BeginFrame();
CommandList* a = device->CreateCommandList();   // on any thread, in parallel with others
a->Begin();
a->Barrier(dst, ResourceState::Undefined, ResourceState::CopyDst);
a->CopyBuffer(src, dst, 0, 0, size);
a->End();
FenceValue done = device->Submit(lists, count);  // one thread, lists in order
device->EndFrame();
device->WaitFence(done, timeout_ns);             // optional: wait on the CPU
```

**Barriers.** A resource is always in one explicit `ResourceState`, and `Barrier(resource, before, after)` moves
it. In Vulkan every state maps to pipeline stages, memory accesses and (for textures) an image layout through one
table, `GetBarrierScopes` in `vk_barrier.h`; moving from `Undefined` waits on all earlier work and discards a
texture's contents. Buffers in `CpuToGpu` and `GpuToCpu` memory take no barriers.

**Fences.** `Submit` returns a `FenceValue` on the device's timeline semaphore. The values only increase; wait with
`WaitFence(value, timeout_ns)` (0 polls) or poll `CompletedFence`. Submissions are serialized by a mutex, and every
submission runs on the single graphics queue in call order, so a later submission never needs an explicit wait on
an earlier one; the wait/signal lists of a multi-queue submit arrive with a second queue.

**A lost device.** `VK_ERROR_DEVICE_LOST` from any call sets a sticky state, `DeviceLost()`, and logs once.
After that `Submit`, `UploadBuffer` and `ReadbackBuffer` fail and `CreateCommandList` returns null, and frames still
begin and end, so the application can shut down cleanly or recover by creating a new device. `WaitIdle` and `Quiesce` on a lost device
return as soon as the driver reports the loss and release what was retired (an injected loss leaves a healthy device, so the wait is still real). Tests exercise this with
[[rhi::FaultInjection]] (`lose_device_at_frame`, `lose_device_at_submit`, `lose_device_at_present`, and `strikes_left` to limit how many
devices an injected loss strikes) and `VulkanDevice::InjectSubmitFailure`.

### Presenting to a window

A device that presents is created for one window: set `DeviceCreateInfo::window` and the Vulkan backend makes a
presentation surface from the window's native handles (a Wayland surface, or an Xlib window through an isolated
translation unit that keeps the X11 headers away from the rest of the backend), and only considers GPUs that can
present to it. Then `CreateSwapchain` gives the [[rhi::Swapchain]]. The choices behind it are plain functions in
`vk_swapchain_policy.h`: an sRGB BGRA (then RGBA) surface format with a logged fallback, FIFO unless you ask for
`Mailbox` or `Immediate` and the system offers it, one image more than the minimum, and, where the surface leaves
the size open (Wayland), the window's size clamped to the limits.

A frame renders into `CurrentTexture()`:

```
SwapchainStatus s = swapchain->Acquire();         // Ok, Recreated, Minimized or Lost
if (s == SwapchainStatus::Ok || s == SwapchainStatus::Recreated) {
    // list: Barrier(image, Undefined, ColorAttachment) ... draw ... Barrier(image, ColorAttachment, Present)
    device->Submit(lists, count);
    // device->EndFrame() goes here; the demo then calls Present (see "The frame loop")
    swapchain->Present();                          // Ok, Recreated or Lost
}
```

**Resize and minimize.** `Acquire` compares the window's `ResizeSerial()` with the one the swapchain was built
for and rebuilds when it changed (the call then returns `Recreated` and still delivers an image); the same happens
when the driver reports the surface out of date or suboptimal. A window with no area, or one the window system
reports hidden, makes `Acquire` return `Minimized` at once, and a swapchain made for it later is built as soon as
the window has an area again. `Resize(0, 0)` is the explicit form. Wayland has no minimized or occluded state a client
can see, and a surface the compositor stopped drawing stops handing out images, so `Acquire` waits at most half a
second for one and then also answers `Minimized`; render again next frame. Recreating a swapchain waits for the
device to go idle, so avoid doing it every frame (a window being dragged larger rebuilds once per size change).

### The frame loop

Everything the device does is paced by frames. A frame is the span from `BeginFrame` to `EndFrame`:

```
device->BeginFrame();
    // acquire, record, Submit
device->EndFrame();
swapchain->Present();     // presentation follows the frame's submit
```

**Frames in flight.** `DeviceCreateInfo::frames_in_flight` (1 to 3, default 2) is how many frames the CPU may run
ahead of the GPU. `BeginFrame` waits for the frame that many back to finish, then resets that frame's command pools,
frees every resource destroyed in a frame the GPU has completed, and advances `FrameNumber()`. After it returns,
`FrameNumber() - CompletedFrame() <= frames_in_flight` holds. One in flight is the lowest latency and stalls the CPU
on every frame; three smooths out a variable frame time at the cost of one more frame of input latency. The
program renders the same pixels with any setting.

**What may be touched between `BeginFrame` and `EndFrame`.**

- Command lists from `CreateCommandList` belong to the calling thread and to this frame; they are invalid after the
  next `BeginFrame`. Record them on any thread, then `Submit` from one thread.
- Resources may be created and destroyed at any time. A destroyed resource stays alive until the GPU has finished
  every frame that could have used it, so a command list recorded this frame may still name it.
- `UploadBuffer`, `UploadTexture` and the readbacks may be called in the frame; they use the shared staging ring and,
  for readbacks, wait for the GPU.
- Contents of a buffer that the GPU reads are not versioned per frame: a buffer the CPU rewrites every frame needs one
  copy per frame in flight.
- `Acquire` and `Present` belong to the frame's thread, like `Submit`.

**Ownership at shutdown.** Destroy what you created, call `WaitIdle`, destroy the swapchain, then the device. The
Vulkan device counts anything still alive at that point (`VulkanDevice::GetLeakCounts`: your buffers, textures,
samplers and swapchain, and the backend's own raw buffers and images), logs it as an error and frees it; other Vulkan
object types are covered by the validation layer's own leak report; the frame-loop tests assert the counts are zero.

The `rhi_demo` app (`game/rhi_demo/main.cpp`) is the smallest complete loop, and
[Hello RHI](../tutorials/hello-rhi.md) walks through it.

### Pipelines

A graphics pipeline is created from a [[rhi::GraphicsPipelineDesc]]: the two shaders, the vertex layout, the fixed-function
state, the attachment formats (there are no render pass objects; the formats stand in for them), the bind group layouts
and the push-constant range. Viewport and scissor are dynamic: set them on the command list inside every pass.

```
GraphicsPipelineDesc desc;
desc.vertex_shader = vs;
desc.fragment_shader = fs;
desc.vertex_bindings = rhi::Span<VertexBindingDesc>(&binding, 1);
desc.vertex_attributes = rhi::Span<VertexAttributeDesc>(attributes, 2);
desc.color_targets[0].format = swapchain->ImageFormat();
desc.color_target_count = 1;
PipelineHandle pipeline = device->CreateGraphicsPipeline(desc);   // invalid handle + a logged reason on failure

// in a command list, inside BeginRendering/EndRendering:
list->SetPipeline(pipeline);
list->SetViewport(...); list->SetScissor(...);
list->SetVertexBuffer(0, vertices, 0);
list->Draw(3, 1, 0, 0);
```

A pipeline works only in a pass whose attachment formats equal its own, and binding it ends with the pass, so bind
it again in the next one. Creation fails (returns an invalid handle, logs why, and never reaches the driver for
these) when a shader is missing, destroyed or of the wrong stage, a bind group layout is stale, a limit is exceeded, or a
format cannot be rendered to on this device. Wireframe needs the device feature and fails without it.

**Sharing.** Two bind group layouts with the same bindings share one `VkDescriptorSetLayout`, and pipelines with
equal bind group layouts and push-constant range share one `VkPipelineLayout`. The handles stay distinct; the sharing is
internal and shows in `VulkanDevice::Counts()`. Pipeline layouts live as long as the device (there are few).

**The pipeline cache.** The Vulkan device keeps a `VkPipelineCache` and stores it on disk so later runs compile
faster. It is read when the device is created and written, atomically (a temporary file then a rename), when the device is
destroyed. The file is named from the vendor, device and driver UUID, so a driver update starts a new file. A file
whose header does not match, or that the driver rejects, is ignored with a log line; it is never an error. The
directory is, in order: `DeviceCreateInfo::pipeline_cache_dir`, `$ENGINE_RHI_CACHE_DIR`, then the user's cache directory for the
application (`io::UserCacheDirectory`: `$XDG_CACHE_HOME/<application name>` or `$HOME/.cache/<application name>`). A configured
directory that cannot be used (too long, a file, not writable) means no cache or a warning when saving; the device still works. All
file and directory work goes through the file layer (`io::File`, `io::WriteFileAtomic`, `io::CreateDirectories`); see [io](io.md).
Set `DeviceCreateInfo::pipeline_cache` to false to turn it off. The test
runner points `ENGINE_RHI_CACHE_DIR` into the build tree so tests never write to your home directory. The directory rule and the file
reading and writing are shared by every backend ([[rhi::ResolvePipelineCacheDir]], `rhi/pipeline_cache_file.h`); what the file holds is
each backend's own (the DX12 backend's is below).

### Binding resources

Shaders read buffers, textures and samplers through bind groups. The shape is a **layout** ([[rhi::BindGroupLayoutDesc]]:
a binding index, a type, the stages that use it and an array size for each entry), the contents are a **bind group**
([[rhi::BindGroupDesc]]: one entry per binding element, naming the buffer range, texture or sampler), and a pipeline
declares which layouts it uses at group slots 0, 1, ... In Slang a layout is `[[vk::binding(n, slot)]]`.

```
BindingDesc entries[3] = {{0, BindingType::UniformBuffer,  ShaderStage::Fragment, 1},
                          {1, BindingType::SampledTexture, ShaderStage::Fragment, 1},
                          {2, BindingType::Sampler,        ShaderStage::Fragment, 1}};
BindGroupLayoutHandle layout = device->CreateBindGroupLayout({.entries = rhi::Span<BindingDesc>(entries, 3)});
// ... a pipeline created with desc.bind_group_layouts[0] = layout ...
BindGroupHandle group = device->CreateBindGroup(group_desc);   // entries name the buffer, texture and sampler
list->SetPipeline(pipeline);
list->SetBindGroup(0, group);
list->SetPushConstants(0, &tint, sizeof(tint));
list->Draw(3, 1, 0, 0);
```

A sampled texture must be in `ShaderRead` and a storage texture in `ShaderWrite` when the draw executes; the device
cannot see states inside your command list, so the validation layers (and the null backend) are what catch a mistake.
`SetBindGroup` may come before or after `SetPipeline`: the groups are bound when the draw happens, against that pipeline's
layouts, and a group whose layout differs from the pipeline's for that slot is an error.

**Lifetimes.** Choose per group:

| | Persistent (default) | Transient (`BindGroupDesc::transient`) |
|---|---|---|
| Valid | until `DestroyBindGroup` | in the frame it is created in |
| Destroyed by | you | the device, once the GPU has finished that frame |
| Cost | descriptors from a pool that frees sets one by one | descriptors from a per-frame pool that is reset in one call |
| Use for | materials, long-lived resources | per-draw or per-frame data |

Both kinds grow instead of failing: when a descriptor pool is full the device adds another (`DeviceCreateInfo::
descriptor_sets_per_pool` sets the size of each, 1024 by default), and a layout with an array too large for the
default size gets a pool of its own. Ten thousand transient groups in one frame are fine.

**Storage buffers have a stride.** A `StorageBuffer` or `ReadOnlyStorageBuffer` binding gives `BindingDesc::stride`, the size of one
element as the shader declares it (`StructuredBuffer<T>`: `sizeof(T)` in std430), and a bound range starts on a multiple of it as well
as on the device's storage alignment (the lcm of the two, 768 for 48-byte elements and 256-byte alignment). Direct3D 12 makes a
structured view from it and refuses a storage binding without one; Vulkan ignores it, and every backend checks the offset. Layouts built
from reflection get it from the shader. `ByteAddressBuffer` is not supported: use `StructuredBuffer<uint>`.

A group does not keep what it names alive: destroy a buffer, texture or sampler a group uses and the group cannot be
bound any more (`SetBindGroup` reports it). Equal layouts share one Vulkan object, and pipelines with equal layouts share one
pipeline layout (see Pipelines).

**Push constants** are the cheapest way to send a few bytes per draw: declare `push_constant_bytes` (a multiple of 4, at
most `DeviceCaps::max_push_constant_bytes`, 128 on every device) and `push_constant_stages` in the pipeline, then call
`SetPushConstants(offset, data, size)` after `SetPipeline`. The data is copied during the call.

**Per-frame uniform data.** [[rhi::UniformRing]] hands out aligned slices of one mapped buffer, a fresh region each frame
(`BeginFrame` after `Device::BeginFrame`). Write into `Allocate(size).data` and bind that `buffer`, `offset` and `size` in a
bind group entry (transient groups pair well with it):

```
ring.BeginFrame();
UniformAllocation u = ring.Allocate(sizeof(Params));
std::memcpy(u.data, &params, sizeof(Params));
entry.buffer = u.buffer; entry.offset = u.offset; entry.size = u.size;
// ... all the frame's Allocate calls done ...
ring.Flush();   // before Submit: makes the writes visible on devices without coherent host memory
```

A bind group entry with `size` 0 binds the buffer from `offset` to its end, and the range must not exceed the device's
uniform (or storage) buffer range limit, 64 KiB on many GPUs: bind each allocation with its own `offset` and `size`
rather than the whole ring buffer.

### Depth, indexed draws and the 3D conventions

**Depth.** Create a texture with `TextureUsage::DepthAttachment` in `DeviceCaps::depth_format` (the best supported
depth format on this device), move it to `ResourceState::DepthAttachment`, and put it in the pass:
`RenderingDesc::depth.texture` with a load op and `clear_depth` (1.0 clears to the far plane). A pipeline that tests
depth sets `depth_format` to the same format, `depth_test`, `depth_write` and a `depth_compare` (`Less` by default).
`SetPipeline` checks that the pipeline's color and depth formats match the pass. A depth texture is as large as the
color target it goes with: when a swapchain is resized, create a new depth texture of the new size (the device destroys
the old one once the GPU is done with it); `rhi_demo` does this every frame it notices a different size.

**Indexed draws.** Put 16- or 32-bit indices in a buffer with `BufferUsage::Index`, bind it with
`SetIndexBuffer(buffer, offset, IndexType)` and call `DrawIndexed(index_count, instance_count, first_index,
vertex_offset, first_instance)`. An indexed and a non-indexed draw of the same triangles render identical pixels.

**Conventions.** The RHI follows Vulkan: framebuffer and clip-space Y point down and depth runs 0 (near) to 1
(far). The RHI does not hide this; the *renderer side* builds matrices for it. The helpers in `game/common/cube_scene.h`
(used by `rhi_demo` and the tests; not part of the RHI) do that:

- World and view space are right-handed, Y up; the camera looks down -Z. `Mat4` is column-major, which is also
  std140's layout for a `mat4`, so it can be copied into a uniform buffer unchanged.
- `PerspectiveVulkan(fov_y, aspect, near, far)` flips Y and maps depth to 0..1; `SetViewport` always takes a positive
  height.
- Meshes are authored counter-clockwise when seen from outside; with that projection they are front faces under
  `FrontFace::CounterClockwise`, and `CullMode::Back` removes the faces that point away.

See also the [math conventions](foundation-math.md#conventions-read-this-once).

### Textures in draws

To sample a texture in a draw: create it with `Sampled` and `CopyDst` usage (plus `CopySrc` if you will generate
mips), upload level 0 with `UploadTexture` (the texture ends up in `ShaderRead`, the state a sampled texture must be
in while the draw runs), and call `GenerateMips` once in a command list when `CanGenerateMips(format)` says the format
allows it - or upload every level yourself with `UploadTexture(texture, mip, ...)`, as `.rtex` files do. Bind the
texture and a sampler in a bind group (`SampledTexture` and `Sampler` entries) and declare them in Slang as
`Texture2D` and `SamplerState` and sample with `tex.Sample(smp, uv)`.

- **Mips.** With `mip_filter = Linear` the sampler blends two levels (trilinear), so a texture drawn smaller than its
  size is averaged instead of aliased. A texture with one mip level, drawn the same way, shows the aliasing; the tests
  compare the two.
- **Anisotropy.** `SamplerDesc::max_anisotropy` up to 16 (clamped to the device limit) sharpens textures seen at a
  grazing angle.
- **Color spaces.** Shader math is linear. Create color textures (albedo, photographs) in an `_SRGB` format: the
  hardware decodes them to linear when sampled. Data that is not a color (normals, masks, lookup tables) uses a `UNORM`
  format and is read as stored. The color target decides how the shader output is stored: an `_SRGB` target (the usual
  swapchain format) encodes on write, a `UNORM` target stores the linear value as it is - so an offscreen `UNORM`
  image of an sRGB-textured scene looks darker than the window does.
- **Files.** `game/common/textures.h` (shared by the demo and tests, not part of the RHI) has procedural textures and
  the throwaway `.rtex` container the demo's checkerboard is stored in; real image formats arrive with the asset
  pipeline.

### Compute

A compute pipeline is a compute shader plus the layouts of its bind groups and a push-constant size; its push
constants are visible to the compute stage. Record it outside a rendering pass:

```
ComputePipelineDesc cp;
cp.shader = shader;                       // a ShaderStage::Compute module
cp.bind_group_layouts[0] = layout;        // storage buffers and storage textures bind like any resource
cp.bind_group_layout_count = 1;
cp.push_constant_bytes = 4;
PipelineHandle pipeline = device->CreateComputePipeline(cp);

list->SetPipeline(pipeline);
list->SetBindGroup(0, group);
list->SetPushConstants(0, &k, sizeof(k));
list->Dispatch((element_count + 63) / 64, 1, 1); // workgroup counts (the shader ignores the extra invocations), each at most
                                                  // DeviceCaps::max_compute_work_group_count
```

Compute runs on the graphics+compute queue; there is no async compute yet.

**Barriers.** The device cannot see what a shader does, so you order shader work with `Barrier`:

| Hazard | Barrier |
|---|---|
| a dispatch writes, a later dispatch or draw reads | `ShaderWrite` -> `ShaderRead` |
| a dispatch writes, a later dispatch writes the same data | `ShaderWrite` -> `ShaderWrite` |
| a dispatch writes, a copy or readback reads | `ShaderWrite` -> `CopySrc` |
| a copy or upload wrote, a dispatch reads or writes | `CopyDst` -> `ShaderRead` / `ShaderWrite` |

A storage texture must be in `ShaderWrite` (layout `GENERAL`) while a dispatch uses it. Two dispatches in a row with
no hazard between them need no barrier. A missing barrier is not an error the RHI can see on Vulkan; the validation
layer's synchronization validation reports it, which is why the tests run with it on. The null device tracks the states
and reports a resource in the wrong one.

### GPU memory

`vkAllocateMemory` is limited (commonly 4096 live objects) and slow, so the Vulkan backend never allocates one
memory object per resource. [[rhi::vulkan::MemoryAllocator]] allocates large blocks (64 MiB, or 256 MiB in a
device-local heap of 8 GiB or more, but never more than an eighth of a small heap) and carves resources out of them
with a best-fit free list that merges neighbors when ranges are freed. Alignment is honored per request. Buffers and
linear images live in different blocks from optimally-tiled images, which satisfies `bufferImageGranularity`
without tracking it per neighbor. A request larger than half a block, or one the driver wants dedicated, gets its own
memory object. A block that becomes empty is released, except that each pool keeps one empty block as a spare so
a steady allocate/free pattern does not thrash the driver. Host-visible blocks are mapped once, when they are created.

The memory type for a resource comes from [[rhi::vulkan::SelectMemoryType]]: `GpuOnly` prefers device-local memory
that is not host-visible; `CpuToGpu` prefers coherent host-visible memory that is not device-local, falling back to
device-local host-visible memory (resizable BAR, unified memory); `GpuToCpu` prefers cached coherent host memory.

**Failure policy.** Allocation never aborts. If the device or host runs out of memory, the allocation-count limit
(minus a headroom left for objects allocated outside the allocator, such as swapchain images) is reached, or no memory type fits, the allocator
returns an empty [[rhi::vulkan::MemoryAllocation]] and a [[rhi::vulkan::MemoryResult]] saying which, and logs the
per-heap breakdown on `LogRhi`; the resource creation above it then fails with an invalid handle and a log line.
Freeing a range twice, or a range that is not the allocator's, is a programming error and aborts.

[[rhi::vulkan::MemoryAllocator::Stats]] reports per heap the bytes allocated and used, block and dedicated-object
counts, the free bytes and the largest free range, and a fragmentation estimate (`1 - largest free / total free`).
With `VK_EXT_memory_budget`, [[rhi::vulkan::VulkanDevice::HeapBudget]] adds the driver's own budget and usage; it
is informational and never makes an allocation fail.

### Vulkan backend notes

The backend never links against Vulkan. [[rhi::vulkan::LoadVulkanLoader]] opens the platform's loader at run time
([[os::VulkanLoaderName]]: `libvulkan.so.1`, `vulkan-1.dll`), or the path in `ENGINE_VULKAN_LIBRARY`, which tests use to
simulate a machine without Vulkan, and
resolves `vkGetInstanceProcAddr`. A missing library is reported as `NoLoader` and logged; it is not a crash,
so an engine built with this backend still starts on a machine without a driver. The library handle is
deliberately never unloaded: drivers and layers keep threads and global state alive past `vkDestroyInstance`.

Every other entry point is resolved into plain tables — [[rhi::vulkan::VkGlobalFns]] before an instance
exists, [[rhi::vulkan::VkInstanceFns]] per instance and [[rhi::vulkan::VkDeviceFns]] per device (through
`vkGetDeviceProcAddr`, skipping the loader trampoline). The functions are listed once in
`engine/rhi/vulkan/vk_functions.inc`; adding one is a single line. A missing mandatory function fails the
load and the log names it; a missing optional one (swapchain, surfaces, debug utils) is left null, so test
for null before calling those.

[[rhi::vulkan::CheckInstanceVersion]] requires Vulkan 1.3 and logs "driver too old" otherwise.
`ENGINE_VK_CHECK(call)` evaluates a call, logs a failing `VkResult` by name with file and line, and returns a
[[rhi::vulkan::VkResultClass]] so callers can branch on `Recoverable` (out-of-date swapchain), `DeviceLost`
and `Fatal`.

### Validation layers

`VK_LAYER_KHRONOS_validation` is a development tool, never a requirement. [[rhi::vulkan::VulkanInstance::Create]]
enables it when the descriptor says `ValidationMode::On`, or when it says `Auto` and either
`ENGINE_RHI_VALIDATION=1` is set or the build is a Debug build (`ENGINE_RHI_VALIDATION=0` turns it off).
If the layer is not installed, one warning is logged and the instance is created without it.

| Variable | Effect |
| --- | --- |
| `ENGINE_RHI_VALIDATION` | `1` / `0` forces validation on / off for `Auto` instances. |
| `ENGINE_RHI_VALIDATION_SYNC` | `1` / `0` overrides synchronization validation (barrier and hazard checks). |
| `ENGINE_RHI_VALIDATION_BEST_PRACTICES` | `1` / `0` overrides best-practices validation. |
| `ENGINE_RHI_VALIDATION_FATAL` | `1` aborts the process on the first validation error — for interactive runs. |
| `ENGINE_RHI_GPU` | GPU index or name substring to use instead of the highest-ranked suitable GPU. |
| `ENGINE_VULKAN_LIBRARY` | Path of the Vulkan loader library to use instead of `libvulkan.so.1` or `vulkan-1.dll`. |

Every validation message is logged on `LogRhi` with its severity (`ERROR` becomes `Error`, `WARNING` `Warn`,
`INFO` `Debug`, `VERBOSE` `Trace`) and its message id, and errors and warnings are counted:
[[rhi::vulkan::GetValidationCounts]] reads the process-wide totals and [[rhi::vulkan::SetValidationCapture]]
installs a hook that sees each message. Log lines are truncated at 512 bytes; the capture hook gets the full text.

Tests do not read the counters by hand. Every `[gpu]` test constructs `gpu_test::GpuFixture`
(`tests/rhi/gpu_fixture.h`) and calls `Require()`: it skips the test when there is no Vulkan (always on macOS, where Metal runs the per-backend tests; or fails it under
`ENGINE_RHI_REQUIRE_GPU=1`), owns a validation-enabled instance with synchronization validation on, and fails the
test at teardown if any validation error was reported. A test that provokes an error on purpose declares it with
`ExpectErrors(n)`.

### Vulkan on Windows

The same backend runs on Windows. What differs:

- **The loader** is `vulkan-1.dll`, which every Vulkan driver installs into System32; a program needs no SDK to run. It is opened
  through `os::LoadLibrary`, whose search order never includes the current directory.
- **The surface** is `VK_KHR_win32_surface`, made from the window's `HINSTANCE` and `HWND` ([[os::NativeWindowKind]] `Win32`) in
  `engine/rhi/vulkan/windows/vk_win32_surface.cpp`, the backend's one file that sees `<windows.h>` (as the Xlib surface has its own file on
  Linux). Whether a queue family can present is asked through the surface, as on Linux. The window must outlive the surface: the device
  is destroyed (or `RecreateSurface` called) before the window.
- **The validation layers** come from the Vulkan SDK (BUILD.md), which registers them; the variables above behave the same.
- **The pipeline cache** defaults to `%LOCALAPPDATA%\<app>\cache\pipeline_cache_<vendor>_<device>_<uuid>.bin` (the user's cache
  directory, `io::UserCacheDirectory`), named by adapter identity as on Linux.
- **Seen on the AMD Radeon 860M** (driver 26.10.1): the timeline semaphore can read one value short for about 100 us after
  `vkDeviceWaitIdle`, so [[rhi::Device::WaitIdle]] also waits on the timeline before it returns. The memory types the allocator picks on
  this integrated GPU, which reports a device-local heap and a host heap: GPU-only memory from the device-local type that is not host
  visible, uploads from the host-visible, coherent type of the host heap, readbacks from the host-cached type; AMD's uncached
  device-coherent types are never chosen.

### The DX12 backend

Direct3D 12 is the native backend on Windows, library `rhi_dx12`, registered by
[[rhi::RegisterCompiledBackends]] as `dx12`. Ask for it with `DeviceCreateInfo::backend = rhi::Backend::Dx12` or the `backend` setting;
`Auto` picks it on Windows (rank 20, above Vulkan's 10) since it passes the same suites. Its shaders are the minted DXIL
([[rhi::DeviceCaps::shader_format]] `Dxil`), and its devices read the object index from the renderer's index stream
([[rhi::DeviceCaps::object_index_stream]]).

- **Requirements.** Windows 10 or 11 and an adapter with Direct3D 12 at feature level 12_0 and resource binding tier 2. The backend uses
  the OS's own `d3d12.dll` and `dxgi.dll`: no Agility SDK, no D3D12MA, no DirectX-Headers package and no WRL (it has its own
  [[rhi::dx12::ComRef]]).
- **Adapters.** [[rhi::EnumerateAdapters]] lists DXGI's adapters in high-performance order and adds WARP, the software rasterizer, which
  every Windows has. An adapter's id is `dx12-<vendor>-<device>-<subsystem>-<revision>` in hex, stable across runs; its LUID is the second
  form [[rhi::MatchAdapter]] accepts. Reading the binding tier and whether memory is unified needs a device: enumeration makes a
  temporary one per adapter (D3D12 hands back the process's existing device on that adapter, so a live device is not disturbed). With no
  request the device takes the first suitable hardware adapter, else WARP.
- **The debug layer.** Validation `On` needs the Graphics Tools optional feature (`d3d12SDKLayers.dll`, `BUILD.md`); without it device
  creation fails with that reason instead of running unvalidated. `Auto` follows `ENGINE_RHI_VALIDATION`, else turns it on in Debug
  builds when the layer is installed. `sync_validation` (or `ENGINE_RHI_VALIDATION_SYNC=1`) adds GPU-based validation, the closest
  counterpart of Vulkan's synchronization validation, and much slower. The layer is process-wide: once a device turned it on, every
  later device of the process has it. Messages go to `LogRhi` through `ID3D12InfoQueue1`'s callback and are counted
  ([[rhi::dx12::GetDebugMessageCounts]]); a debugger, if attached, breaks on errors. At destruction a validated device asks the layer for
  its live objects: anything but the device itself is a leak, logged and counted ([[rhi::dx12::Dx12Device::GetLeakCounts]]).
- **Fences and waits.** One direct queue runs everything, with one fence whose value grows with each submission. Every wait is bounded:
  it waits in slices of 100 ms and checks for a removed device between them, so a hung or removed GPU ends the wait instead of the
  program.
- **Resources** are committed resources, one heap each. `GpuOnly` buffers live on the default heap, `CpuToGpu` on the upload
  heap and `GpuToCpu` on the readback heap; both of those are mapped once for the buffer's life (D3D12 allows it, and its upload and
  readback heaps are coherent, so [[rhi::Device::FlushMappedBuffer]] does nothing), and they keep a fixed state: an upload-heap buffer
  cannot be a storage (unordered-access) buffer. A buffer with `Uniform` usage is rounded up to 256 bytes, so every constant-buffer view
  over it fits. A sampled depth texture is created typeless (`R32_TYPELESS` for `D32_Float`) with a typed depth view and a shader view
  (`R32_FLOAT`). Uploads and readbacks repack rows to and from D3D12's 256-byte row pitch through a 64 MiB staging ring, in 16 MiB parts;
  a depth level is copied whole, as D3D12 requires. D3D12 has no "undefined" before-state, so the device records each texture's actual
  state after everything submitted, and uses it where the RHI says `Undefined`. Creating 1000 small buffers takes about 310 ms here
  against Vulkan's 22 ms (the Vulkan backend sub-allocates); placed resources are filed for when a measurement asks for them.
- **Bindings**. A pipeline layout becomes a root signature (version 1.1, cached by the layouts' content and the push range): per
  bind group one CBV/SRV/UAV descriptor table and one sampler table, with registers numbered as the bindings in `space<slot>`, and the push
  constants as root constants at `b0, space15` (shaders/target_common.slang). A bind group keeps its views together in a CPU heap;
  `SetBindGroup` copies them into the current frame's part of one shader-visible heap (131072 descriptors per frame, three frames),
  which is emptied when that frame comes round again, so running out is an error that names the sizes, not a wait. Samplers are few
  and the shader-visible sampler heap holds 2048: a group's samplers are copied once into a table that every group with the same
  samplers in the same order shares. A uniform buffer view covers the bound range rounded up to 256 bytes; a writable storage buffer
  must be `GpuOnly`.
- **Pipelines**. A shader takes the minted DXIL (`ShaderBlobSet::For(Caps().shader_format)`; SPIR-V is refused with the shader's
  name) and keeps a copy. Vertex inputs are `TEXCOORD<location>`, the convention every shader is minted with. The clip-space Y flip in the
  D3D12 build puts every vertex where Vulkan puts it on the render target, so the winding there, and `front_face`, are the same as on
  Vulkan. D3D12 refuses color factors in an alpha blend; on the alpha channel they mean their alpha counterparts, as Vulkan treats them.
  A strip with `primitive_restart` gets two states, cutting at `0xFFFFFFFF` and at `0xFFFF`, because D3D12 fixes the cut value in the
  pipeline and the index size comes with the draw.
- **Command lists**. Each list has its own command allocator; the lists of a frame slot are handed out again once that slot's
  frame has finished, so lists record on any number of threads at once. `Submit` runs each list in its own `ExecuteCommandLists`, which
  returns every buffer to `COMMON`: a list tracks its buffers from there (promoted implicitly on use, transitioned explicitly from the
  tracked state), so a buffer barrier's `before` is not needed. Textures keep their state between lists: a list records the state its
  first barrier on each texture needs, and `Submit` first transitions the texture there from the state the device recorded, so a barrier
  from `Undefined` works whatever the texture went through before. `ShaderWrite` to `ShaderWrite` is a UAV barrier. A pass clears with
  `ClearRenderTargetView`/`ClearDepthStencilView`, a don't-care color load or store discards; the viewport is Vulkan's (the shaders flip
  Y). Debug labels are `BeginEvent` with PIX's ANSI string encoding. The debug layer's "clear value does not match" hint is logged as
  information: a texture is created without knowing what its passes clear to. Measured (Release, validation off): a cube draw with its
  transient bind group records in 0.40 us on DX12 against 0.74 to 1.00 us on Vulkan on the same PC.
- **`GenerateMips`** runs `shaders/mipgen.comp` once per level: a bilinear sample of the level above at each texel centre, every
  layer at once. A texture that can have mips generated (`Sampled`, `CopySrc` and `CopyDst`, more than one level, 2D, one sample, a format
  with typed storage) is created with unordered access; an sRGB one through a typeless resource, read through its sRGB view and written
  through a linear view with the shader encoding sRGB. 3D, multisampled and depth textures are refused ([[rhi::Device::CanGenerateMips]]
  answers for the format).
- **The swapchain** is a flip-discard DXGI swap chain of three buffers on the device's Win32 window, with a frame-latency
  waitable object (at most `FramesInFlight` frames queued) that `Acquire` waits on for at most half a second (a window Windows is not
  composing then counts as `Minimized`, as on Vulkan; a minimized window returns at once). The buffers are linear (`B8G8R8A8_UNORM` by
  default) and an sRGB format is their render-target view. `Fifo` presents with a sync interval of 1; `Immediate` with 0 and tearing when
  the system allows it (`DXGI_FEATURE_PRESENT_ALLOW_TEARING`, so the swap chain is created with tearing from the start); `Mailbox` is not
  offered and falls back to `Fifo`. scRGB HDR is a 16-bit float buffer in `G10_NONE_P709` when the output takes it (the compositor maps it
  to an SDR desktop too); HDR10 waits for a 10-bit RHI format. A resize or format change is `ResizeBuffers` after the GPU is idle and every
  reference to the old buffers is gone; each present is followed by a fence signal, so waiting for the device waits for the presents too.
  DXGI's Alt+Enter is off: fullscreen is the window's borderless mode. `RecreateSurface` only changes the window (DXGI has no surface object).
- **Golden images**: every golden (triangle, cube, textured cube, lit cube, the compute results) matches through
  Direct3D 12 with the tolerances unchanged, on the Radeon 860M and on WARP. On the 860M the triangle and the cube are pixel-identical to
  Vulkan's images; the textured and lit cubes differ from them by at most 7 and 6 levels on a few pixels, and both backends are equally
  far from the NVIDIA-made goldens, so the remaining differences are the driver's, not the backend's. `ENGINE_RHI_GPU` picks the adapter
  as on Vulkan.
- **The pipeline cache** is an `ID3D12PipelineLibrary` in `<cache dir>/pipelines_<adapter id>_<driver>.bin` (the same
  directory rule as Vulkan's). Each pipeline is stored under a name hashed from its shaders, its root signature's serialized bytes and
  its whole fixed state. The file starts with a header (magic, format version, adapter and driver identity, size, hash) and the list of
  stored names, and is checked before D3D12 reads any of it; a truncated, damaged or foreign file, or one the runtime refuses, is
  ignored with a log line and the pipelines compile. It is written at destruction or by `Dx12Device::SavePipelineCache`, atomically,
  and only when it gained pipelines. Measured on the Radeon 860M: the 33 pipelines of the committed shaders take 12 ms with an empty
  library and 11 ms from it, because the driver keeps its own shader cache; a pipeline's own cached blob (`GetCachedBlob`) brought one
  G-buffer pipeline from 0.10 to 0.06 ms. The library is kept for drivers or machines without that cache; first-ever compiles (with the
  driver's cache empty) were not measured.

### The Metal backend

Metal is the native backend on macOS, library `rhi_metal` (Objective-C++ with ARC, the one place Metal is named: `rhi_boundary_check`),
registered by [[rhi::RegisterCompiledBackends]] as `metal` ([[rhi::RegisterMetalBackend]]). Ask for it with `DeviceCreateInfo::backend = rhi::Backend::Metal` or
`--rhi metal`; `Auto` picks it on macOS (rank 30). Its shaders are the minted Metal libraries
([[rhi::DeviceCaps::shader_format]] `MetalLib`, see [Shaders](rhi-shaders.md)), and it reads the object index from the renderer's index stream
([[rhi::DeviceCaps::object_index_stream]]): Metal's instance id leaves out the base instance.

- **Adapters.** Every `MTLDevice` of the system (`MTLCopyAllDevices`; enumeration creates no queue). An adapter's id is its registry ID as 16 lowercase hex digits, stable
  across runs; its driver is the macOS version. A device is integrated when its memory is unified or it is the low-power GPU, and suitable with the Mac2 family and Metal 3.
  The pick follows the other backends: `renderer.adapter` (or `ENGINE_RHI_GPU`) through [[rhi::MatchAdapter]], else a discrete GPU before an integrated one.
- **The device.** One `MTLCommandQueue` for all work. The fence is an `MTLSharedEvent` timeline: each submission signals the next value, frames in flight wait on the
  fence of the frame that reuses their slot, and `WaitFence`, `BeginFrame` and `WaitIdle` wait through a shared-event listener in 100 ms slices with a lost-device check
  between them, so a wait never outlives a lost device.
- **Validation is advisory.** Metal's API validation is an environment setting the framework reads when the process starts it, not something a device can switch on:
  run with `MTL_DEBUG_LAYER=1` (and `MTL_SHADER_VALIDATION=1`; `MTL_DEBUG_LAYER_ERROR_MODE=assert` stops at the first error). `renderer.validation` `on` without them
  logs a warning and carries on. The RHI GPU tests run with all three set, and `metal_validation_planted` shows that a validation error stops such a run.
- **Errors and loss.** Every command buffer gets a completion handler: a failed buffer is logged with its label and Metal's error code, and an error that leaves the
  device unusable (removed, access revoked, timeout, page fault, internal) marks it lost, as [[rhi::Device::DeviceLost]] reports. A device observer
  (`MTLCopyAllDevicesWithObserver`) does the same when the system removes the GPU or asks to (an external GPU unplugged). `InjectDeviceLoss` records a loss
  the same way, since Metal has no call that removes a device, and every later submission, upload, readback and present is refused; destroying a lost device
  does not wait. `Auto` picks Metal on macOS from
- **Capabilities.** 8 color attachments, 16384 for 2D textures, 256-byte constant-buffer offsets, no row-pitch alignment beyond the texel size for copies; the pixel
  formats are asked with the device's family (`D24S8` only where the GPU has it, BC formats where it decodes them), and the G-buffer formats all pass.
- **Memory.** GPU-only buffers and every texture are `Private`; readback memory (`GpuToCpu`) is `Shared`, so a persistent mapping sees the GPU's
  finished writes. CPU-written memory (`CpuToGpu`) is `Shared` on a GPU with unified memory (every Apple silicon Mac and Intel's integrated GPUs) and
  `Managed` otherwise: there the CPU's writes reach the GPU through `didModifyRange:`, which `FlushMappedBuffer`, `UnmapBuffer` and a `CpuToGpu`
  `UploadBuffer` send (so call `FlushMappedBuffer` for a buffer you keep mapped). The staging ring (64 MiB, `Shared`) feeds uploads. Metal copies
  between buffers in 4-byte units on macOS; the backend widens an unaligned range to the window around it (reading the window's edges back first for
  a write), so any offset and size work, at the cost of a wait for an unaligned write. Copies between buffers and textures use the caller's rows
  packed, since Metal needs no row pitch beyond the texel size ([[rhi::DeviceCaps::buffer_texture_copy_row_pitch_alignment]] 1). A unified Mac
  tests the `Managed` path through a test hook (`rhi::metal::ForceManagedStorageForTest`), which is all of its coverage until a Mac with a discrete
  GPU runs the suites.
- **Bindings.** Resources are bound directly (`setVertexBuffer:`, `setFragmentTexture:`, ... at the slots of each stage's minted slot
  table, [[rhi::ShaderSlotTable]]), not through argument buffers (the Iris Plus 645 tested here has argument-buffer tier 1). A
  pipeline matches each stage's table against its bind group layouts when it is created: a resource the shader binds that the layout of its set
  lacks, or that is not visible to the stage, or two resources at one slot, fail the creation with the reason. Groups hold their resources by
  handle and are resolved when they are bound, and a slot that already holds the resource gets no second call (a new encoder starts empty).
  Push constants are `setVertexBytes:`/`setFragmentBytes:` at the table's push-constant slot. Equal sampler descriptions share one Metal sampler;
  `ClampToBorder` uses Metal's border colors, which macOS has (iOS before 14 does not).
- **Pipelines.** A shader is an `MTLLibrary` made from the minted bytes and the function its blob names, with a copy of its slot table. A
  render pipeline puts vertex buffer binding `b` at buffer slot `30 - b` (the top of the table, clear of the shader's own buffers, which Slang
  numbers from 0; the creation fails if they would meet). Cull mode, winding, fill mode and the depth-stencil state are set on the encoder when
  the pipeline is bound, and equal depth-stencil descriptions share one state. A compute pipeline keeps the threadgroup size the shader
  declares (minted in its slot table, since a Metal dispatch passes it) and is refused when it exceeds the pipeline's
  `maxTotalThreadsPerThreadgroup`.
- **The pipeline cache** is an `MTLBinaryArchive`: a pipeline is looked up in it first (`MTLPipelineOptionFailOnBinaryArchiveMiss`), and a miss
  is compiled once, into the archive, and created from it. Every use of the archive holds one lock (Metal does not document a lookup beside an add
  as safe), so pipeline creation is serialized while a cache is open; `pipeline_cache = false` lifts that for a program that compiles from
  many jobs and keeps no cache. `Quiesce` and the device's destruction write it to `metal-<adapter id>.bin` in the cache directory
  ([[rhi::ResolvePipelineCacheDir]]), behind a header with the adapter's id and the macOS build and a hash of the archive. A file for another
  adapter or macOS, or one that is damaged, is discarded with a log line and the cache starts empty. Metal reads an archive only from a file,
  so the checked bytes are written beside the cache file for it (`.<process>-<device>.archive`, each device's own), and it serializes into a
  temporary file of the device's own that the backend reads back and writes atomically.
- **Command lists.** Each list records into its own retained `MTLCommandBuffer` (so a resource it uses stays alive until the GPU is done), so lists
  record on several jobs at once; `Submit` commits them in the order given, which is the order the queue runs them, and signals the timeline once
  after the last. A rendering pass is one render encoder; outside a pass, copies and mips share a blit encoder and dispatches a compute encoder,
  and the open one ends when the kind changes. The pipeline, the bind groups and the push constants are applied at draw and dispatch time, so a
  new encoder (which starts with nothing bound) gets them back; the viewport and scissor are set inside a pass (Metal keeps them on the render
  encoder), the scissor clipped to the attachments.
- **Barriers.** Metal tracks each resource's use and orders the encoders of a command buffer, so a `Barrier` is nothing between encoders; after a
  dispatch whose writes the next dispatch in the same compute encoder reads (`before` is `ShaderWrite`), it is a `memoryBarrierWithScope:`.
- **Draws.** `first_instance` is Metal's `baseInstance` and `vertex_offset` its `baseVertex`; strips always restart at the all-ones index on Metal,
  which is what `primitive_restart` asks for (a strip without restart must not use that index). `CopyBuffer` needs offsets and a size that are
  multiples of 4 (Metal's rule on macOS). `GenerateMips` is a blit encoder's `generateMipmapsForTexture:`, no compute shader. Debug labels are
  encoder debug groups, closed at an encoder's end and opened again on the next.
- **Presentation.** The swapchain is the Cocoa window's own `CAMetalLayer` ([[os::NativeWindowInfo]]'s `surface`): `Acquire` is `nextDrawable`,
  which waits at most the layer's one-second timeout (a timeout, or a minimized window, is `Minimized`), and `Present` encodes `presentDrawable:`
  in a command buffer of its own, after the frame's lists, then lets the drawable go: one texture handle follows the current drawable and is empty
  between frames. `Fifo` is `displaySyncEnabled`, `Immediate` turns it off, and `Mailbox` (which macOS does not have) falls back to `Fifo`. The
  formats are `bgra8_srgb` (the default) and `bgra8_unorm`; `framebufferOnly` is off so an image can be copied (screenshots). HDR
  (`ScRgbLinear`: `rgba16_float`, extended linear sRGB, `wantsExtendedDynamicRangeContent`) is granted only on a screen whose potential EDR value
  is above 1. A window resize changes `drawableSize` at the next `Acquire`, which then says `Recreated`; `RecreateSurface` takes the window's
  layer as it is when the swapchain is made again.

### What the RHI reports when something goes wrong

The Vulkan device registers the adapter, its driver version and the API version as the `gpu` line of every crash report. A lost
device (real, or injected with `--inject-device-loss`) and the first validation error each print a trace of where they were
noticed, through an `ENGINE_ENSURE` that reports once per site; the frame still ends as `DeviceLost`. The traces use the same
mechanism as any ensure and need no Vulkan names outside `engine/rhi/vulkan`. To add device state to the report, call
[[debug::SetCrashStateText]] from the backend.

### See also

- [Shaders: from Slang to a shader module](rhi-shaders.md) - writing and building shaders.
- [Tutorial: From a Cleared Window to a Lit Cube](../tutorials/hello-rhi.md).
- [Building and Testing](building-and-testing.md) - GPU tests, golden images, sanitizers and the boundary gate.
- [Open a Window](../tutorials/hello-window.md) and [Windowing and input](platform-windowing-and-input.md) - the platform
  layer under the swapchain.
- `docs/superpowers/specs/2026-11-04-rhi-design.md` and `2026-11-04-rhi-portability-review.md` - why it is shaped this way.
