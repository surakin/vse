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
