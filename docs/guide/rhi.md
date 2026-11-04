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

_TODO_

### Devices, command lists and lifetimes

_TODO_

### Rules & contracts

_TODO_

### Common mistakes

_TODO_

### Platform notes

_TODO_

### See also

_TODO_
