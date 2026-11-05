# Phase 4 / Day 80a — Foundation `Array` & `Vector` (no raw `new[]`/`delete[]`)

**Phase:** [Phase 4 — Renderer Core](../phase-04-renderer-core.md)
**Depends on:** Day 80. Inserted before Day 81 (point lights), which then builds on the new containers.

A search for `delete[]` found ~60 hand-paired `new T[n]`/`delete[] p` sites in `renderer`, `rhi`, `rhi_vulkan` and `jobs`. They are correct today but easy to get wrong (every early return must free). Policy: PLAN.md guiding principle "Owning arrays are foundation containers".

**Naming decision (user, 2026-11-05):** `Array<T>` is a constant-size memory block; the growable container is `Vector<T>`. The existing growable `containers/array.h` type (Phase 0 Day 6, ~200 uses) is therefore **renamed `Vector`**, and a new fixed-size `Array` is added. Older day files keep their historical wording.

**Settled with the user (2026-11-05):** `Array<T, Allocator = HeapAllocator>` takes an allocator parameter (default: heap). The rename is a separate first commit. `Vector` keeps its allocator behavior unchanged (no free-on-growth; not needed by this day). Out-of-memory: `Array` has an aborting `Allocate(n)` and a `bool TryAllocate(n)` for sites that report errors today. Non-movable element types are supported by `Array` (it never moves elements).

**Original open points (now answered above):**
- Does `Array<T>` take an allocator, or default to the heap? (Proposal: `Array<T, Allocator = HeapAllocator>`, size given at construction, freed by the destructor.)
- A `HeapAllocator` with `Free` is needed (no engine allocator frees a single block). Should `Vector` free old blocks on growth when its allocator has `Free`?
- Out-of-memory: `Array` construction aborts, or offers a `bool Allocate(n)` form for the sites that return an error today (mesh creation, material parameter buffers)? (Proposal: a `bool Allocate(n)` plus an aborting constructor.)
- Non-movable element types (the scheduler's lanes hold atomics) must be supported by `Array`, which never moves its elements.

## Tasks

- [x] Settle the open design points above with the user and record the answers here.
- [x] Rename the growable container to `Vector` (`vector.h`, `Vector<T, Allocator>`; every user in `engine/`, `tools/`, `tests/`, `docs/guide/`; `docs/reference/modules.txt`), with no behavior change; the full ctest stays green before anything else changes.
- [ ] Add `HeapAllocator` and the fixed-size `Array<T>` (`array.h`): construction/allocation, `operator[]`, `Size()`, `Data()`, move-only, destructor frees; elements value-initialized.
- [ ] Foundation tests: size/zero-init, destruction of non-trivial elements, move, non-movable elements, allocation failure for an absurd size, leak-free under the ASan flavor.
- [ ] Replace every raw array in `engine/` (`mesh.cpp`, `gbuffer.cpp`, `material.cpp`, `transient_pool.cpp`, `render_scene.cpp`, `vulkan_device_bindings.cpp`, `vulkan_device_pipelines.cpp`, `vulkan_device.cpp`, `vk_instance.cpp`, `jobs/scheduler.cpp`); existing out-of-memory error paths keep their behavior.
- [ ] Source-scan test (`ctest`): no `new T[` / `new (std::nothrow) T[` / `delete[]` under `engine/` outside `engine/foundation/mem/`.
- [ ] **Documentation:** `///` comments on the new/renamed API, `docgen lint` at zero findings, `docs/reference/` regenerated and committed, `docs/guide/foundation-containers.md` covers both types with a tagged compiled sample, `ctest -L docs` passes.
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high`, record under "Post-implementation code review", mention in `STATUS.md`.
- [ ] Update `STATUS.md`; commit and push. (This day may be split into a rename day and a conversion day if the rename diff is large.)

**Done:** `grep -rn "delete\[\]" engine` is empty (outside the allocators), the source-scan test enforces it, the full ctest (plain, ASan, TSan flavors) passes, and the docs pages are named in the commit.
