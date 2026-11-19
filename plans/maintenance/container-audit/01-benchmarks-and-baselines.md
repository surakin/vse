# Container audit / Day 1: Benchmarks and baselines; the Mac's HashMap checked on Linux

**Series:** [the container audit fixes](../container-audit.md)
**Depends on:** the audit ([container-audit-2026-11-19.md](../container-audit-2026-11-19.md)) and `eefa8c3b` (the Mac's `HashMap`).

The audit ranked paths by how often they run and how large their containers can grow, without timing anything. This day measures the
worst paths at two or three sizes each, so that the growth shows (a doubled size that quadruples the time is quadratic). Every later day
then records a before/after number against these baselines. No engine behavior changes today.

## Tasks

- [ ] **The Mac's `HashMap` (`eefa8c3b`), checked on Linux:**
  - build it clean with Clang and GCC, unity on and off;
  - run the phase 10 tests and every module that uses `HashMap` (`--affected` selects them: the change touches a foundation header);
  - run asan and tsan;
  - regenerate `docs/reference` if the Mac has not, with `docgen lint` at zero and `ctest -L docs`;
  - fix or record whatever fails (its commit message says the last review fixes were not built).
- [ ] **A container microbenchmark** `game/container_bench` (Release only, like the other benches; no assertions; `--csv`). It times:
  - `HashMap<u64, u32>`: insert, find hit and miss, remove, at 1e3, 1e5 and 1e6;
  - `HashMap<String, u32>` found by `StringView`;
  - `Vector` push, and `Remove(0)` against the end;
  - `Sort` on random, sorted and all-equal input;
  - `String::Append` of 1 MB in 16-byte pieces.

  It records the probe cost of `%`, so Day 2's mask has a number. Add its module line, `tests/phases.txt` line (a testopt check is not
  needed) and `BUILD.md` entry if anything new is required.
- [ ] **Physics** (`physics_bench`, its existing scenes swept):
  - `joints` at 100, 1,000 and 5,000 joints, with the bodies held fixed (the pair × joint scan, `physics/step.cpp:318`);
  - `characters` at 10, 100 and 1,000 characters over 1,000 and 10,000 colliders (queries without the broad phase);
  - `rays` at 1,000 and 20,000 colliders;
  - a new `destroy` scene: destroy N of N bodies with joints and contacts (`DestroyBody` scans);
  - the per-step sort in `sync.cpp`/`joint_sync.cpp`, recorded as its share of the step at 20,000 bodies.
- [ ] **Scripting** (`script_bench`):
  - `waits` at 1,000, 10,000 and 50,000 waiting coroutines (`DoResume`'s scan and `Remove(i)`);
  - `events` with 10, 100 and 1,000 waiters (`HasHandler`);
  - a new `values` scene: building maps of 16, 1,000 and 65,536 keys through `Value::Set`, and decoding them;
  - a new `overlay` scene: one callback touching 10, 100 and 1,000 entities (`command_buffer.cpp`);
  - `tasks` at the queue limit (`TakeTask`);
  - `query.page` paging through 10,000 entities.
- [ ] **Renderer and UI** (`renderer_bench` on the null device, no window):
  - a new `materials` section: 1,000 and 10,000 objects over 16 and 256 pipelines (`GetPipeline`);
  - `ui` at 100 and 2,000 widgets, with the number of distinct theme keys swept past 4,096 (`ThemeSet::Resolve`, the thrash at the cap);
  - `text`: 1,024 labels, unchanged between frames (the relayout every frame);
  - the `frame` section's two sorts of the visible slots and the point-light sort, at 10,000 objects and 1,000 lights.
- [ ] **The C API scene** (a new `host_bench` C program beside the samples, or a section of an existing bench through `engine_c`):
  - an offscreen surface (`ENGINE_SURFACE_OFFSCREEN`) with 1,000 and 10,000 drawn entities, timing `surface_scene.cpp`'s per-frame pass.
  - If the offscreen surface refuses the null device, use Vulkan offscreen, with no window, and record that.
- [ ] **ECS, storage, net, assets, settings:**
  - `ecs_bench`: building a hierarchy of 1,000 and 10,000 entities by `SetParent` (`transform.cpp`);
  - `storage::MemoryStore`: saving 10,000 records in random order;
  - the simulated UDP transport: one lost packet with 1,024 buffered behind it;
  - `ResourceManager::Replace` with 100 and 1,000 dependents;
  - `ResourceTable` removal of 1,000 of 10,000 paths;
  - `AsyncLoader` draining 10,000 queued loads;
  - settings startup with a 2,000-line file;
  - `asset_mint` loading a level of 10,000 named entities.

  Each is a timed case in the module's bench, or in `container_bench` when the module has none.
- [ ] **The baselines** recorded in this file, as a table of path, sizes, times and growth, with the machine and build type.
  - The day ranks the P1 list by measured cost. A path the audit called P1 that measures flat is moved down in its day file, with the number as the reason.
- [ ] **Standing rows** (as [the series rules](../container-audit.md#rules-for-every-day)):
  - runs: `--affected`, plain; asan and tsan for the `HashMap` check;
  - builds: both compilers, both unity modes;
  - `ctest -L docs`;
  - `/code-review high` recorded below;
  - `STATUS.md` and `KNOWN-ISSUES.md`; commit and push.

## Notes

- Benches run in the `build` tree (Release) on an otherwise idle machine. Each figure is the median of at least five runs. A display is not needed for any case.
- Benches assert nothing. The regression bounds come on Day 12, from the after numbers.

## Runs

## Baselines

## Post-implementation code review

**Done:** the Mac's `HashMap` builds and passes on Linux in every flavor, with its docs current. Every audit P1 path and the P3 paths named above have a measured baseline at two or more sizes. The P1 ranking is updated from the numbers.
