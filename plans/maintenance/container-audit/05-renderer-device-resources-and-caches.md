# Container audit / Day 5: Renderer I: DeviceResources without a cap, the material and theme caches, UI

**Series:** [the container audit fixes](../container-audit.md)
**Depends on:** Days 2-3.

`DeviceResources` holds at most 256 owners in a fixed C array. Every `GpuMesh`, `GpuTexture` and `GpuSampler` registers there, and a mesh fails to
create when it is full, so a level with more than about 240 meshes plus textures does not load. Two per-frame caches are scanned linearly: the material
pipeline cache for each object, and the theme cache for each widget.

## Tasks

- [ ] **`DeviceResources` without a cap** (`renderer/device_resources.h:83,172`, `.cpp`):
  - the entries become a `Vector` (also removing the raw array);
  - registration and unregistration are O(1) amortized: an index stored in the `ScopedRegistration`, with swap-remove, or a free list. The reset's tier
    order and its registration sequence order are kept: Release and Recreate still walk by tier, then by sequence;
  - the O(n²) selection loops in `device_resources.cpp:102-114,140-152` become one sort by (tier, sequence) per reset;
  - registration during a reset is still refused.
  - Tests:
    - 10,000 meshes plus textures register, reset and recreate, with the counts equal (`tests/renderer/reset_rig.h`);
    - `mesh_topology_test.cpp:628`, which fills the table to the cap, is rewritten to test the new behavior;
    - show the new test failing on the old code.
- [ ] **The material pipeline cache** (`renderer/material.cpp:1803-1806`, called per object from `gbuffer.cpp:530`):
  - a `HashMap` from a key hash (`HashCombine` of template, generation, key bits, target, layout, topology, restart) to the entry index, with the
    full equality kept on a hit;
  - `max_pipelines` and the eviction rules unchanged;
  - the hit and miss statistics unchanged.
  - Test: hits and misses equal to the old cache over a scripted sequence.
  - Day 1 number before/after.
- [ ] **The theme cache** (`ui/theme.cpp:629-653`):
  - a `HashMap` keyed by (kind, variant, state, scale bits);
  - at `kMaxCache` the cache evicts (by a clock or a generation counter) instead of `Clear()`, so it doesn't thrash;
  - a cache hit for a missing style no longer builds a `String`: the missing-style log is a `HashSet` of (kind, variant), and it formats only the first time.
  - Test: resolved styles equal the old ones over the UI test suite.
  - Day 1 number before/after.
- [ ] **UI and renderer scans:**
  - `ui_renderer.cpp:395-409`: stop at the match, or index `images_` by handle;
  - `ui/debug_overlay.cpp:128-149`: compute the filtered line list once per filter or content change, not once per row;
  - `ui/callback.h:65-69` `Invoke`: no heap snapshot per call. Use a reentrancy-safe iteration, such as a generation check or a small inline buffer, with a test of removal during `Invoke`;
  - `ui/widgets/text_field.cpp:466-478`: edit the buffer in place (`Insert`/`RemoveRange`), not by rebuilding it;
  - `RegisterVariant` (`theme.cpp:51`): keyed by name.
- [ ] **Standing rows** (as [the series rules](../container-audit.md#rules-for-every-day)):
  - runs: `--affected` (renderer, ui), plain, then asan and tsan (registration lifetime);
  - display tests only if the user allows them that day, otherwise `--no-display` and recorded;
  - the golden-picture tests on Vulkan when a display is allowed;
  - builds: both compilers, both unity modes;
  - docs (`docs/guide/renderer.md` "Surviving a reset": no owner cap); `ctest -L docs`;
  - `/code-review high` recorded below;
  - `STATUS.md` and `KNOWN-ISSUES.md` (the cap entry removed); commit and push.

## Runs

## Post-implementation code review

**Done:**
- Any number of device-resource owners registers and survives a reset, shown with 10,000 owners.
- The material and theme caches are hashed, and the theme cache no longer thrashes at its cap, each with its Day 1 number before and after.
- The UI scans listed above are fixed or declined with a reason.
