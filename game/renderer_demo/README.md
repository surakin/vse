# renderer_demo

The Phase 4 showcase: a tiled ground, a few hundred spheres and cubes in eight materials, hundreds of point lights, a sun and
a little ambient light, drawn by the deferred renderer (G-buffer, light volumes, ACES tonemap) into a Wayland or X11 window,
or offscreen. `rhi_demo` stays as the RHI's own reference client; this one goes through `renderer::Renderer` and the render
graph and never names Vulkan except to create the device.

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

## Options

| Option | Meaning |
|---|---|
| `--backend wayland\|x11` | display backend; default automatic (Wayland first). Same as `ENGINE_WINDOW_BACKEND` |
| `--frames N` | stop after N frames (also `RENDERER_DEMO_FRAMES=N`); a run of 200 or more frames also checks that resident memory did not grow by more than 8 MiB between frame 100 and the end |
| `--screenshot FILE` | render offscreen with a fixed camera and time, with no window, and write the last frame to FILE as a binary PPM |
| `--objects N` | total objects, the 49 ground tiles included, 64 to 20000 (default 540) |
| `--lights N` | point lights, 0 to 16000 (default 300) |
| `--gbuffer LAYER` | `albedo`, `normal`, `material`, `depth` or `worldpos` instead of the lit picture |
| `--no-vsync` | present with `Immediate` instead of `Fifo` |
| `--validation` | enable the Vulkan validation layer with synchronization validation |
| `--inject-device-loss F`, `--inject-oom N` | fault injection (see the tests) |
| `--help` | print the options |

Exit codes: 0 success, 1 a runtime failure (no device, window or swapchain, a failed frame, or memory growth), 2 a usage
error, 3 the GPU device was lost (the demo stops cleanly). Other environment variables: `ENGINE_RHI_VALIDATION*` (see the RHI
guide), `ENGINE_RHI_GPU` to pick a GPU.

Once a second the demo logs the average frame time, the objects and draw calls, and the point lights (written, culled by the
frustum, dropped for lack of room) and light volume draws.

## Controls

Orbit camera (default): arrow keys or dragging with the left mouse button turn and tilt it, the wheel zooms. **Tab** switches
to the fly camera: W/A/S/D move, Q/E (or Space) go down/up, Shift is faster, arrows or dragging look around. The point lights
drift slowly around where they were placed.

## What it shows

- the frame loop (`Renderer::RenderFrame`) with a render graph of three passes (G-buffer, lighting with light volumes,
  tonemap) and a depth buffer that follows window resizes;
- eight materials: plain, checker and stripe albedo textures, a normal map, smooth gold, rough copper, an emissive one and
  per-vertex colors; the objects are instanced by (pipeline, material, mesh), so 540 objects take a handful of draw calls;
- hundreds of point lights, each a scissored additive sphere draw, culled against the view frustum.

## Assets

`assets/*.rtex` are written by `renderer_demo_mint` from the procedural code in `demo_assets.cpp` (a checker, diagonal stripes
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
| `main.cpp` | options, the window loop, the offscreen path, the cameras |
| `demo_scene.*` | the scene: meshes, textures, materials, objects, lights (also used by the tests) |
| `demo_frame.*` | material system, per-frame buffers, passes and `Renderer` around them (also used by the tests) |
| `demo_assets.*`, `mint_assets.cpp` | the procedural textures and the tool that writes them |
| `/tests/rhi/vulkan_renderer_demo_test.cpp` | the golden image, draw-order independence and the crossing-planes depth test |
| `/tests/rhi/golden/renderer_demo.ppm` | the golden image (regenerate with `ENGINE_RHI_UPDATE_GOLDEN=1`, then look at it) |

## Tests

`ctest -L demo` runs the demo's own checks: a screenshot with validation on, every G-buffer layer, device loss at frame 3
(exit 3), allocation failures (exit 1, no crash), bad options (exit 2), no Vulkan loader, and the asset check.
