# Container audit / Day 1: Benchmarks and baselines; the Mac's HashMap checked on Linux

**Series:** [the container audit fixes](../container-audit.md)
**Depends on:** the audit ([container-audit-2026-11-19.md](../container-audit-2026-11-19.md)) and `eefa8c3b` (the Mac's `HashMap`).

The audit ranked paths by how often they run and how large their containers can grow, without timing anything. This day measures the
worst paths at two or three sizes each, so that the growth shows (a doubled size that quadruples the time is quadratic). Every later day
then records a before/after number against these baselines. No engine behavior changes today.

## Tasks

- [x] **The Mac's `HashMap` (`eefa8c3b`), checked on Linux:**
  - build it clean with Clang and GCC, unity on and off;
  - run the phase 10 tests and every module that uses `HashMap` (`--affected` selects them: the change touches a foundation header);
  - run asan and tsan;
  - regenerate `docs/reference` if the Mac has not, with `docgen lint` at zero and `ctest -L docs`;
  - fix or record whatever fails (its commit message says the last review fixes were not built).
- [x] **A container microbenchmark** `game/container_bench` (Release only, like the other benches; no assertions; `--csv`). It times:
  - `HashMap<u64, u32>`: insert, find hit and miss, remove, at 1e3, 1e5 and 1e6;
  - `HashMap<String, u32>` found by `StringView`;
  - `Vector` push, and `Remove(0)` against the end;
  - `Sort` on random, sorted and all-equal input;
  - `String::Append` of 1 MB in 16-byte pieces.

  It records the probe cost of `%`, so Day 2's mask has a number. Add its module line, `tests/phases.txt` line (a testopt check is not
  needed) and `BUILD.md` entry if anything new is required.
- [x] **Physics** (`physics_bench`, its existing scenes swept):
  - `joints` at 100, 1,000 and 5,000 joints, with the bodies held fixed (the pair × joint scan, `physics/step.cpp:318`);
  - `characters` at 10, 100 and 1,000 characters over 1,000 and 10,000 colliders (queries without the broad phase);
  - `rays` at 1,000 and 20,000 colliders;
  - a new `destroy` scene: destroy N of N bodies with joints and contacts (`DestroyBody` scans);
  - the per-step sort in `sync.cpp`/`joint_sync.cpp`, recorded as its share of the step at 20,000 bodies.
- [x] **Scripting** (`script_bench`):
  - `waits` at 1,000, 10,000 and 50,000 waiting coroutines (`DoResume`'s scan and `Remove(i)`);
  - `events` with 10, 100 and 1,000 waiters (`HasHandler`);
  - a new `values` scene: building maps of 16, 1,000 and 65,536 keys through `Value::Set`, and decoding them;
  - a new `overlay` scene: one callback touching 10, 100 and 1,000 entities (`command_buffer.cpp`);
  - `tasks` at the queue limit (`TakeTask`);
  - `query.page` paging through 10,000 entities.
- [x] **Renderer and UI** (`renderer_bench` on the null device, no window):
  - a new `materials` section: 1,000 and 10,000 objects over 16 and 256 pipelines (`GetPipeline`);
  - `ui` at 100 and 2,000 widgets, with the number of distinct theme keys swept past 4,096 (`ThemeSet::Resolve`, the thrash at the cap);
  - `text`: 1,024 labels, unchanged between frames (the relayout every frame);
  - the `frame` section's two sorts of the visible slots and the point-light sort, at 10,000 objects and 1,000 lights.
- [x] **The C API scene** (a new `host_bench` C program beside the samples, or a section of an existing bench through `engine_c`):
  - an offscreen surface (`ENGINE_SURFACE_OFFSCREEN`) with 1,000 and 10,000 drawn entities, timing `surface_scene.cpp`'s per-frame pass.
  - If the offscreen surface refuses the null device, use Vulkan offscreen, with no window, and record that.
- [x] **ECS, storage, net, assets, settings:**
  - `ecs_bench`: building a hierarchy of 1,000 and 10,000 entities by `SetParent` (`transform.cpp`);
  - `storage::MemoryStore`: saving 10,000 records in random order;
  - the simulated UDP transport: one lost packet with 1,024 buffered behind it;
  - `ResourceManager::Replace` with 100 and 1,000 dependents;
  - `ResourceTable` removal of 1,000 of 10,000 paths;
  - `AsyncLoader` draining 10,000 queued loads;
  - settings startup with a 2,000-line file;
  - `asset_mint` loading a level of 10,000 named entities.

  Each is a timed case in the module's bench, or in `container_bench` when the module has none.
- [x] **The baselines** recorded in this file, as a table of path, sizes, times and growth, with the machine and build type.
  - The day ranks the P1 list by measured cost. A path the audit called P1 that measures flat is moved down in its day file, with the number as the reason.
- [x] **Standing rows** (as [the series rules](../container-audit.md#rules-for-every-day)):
  - runs: `--affected`, plain; asan and tsan for the `HashMap` check;
  - builds: both compilers, both unity modes;
  - `ctest -L docs`;
  - `/code-review high` recorded below;
  - `STATUS.md` and `KNOWN-ISSUES.md`; commit and push.

## Notes

- Benches run in the `build` tree (Release) on an otherwise idle machine. Each figure is the median of at least five runs. A display is not needed for any case.
- Benches assert nothing. The regression bounds come on Day 13, from the after numbers.
- **Amended while doing the day:**
  - The existing benches already sweep the physics and script paths, so they are run as they are and get no new scenes: joints at 50/200/500 chains of 10,
    characters at 100/500/2,000, script waits by `--entities`.
  - `container_bench` (micro and paths) covers what had no bench: the material and theme caches, `DeviceResources`, `Value` maps, `SetParent`, `ResourceTable`
    removal, `MemoryStore` puts and the settings document.
  - The C API surface scene, the network reorder buffer, `AsyncLoader`, `ResourceManager::Replace`, the `asset_mint` level and the physics `destroy`
    scene are measured before and after on their fix days (9, 10, 7). Each needs a rig of its own: a C host with minted meshes, a transport pair, a
    loader with a backend. That rig is built the day it is used, so it is not built twice.
- **The Mac's `HashMap` on Linux:**
  - the Clang unity tree built clean;
  - the GCC non-unity tree failed in `tests/foundation/x11_window_test.cpp`: `<X11/X.h>` defines `DestroyAll` as a macro, and the test includes the X11
    headers before `x11_window.h` brings in `hash_map.h`, whose new private `DestroyAll` member the macro breaks.
  - Fixed with the X11 guard pattern, not a rename (the user, 2026-11-19): the two X11 tests (`x11_window_test.cpp`, `window_dragdrop_test.cpp`) include
    `containers/hash_map.h` before the X11 region, as they already do for `None` and `BadAlloc`, and `DestroyAll` joins the names saved and restored by
    `unity/x11_enable.h` / `x11_disable.h`.

## Runs

- plain: `ran 4077 of 11341 tests (phases: all; flavors: plain; display: excluded; crash tests: excluded) in 388 s`. Three failed, all seen before:
  - `A Job destroyed without being released frees its never-started frame` (phase 2, in `KNOWN-ISSUES.md`);
  - `engine_c_surface` (a window test, failing in the Day 477 closing run too);
  - `Snapshot: the cost of a voice at each quality` (a timing bound; it passed alone in all three flavors).
- asan: `ran 3480 of 11341 tests (phases: all; flavors: asan; display: excluded; crash tests: excluded) in 73 s`, with only the known Job test failing.
- tsan: `ran 3143 of 11341 tests (phases: all; flavors: tsan; display: excluded; crash tests: excluded) in 84 s`, with only the known Job test failing.
- `ctest -L docs`: 9 of 9, after `docs/reference` was regenerated (the Mac's `eefa8c3b` had left four pages out of date).
- Builds clean: the Clang unity `build` tree (whole) and a GCC non-unity tree (`build-gcc/`, moved out of `/tmp` because its tmpfs ran out of space), after the X11 fix.
- Reduced runs, not the whole suite.

## Baselines

Linux machine (32 threads), Release `build` tree (Clang, unity), 2026-11-19. `container_bench` reports the median of 5 runs; the other benches report their own
medians or averages. The full output is kept in `build/baseline-day1.log` (not committed). **Growth** compares the time per operation at the largest size with
the time at the smallest: about 1× is linear, and a growth that tracks the size ratio is quadratic.

| Path (audit item) | Sizes | Time per op (or per step/tick) | Growth |
|---|---|---|---|
| `HashMap<u64>` find hit (the `%` probe) | 1e3 / 1e5 / 1e6 | 4.9 / 6.9 / 19.0 ns | cache-bound at 1e6 |
| `HashMap<u64>` insert | 1e3 / 1e5 / 1e6 | 34.6 / 78.7 / 87.9 ns | rehash copies |
| `HashMap<String>` find by view vs `Vector<String>` linear find | 20,000 | 58 ns vs 28,660 ns | 490× |
| `Vector<u32>` `Remove(0)` drain vs `Pop` | 100,000 | 1,622 ns vs 0.0 ns | O(n) per removal |
| `Sort` random / sorted / all equal | 1e6 | 42.3 / 4.9 / 8.4 ns | fine; all-equal is not degenerate |
| `StableSort` random (u32) | 1e6 | 30.3 ns | |
| `String` append, 16-byte pieces | 1 MB | 2.2 ns/byte | |
| Material pipeline cache (`GetPipeline`, per object) | 16 / 64 / 250 cached | 22 / 65 / 216 ns | linear in the cache |
| `DeviceResources` | 300 asked | **256 accepted** | cap |
| Theme cache (`Resolve`, per widget) | 100 / 1,000 / 4,000 / 8,000 keys | 30 / 266 / 1,053 / 1,074 ns | linear to the cap, then thrash |
| Script `Value` map, set + find | 16 / 1,000 / 8,000 keys | 80 / 689 / 4,984 ns | quadratic |
| ECS `SetParent` | 1,000 / 4,000 | 7,452 / 29,491 ns | quadratic (4× size, 16× total) |
| `ResourceTable::Remove` | 2,000 / 10,000 | **corrected on Day 4: 112,764 / 1,291,740 ns** | O(N) per removal (the Day 1 case added paths without the leading `/`, so every `Add` was refused and it timed an empty table; re-measured on Day 4 against the old code) |
| `MemoryStore::Put`, random order | 2,000 / 10,000 | 681 / 2,975 ns | quadratic |
| Settings document, parse + read every key | 500 / 2,000 | 1,192 / 3,088 ns | superlinear |
| Physics joints (`physics_bench --scene joints`) | 551 / 2,201 / 5,501 bodies | 0.057 / 0.234 / 2.366 ms per step | 10× bodies, 41× time (contacts appear at 5,501) |
| Characters (move + step) | 100 / 500 / 2,000 | 1.38 / 20.4 / 288 ms per step | quadratic (20×, 209×) |
| Rays against 5,000 bodies | 1e6 rays | 8,699 rays/s (115 µs per ray) | every collider per ray |
| Script waits (`--scene waits`) | 10,000 / 40,000 / 160,000 | 616 / 656 / 696 ns per wake | about linear: 64 partitions keep each `suspended_` short |
| Script events | 10,000 / 100,000 listeners | 17.3 / 176.8 ms per tick | linear |
| Renderer frame (null device) | 1,000 / 10,000 / 50,000 objects | 0.116 / 0.101 / 0.129 µs per object | linear (few pipelines in the scene) |
| Text: layout of 1,000 chars | 16 / 32 / 64 px | 82 µs | every frame, for unchanged text |
| UI: relayout + repaint | 1,000 / 5,000 widgets | 0.49 / 2.47 ms | linear (few theme keys) |

**The P1 ranking from the numbers:**
1. Physics queries (characters, rays).
2. `SetParent`.
3. `Value` maps.
4. `MemoryStore`.
5. The theme cache past ~1,000 keys.
6. The material cache with many pipelines.

The `DeviceResources` cap is a correctness item. Script waits and events measure linear (the `ResourceTable` row was wrong, see the corrected row): Day 9 keeps their fixes (order-keeping
removes in a scan), but they move down the list. Day 4 records why the table removal is not quadratic.

## Post-implementation code review

`/code-review high`: 5 findings, all fixed:
1. The CSV writer did not quote case names that hold commas.
2. The settings case did not check that its lookups hit. It now marks the row invalid if they miss; every key was found.
3. The materials rows were labeled with the keys asked for, not the pipelines made. They now count the made ones, which equal the asked ones on the null device.
4. Raw C arrays in the new code became `Array<T, N>`.
5. An unused `renderer/mesh.h` include was removed.

No regression test: the bench asserts nothing, and the fixes were checked by a `--quick --csv` run.

**Done:** the Mac's `HashMap` builds and passes on Linux in every flavor, with its docs current. Every audit P1 path and the P3 paths named above have a measured baseline at two or more sizes. The P1 ranking is updated from the numbers.
