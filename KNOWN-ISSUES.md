# Known issues

Failing or flaky tests and deferred items, in one place. `STATUS.md` says where the plan is; this file says what is
known to be wrong or unfinished. Remove an entry when it is fixed (the commit that fixes it removes it), and add one
whenever a day leaves something broken, unmeasured or deferred. Entries state facts that were observed, with where.

## Failing tests

| Test | Since | Cause | Status |
|---|---|---|---|
| (none) | | | |

## Flaky tests

Seen failing once or intermittently, passing on rerun. Cause not investigated.

- (none). The window tests (X11, Wayland, and the RHI presentation tests) used to fail at random under `ctest -j6`; they now hold the ctest resource `display` and run one at a time (5 full runs in a row passed, 2026-11-05).


## Unverified by a human

Automated checks pass; nobody has looked at the real thing yet (from `STATUS.md`, Phases 1-2).

- Visual confirmation of the Phase 1 windows, live keyboard and mouse updates, and the real close-button exit on Wayland and X11 (Days 19-26).
- `fr`/`de` keyboard layouts through a live X server (xkbcommon tests cover them).
- Phase 4 Day 70: vsync-paced `rhi_demo` frame time was 50 ms on Wayland and 17 ms on X11 against 4.1 ms in Phase 3 in that session. The cause is believed to be compositor pacing (negligible shader cost with `--no-vsync`) but it was not re-measured after a session restart.
- Phase 4 Day 78 (2026-11-05): in repeated full `ctest -j8` runs, one run had 1 failure and two had X11Window tests fail (`translates key press/release into Key via the server's keymap`, `releases keys and modifiers on a real focus loss but not during WM grabs`, `releases the Key a code was pressed as even if modifiers changed`); other runs passed 1643/1643 and 1649/1649. No code those tests exercise changed that day; not investigated.

## Deferred items

| Item | Needed by | Where recorded |
|---|---|---|
| Pipeline creation time for permuted shaders was not measured; specialization constants are the Vulkan-only escape hatch if profiling asks for it | Only if profiling shows a problem | `docs/superpowers/specs/2026-11-04-renderer-design.md` R10.1 |
| `shader_mint --permute` starts `slangc` with `posix_spawnp` (Linux only) | Phase 5 (Windows) | `shaders/README.md`, Day 72 day file |
| Clip-space Y flip in the shader path (Slang does not apply it) | Phases 5-6 | `shaders/README.md`, RHI portability review |
| Metal renames an entry point called `main` | Phase 6 | `shaders/README.md` |
| `MaterialSystem::GetPipeline` scans the pipeline cache linearly and `FlushMaterials` walks every material slot up to the highest used; neither was measured with a real scene | Day 80-81 (when a frame's real call counts are known) | Day 74 day file |
| `ListToStrip` looks up shared edges by binary search over equal keys without removing used triangles, so heavily non-manifold input (many triangles on one directed edge) can take quadratic time; it is a load-time helper | Only if a real mesh shows it | Day 78 review |
| `GBufferPass::Prepare` calls `MaterialSystem::GetPipeline` (a linear scan of the pipeline cache) for every visible object every frame; cache per (material, mesh layout) if profiling shows it | Day 83 profiling | Day 79 review |
| The failure path of a bind-group rebuild in `FlushMaterials` (slots revert to what the groups show) has no test: it needs RHI fault injection | Day 77 | Day 74 day file |
| The per-object index comes from `SV_VulkanInstanceID` (`gl_InstanceIndex`); D3D12 and Metal shaders cannot see a draw's base instance that way, so those backends need another delivery (root constant, per-draw vertex stream, or `SV_StartInstanceLocation` where it exists) | Phases 5-6 | `shaders/README.md` "Material shaders" |
| Transient resources are pooled by description but their memory is not aliased (two non-overlapping resources of different sizes or formats each keep their own allocation); the plan marks aliasing optional | Only if frame memory becomes a problem (Phase 4 Days 80-83 will show) | Day 76 day file |
| A pass records into one command list: splitting one pass's draw list over several lists (the plan's "slices of the draw list") needs the RHI to suspend and resume a rendering pass or secondary command lists, which it does not have | Only if the G-buffer pass becomes CPU-bound (Day 79/83 profiling) | Day 77 day file |
| Other items filed per backend (typeless depth, descriptor copies, `ClampToBorder`, ...) | Phases 5-6 | RHI portability review, section 5 |
| Metal backend behavior on Apple Silicon (unified memory, tile-based rendering, memoryless attachments) cannot be verified on the developer's 2019 Intel MacBook Pro | Phase 6 (record), Phase 14 (iOS device) | `plans/phase-06-macos-metal.md` |
| How Phases 7-13 keep the Windows/macOS builds working (per-phase closing day vs periodic catch-up) | Decide when Phase 6 completes | `PLAN.md` assumption #11 |
| Epic's UE EULA terms need review | Before Phase 16 starts | `PLAN.md` (open risk) |

- Phase 4 Day 81: the point-light scale numbers (1 / 100 / 1000 lights at 1080p) are CPU wall time from submit to idle; GPU-only time was not measured because the RHI has no timestamp queries yet.
- `rhi::detail::GrowArray` (`engine/rhi/grow_array.h`, 12 uses in `vk_memory`, `vk_ring`, `null_device`) is a private heap-backed growable array that duplicates `Vector`, which now owns heap storage. Not replaced in Phase 4 Day 80a (it holds trivially copyable values and has `EraseFront`/`Erase` with `memmove`). Seen in the Day 80a review.
- `raw_new_check` is line-based and walks only `engine/` and `game/`: a `new` separated from its type across lines is missed, and `tests/` and `tools/` (which may use the STL) are not scanned. Seen in the Day 80a and 80b reviews.
- `SharedPtr` with `AtomicRefCount` was checked under ThreadSanitizer only by a standalone program (8 threads, 20000 copy/release rounds, clean; the `PlainRefCount` variant is reported as a race, as expected). The foundation test executable has no TSan flavor, so `ctest` does not run that check. Seen in Day 80b.
