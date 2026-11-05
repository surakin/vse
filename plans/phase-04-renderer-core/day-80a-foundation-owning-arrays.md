# Phase 4 / Day 80a — Foundation `Array` & `Vector` (no raw `new[]`/`delete[]`)

**Phase:** [Phase 4 — Renderer Core](../phase-04-renderer-core.md)
**Depends on:** Day 80. Inserted before Day 81 (point lights), which then builds on the new containers.

A search for `delete[]` found ~60 hand-paired `new T[n]`/`delete[] p` sites in `renderer`, `rhi`, `rhi_vulkan` and `jobs`. They are correct today but easy to get wrong (every early return must free). Policy: PLAN.md guiding principle "Owning arrays are foundation containers".

**Decision (user, 2026-11-05):** `Array<T, N>` is `std::array`-like: compile-time size, always inline storage. Every other case (runtime size, growth) is `Vector<T>`; there is no runtime-sized fixed type. The existing growable `containers/array.h` type (Phase 0 Day 6, ~200 uses) is **renamed `Vector`** (done, step 1) and gains the ability to own heap storage: `Vector<T, Allocator = HeapAllocator>` with a `HeapAllocator` that can `Free`; when the allocator has `Free`, `Vector` frees its old block on growth and in the destructor (arena behavior unchanged); `Resize`/`TryResize` (value-initialize, `TryResize` returns false instead of aborting for the sites that report out-of-memory today), `Data()`, `Clear()`, a default constructor when the allocator has `Default()`; non-movable element types (atomics) are supported while never grown past their first `Resize`. The raw `new[]` sites become `Vector`. `rhi::detail::GrowArray` (RHI-private heap vector) is left as is.

## Tasks

- [x] Settle the design with the user and record it above.
- [x] Rename the growable container to `Vector` (step 1, commit 64193d8).
- [x] `HeapAllocator` (`mem/heap_allocator.h`) and the `Vector` extensions above, with foundation tests (heap growth frees old blocks, `Resize` zero-init and shrink, `TryResize` failure, non-movable elements, move-assign frees, arena behavior unchanged).
- [x] New `Array<T, N>` (`array.h`): aggregate with inline storage, `operator[]`, `Data()`, `Size()`, iteration; tests.
- [x] Replace every raw array in `engine/` with `Vector` (`mesh.cpp`, `gbuffer.cpp`, `material.cpp`, `transient_pool.cpp`, `render_scene.cpp`, `vulkan_device_bindings.cpp`, `vulkan_device_pipelines.cpp`, `vulkan_device.cpp`, `vk_instance.cpp`, `jobs/scheduler.cpp`); out-of-memory error paths keep their behavior.
- [x] Source-scan test (`ctest`): no `new T[` / `new (std::nothrow) T[` / `delete[]` under `engine/` outside `engine/foundation/mem/`.
- [x] **Documentation:** `///` comments on the new/renamed API, `docgen lint` at zero findings, `docs/reference/` regenerated and committed, `docs/guide/foundation-containers.md` covers both types with a tagged compiled sample, `ctest -L docs` passes.
- [x] **End-of-day review (CLAUDE.md):** `/code-review high`, record under "Post-implementation code review", mention in `STATUS.md`.
- [x] Update `STATUS.md`; commit and push. (This day may be split into a rename day and a conversion day if the rename diff is large.)

**Done:** `grep -rn "delete\[\]" engine` is empty (outside the allocators), the source-scan test enforces it, the full ctest (plain, ASan, TSan flavors) passes, and the docs pages are named in the commit.

## Result

- `Vector<T, Allocator = HeapAllocator>` owns heap storage (frees on growth and destruction when the allocator has `Free`), with `Resize`/`TryResize`/`TryResizeUninitialized`, `TryReserve`, `Clear`, `Reset`, `Data()`; `HeapAllocator` (aligned `operator new`, at least 16-byte aligned); `Array<T, N>` (inline, aggregate, unchecked index like `Vector`).
- All 50 raw array sites in `engine/` now use `Vector` (`mesh`, `gbuffer`, `material`, `transient_pool`, `render_scene`, the Vulkan bindings/pipelines/device/instance, `jobs/scheduler`). `ctest -L arrays` (`raw_array_check`, planted-violation and clean-tree fixtures) enforces the rule on `engine/` and `game/`.
- `BindGroupRes.refs` is a `Vector`; the group is moved into the handle pool, so the manual `delete[] refs` on the out-of-handles path is gone.
- A latent test-helper gap surfaced under ASan: `tests/renderer/render_graph_test.cpp` replaced every `operator new/delete` form except the aligned nothrow pair, so `HeapAllocator` blocks were allocated by ASan and freed by `free()`. The missing overloads were added.
- Full ctest (plain, ASan, TSan): 1741 pass. Docs 100%, guide section "Array and Vector: which one".
- Left as is: `rhi::detail::GrowArray` (RHI-private heap vector, trivially copyable values, 12 uses) and the single-object `new`/`delete` sites (`new WorkerPool`, `new ShaderReflection`); both are in `KNOWN-ISSUES.md`.

## Post-implementation code review

`/code-review high`: 8 findings.

- **Fixed:** `TryResize` zero-filled buffers that are immediately overwritten (the pipeline-cache file read, its save, and `MeshData::Set`'s vertex/index copies), where `new unsigned char[n]` did not touch the memory. Added `TryResizeUninitialized` (trivial element types only, enforced by `static_assert`) and used it at those three sites. Test.
- **Fixed:** `TryResize` grew to exactly `new_size`, making repeated `Resize(Size() + k)` quadratic. An empty vector still gets exactly `new_size` (a table sized once); one holding elements grows at least geometrically. Test counts the reallocations.
- **Fixed:** `TryReserve`/`TryResize` on a non-movable `T` that already held elements allocated a block and then aborted, contradicting their "reports instead of aborting" contract and leaking the block. The check now comes before the allocation and returns false. Test.
- **Fixed:** `GBufferPass` inferred allocation success from `draws_.Data() == nullptr`, which is also what a successful zero-size resize gives; it now has an explicit `storage_ok_`.
- **Fixed:** `Array::operator[]` was bounds-checked in every build while `Vector::operator[]` is not, so the container choice changed release cost. `Array` is unchecked like `Vector`, documented.
- **Fixed:** the gate's comment stripping treated `//` inside a string literal as a comment (a `new T[n]` after `"http://..."` was missed) and left a one-line `/* ... */` unstripped. String literals are blanked first and one-line block comments removed; fixtures cover both.
- **Declined:** `HeapAllocator` depends on `::operator new` and so on any program-wide replacement. That is intended: tests that replace `operator new` to count heap allocations see `HeapAllocator` traffic, which is what they want. The one gap this exposed (the aligned nothrow overloads) was fixed in the test helper. Calling `aligned_alloc` directly would hide the allocations from those counters.
- **Declined (recorded in `KNOWN-ISSUES.md`):** the gate walks only `engine/` and `game/` and is line-based, so `tests/` and `tools/` (which may use the STL) are not covered and a `new` split from its `[` across lines is missed.
