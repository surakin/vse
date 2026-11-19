# model_viewer

Shows a glTF 2.0 file (`.glb`, or `.gltf` with its buffers and images beside it) in a window. The file is imported in memory
(`asset_import`), its default scene is spawned through the resource manager and framed, and a skinned model plays its first clip.

```
model_viewer FILE.glb|FILE.gltf [--frames N] [--screenshot out.ppm] [--clip N] [--no-animation]
             [--backend wayland|x11|win32|cocoa]
```

| Input | Does |
|---|---|
| Left-drag | Orbit around the target |
| Wheel | Zoom (toward or away from the target) |
| Right- or middle-drag | Strafe: move the target in the view plane |
| W / A / S / D, Q / E | Strafe with the keys (forward, left, back, right; down, up) |
| F | Frame the model again |
| Space | Pause or resume the clip |
| N | Play the next clip |
| F1 | Hide or show the overlay (the file, the counts, the clip) |
| Escape | Quit |

- `--screenshot P` draws N frames (default 120) offscreen and writes the last as a binary PPM; the camera turns slowly.
- `--clip N` starts with the file's clip N; `--no-animation` shows a skinned model in its rest pose.
- The renderer's settings (`--rhi`, `--gpu`, `--set renderer.validation=on`, ...) are those of the other demos; `--help` lists them.
- A file the importer refuses (not glTF, beyond the import limits, a buffer or image it cannot read) ends with the importer's message and
  exit code 1.

Tests: `tests/model_viewer` (the orbit camera), and the ctests `model_viewer_screenshot_*` (the sample robot, animated; the sample
meadow, static; the small `.gltf` of the asset tests), `model_viewer_missing_file`, `model_viewer_not_gltf` and
`model_viewer_window_*` (300 frames of the robot in a window with validation on).
