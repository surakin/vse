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
