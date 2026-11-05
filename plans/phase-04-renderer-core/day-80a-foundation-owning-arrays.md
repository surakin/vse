# Phase 4 / Day 80a — Foundation `Array` & `Vector` (no raw `new[]`/`delete[]`)

**Phase:** [Phase 4 — Renderer Core](../phase-04-renderer-core.md)
**Depends on:** Day 80. Inserted before Day 81 (point lights), which then builds on the new containers.

A search for `delete[]` found ~60 hand-paired `new T[n]`/`delete[] p` sites in `renderer`, `rhi`, `rhi_vulkan` and `jobs`. They are correct today but easy to get wrong (every early return must free). Policy: PLAN.md guiding principle "Owning arrays are foundation containers".

**Decision (user, 2026-11-05):** `Array<T, N>` is `std::array`-like: compile-time size, always inline storage. Every other case (runtime size, growth) is `Vector<T>`; there is no runtime-sized fixed type. The existing growable `containers/array.h` type (Phase 0 Day 6, ~200 uses) is **renamed `Vector`** (done, step 1) and gains the ability to own heap storage: `Vector<T, Allocator = HeapAllocator>` with a `HeapAllocator` that can `Free`; when the allocator has `Free`, `Vector` frees its old block on growth and in the destructor (arena behavior unchanged); `Resize`/`TryResize` (value-initialize, `TryResize` returns false instead of aborting for the sites that report out-of-memory today), `Data()`, `Clear()`, a default constructor when the allocator has `Default()`; non-movable element types (atomics) are supported while never grown past their first `Resize`. The raw `new[]` sites become `Vector`. `rhi::detail::GrowArray` (RHI-private heap vector) is left as is.

## Tasks

- [x] Settle the design with the user and record it above.
- [x] Rename the growable container to `Vector` (step 1, commit 64193d8).
- [ ] `HeapAllocator` (`mem/heap_allocator.h`) and the `Vector` extensions above, with foundation tests (heap growth frees old blocks, `Resize` zero-init and shrink, `TryResize` failure, non-movable elements, move-assign frees, arena behavior unchanged).
- [ ] New `Array<T, N>` (`array.h`): aggregate with inline storage, `operator[]`, `Data()`, `Size()`, iteration; tests.
- [ ] Replace every raw array in `engine/` with `Vector` (`mesh.cpp`, `gbuffer.cpp`, `material.cpp`, `transient_pool.cpp`, `render_scene.cpp`, `vulkan_device_bindings.cpp`, `vulkan_device_pipelines.cpp`, `vulkan_device.cpp`, `vk_instance.cpp`, `jobs/scheduler.cpp`); out-of-memory error paths keep their behavior.
- [ ] Source-scan test (`ctest`): no `new T[` / `new (std::nothrow) T[` / `delete[]` under `engine/` outside `engine/foundation/mem/`.
- [ ] **Documentation:** `///` comments on the new/renamed API, `docgen lint` at zero findings, `docs/reference/` regenerated and committed, `docs/guide/foundation-containers.md` covers both types with a tagged compiled sample, `ctest -L docs` passes.
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high`, record under "Post-implementation code review", mention in `STATUS.md`.
- [ ] Update `STATUS.md`; commit and push. (This day may be split into a rename day and a conversion day if the rename diff is large.)

**Done:** `grep -rn "delete\[\]" engine` is empty (outside the allocators), the source-scan test enforces it, the full ctest (plain, ASan, TSan flavors) passes, and the docs pages are named in the commit.
