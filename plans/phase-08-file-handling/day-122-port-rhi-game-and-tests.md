# Phase 8 / Day 122 — Port 2: RHI Pipeline Cache, Textures, Demos and Test Helpers

**Phase:** [Phase 8 — File Handling](../phase-08-file-handling.md)
**Depends on:** Day 121.

## Tasks

- [ ] `engine/rhi/vulkan/vulkan_device_pipelines.cpp`: the pipeline cache directory lookup (`ENGINE_RHI_CACHE_DIR`, `$XDG_CACHE_HOME`, `$HOME/.cache`) becomes `UserCacheDirectory` plus the override; creating the directory, reading the cache file, and writing it (`mkdir`, `stat`,
      `fopen`, `rename`) become `CreateDirectories`, `File::Open` + `ReadAll`, and `WriteFileAtomic`; the file-name identity rules (vendor, device, driver) are unchanged and keep their tests; a cache file that is damaged or from another driver is still ignored with a log line.
- [ ] `DeviceCreateInfo::pipeline_cache_dir` becomes an `io::Path` (or a `StringView` converted at the boundary, as the spec decides); `rhi.h` docs updated; the null backend ignores it as before.
- [ ] `game/common/textures.cpp`: `.rtex` reading and writing (`ReadRtex`, `WriteRtex`) on `File` with statuses; `Pixels`/`RtexImage` unchanged; the mint tools use `WriteFileAtomic` so a failed write never leaves a half file.
- [ ] `game/renderer_demo` and `game/rhi_demo`: the PPM writers, the asset directory handling (`RENDERER_DEMO_ASSET_DIR`, `asset_dir` joins done with `snprintf`), the screenshot output, and `DemoScene`'s texture loading (`LoadTextureImage`) use `Path` and `File`.
- [ ] `tests/rhi/image_compare.cpp` (PPM read and write), `GoldenPath`, the temporary files of the RHI, renderer and docgen tests, and the shared test fixtures use `Path`, `File` and a common temporary-directory fixture (from Day 117) instead of `fopen`/`mkdtemp`/`system("rm -rf")`-style code.
- [ ] Tests: everything passes unchanged in behavior (pipeline cache tests, `rtex` round trips, golden comparisons, the demo ctests); new cases for a read-only cache directory (the device still works, a warning is logged), a corrupted cache file, a cache directory that is a file, and a very long cache path.
- [ ] **Documentation:** `rhi.md` ("Creating a device" cache notes) and the demo READMEs updated; reference regenerated; `docgen lint` zero; `ctest -L docs`.
- [ ] Run `scripts/test-phase.sh --affected` plus the asan and tsan flavors; the demos 1000 frames on Wayland and X11 with `--validation`; quote the lines.
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high`.
- [ ] Update `STATUS.md`, the inventory table; commit.

**Done:** the pipeline cache, the texture container code, both demos and the test helpers use `io` for every file and path, with the behavior-preserving tests passing and the new failure cases covered.
