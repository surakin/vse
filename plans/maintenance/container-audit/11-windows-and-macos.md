# Container audit / Day 11: Windows and macOS: the backend fixes and the runs (the user's machines)

**Series:** [the container audit fixes](../container-audit.md)
**Depends on:** Days 1-10, pushed. Done by the user on the Windows PC and the Mac. Each machine fetches first.

The Direct3D 12, Metal and `select` code is not built on Linux, so its audit items are fixed here. Every shared change of Days 1-10 is built and
tested on both machines, and the physics state hashes are compared across all three.

## Tasks

**The Windows PC:**

- [ ] `rhi/dx12/dx12_command_list.cpp:158-189`: `FindBuffer`/`FindTexture` use a per-list `HashMap` by handle, cleared on reset (`Clear()`).
  Day 1-style number for a list of 2,000 draws before/after.
- [ ] `rhi/dx12/dx12_device_bindings.cpp:327-339` `AcquireSamplerTableLocked`: a `HashMap` keyed by `Fnv1a64` of the sampler set, compared on a hit.
  `:366-369` and `:611` use `Append` or reserve.
- [ ] `rhi/dx12/dx12_pipeline_cache.cpp:149-189`: `InsertSortedKey` uses `LowerBound`, or a `HashSet` if the sorted order is not needed.
  The FNV copies in `dx12_device_pipelines.cpp`, `dx12_pipeline_cache.*` use `Fnv1a64`, with the pipeline cache keys unchanged (a cache built before still loads).
- [ ] `net/windows/poller_select.cpp:75-345`: `SocketSet::Contains` and `ArmedSet::IndexOf` through a `HashMap` by socket, so `Wait` is linear.
- [ ] Build `windows-msvc` and `windows-clang-cl`, unity on and off.
  - Run `scripts/test-phase.sh --affected` plain and asan, quoting the lines.
  - Run the physics reference state hashes and the script hashes, compared with Linux's (Day 7, Day 8).

**The Mac:**

- [ ] `rhi/metal/metal_device_bindings.mm:629` `RefFor`: a per-encoder map. `:229-233` drops the span cast (Day 3's `Sort` overload).
  `metal_device_pipelines.mm:174-178` uses `Append`.
- [ ] The FNV copies in `metal_pipeline_cache.*` use `Fnv1a64`, with the keys unchanged.
- [ ] Build `macos-clang` (unity on and off) and `macos-clang-arm64` (compile only, both modes).
  - Run `scripts/test-phase.sh --affected` plain (the user's standing rule for the Mac: no sanitizer runs for now), quoting the line.
  - Compare the physics and script hashes with Linux's.
  - Format with MacPorts' clang-format.

**Both:**

- [ ] **Standing rows** (as [the series rules](../container-audit.md#rules-for-every-day)):
  - docs (`ctest -L docs`) on each machine;
  - `/code-review high` over each machine's diff, recorded below;
  - `STATUS.md` and `KNOWN-ISSUES.md` (what still waits for which machine); commit and push.

## Runs

## Post-implementation code review

**Done:**
- The D3D12, Metal and `select` audit items are fixed.
- Both machines build clean in every configuration and pass their scoped runs.
- The physics and script hashes equal Linux's, or the difference is recorded in `KNOWN-ISSUES.md`.
