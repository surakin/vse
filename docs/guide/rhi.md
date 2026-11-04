# The Render Hardware Interface (RHI)

> **Status:** skeleton — not yet written  
> **Audience:** engine and renderer programmers  
> **Covers:** Phase 3

## Source of truth

- `engine/rhi/*.h`
- `engine/rhi/vulkan/` (backend internals)
- `docs/superpowers/specs/2026-11-04-rhi-design.md` (design decisions D1-D8)
- `plans/phase-03-vulkan-rhi.md`

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
- [[rhi::vulkan::VulkanDevice]] — the Vulkan device
- [[rhi::vulkan::SelectPhysicalDevice]] and [[rhi::vulkan::PlanQueues]] — GPU choice and queue planning
- [[rhi::vulkan::MemoryAllocator]] — the GPU memory suballocator, with [[rhi::vulkan::MemoryStats]]
- [[rhi::Device::UploadBuffer]], [[rhi::UploadTicket]] and [[rhi::Device::ReadbackBuffer]] — moving bytes to and from buffers
- [[rhi::vulkan::RingAllocator]] — the staging ring behind uploads
- [[rhi::vulkan::LoadVulkanLoader]] — opens the Vulkan loader at runtime
- [[rhi::vulkan::VkGlobalFns]], [[rhi::vulkan::VkInstanceFns]], [[rhi::vulkan::VkDeviceFns]] — the function-pointer tables
- [[rhi::vulkan::VulkanInstance]] and [[rhi::vulkan::VulkanInstanceDesc]] — the instance, its extensions and validation
- [[rhi::vulkan::GetValidationCounts]] and [[rhi::vulkan::SetValidationCapture]] — validation message counters and capture hook
- [[rhi::vulkan::VkResultName]], [[rhi::vulkan::ClassifyVkResult]] — readable and classified `VkResult`s

## Already known — seed material (verify against the code when writing)

- All GPU resources are opaque generation-checked handles owned by the device; a stale handle is detected, never dereferenced
- Creation failure returns an invalid handle and logs on `LogRhi`; programming errors are `ENGINE_ASSERT`; device loss is a sticky queryable state
- `Destroy*` is deferred until the GPU finishes the frames that could use the resource
- Synchronization is explicit: `Barrier(resource, before, after)` in portable `ResourceState`s; fences are monotonically increasing timeline values
- Binding model: bind group layouts, bind groups and push constants; pipelines use dynamic rendering (no render-pass objects)
- Command lists record on any thread; submission is serialized on the device
- Vulkan 1.3 baseline, loaded at runtime from `libvulkan.so.1`; validation layers are dev-time only (`ENGINE_RHI_VALIDATION`)
- GPU tests are tagged `[gpu]` and skip without a device (`ENGINE_RHI_REQUIRE_GPU=1` turns a skip into a failure)

## Outline

### Overview

_TODO_

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

using namespace engine_placeholder::rhi;

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

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;

int main() {
    LogRhi.SetEnabled(false);  // keep the expected error out of the output
    NullDevice* device = CreateNullDevice();
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
    DestroyDevice(device);
}
```

```output
errors: 1
```

### Rules & contracts

_TODO_

### Common mistakes

_TODO_

### Platform notes

_TODO_

### See also

_TODO_

### Creating a device

[[rhi::CreateDevice]] takes a [[rhi::DeviceCreateInfo]] and returns a device for the requested [[rhi::Backend]]
(`Null` or `Vulkan`), or null with the reason logged. The device is headless; a presentation window is added
with the swapchain.

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
begin and end, so the application can shut down cleanly. Tests exercise this with
`VulkanDevice::InjectSubmitFailure`.

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

The backend never links against Vulkan. [[rhi::vulkan::LoadVulkanLoader]] opens `libvulkan.so.1` at runtime
(or the path in `ENGINE_VULKAN_LIBRARY`, which tests use to simulate a machine without Vulkan) and
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
| `ENGINE_VULKAN_LIBRARY` | Path of the Vulkan loader library to use instead of `libvulkan.so.1`. |

Every validation message is logged on `LogRhi` with its severity (`ERROR` becomes `Error`, `WARNING` `Warn`,
`INFO` `Debug`, `VERBOSE` `Trace`) and its message id, and errors and warnings are counted:
[[rhi::vulkan::GetValidationCounts]] reads the process-wide totals and [[rhi::vulkan::SetValidationCapture]]
installs a hook that sees each message. Log lines are truncated at 512 bytes; the capture hook gets the full text.

Tests do not read the counters by hand. Every `[gpu]` test constructs `gpu_test::GpuFixture`
(`tests/rhi/gpu_fixture.h`) and calls `Require()`: it skips the test when there is no Vulkan (or fails it under
`ENGINE_RHI_REQUIRE_GPU=1`), owns a validation-enabled instance with synchronization validation on, and fails the
test at teardown if any validation error was reported. A test that provokes an error on purpose declares it with
`ExpectErrors(n)`.
