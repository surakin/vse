# Phase 8 / Day 122 — Port 2: RHI Pipeline Cache, Textures, Demos and Test Helpers

**Phase:** [Phase 8 — File Handling](../phase-08-file-handling.md)
**Depends on:** Day 121.

## Tasks

- [x] `engine/rhi/vulkan/vulkan_device_pipelines.cpp`: the pipeline cache directory lookup (`ENGINE_RHI_CACHE_DIR`, `$XDG_CACHE_HOME`, `$HOME/.cache`) becomes `UserCacheDirectory` plus the override; creating the directory, reading the cache file, and writing it (`mkdir`, `stat`,
      `fopen`, `rename`) become `CreateDirectories`, `File::Open` + `ReadAll`, and `WriteFileAtomic`; the file-name identity rules (vendor, device, driver) are unchanged and keep their tests; a cache file that is damaged or from another driver is still ignored with a log line.
- [x] `DeviceCreateInfo::pipeline_cache_dir` becomes an `io::Path` (or a `StringView` converted at the boundary, as the spec decides); `rhi.h` docs updated; the null backend ignores it as before.
- [x] `game/common/textures.cpp`: `.rtex` reading and writing (`ReadRtex`, `WriteRtex`) on `File` with statuses; `Pixels`/`RtexImage` unchanged; the mint tools use `WriteFileAtomic` so a failed write never leaves a half file.
- [x] `game/renderer_demo` and `game/rhi_demo`: the PPM writers, the asset directory handling (`RENDERER_DEMO_ASSET_DIR`, `asset_dir` joins done with `snprintf`), the screenshot output, and `DemoScene`'s texture loading (`LoadTextureImage`) use `Path` and `File`.
- [x] `tests/rhi/image_compare.cpp` (PPM read and write), `GoldenPath`, the temporary files of the RHI, renderer and docgen tests, and the shared test fixtures use `Path`, `File` and a common temporary-directory fixture (from Day 117) instead of `fopen`/`mkdtemp`/`system("rm -rf")`-style code.
- [x] Tests: everything passes unchanged in behavior (pipeline cache tests, `rtex` round trips, golden comparisons, the demo ctests); new cases for a read-only cache directory (the device still works, a warning is logged), a corrupted cache file, a cache directory that is a file, and a very long cache path.
- [x] **Documentation:** `rhi.md` ("Creating a device" cache notes) and the demo READMEs updated; reference regenerated; `docgen lint` zero; `ctest -L docs`.
- [x] Run `scripts/test-phase.sh --affected` plus the asan and tsan flavors; the demos 1000 frames on Wayland and X11 with `--validation`; quote the lines.
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high`.
- [ ] Update `STATUS.md`, the inventory table; commit.

**Done:** the pipeline cache, the texture container code, both demos and the test helpers use `io` for every file and path, with the behavior-preserving tests passing and the new failure cases covered.

## Result

- Pipeline cache (`vulkan_device_pipelines.cpp`): reading is `io::Stat` + `io::ReadFile` (a missing file, a directory, an empty or oversized one is ignored), writing is `io::CreateDirectories` + `io::WriteFileAtomic`; the directory is the explicit one, then `ENGINE_RHI_CACHE_DIR`, then `io::UserCacheDirectory` (application name sanitized by `SanitizePipelineCacheAppName`, new pure function with tests). `ChoosePipelineCacheDir` shrank to the two explicit sources. A configured directory that does not fit means no cache; it never falls back to another one. `DeviceCreateInfo::pipeline_cache_dir` stays a `const char*` (converted to a `Path` at the boundary): the spec's second option, because the field is part of a plain-data descriptor shared with the null backend and the demos' command lines.
- `game/common/textures.cpp`: `ReadRtex` and `WriteRtex` take an `io::Path` and return an `io::Status` (`Invalid` for a malformed or truncated file); writing is atomic. `renderer_demo_mint`, `DemoScene`, both demos' PPM writers and screenshot paths, `rhi_demo`'s asset path and `rhi_bench`'s cache directory use `Path`/`File`/`TempDirectory`/`RemoveAll`.
- Tests: `tests/rhi/image_compare.cpp` (PPM read and write in memory over `io::ReadFile`/`WriteFile`, `TempPath` from `io::TempDirectory`), the golden, textured-cube and pipeline-cache tests (shared `tests/io/temp_dir.h` fixture, `io::File` offset patches instead of `fopen`/`fseek`); three new cases for a cache directory that is a file, one that cannot be written, and one whose path is too long: the device keeps working. A `deep` directory test in `tests/io` leaked a temp directory per run (its tree was too deep for the fixture's own `RemoveAll`); it now thins the tree before cleanup.
- Left raw on purpose (documented for the Day 124 gate as exception E4, measuring the process rather than touching files): the `/proc/self/statm` and `/proc/self/fd` readers in `rhi_demo`, `renderer_demo`, `device_loss_recovery_test` and `vulkan_reset_golden_test`. Environment reads (`getenv`) are not file access.
- Docs: `rhi.md` (pipeline cache), `rhi.h` field comment, demo README; baseline 3390/3390.
- Scoped runs (after the review fixes: all pass, the known X11 window test passed too): `ran 1256 of 2899 tests (phases: all; flavors: plain; display: included; crash tests: included)`, `ran 808 of 2899 ... flavors: asan`, `ran 527 of 2899 ... flavors: tsan`; `ctest -L docs` 4/4. `rhi_demo` and `renderer_demo --frames 1000 --validation` ran on Wayland and X11 to the end (the last lines show flat memory: rhi_demo 60104 to 60136 KiB on Wayland, 58872 to 58896 on X11; renderer_demo 68536 to 68572 and 65868 to 65884, 18 open descriptors, 0 resets).
- Housekeeping: earlier runs of the `tests/io` executables had left 72 `re_io_test.*` directories in `/tmp` (the leak above plus runs I interrupted). A bulk `rm` was blocked by the sandbox's permission check, so they are still there; they are safe to delete by hand.

## Post-implementation code review

`/code-review high`: 8 findings, 7 fixed, 1 declined.

Fixed:
1. The "was a directory configured?" test existed twice (policy function and caller) and the function returned false for two reasons: `ChoosePipelineCacheDir` now returns `PipelineCacheDirChoice` (`NotConfigured`, `Chosen`, `TooLong`), so a configured directory that does not fit can never fall back (tests for all three).
2. `TempPath` ignored a failing `TempDirectory` and would have written into the working directory: it falls back to `/tmp` as before.
3. The demo's texture warning lost the path and the reason: it prints the full path and the status name (or "the format differs").
4. The PPM writer was written three times: the two demos share `demo::WritePpm(io::Path, ...)` in `game/common`; the test helper keeps its own (it reads `Image`, and the renderer tests do not all link `game/common`).
5. Include order in `demo_scene.cpp` (own header first again).
6. The thinning loop of the deep-tree test removed from a rebuilt path each time and assumed `RemoveAll` had deleted nothing: it now walks up with `Parent()`, with the reason for the assumption in a comment (the refusal happens before anything below the limit is deleted).
7. The no-op `cleanup` lambda of the pipeline-cache test is gone, `REQUIRE_FALSE`-style checks became comparisons on the enum, and the `snprintf` of the temp path is guarded.

Declined: `WriteRtex` builds a combined buffer (a second copy of the payload) so `WriteFileAtomic` can write it in one piece: it runs in the mint tool and in the golden-update path only, never at runtime, and the textures are small (the largest committed asset is 256x256).
