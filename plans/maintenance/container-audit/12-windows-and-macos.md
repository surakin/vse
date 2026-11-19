# Container audit / Day 12: Windows and macOS: the backend fixes and the runs (the user's machines)

**Series:** [the container audit fixes](../container-audit.md)
**Depends on:** Days 1-11, pushed. Done by the user on the Windows PC and the Mac. Each machine fetches first.

The Direct3D 12, Metal and `select` code is not built on Linux, so its audit items are fixed here. Every shared change of Days 1-11 is built and
tested on both machines, and the physics state hashes are compared across all three.

## Tasks

**The Windows PC:**

- [x] `rhi/dx12/dx12_command_list.cpp:158-189`: `FindBuffer`/`FindTexture` use a per-list `HashMap` by handle, cleared on reset (`Clear()`).
  Day 1-style number for a list of 2,000 draws before/after.
- [x] `rhi/dx12/dx12_device_bindings.cpp:327-339` `AcquireSamplerTableLocked`: a `HashMap` keyed by `Fnv1a64` of the sampler set, compared on a hit.
  `:366-369` and `:611` use `Append` or reserve.
- [x] `rhi/dx12/dx12_pipeline_cache.cpp:149-189`: `InsertSortedKey` uses `LowerBound`, or a `HashSet` if the sorted order is not needed.
  The FNV copies in `dx12_device_pipelines.cpp`, `dx12_pipeline_cache.*` use `Fnv1a64`, with the pipeline cache keys unchanged (a cache built before still loads).
- [x] `net/windows/poller_select.cpp:75-345`: `SocketSet::Contains` and `ArmedSet::IndexOf` through a `HashMap` by socket, so `Wait` is linear.
- [x] Build `windows-msvc` and `windows-clang-cl`, unity on and off.
  - Run `scripts/test-phase.sh --affected` plain and asan, quoting the lines.
  - Run the physics reference state hashes and the script hashes, compared with Linux's (Day 8, Day 10).

**The Mac:**

- [x] `rhi/metal/metal_device_bindings.mm:629` `RefFor`: a per-encoder map. `:229-233` drops the span cast (Day 3's `Sort` overload).
  `metal_device_pipelines.mm:174-178` uses `Append`.
  - **Changed on the Mac (2026-11-19): not a per-encoder map but sorted refs and a binary search.** A stage's refs are fixed when its pipeline
    is made, so `MapStageSlots` sorts them once by (set, binding) (`SlotRefKey`, a stable sort, so of two refs for one binding the first still
    wins) and `RefFor` finds a resource with `LowerBound`: O(log S) per resource with nothing built per encoder, where a map would be filled
    again for every encoder.
- [x] The FNV copies in `metal_pipeline_cache.*` use `Fnv1a64`, with the keys unchanged.
- [x] Build `macos-clang` (unity on and off) and `macos-clang-arm64` (compile only, both modes).
  - **Unity only (the user, 2026-11-19: the Mac skips the unity-off builds from now on).**
  - [x] Run `scripts/test-phase.sh --affected` plain (the user's standing rule for the Mac: no sanitizer runs for now), quoting the line.
  - [x] Compare the physics and script hashes with Linux's.
  - [x] Format with MacPorts' clang-format.

**Both:**

- [x] **Standing rows** (as [the series rules](../container-audit.md#rules-for-every-day)):
  - docs (`ctest -L docs`) on each machine;
  - `/code-review high` over each machine's diff, recorded below;
  - `STATUS.md` and `KNOWN-ISSUES.md` (what still waits for which machine); commit and push.

## Notes (the Windows PC, 2026-11-19)

**The D3D12 and `select` items:**
- `Dx12CommandList` keeps `buffer_index_` and `texture_index_` (`HashMap<ID3D12Resource*, u32>`) beside its state vectors, cleared in
  `Recycle`; the vectors stay, since the submit prologue walks the textures in first-use order.
- `AcquireSamplerTableLocked` finds a live table through `sampler_table_index_` (`Fnv1a64Builder` over the sampler handles and the count,
  compared on a hit; a colliding set gets a table of its own, unindexed) and reuses entries from `free_sampler_tables_`. The keys are
  copied with `Append`, and a bind group's `refs` are reserved before the binding loop.
- `InsertSortedKey`/`HasSortedKey` use the foundation `LowerBound` and `Vector::Insert`. The order is kept: the keys are written to the
  cache file sorted. The local `Fnv1a` is `Fnv1a64Builder(seed).AddBytes`, and the `0xcbf29ce484222325` literals are `kFnv1a64Basis`, so
  every pipeline key and the cache identity are unchanged (the pipeline cache tests load files written before).
- The `select` poller keeps `index` (handle to position, updated by the swap-remove in `Disarm`) and gathers the three result sets into
  one `ready` map per wait, so `Wait` is linear in the armed sockets.

**Upstream bugs this day found (Days 2-11 ran no tests, and no Windows build ran them):**
- **`HashMap::Insert` crashed on MSVC when it grew the table:** `table_.Make(table_.SlotForInsert(held_key), Entry{Move(held_key), ...})`.
  The order of a call's arguments is unspecified, and cl evaluates them right to left, so the key was moved into the entry before it was
  hashed (an empty, moved-from `String`: a null read). 271 tests segfaulted in the first plain run (UI, `asset_mint`, script hot reload);
  Clang and GCC evaluate left to right and never saw it. Fixed in `hash_map.h` and the same shape in `hash_set.h`: the slot is computed in
  a statement of its own. The existing `HashMap: a key of another type is converted before it is hashed` crashed before the fix.
- **`String::AppendRawBytes` grew twice per append** (Day 3): the bytes to an exact size, then the terminator's push doubled the buffer.
  In an arena, which never frees, the Windows window host's 128-byte scratch for `Window: no usable backend (wayland is a Linux backend)`
  ran out (4 + 27 + 54 + 108 bytes), so `input_probe_runs_wayland` aborted with a Debug CRT dialog instead of skipping. Now one growth
  counts the terminator (append after the `'\0'`, shift the bytes down over it). New test `String: an append grows once, so the window
  host's message fits its 128-byte arena`, shown aborting on the old code.
- **Three gates failed on audit code:** `file_access_check` on `DenseMap::RemoveIf`'s parameter `remove` (renamed `should_remove`),
  `platform_neutral_check` on `ENGINE_CHECK_SLOW` in `open_table.h` (now `ENGINE_ASSERT`), `shared_unit_check` on a `GpuMesh*` member in
  `tests/renderer/mesh_topology_test.cpp` (now a `GpuMeshPtr`). `docs/reference` was stale and is regenerated. Day 13 fixed the same three
  on Linux meanwhile (`drop`, `ENGINE_ASSERT`, an owned `GpuMesh`); the rebase keeps Linux's versions and drops the PC's.
- The MSVC `HashMap` value narrowing (C4244: `Insert(key, 1)` into a `u8` map converts the `int` inside the template) was worked around at
  `tests/storage/identity_test.cpp` with `u8{1}`. That map was a set, so after the push it became a `HashSet<EntityKey>` (the user's choice;
  the audit's own fix for the engine's `u8` sets), and the workaround and its `KNOWN-ISSUES.md` entry are gone. `HashMap::Insert` still
  makes cl warn for a value of another type; no other call site does that.

**Before and after (Release, `build-msvc-release`):**

| Path | Before | After |
|---|---|---|
| A D3D12 list touching 2,000 buffers, two barriers each (`[dx12-list-bench]`, new, hidden; median of 5 runs of 20) | 5.26 ms | 4.19 ms |
| `select` poller, 1,000 armed sockets, a wait with 0 / 1 / 16 / 1,000 ready (`[poller-bench]`) | 53.8 / 58.4 / 63.0 / 195.5 µs | 53.8 / 58.2 / 67.0 / 190.3 µs |
| The same, one re-arm (0 / 1 / 16 / 1,000 ready) | 0.00 / 0.31 / 0.22 / 0.22 µs | 0.00 / 0.16 / 0.11 / 0.10 µs |

The list's time is mostly the D3D12 validation layer (the test device runs validated) and the barrier calls; single runs ranged from
3.3 to 6.8 ms. The poller's wait is `select` itself; the lookups it no longer does show in the re-arm.

## Notes (the Mac, 2026-11-19)

**The Metal items:**
- `RefFor` (`metal_device_bindings.mm`): not the per-encoder map of the task (see the task's note) but sorted refs. `MapStageSlots` sorts a stage's
  refs once by `SlotRefKey(set, binding)` (`metal_device.h`) with `StableSort`, and `RefFor` finds a resource with `LowerBound`; a set or binding
  above 255 matches nothing, as before (a ref's set and binding are bytes). `HighestBufferSlot` and the overlap check do not depend on the order.
- `CreateBindGroupLayout` sorts through the container `Sort` overload (no span cast); `CopySlots` uses `Append`.
- `MetalArchiveFnv` is `Fnv1a64Builder(seed).AddBytes`, with `kFnv1a64Basis` as the default seed, so the archive identity and checksum are
  unchanged; `metal_pipeline_test.mm` now pins three values computed by the old loop (the identity of its test adapter, `"a"`, and a seeded
  hash). `container_use_check`'s `engine/rhi/metal/` allow-list entry is removed.

**Found on the Mac:**
- **`PlatformWindowHost refuses Wayland, X11 and Win32` and `OpenFromEnvironment honors ENGINE_WINDOW_BACKEND` aborted** in the first run
  (`Vector::Reserve` out of memory in the window host's 128-byte arena). The PC's `AppendRawBytes` fix makes the Wayland and X11 lines fit,
  but the macOS refusal of Win32, `Window: no usable backend (win32 is the Windows backend)` (56 characters), still needs a 112-byte block
  after 28 and 56 (196 bytes; an arena never frees). `window_host_macos.cpp` builds the line with `HeapAllocator` now (a failure path). The
  Linux and Windows hosts keep their arenas: their own refusals fit after the PC's fix. Both tests aborted every time before the fix.
- **`directory watcher: the native backend where there is one` failed on the Mac** after the pull: Day 473 added a rename case that expects
  one `Renamed` from every native backend, but the macOS backend reports a rename as `Removed` and `Created` (Day 474, `directory_watcher.h`).
  The case now expects the two names on macOS.

## Runs

The Windows PC, 2026-11-19, after the fixes. `scripts/test-phase.sh --affected` selects everything here (the series changed foundation
headers), and on this PC the phase 8 reset-loop test (`Resets in a loop on a real device leave the counts, memory and descriptors where
they were`) is never run (the user's rule: it froze the PC), so the runs are ctest directly over every test but that one, crash tests left
out, display tests included (the console session was unlocked):

- `build-msvc` plain: **4015 of 4017 passed** (ctest: "99% tests passed, 2 tests failed out of 4017") in 259 s. Failed, both known:
  `Snapshot: the cost of a voice at each quality, with and without effects` (the Debug voice-cost bound) and `Throughput scales with
  participant count` (the jobs flake).
- `build-msvc` asan: **3443 of 3443 passed** ("100% tests passed out of 3443") in 657 s.
- `build-clang-cl` plain: **4046 of 4047 passed** ("99% tests passed, 1 tests failed out of 4047") in 537 s. Failed: the voice-cost bound.
- `ctest -L docs` (`build-msvc`): 8 of 8 after `docs/reference` was regenerated.
- Built clean (warnings are errors): `build-msvc`, `build-clang-cl`, `build-windows-msvc-nounity`, `build-windows-clang-cl-nounity`, and
  the Release tree for the benchmarks.
- **Hashes:** the physics determinism and reference-hash tests (phase 25) and the script hashes (`Script demo: the state hash after 150
  frames is the same on every machine`, `0xCD4A4433E698FAD7`; `engine_c_script_sample`, `e3e3ca13a45686f9`, Linux's value) pass in all
  three runs, so the series left them unchanged on this PC. Linux runs the series' tests on Day 13.

**After the rebase onto Day 13 and the `Sort` follow-up** (pattern-defeating quicksort, the G-buffer's `StableSort`), the same runs again:
`build-msvc` plain **4022 of 4024** (the voice-cost bound and the jobs flake), asan **3442 of 3443** (`engine_c_runtime_asan` timed out, the
Phase 23 hang; 0.4 s alone on Day 473), `build-clang-cl` plain **4050 of 4054** (the voice-cost bound, the jobs flake,
`container_use_check`, which named `engine/rhi/dx12/` a stale entry of its allow list now that this day removed the FNV copies: the entry is
removed and the gate passes; `engine_c_runtime` timed out once and passed alone in 0.16 s). One MSVC link failed with LNK1103 (a corrupt
`foundation_debug` object left by an interrupted build); recompiled, it links.

Before the fixes, the first plain run had 271 segfaults (the `HashMap` growth bug), a later one three gate failures, `docs_check`, and
three tool segfaults that were stale links left by interrupted builds (relinked, they pass).

**The Mac, 2026-11-19, after the pull of `89a79335` (the PC's half and Day 473 included), x86-64 Debug, unity only** (the user's rule from
this day: the Mac builds no unity-off tree; plain only, no sanitizer runs):

- `scripts/test-phase.sh --affected` (everything: foundation headers and CMake changed) **`ran 4073 of 11236 tests (phases: all; flavors:
  plain; display: included; crash tests: excluded) in 1427 s`**, 3 failures: the known voice-cost bound (phase 26), and the two window-host
  aborts and the watcher rename case above, all fixed afterwards (the four tests pass 3 of 3 reruns). The run before the pull (`ran 4060
  of 11203`, 711 s) had the voice-cost bound and the two window-host aborts.
- Built clean (warnings are errors): `build-macos` (x86-64, unity) and `build-macos-arm64` (unity, compile only).
- `ctest -L docs`: 7 of 7. The gates (`container_use_check` with the Metal entry gone, `std_usage_check`, `platform_neutral_check`,
  `rhi_boundary_check`, `metal_autorelease_pools` and the rest by name): 25 of 25. The Metal tests: 66 of 66 before the review, and the new
  out-of-order case after it.
- **Hashes:** the physics golden and determinism tests (phase 25) pass, so their committed constants hold on the Mac; the query index and
  children index tests of the Query and hierarchy indices series pass. Script: `engine_c_script_sample` `e3e3ca13a45686f9` at 1, 2, 4 and 8
  partitions (Linux's); `Script demo: the state hash after 150 frames is the same on every machine` (`0xCD4A4433E698FAD7`, the PC's) passes;
  `script_demo` at the default 600 frames ends on `424a6fbf279d2104` at 1, 2 and 8 threads, Linux's value. `script_demo_window_cocoa` (the
  window, 1,000 frames with Metal validation) passes.
- **Also on the Mac this day** (open items of other entries): the nojit tree (`ENGINE_LUAJIT_JIT=OFF`, script targets only): `script_tests`
  242 cases, `script_physics_tests` 35, `script_media_tests` 19, `script_demo_tests` 5, all pass; the shared library
  (`-DENGINE_BUILD_SHARED=ON`, the plain host tests of `tests/engine_library/` only): 48 of 48, `engine_c_script`, `engine_c_script_sample`
  and `engine_c_import_off` among them, the ABI snapshot unchanged.

## Post-implementation code review

`/code-review high` over the PC's diff (Day 12's Windows changes and the foundation fixes): 5 findings.

- **Fixed:** the hidden DX12 list benchmark recorded on a validated device without checking the debug layer; it now holds
  `CommandsSilence` (silent, 2,043 assertions).
- **Recorded, not fixed** (`KNOWN-ISSUES.md`):
  - `Poller::Arm` used to answer `LimitExceeded` when the socket vector could not grow; the new handle index grows with `HashMap::Insert`,
    which aborts when out of memory. `HashMap` has no fallible insert yet.
  - `open_table.h`'s changed-while-iterating check is now `ENGINE_ASSERT`, so it runs in optimized builds too (a header may not use
    `ENGINE_CHECK_SLOW`); one compare per iterator step.
  - The list benchmark is dominated by the validation layer, so it would hide a small lookup regression.
- **No change needed:** two sampler sets whose 64-bit hashes collide do not share a table; it stays correct and costs only heap room.

`/code-review high` over the Mac's diff (the Metal changes, the window hosts and the test before the pull): 7 findings.

- **Fixed:** no test listed a slot table out of order, so nothing guarded the sort the binary search relies on: new case `A slot table
  listed out of binding order maps sorted and binds every resource (container audit Day 12)` (`metal_bindings_test.mm`), shown failing with
  the sort disabled. The plan's "per-encoder map" was not edited to say what was built: the task now carries the note. `SlotRefKey` had been
  put between `MetalStageSlots` and its comment: moved above it. A history comment repeated at four call sites: removed.
- **Moot after the pull:** the Linux and Windows window hosts had been edited on the Mac, which cannot build them; the edits were dropped
  when the PC's `String` fix arrived (only the macOS host changes, and the Mac builds it).
- **Recorded, not fixed** (`KNOWN-ISSUES.md`): the same "string built in a small fixed arena" pattern remains at other call sites
  (`game/input_probe/main.cpp:318` with 128 bytes; `wayland_connection.cpp`, `wayland_window.cpp`, `x11_window.cpp`, `win32_window.cpp`,
  `game/hello_window/main.cpp` with 256); a long enough name or message aborts them.
- **Declined:** "no regression test for the window-host abort": `PlatformWindowHost refuses Wayland, X11 and Win32` is that test; it aborted
  in every run before the fix and passes after it.

**Done:**
- The D3D12, Metal and `select` audit items are fixed.
- Both machines build clean in every configuration and pass their scoped runs.
- The physics and script hashes equal Linux's, or the difference is recorded in `KNOWN-ISSUES.md`.
