# renderer_demo

The Phase 5 showcase: a tiled ground, a few hundred spheres and cubes in eight materials, hundreds of point lights, a sun and
a little ambient light, drawn by the deferred renderer (G-buffer, light volumes, ACES tonemap) into a window (Wayland or X11
on Linux, Win32 on Windows) or offscreen. `rhi_demo` stays as the RHI's own reference client; this one goes through `renderer::Renderer` and the render
graph and never names a backend: the device is Vulkan or Direct3D 12 by the `renderer.backend` setting (`--rhi`; `auto` is
Direct3D 12 on Windows), and the `backend` setting switches it while the demo runs.

## Build and run

```
cmake --build build --target renderer_demo
./build/game/renderer_demo/renderer_demo                          # a window, until you close it
./build/game/renderer_demo/renderer_demo --backend x11            # force a display backend (or wayland)
./build/game/renderer_demo/renderer_demo --frames 1000 --validation   # a bounded run with sync validation
./build/game/renderer_demo/renderer_demo --screenshot out.ppm     # offscreen, no window: a 640x360 PPM
./build/game/renderer_demo/renderer_demo --gbuffer normal         # show a G-buffer layer instead of the lit picture
./build/game/renderer_demo/renderer_demo --objects 2000 --lights 1000
```

On Windows (Phase 13), from a terminal in the checkout (`BUILD.md` has the prerequisites; `build-msvc` is configured with the MSVC toolchain file):

```
cmake --build build-msvc --target renderer_demo
build-msvc\game\renderer_demo\renderer_demo.exe --frames 1000 --validation
build-msvc\game\renderer_demo\renderer_demo.exe --help              # printed to the terminal it was started from
```

It is a windowed program (the Windows subsystem, `ENGINE_WINDOWED_PROGRAM`): started from Explorer it opens no console window; started from a
terminal its output goes to that terminal. Its settings file is `%APPDATA%\renderer_demo\settings.cfg`, its crash dumps go to
`%LOCALAPPDATA%\renderer_demo\crashes`.

## Options

| Option | Meaning |
|---|---|
| `--backend wayland\|x11\|win32` | display backend; default automatic (Wayland first on Linux, Win32 on Windows). Same as `ENGINE_WINDOW_BACKEND` |
| `--frames N` | stop after N frames (also `RENDERER_DEMO_FRAMES=N`); a run of 200 or more frames also checks that resident memory did not grow by more than 8 MiB between frame 100 and the end |
| `--screenshot FILE` | render offscreen with a fixed camera and time, with no window, and write the last frame to FILE as a binary PPM |
| `--objects N` | total objects, the 49 ground tiles included, 64 to 20000 (default 540) |
| `--lights N` | point lights, 0 to 16000 (default 300) |
| `--gbuffer LAYER` | `albedo`, `normal`, `material`, `depth` or `worldpos` instead of the lit picture |
| `--no-vsync` | present with `Immediate` instead of `Fifo` (the same as pressing F1 once the window is open) |
| `--gpu G` | the GPU to use: an id from `--list-gpus`, an index, or part of its name. A GPU that does not exist or cannot present fails (exit 1); it never falls back to another one |
| `--list-gpus` | print the backend and its GPUs (id, name, kind, memory, driver, and why one is unsuitable) and exit |
| `--rhi NAME` | the graphics backend: `auto` (default), `vulkan`, `dx12` or `null`; the `renderer.backend` setting |
| `--text` | draw the text overlay (a greeting and a status line) over the picture; in screenshot mode it is off unless given (F11 toggles it in the window) |
| `--prewarm` | after a device reset rebuild every pipeline at once instead of when it is first used |
| `--script WORDS` | run the live paths without a keyboard (below) |
| `--validation` | enable the backend's validation layer with synchronization validation (Direct3D 12: GPU-based validation) |
| `--inject-device-loss F`, `--inject-oom N` | fault injection (see the tests) |
| `--help` | print the options |

Exit codes: 0 success, 1 a runtime failure (no device, window or swapchain, a failed frame, or memory growth), 2 a usage
error, 3 the GPU device was lost (the demo stops cleanly). Other environment variables: `ENGINE_RHI_VALIDATION*` (see the RHI
guide). `--gpu` is the preferred way to pick a GPU; `ENGINE_RHI_GPU` still works when `--gpu` is not given.

Once a second the demo logs the average frame time, the objects and draw calls, and the point lights (written, culled by the
frustum, dropped for lack of room) and light volume draws.

## Controls

Orbit camera (default): arrow keys or dragging with the left mouse button turn and tilt it, the wheel zooms. **Tab** switches
to the fly camera: W/A/S/D move, Q/E (or Space) go down/up, Shift is faster, arrows or dragging look around. The point lights
drift slowly around where they were placed.

## Changing settings while it runs

The window demo goes through `renderer::Renderer::ApplySettings`, so rendering settings change without restarting. Keys (printed at
startup, and every change is logged with its cost class and the time it took):

| Key | Setting | Cost class |
|---|---|---|
| F1 | vsync on/off (`present_mode` fifo/immediate) | swapchain |
| F2 | validation on/off | device reset |
| F3 | next GPU | device reset |
| F4 | render scale (no consumer yet) | dynamic |
| F5 | fullscreen | swapchain |
| F6 | swapchain format (auto, srgb8, rgba16f) | swapchain |
| F7 | shader quality (low, medium, high; no consumer yet, so it only rebuilds the pipelines) | pipeline rebuild |
| F9 | force a device reset with the settings unchanged | device reset |
| F11 | text overlay on/off (not a setting: it only adds the text pass) | none |
| F10 | tell the device it is lost (it recovers; see the renderer guide, "Device loss and recovery") | recovery |

While a change runs the window keeps its last picture; the log says what is happening and, afterwards, how long the first good frame
took. There is no progress display: a device reset takes about 40 ms here (see the Day 113 measurements in
`plans/phase-08-rhi-reset/day-113-demo-seams-and-measurements.md`).

`--script` takes space-separated steps: `frame:N` (run until N frames were drawn), `set:key=value` (a setting by its name, for example
`set:present_mode=immediate`), `reset`, `gpu:next`, `loss` (lose the device), `overlay` (toggle the text overlay), `quit`. The keys are off while a script runs. Example:

```
renderer_demo --validation --frames 120 --script "frame:10 set:present_mode=immediate frame:20 reset frame:30 set:shader_quality=low frame:40 loss frame:100 quit"
```

`--inject-device-loss F` still means a terminal loss (exit 3): it turns recovery off so the tests can see the old behavior.

## What it shows

- the frame loop (`Renderer::RenderFrame`) with a render graph of three passes (G-buffer, lighting with light volumes,
  tonemap) and a depth buffer that follows window resizes;
- eight materials: plain, checker and stripe albedo textures, a normal map, smooth gold, rough copper, an emissive one and
  per-vertex colors; the objects are instanced by (pipeline, material, mesh), so 540 objects take a handful of draw calls;
- hundreds of point lights, each a scissored additive sphere draw, culled against the view frustum.

## Assets

`assets/*.rtex` are written (atomically, through the file layer) by `renderer_demo_mint` from the procedural code in `demo_assets.cpp` (a checker, diagonal stripes
and a tangent-space normal map of round bumps): no binary file is without a way to regenerate it.

```
cmake --build build --target renderer_demo_mint
./build/game/renderer_demo/renderer_demo_mint game/renderer_demo/assets
```

Minting is deterministic: the ctest `renderer_demo_assets_current` writes the files twice and compares both with the committed
ones, byte for byte. A texture file that is missing or unreadable is replaced by its procedural version at startup (a warning
is logged), so a checkout without the files still runs.

## Where things are

| | |
|---|---|
| `main.cpp` | options, the window loop and its keys and script, the offscreen path, the cameras |
| `demo_scene.*` | the scene: meshes, textures, materials, and the objects, lights and camera as ECS entities (a schedule moves the lights, propagates transforms and mirrors them into the `RenderScene`; also used by the tests) |
| `demo_frame.*` | material system, per-frame buffers, passes and `Renderer` around them (also used by the tests) |
| `demo_assets.*`, `mint_assets.cpp` | the procedural textures and the tool that writes them |
| `/tests/rhi/renderer_demo_gpu_test.cpp` | the golden image, draw-order independence and the crossing-planes depth test |
| `/tests/rhi/golden/renderer_demo.ppm` | the golden image (regenerate with `ENGINE_RHI_UPDATE_GOLDEN=1`, then look at it) |

## Tests

`ctest -L demo` runs the demo's own checks: a screenshot with validation on, every G-buffer layer, device loss at frame 3
(exit 3), allocation failures (exit 1, no crash), bad options (exit 2), no Vulkan loader, and the asset check; on Windows the screenshot, the
loss, the allocation failure and the scripted run also run with `--rhi dx12`.

## Settings options

Every program built on the settings system accepts these options, the settings of `--list-settings`, and the environment variables below. The list is generated from the registry; the ctest `renderer_demo_readme_current` fails when this block differs from `--help`.

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
