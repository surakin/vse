# Container audit / Day 5: Renderer I: DeviceResources without a cap, the material and theme caches, UI

**Series:** [the container audit fixes](../container-audit.md)
**Depends on:** Days 2-3.

`DeviceResources` holds at most 256 owners in a fixed C array. Every `GpuMesh`, `GpuTexture` and `GpuSampler` registers there, and a mesh fails to
create when it is full, so a level with more than about 240 meshes plus textures does not load. Two per-frame caches are scanned linearly: the material
pipeline cache for each object, and the theme cache for each widget.

## Tasks

- [x] **`DeviceResources` without a cap** (`renderer/device_resources.h:83,172`, `.cpp`):
  - the entries become a `Vector` (also removing the raw array);
  - registration and unregistration are O(1) amortized: an index stored in the `ScopedRegistration`, with swap-remove, or a free list. The reset's tier
    order and its registration sequence order are kept: Release and Recreate still walk by tier, then by sequence;
  - the O(n²) selection loops in `device_resources.cpp:102-114,140-152` become one sort by (tier, sequence) per reset;
  - registration during a reset is still refused.
  - Tests:
    - 10,000 meshes plus textures register, reset and recreate, with the counts equal (`tests/renderer/reset_rig.h`);
    - `mesh_topology_test.cpp:628`, which fills the table to the cap, is rewritten to test the new behavior;
    - show the new test failing on the old code.
- [x] **The material pipeline cache** (`renderer/material.cpp:1803-1806`, called per object from `gbuffer.cpp:530`):
  - a `HashMap` from a key hash (`HashCombine` of template, generation, key bits, target, layout, topology, restart) to the entry index, with the
    full equality kept on a hit;
  - `max_pipelines` and the eviction rules unchanged;
  - the hit and miss statistics unchanged.
  - Test: hits and misses equal to the old cache over a scripted sequence.
  - Day 1 number before/after.
- [x] **The theme cache** (`ui/theme.cpp:629-653`):
  - a `HashMap` keyed by (kind, variant, state, scale bits);
  - at `kMaxCache` the cache evicts (by a clock or a generation counter) instead of `Clear()`, so it doesn't thrash;
  - a cache hit for a missing style no longer builds a `String`: the missing-style log is a `HashSet` of (kind, variant), and it formats only the first time.
  - Test: resolved styles equal the old ones over the UI test suite.
  - Day 1 number before/after.
- [x] **UI and renderer scans:**
  - `ui_renderer.cpp:395-409`: stop at the match, or index `images_` by handle;
  - `ui/debug_overlay.cpp:128-149`: compute the filtered line list once per filter or content change, not once per row;
  - `ui/callback.h:65-69` `Invoke`: no heap snapshot per call. Use a reentrancy-safe iteration, such as a generation check or a small inline buffer, with a test of removal during `Invoke`;
  - `ui/widgets/text_field.cpp:466-478`: edit the buffer in place (`Insert`/`RemoveRange`), not by rebuilding it;
  - `RegisterVariant` (`theme.cpp:51`): keyed by name.
- [x] **Standing rows** (as [the series rules](../container-audit.md#rules-for-every-day)):
  - runs: `--affected` (renderer, ui), plain, then asan and tsan (registration lifetime);
  - display tests only if the user allows them that day, otherwise `--no-display` and recorded;
  - the golden-picture tests on Vulkan when a display is allowed;
  - builds: both compilers, both unity modes;
  - docs (`docs/guide/renderer.md` "Surviving a reset": no owner cap); `ctest -L docs`;
  - `/code-review high` recorded below;
  - `STATUS.md` and `KNOWN-ISSUES.md` (the cap entry removed); commit and push.

## Notes

- **`DeviceResources`:**
  - a `Vector` of entries and a `HashMap<Recreatable*, u32>` index, so `Register`, `Unregister` and `Rebind` are O(1) (removal swaps the last entry in;
    the order lives in `sequence`);
  - each reset step sorts the candidates once by (tier, sequence) and visits them, re-checking the owner and its liveness. That gives the old pick
    loop's order, because registration is refused during a reset and an unregister only marks an entry dead;
  - `kMaxOwners` is gone.
  - Tests: 10,000 owners register, unregister out of order and reset twice, with every count checked. The refused-registration test makes the mesh
    from inside a reset (the remaining refusal), and leaves nothing behind.
- **Material pipeline cache:** each entry keeps its `PipelineKeyHash`, and `pipeline_index_` maps a hash to its first entry. A hit is checked field by
  field, and a collision falls back to the old scan. The slots, `max_pipelines`, the eviction and the statistics are unchanged.
- **Theme cache:**
  - a `u128` key (kind, variant, state, scale bits) indexes it;
  - at `kMaxCache` it replaces entries in turn instead of `Clear()`, so it no longer thrashes;
  - the missing-style log is a `HashSet`, and the variant's name is made only when the line is logged;
  - `RegisterVariant`'s name table has a name-to-id map.
- **UI:**
  - the image lookup stops at its match (ids are unique);
  - the debug overlay keeps its filtered line list until a line, the filter or a clear changes it;
  - `Callback::Invoke` copies up to 4 listeners to the stack (the heap copy was made on every call);
  - the text field edits with one allocation and bulk copies (a flat buffer edit stays O(n)).
- Pages touched: `docs/guide/renderer.md` ("Surviving a reset": no owner limit) and `docs/reference`.

## Runs

- **Deferred to Day 12** (the series rule), and so are the before/after numbers. Built clean: the whole Clang unity tree. Run while writing: the
  renderer's `[reset],[geometry]` (4 cases, including the new 10,000-owner test) and `[material]` (18 cases), and the UI's
  `[theme],[text_field],[debug_overlay],[callback]` (19 cases); all pass.

## Post-implementation code review

`/code-review high`: 1 finding, fixed. When `DestroyPipelines` removed the index entry of a hash, a surviving entry with the same hash (a collision)
was no longer indexed and would have been built again. Destroying now re-indexes the surviving entries. No test: it needs a 64-bit collision.

**Done:**
- Any number of device-resource owners registers and survives a reset, shown with 10,000 owners.
- The material and theme caches are hashed, and the theme cache no longer thrashes at its cap, each with its Day 1 number before and after.
- The UI scans listed above are fixed or declined with a reason.
