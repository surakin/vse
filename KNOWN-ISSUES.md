# Known issues

Failing or flaky tests and deferred items, in one place. `STATUS.md` says where the plan is; this file says what is
known to be wrong or unfinished. Remove an entry when it is fixed (the commit that fixes it removes it), and add one
whenever a day leaves something broken, unmeasured or deferred. Entries state facts that were observed, with where. A flaky or failing test names the phase it belongs to (its `phaseN` label), because a scoped run (`scripts/test-phase.sh`) of another phase does not run it.

## Failing tests

| Test | Since | Cause | Status |
|---|---|---|---|
| (none) | | | |

## Flaky tests

Seen failing once or intermittently, passing on rerun. Cause not investigated.

- Phase 5 Day 85 (2026-11-06): `X11Window::Create opens a mapped window on the real X server` failed consistently (alone and under `ctest`; `foundation_tests` exit 42) early in the session and passed in every run later that day (1843/1843 on Day 86). Cause not investigated; the earlier Day 69 and Day 44 notes record the same test failing under Xwayland, and a locked session breaks window tests.
- The window tests (X11, Wayland, and the RHI presentation tests) used to fail at random under `ctest -j6`; they now hold the ctest resource `display` and run one at a time (5 full runs in a row passed, 2026-11-05).


## Unverified by a human

Automated checks pass; nobody has looked at the real thing yet (from `STATUS.md`, Phases 1-2).

- Visual confirmation of the Phase 1 windows, live keyboard and mouse updates, and the real close-button exit on Wayland and X11 (Days 19-26).
- `fr`/`de` keyboard layouts through a live X server (xkbcommon tests cover them).
- Phase 4 Day 70: vsync-paced `rhi_demo` frame time was 50 ms on Wayland and 17 ms on X11 against 4.1 ms in Phase 3 in that session. The cause is believed to be compositor pacing (negligible shader cost with `--no-vsync`) but it was not re-measured after a session restart.
- Phase 4 Day 82 (2026-11-05): the demo's interactive controls (orbit, fly with Tab and WASD, mouse drag, wheel) were not exercised with real input; only the offscreen screenshot, the bounded runs (`--frames 1000`, `--validation`) and the 16 `demo` ctests were run.
- Phase 4 Day 82: the demo golden (`tests/rhi/golden/renderer_demo.ppm`) was blessed on an NVIDIA GeForce RTX 3060 only; the tolerance (6 levels, wider on edges) is untested on other GPUs and drivers, and so is the exact draw-order independence.
- Phase 4 Day 78 (2026-11-05): in repeated full `ctest -j8` runs, one run had 1 failure and two had X11Window tests fail (`translates key press/release into Key via the server's keymap`, `releases keys and modifiers on a real focus loss but not during WM grabs`, `releases the Key a code was pressed as even if modifiers changed`); other runs passed 1643/1643 and 1649/1649. No code those tests exercise changed that day; not investigated. Seen again on Phase 4 Day 84 (2026-11-05): one `ctest -j16` run failed `X11Window maps event state bits to engine modifiers` (modifiers 0, expected 15) and `releases keys and modifiers on a real focus loss...`; the next full run failed `translates key press/release into Key via the server's keymap`; the third run passed 1839/1839, as did 1832/1832 on Day 83, and the two tests passed 6 of 6 times rerun alone.

## Deferred items

| Item | Needed by | Where recorded |
|---|---|---|
| Pipeline creation time for permuted shaders was not measured; specialization constants are the Vulkan-only escape hatch if profiling asks for it | Only if profiling shows a problem | `docs/superpowers/specs/2026-11-04-renderer-design.md` R10.1 |
| `shader_mint --permute` starts `slangc` with `posix_spawnp` (Linux only) | Phase 8 (Windows) | `shaders/README.md`, Day 72 day file |
| Clip-space Y flip in the shader path (Slang does not apply it) | Phases 8-9 | `shaders/README.md`, RHI portability review |
| Metal renames an entry point called `main` | Phase 9 | `shaders/README.md` |
| `MaterialSystem::GetPipeline` scans the pipeline cache linearly and `FlushMaterials` walks every material slot up to the highest used; neither was measured with a real scene | Day 80-81 (when a frame's real call counts are known) | Day 74 day file |
| `ListToStrip` looks up shared edges by binary search over equal keys without removing used triangles, so heavily non-manifold input (many triangles on one directed edge) can take quadratic time; it is a load-time helper | Only if a real mesh shows it | Day 78 review |
| `GBufferPass::Prepare` calls `MaterialSystem::GetPipeline` (a linear scan of the pipeline cache) for every visible object every frame; cache per (material, mesh layout) if profiling shows it. Day 83 profile of a 10000-object frame (3 pipelines): `ReadsVertexInput` 5.7% + `GetPipeline` 4.8% of CPU time, not a defect at that size | Only if scenes grow past 100000 objects or the pipeline count grows | Day 83 day file |
| The failure path of a bind-group rebuild in `FlushMaterials` (slots revert to what the groups show) has no test: it needs RHI fault injection | Day 77 | Day 74 day file |
| The per-object index comes from `SV_VulkanInstanceID` (`gl_InstanceIndex`); D3D12 and Metal shaders cannot see a draw's base instance that way, so those backends need another delivery (root constant, per-draw vertex stream, or `SV_StartInstanceLocation` where it exists) | Phases 8-9 | `shaders/README.md` "Material shaders" |
| Transient resources are pooled by description but their memory is not aliased (two non-overlapping resources of different sizes or formats each keep their own allocation); the plan marks aliasing optional | Only if frame memory becomes a problem (Phase 4 Days 80-83 will show) | Day 76 day file |
| A pass records into one command list: splitting one pass's draw list over several lists (the plan's "slices of the draw list") needs the RHI to suspend and resume a rendering pass or secondary command lists, which it does not have | Only if the G-buffer pass becomes CPU-bound (Day 79/83 profiling) | Day 77 day file |
| Metal cannot run `fill.comp` and `saxpy.comp` (`RWStructuredBuffer.GetDimensions` is unavailable in MSL); `shaders/portability.cmake` lists them with the reason | Phase 9 | Day 84 day file, renderer portability review |
| The HLSL and Metal per-object index: D3D12 shaders have no base instance, and Metal's `[[instance_id]]` excludes it (`[[base_instance]]` must be added); every material vertex shader reads `SV_VulkanInstanceID` today | Phases 8-9 | Day 84 day file, renderer portability review |
| Other items filed per backend (typeless depth, descriptor copies, `ClampToBorder`, ...) | Phases 8-9 | RHI portability review, section 5 |
| Metal backend behavior on Apple Silicon (unified memory, tile-based rendering, memoryless attachments) cannot be verified on the developer's 2019 Intel MacBook Pro | Phase 9 (record), Phase 17 (iOS device) | `plans/phase-09-macos-metal.md` |
| How Phases 10-16 keep the Windows/macOS builds working (per-phase closing day vs periodic catch-up) | Decide when Phase 9 completes | `PLAN.md` assumption #11 |
| Epic's UE EULA terms need review | Before Phase 19 starts | `PLAN.md` (open risk) |

- Phase 4 Day 81: the point-light scale numbers (1 / 100 / 1000 lights at 1080p) are CPU wall time from submit to idle; GPU-only time was not measured because the RHI has no timestamp queries yet.
- Phase 4 Day 84: the optional DXIL step of `shaders_portability` (`dxc`) has not produced a usable result. The `dxc` installed on the development machine (Arch package, 1.10.2605.37) aborts with `assert(HasHungOffUses ...)` inside LLVM on almost every shader, including a one-input vertex shader written by hand; only a shader with no inputs compiled. To be retried with another `dxc` build or on Windows. A working `dxc` is expected to reject the vertex shaders that read `SV_VulkanInstanceID`; the test reports rejections as notes, not failures. The Metal compiler step is not run (needs macOS). The renderer demo on both display backends was run live on 2026-11-05 (Wayland and X11, 1000 frames each, validation and sync validation silent); a locked session breaks that check.
- Phase 4 Day 83: the real-device (Vulkan) device-loss injection fires at the start of a frame only, so losing the device in the middle of a frame (in the callback, in a pass) is tested on the null device (`tests/renderer/renderer_fault_test.cpp`) but not on the real backend.
- Phase 4 Day 83: `perf record` here is limited to user-space sampling without call graphs (`kernel.perf_event_paranoid=2`; `-g` and kernel events fail to open), so the 10000-object profile is a flat symbol list. The `renderer_bench` numbers come from one machine (RTX 3060, Ryzen 9 7950X3D).
- Phase 4 Day 82: the demo has no roughness map: the G-buffer fragment shader has no roughness texture input (permutations: normal map, alpha test, vertex color). Roughness and metallic come from material parameters only. Adding a texture input means a shader permutation and a material-layout change.
- `rhi::detail::GrowArray` (`engine/rhi/grow_array.h`, 12 uses in `vk_memory`, `vk_ring`, `null_device`) is a private heap-backed growable array that duplicates `Vector`, which now owns heap storage. Not replaced in Phase 4 Day 80a (it holds trivially copyable values and has `EraseFront`/`Erase` with `memmove`). Seen in the Day 80a review.
- `raw_new_check` is line-based and walks only `engine/` and `game/`: a `new` separated from its type across lines is missed, and `tests/` and `tools/` (which may use the STL) are not scanned. Seen in the Day 80a and 80b reviews.
- `SharedPtr` with `AtomicRefCount` was checked under ThreadSanitizer only by a standalone program (8 threads, 20000 copy/release rounds, clean; the `PlainRefCount` variant is reported as a race, as expected). The foundation test executable has no TSan flavor, so `ctest` does not run that check. Seen in Day 80b.
- Phase 5 Day 90 (2026-11-06), phase 0: two planted faults in `ArenaAllocator::Alloc` (`(offset_ + align)` instead of `(offset_ + align - 1)`, and `offset_ = aligned + size + 1`) were not caught by any test in the suite (`--affected --since HEAD`, 871 tests, all passed); `tests/foundation/arena_allocator_test.cpp` checks non-overlap and integrity, not the exact bytes used or alignment minimality. A third plant (`aligned + size - 1`) was caught.

