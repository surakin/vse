# Maintenance: the container audit fixes

This is not a numbered phase: it is a maintenance series of twelve days, chosen by the user on 2026-11-19 over inserting a phase. It does not change `CURRENT_PHASE` or any phase or day number.

**Origin:** the audit [container-audit-2026-11-19.md](container-audit-2026-11-19.md). It found slow containers and algorithms everywhere, most chosen because
the fast container lacked a feature. It also found three correctness problems: the renderer's 256-owner cap, hashed path keys that are never compared, and a joint list
said to be sorted that is not.

**Already done before the series (the Mac, 2026-11-19, `eefa8c3b`):**
- `HashMap` takes move-only keys and values, and looks up by a borrowed key (`HeterogeneousKey`).
- It gained `ForEach` and move construction and assignment.
- `Hash<StringView>` and `Hash<String>` exist.

The commit says it was pushed before its last review fixes were built. On Linux, `foundation_base_tests` builds with Clang and passes 132 test cases. The
other builds and `docs/reference` wait for Day 1.

**Depends on:** Phase 10 (containers), Phase 16 (wrappers), and the modules each day touches. Phase 27 is closed on Linux, and the series runs before
Phase 28's Day 478 unless the user orders otherwise.

**Goal:**
- Give the foundation the containers and algorithms whose absence caused the slow choices.
- Fix the three correctness problems.
- Replace every P1 and P2 path of the audit, and the P3 paths that are quadratic, with the right structure.
- Prove each change with a before/after number from Day 1's benchmarks, and keep every deterministic output bit-identical.

**Definition of done:**
- Every day's boxes are checked.
- The Day 1 benchmarks show each P1 path linear or n log n in the scene size, with the numbers recorded in Day 12.
- The physics reference state hashes, the script world hashes (1 to 16 threads), the minted outputs (determinism tests), the audio render comparisons
  and the ABI snapshot are unchanged.
- No `Remove(0)` queue and no FNV constant remains outside the foundation hash, and a gate enforces both.
- `DeviceResources` takes any number of owners.
- The full suite passes in every flavor on Linux, and the Windows and macOS runs are done or listed in `KNOWN-ISSUES.md`.

## Days

1. [Benchmarks and baselines; the Mac's HashMap checked on Linux](container-audit/01-benchmarks-and-baselines.md)
2. [Foundation I: HashMap completed, HashSet, one byte hash](container-audit/02-foundation-hash-map-set-and-hash.md)
3. [Foundation II: Vector, String, Deque, BinaryHeap and the sorting algorithms](container-audit/03-foundation-vector-string-deque-heap-sort.md)
4. [One FNV, string keys compared, and the map workarounds removed](container-audit/04-one-hash-and-string-keys.md)
5. [Renderer I: DeviceResources without a cap, the material and theme caches, UI](container-audit/05-renderer-device-resources-and-caches.md)
6. [Renderer II: per-frame sorting, text relayout, bind groups, Vulkan memory](container-audit/06-renderer-sorting-text-rhi.md)
7. [Physics: joint pairs, queries through the broad phase, waking and teardown](container-audit/07-physics.md)
8. [Scripting: coroutines, values, handlers, overlay, tasks, queries](container-audit/08-scripting.md)
9. [C API scene, ECS hierarchy, storage and networking](container-audit/09-engine-c-ecs-storage-net.md)
10. [Assets, settings, database, tools and text normalization](container-audit/10-assets-settings-tools.md)
11. [Windows and macOS: the backend fixes and the runs (the user's machines)](container-audit/11-windows-and-macos.md)
12. [The gate, the after numbers, the closing runs and the series review](container-audit/12-gate-numbers-and-closing.md)

## Rules for every day

- **Amended 2026-11-19 (the user, during Day 2): no per-day test runs.** From Day 2 on, each day builds clean in the Clang unity tree and gets its
  `/code-review high`, but runs no ctest and no bench. Day 12 runs the full suite in every flavor, the GCC non-unity build, `ctest -L docs` and every after
  number. The "Runs" and "before/after" rows of Days 2-10 are therefore deferred to Day 12, and each day file says so.

- **Every engine container supports range-for** (the user, 2026-11-19), through public lowercase `begin()`/`end()` with an `Internal:` label, so they stay
  out of the reference: `HashMap`/`HashSet` on Day 2, the rest on Day 3. From Day 4 on, new and rewritten loops over a container use
  `for (auto& x : c)` where no index is needed. Loops a day doesn't otherwise touch are not rewritten for style alone.

- **Deterministic outputs stay bit-identical.** A map or set replaces a scan only for membership and lookup. Anything that iterates in an order that
  reaches an output keeps a sorted or insertion order: `HashMap::ForEach` order is unspecified, and it is never used where order matters. Each day
  names the hashes and determinism tests it re-runs.
- **The before/after number:** each fix day re-runs the Day 1 benchmark of the paths it changed and records both numbers in its file.
- **The standing rows**, as in any phase day:
  - Runs: `scripts/test-phase.sh --affected`, plain, then asan and tsan when the day touches concurrency, lifetime or memory code. Quote the `ran X of Y` line.
  - Build clean in both compilers and both unity modes.
  - Documentation: `///`, `docgen lint` at zero, `docs/reference` regenerated, the module guide's reference facts, `ctest -L docs`.
  - Module rules.
  - `/code-review high` recorded in the day file.
  - `STATUS.md` and `KNOWN-ISSUES.md` in the same commit; commit and push.
- New tests take the phase label of the module they test: phase 10 for the containers, the module's own phase elsewhere.
- Windows-only and macOS-only code (`rhi/dx12`, `rhi/metal`, `net/windows`) is planned for Day 11 on those machines. A shared change is built there no
  later than Day 11.

## Notes

- **Out of scope** unless a benchmark shows it matters: replacing linear scans over containers bounded at 32 or fewer entries, and the build-time
  `ecs/schedule.cpp` overlap check.
- The audit's P1 and P2 lists are the checklist: each item appears in exactly one day below, or is declined in that day with a reason.
