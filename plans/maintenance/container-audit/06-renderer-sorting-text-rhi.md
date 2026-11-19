# Container audit / Day 6: Renderer II: per-frame sorting, text relayout, bind groups, Vulkan memory

**Series:** [the container audit fixes](../container-audit.md)
**Depends on:** Days 3 and 5.

The renderer re-sorts and relays out work every frame that does not change between frames.

## Tasks

- [ ] **One sort of the visible slots:**
  - `render_scene.cpp:350` sorts them, then `gbuffer.cpp:625` sorts them again with `SortRaw` (qsort). Keep one typed `Sort` with the
    combined key, and check that the draw order (and so the golden pictures) is unchanged;
  - `frame_data.cpp:348` selects the first `max_lights_` with `NthElement`, then sorts only those;
  - `ui/draw_list.h:137-149` `SortLayers` uses `StableSort` (Day 3);
  - `shader_reflection.cpp:765` and `vulkan_device_pipelines.cpp:195-200` stay insertion sorts (at most 32 entries), declined with that reason.
- [ ] **Text not relaid out when nothing changed:**
  - `scene_frame.cpp:285` (up to 1,024 labels) and `TextRenderer::Draw` (`text_renderer.cpp:302`) keep each label's layout and redo it only when the
    text, font, size or wrap width changes;
  - `Font::SetPixelSize` returns early for the current size (`font_freetype.cpp:499`);
  - `text/layout.cpp:48-74` `AdvanceCache` lives on the font, per pixel size, so glyph advances survive between layouts. It stays bounded and is
    cleared by a font change or a device reset if it holds anything device-side (it does not);
  - `ui/debug_overlay.cpp:339-343` formats its stats text only when a value changes.
  - Tests: layouts equal the old ones, and the layout count drops to zero for an unchanged frame (a statistics counter).
  - Day 1 number before/after.
- [ ] **Bind groups:**
  - the transient groups rebuilt per texture switch (`ui_renderer.cpp:427`) and per atlas page per frame (`text_renderer.cpp:428`) are cached by their contents
    (texture handle, sampler, buffer), and rebuilt on a reset. The cache is bounded and dropped on `Release`, as the reset rule requires;
  - the Vulkan `CreateBindGroup`'s four heap allocations (`vulkan_device_bindings.cpp:315-319`) come down to one, or none, with an inline array for the usual counts.
- [ ] **Vulkan memory free list** (`rhi/vulkan/vk_memory.cpp`):
  - `Free` finds its place by `LowerBound` (the ranges are sorted);
  - `Allocate` keeps best fit, but over a size-ordered index, or is recorded as declined if Day 1 shows it flat at real heap counts;
  - freeing a dedicated allocation (`:450`) is O(1) through a stored index.
- [ ] **Small renderer allocations per frame:**
  - `text_renderer.cpp:387` `Vector<u32> first` becomes a member reused across frames;
  - `combobox.cpp:207-210` and `value_widgets.cpp:480-490` measure without a probe `TextRun` or `StringBuilder` per `Measure`;
  - `text/layout.cpp:167-210,600-612` reuses its five vectors per `Arrange`. Its flex loop is left (O(n²) only in the number of children of one box), declined unless Day 1 shows otherwise;
  - the copy loops (`text_run.cpp:17-20,122-128`, `scene_frame.cpp:168-192`, the clipboard paths in `platform/`) use `Append`.
- [ ] **Standing rows** (as [the series rules](../container-audit.md#rules-for-every-day)):
  - runs: `--affected` (renderer, rhi, text, ui, platform), plain, then asan and tsan;
  - the golden pictures on Vulkan when a display is allowed;
  - builds: both compilers, both unity modes;
  - `ctest -L docs`;
  - `/code-review high` recorded below;
  - `STATUS.md` and `KNOWN-ISSUES.md`; commit and push.

## Runs

## Post-implementation code review

**Done:**
- The visible slots are sorted once.
- Lights are selected, not fully sorted.
- An unchanged frame lays out no text.
- Transient bind groups are reused.
- The Vulkan free list frees in log time.
- Every change has its Day 1 number before and after, and the golden pictures are unchanged where they ran.
