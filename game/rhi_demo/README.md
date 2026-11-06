# rhi_demo

The RHI's reference client: a textured, depth-tested cube lit by one directional light that orbits it, drawn through
the Vulkan backend into a Wayland or X11 window (or offscreen). Nothing here includes a Vulkan header except the
backend header used to create the device.

## Build and run

```
cmake --build build --target rhi_demo
./build/game/rhi_demo/rhi_demo                           # a window, until you close it
./build/game/rhi_demo/rhi_demo --backend x11             # force a display backend (or wayland)
./build/game/rhi_demo/rhi_demo --frames 1000 --validation   # a bounded run with the validation layer
./build/game/rhi_demo/rhi_demo --screenshot out.ppm      # offscreen, no window: writes a 256x256 PPM
```

## Options

| Option | Meaning |
|---|---|
| `--backend wayland\|x11` | display backend; default automatic (Wayland first). Same as `ENGINE_WINDOW_BACKEND` |
| `--frames N` | stop after N frames (also `RHI_DEMO_FRAMES=N`); a run of 200 or more frames also checks that memory did not grow by more than 8 MiB between frame 100 and the end |
| `--screenshot FILE` | render offscreen with a fixed camera and light, with no window, and write the last frame to FILE as a binary PPM (N frames with `--frames`) |
| `--no-vsync` | present with `Immediate` instead of `Fifo` |
| `--gpu G` | the GPU to use: an id from `--list-gpus`, an index, or part of its name; a GPU that does not exist fails (exit 1) and never falls back |
| `--list-gpus` | print the GPUs and exit |
| `--validation` | enable the Vulkan validation layer with synchronization validation |
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
| `/tests/rhi/vulkan_lit_cube_test.cpp` | the golden and analytic lighting tests |
