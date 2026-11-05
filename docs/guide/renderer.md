# The Renderer

> **Status:** skeleton — not yet written  
> **Audience:** engine and game programmers  
> **Covers:** Phase 4

## Source of truth

- `engine/renderer/*.h`
- `docs/superpowers/specs/2026-11-04-renderer-design.md` (decisions R1-R11)
- `plans/phase-04-renderer-core.md`

## API reference

Full reference: [The renderer](../reference/renderer.md).

- [[renderer::RenderLogV]] — logging on the render channel
- [[renderer::RendererName]] — identification of the library
- [[renderer::ShaderReflection]] — what a compiled shader says about itself (stage, bindings, block layouts, vertex inputs)
- [[renderer::BuildBindGroupLayout]] — a bind group layout from reflection
- [[renderer::BuildVertexLayout]] — a vertex layout from reflection
- [[renderer::ShaderPermutations]] — the key-to-module table of a permuted shader
- [[renderer::FindPermutation]] — choose a shader variant by key
- [[renderer::Camera]] — position, orientation and lens; view and projection matrices
- [[renderer::Frustum]] — the six planes of a view-projection, with sphere and box tests
- [[renderer::MeshData]] — CPU geometry with bounds; [[renderer::MakeCubeMesh]] and friends build shapes
- [[renderer::GpuMesh]] — a mesh's vertex and index buffers
- [[renderer::RenderScene]] — objects and lights with generation-checked ids, and [[renderer::RenderScene::Cull]]
- [[renderer::FrameResources]] — the per-frame and per-object buffers shaders read

## Outline

### Overview

_TODO_

Shader reflection (Phase 4 Day 71) is described in [Shaders](rhi-shaders.md#reflection-shaders-describe-themselves), and shader variants (Day 72) in [Shaders](rhi-shaders.md#permutations-one-source-several-modules).

### Meshes, cameras and the scene

A [[renderer::RenderScene]] is a fixed-capacity list of objects (a `GpuMesh`, a material index, a world matrix) and lights.
`AddObject` returns an id that carries a generation, so an id outliving its object is detected rather than reused.
`Cull` fills a caller-provided array with the slots of the objects that are not hidden and whose world-space bounds touch
the camera's [[renderer::Frustum]]; the array must hold `SlotCount()` entries. `FrameResources::WriteObjects` then packs
the world and normal matrices of exactly those slots into one buffer, and a draw picks its entry with `first_instance`.

The conventions are right-handed, Y up, camera looking down -Z, depth 0 at the near plane and 1 at the far plane.

```cpp compile standalone run
#include <cstdio>

#include "renderer/camera.h"
#include "renderer/render_scene.h"
#include "rhi/null/null_device.h"

using namespace engine_placeholder;
using namespace engine_placeholder::renderer;

int main() {
    rhi::NullDevice* device = rhi::CreateNullDevice();

    {
        MeshData data;
        if (MakeCubeMesh(1.0f, &data) != nullptr) return 1;
        GpuMesh mesh;
        if (!mesh.Create(device, data, "cube")) return 1;

        RenderScene scene(RenderSceneDesc{});
        RenderObjectDesc front;
        front.mesh = &mesh;
        front.transform = ModelMatrix(Vec3(0, 0, -10), 1.0f, 0.0f);
        RenderObjectDesc behind = front;
        behind.transform = ModelMatrix(Vec3(0, 0, 10), 1.0f, 0.0f);
        scene.AddObject(front);
        scene.AddObject(behind);

        Camera camera = MakeLookAtCamera(Vec3(0, 0, 0), Vec3(0, 0, -1), Vec3(0, 1, 0));
        u32 visible[2];
        u32 count = scene.Cull(Frustum::FromViewProjection(camera.ViewProjection()), visible, 2);
        std::printf("%u of %u objects visible\n", count, scene.ObjectCount());
    } // the mesh and scene go before the device

    rhi::DestroyDevice(device);
    return 0;
}
```

```output
1 of 2 objects visible
```

### Rules & contracts

- A `GpuMesh` must be destroyed (or go out of scope) before the device that created it; a scene holds meshes by pointer and
  does not own them, so a mesh must also outlive every object that uses it.

_TODO_

### Common mistakes

_TODO_

### Platform notes

_TODO_

### See also

- [The RHI](rhi.md) and the [renderer design](../superpowers/specs/2026-11-04-renderer-design.md).
