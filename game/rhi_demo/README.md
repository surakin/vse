# rhi_demo

The RHI's reference client: a textured, depth-tested cube lit by one directional light that orbits it, drawn through
the RHI into a window (Wayland or X11 on Linux, Win32 on Windows, Cocoa on macOS) or offscreen. The backend is the `renderer.backend` setting
(`--rhi`): `auto` is Direct3D 12 on Windows, Metal on macOS and Vulkan on Linux; `vulkan`, `dx12` and `metal` name one. Nothing here includes a backend header.

## Build and run

```
cmake --build build --target rhi_demo
./build/game/rhi_demo/rhi_demo                           # a window, until you close it
./build/game/rhi_demo/rhi_demo --backend x11             # force a display backend (or wayland)
./build/game/rhi_demo/rhi_demo --frames 1000 --validation   # a bounded run with the validation layer
./build/game/rhi_demo/rhi_demo --screenshot out.ppm      # offscreen, no window: writes a 256x256 PPM
```

On Windows (Phase 13), from a terminal in the checkout (`BUILD.md` has the prerequisites; `build-msvc` is configured with the MSVC toolchain file):

```
cmake --build build-msvc --target rhi_demo
build-msvc\game\rhi_demo\rhi_demo.exe --frames 1000 --validation
build-msvc\game\rhi_demo\rhi_demo.exe --help              # printed to the terminal it was started from
```

It is a windowed program (the Windows subsystem, `ENGINE_WINDOWED_PROGRAM`): started from Explorer it opens no console window; started from a
terminal its output goes to that terminal. Its settings file is `%APPDATA%\rhi_demo\settings.cfg`, its crash dumps go to
`%LOCALAPPDATA%\rhi_demo\crashes`.

On macOS (Phase 21), with the `macos-clang` preset (`BUILD.md`): the device is Metal (`auto` picks it; there is no Vulkan on macOS).

```
cmake --build --preset macos-clang --target rhi_demo
./build-macos/game/rhi_demo/rhi_demo --frames 1000
```

Run Metal's validation with `MTL_DEBUG_LAYER=1 MTL_DEBUG_LAYER_ERROR_MODE=assert` (and `MTL_SHADER_VALIDATION=1`) in the environment: `--validation`
cannot turn it on after Metal starts. Its settings file is `~/Library/Application Support/rhi_demo/settings.cfg`, its crash dumps go to
`~/Library/Application Support/rhi_demo/crashes`, and its pipeline cache to `~/Library/Caches/engine/`.

## Options

| Option | Meaning |
|---|---|
| `--backend wayland\|x11\|win32\|cocoa` | display backend; default automatic (Wayland first on Linux, Win32 on Windows, Cocoa on macOS). Same as `ENGINE_WINDOW_BACKEND` |
| `--frames N` | stop after N frames (also `RHI_DEMO_FRAMES=N`); a run of 200 or more frames also checks that memory did not grow by more than 8 MiB between frame 100 and the end |
| `--screenshot FILE` | render offscreen with a fixed camera and light, with no window, and write the last frame to FILE as a binary PPM (N frames with `--frames`) |
| `--no-vsync` | present with `Immediate` instead of `Fifo` |
| `--gpu G` | the GPU to use: an id from `--list-gpus`, an index, or part of its name; a GPU that does not exist fails (exit 1) and never falls back |
| `--list-gpus` | print the backend and its GPUs, and exit |
| `--rhi NAME` | the graphics backend: `auto` (default), `vulkan`, `dx12` or `null`; the `renderer.backend` setting |
| `--validation` | enable the backend's validation layer with synchronization validation (Direct3D 12: GPU-based validation) |
| `--help` | print the options |

Exit codes: 0 success, 1 a runtime failure (no device, window, swapchain, or a failed draw, or memory growth), 2 a
usage error. Other environment variables: `ENGINE_RHI_VALIDATION*` (see the RHI guide), `ENGINE_RHI_GPU` to pick a GPU.

## Controls

Left and right arrows orbit the camera around the cube, up and down tilt it; dragging with the left mouse button does
the same; the wheel zooms. The light circles the cube every eight seconds and the background color drifts.

## What it shows

- the frame loop (`BeginFrame`, `Acquire`, record, `Submit`, `EndFrame`, `Present`) and a depth buffer that follows
  window resizes;
- indexed drawing, a `UniformRing` slice and a transient bind group per draw, the lit pipeline (`shaders/lit.*`);
- a mipmapped sRGB texture loaded from `assets/checker.rtex` (a throwaway raw container; regenerate it with
  `ENGINE_RHI_UPDATE_GOLDEN=1 ./build/tests/rhi/rhi_tests "The demo asset is a valid rtex file"`), sampled with trilinear
  filtering and anisotropy 16.

The offscreen screenshot target is linear (UNORM), so it looks darker than the window, whose swapchain is sRGB.

## Where things are

| | |
|---|---|
| `main.cpp` | options, window loop, offscreen path |
| `../common/cube_scene.*` | projection, look-at, the cube mesh, `CubeRenderer` (shared with the tests) |
| `../common/textures.*` | procedural textures, the `.rtex` container |
| `/shaders/lit.vert`, `lit.frag` | the lighting math |
| `/tests/rhi/lit_cube_test.cpp` | the golden and analytic lighting tests |

## Settings options

Every program built on the settings system accepts these options, the settings of `--list-settings`, and the environment variables below. The list is generated from the registry; the ctest `rhi_demo_readme_current` fails when this block differs from `--help`.

```
Settings:
  --set KEY=VALUE         set a setting (repeatable; the value may be quoted); --list-settings shows the keys
  --settings FILE         read settings from FILE (above the project file, below the environment)
  --save-settings[=FILE]  write the settings that differ from their defaults to FILE (default: the user's settings file) and exit
  --list-settings         print every setting: its value, default, where it came from and what it is for, and exit
  --backend NAME          platform.window_backend: the display backend: wayland or x11 on Linux, win32 on Windows, cocoa on macOS (auto picks Wayland, then X11, on Linux)
  --gpu TEXT              renderer.adapter: the GPU: an id from --list-gpus, an index, or part of the name (empty: the best one)
  --rhi NAME              renderer.backend: the graphics backend: auto (the preferred one this program has and this machine runs), null (no GPU), vulkan, dx12 or metal
Environment: ENGINE_SETTING_<KEY> sets any setting (the key in capitals, dots written as __, e.g. ENGINE_SETTING_RENDERER__PRESENT_MODE); also:
  ENGINE_WINDOW_BACKEND                 platform.window_backend
  ENGINE_RHI_GPU                        renderer.adapter
  ENGINE_RHI_VALIDATION_SYNC            renderer.sync_validation
  ENGINE_RHI_CACHE_DIR                  rhi.pipeline_cache_dir
```
