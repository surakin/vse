# Maintenance: a GitHub Actions workflow runs the tests on the three platforms

Not a numbered phase, no day files. Ordered by the user (2026-11-25), who chose: **plain flavor only**, and **an Apple silicon runner** for macOS.

## What

`.github/workflows/ci.yml`: on a push to `main` or a pull request that changes code (`engine/`, `game/`, `tools/`, `tests/`, `include/`, `shaders/`, `assets/`, `abi/`, `cmake/`, `third_party/`, the root `CMakeLists.txt`, `CMakePresets.json`, `scripts/test-phase.sh`, the phase files and the workflow itself; never documentation, plans or status files), or on a manual run, three jobs configure, build and run `scripts/test-phase.sh --all --no-display`:

| Job | Runner | Preset |
|---|---|---|
| Linux x86-64 | `ubuntu-24.04` (apt: clang, cmake, ninja, the Wayland/X11/Vulkan headers, lavapipe) | `linux` |
| Windows x64 | `windows-2025` (MSVC of the image, the Vulkan SDK from `humbletim/setup-vulkan-sdk`) | `windows-msvc` |
| macOS arm64 | `macos-15` | `macos-clang` with `-DCMAKE_OSX_ARCHITECTURES=arm64` |

A failed job uploads `<build>/Testing/Temporary/`. No sanitizer flavors, no window tests (`--no-display`): the runners have no GPU and no desktop session.

## Why these choices

- macOS: the preset is x86-64. On an Apple silicon host the root `CMakeLists.txt` allows an arm64 build that is not compile-only, so the same preset with the architecture overridden builds and runs natively. **The engine's arm64 macOS code has so far only been compiled** (`macos-clang-arm64`, compile-only on the Intel Mac), never run: the first runs may fail for that reason.
- `--all` includes the crash and soak tests, so a run is long; the jobs have the six-hour limit.
- No `slangc`: the build uses the committed shader outputs, as `BUILD.md` says.

## Tasks

- [x] `.github/workflows/ci.yml`
- [x] `BUILD.md`: a section on the CI runners
- [x] `KNOWN-ISSUES.md`: the workflow has not run; what a hosted runner cannot cover
- [x] First run on GitHub (Linux 88 failures: fixed except the hinge hash; macOS had no runner; see `KNOWN-ISSUES.md`)
- [x] Second and third runs: Linux down to the hinge hash (constant not compared on a runner), Windows failures fixed except the two crash-report tests (see `KNOWN-ISSUES.md`)
- [x] macOS arm64 failures of the fourth run: fixes written (see `KNOWN-ISSUES.md`), unrun on arm64
- [ ] Fifth run: Windows crash-report tests, macOS arm64
- [ ] (original) First run on GitHub, and the fixes it needs (the images' compilers, the Vulkan SDK action, tests that need a GPU, tests that fail on arm64): recorded in `KNOWN-ISSUES.md` as they appear

**Done:** the workflow file parses as YAML and uses only presets and scripts that exist. It has not run: the first push to GitHub is the first test.

## Post-implementation code review

Not run: the change adds no engine code (a workflow file and documentation).
