# Phase 4 / Day 80a — Foundation Owning Arrays (no raw `new[]`/`delete[]`)

**Phase:** [Phase 4 — Renderer Core](../phase-04-renderer-core.md)
**Depends on:** Day 80. Inserted before Day 81 (point lights), which then builds on the new containers.

A search for `delete[]` found ~60 hand-paired `new T[n]`/`delete[] p` sites in `renderer`, `rhi`, `rhi_vulkan` and `jobs`. They are correct today but easy to get wrong (every early return must free). The foundation already has `Array`; this day makes it able to own heap storage and replaces every raw array. Policy: PLAN.md guiding principle "Owning arrays are foundation `Array`s".

## Tasks

- [ ] `HeapAllocator` in `engine/foundation/mem/`: `Alloc(size, align)` (nullptr on failure) and `Free(ptr, size)`, backed by libc aligned allocation; stateless, with a shared `HeapAllocator::Default()`.
- [ ] `Array` extensions, keeping the arena behavior unchanged: frees the old block on growth and in the destructor when the allocator has `Free`; `Resize(n)` / `TryResize(n)` (value-initializing new elements, shrinking destroys; `TryResize` returns false instead of aborting); `Data()`; `Clear()`; non-movable element types (atomics) supported when never grown past their first `Resize`; `HeapArray<T>` alias with a default constructor.
- [ ] Foundation tests: heap growth frees old blocks (leak-free under the ASan flavor), `Resize` value-initialization and shrink, `TryResize` failure on an absurd size, non-movable elements, move-assign frees the old storage, arena behavior unchanged.
- [ ] Replace every raw array in `engine/` (`mesh.cpp`, `gbuffer.cpp`, `material.cpp`, `transient_pool.cpp`, `render_scene.cpp`, `vulkan_device_bindings.cpp`, `vulkan_device_pipelines.cpp`, `vulkan_device.cpp`, `vk_instance.cpp`, `jobs/scheduler.cpp`); existing out-of-memory error paths keep their behavior (`TryResize`).
- [ ] Source-scan test (`ctest`): no `new T[` / `new (std::nothrow) T[` / `delete[]` under `engine/` outside `engine/foundation/mem/`.
- [ ] **Documentation:** `///` comments on the new API, `docgen lint` at zero findings, `docs/reference/` regenerated and committed, `docs/guide/foundation-containers.md` updated with a tagged compiled sample, `ctest -L docs` passes.
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high`, record under "Post-implementation code review", mention in `STATUS.md`.
- [ ] Update `STATUS.md`; commit and push.

**Done:** `grep -rn "delete\[\]" engine` is empty (outside the allocators), the source-scan test enforces it, the full ctest (plain, ASan, TSan flavors) passes, and the docs pages are named in the commit.
