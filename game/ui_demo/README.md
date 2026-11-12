# ui_demo

The Phase 15 showcase: a window made only of the engine's own widget toolkit (no Qt, GTK or Dear ImGui). A menu bar (File, Edit, View, Help, with submenus, accelerators and a context
menu), a toolbar with tooltips, a left panel (label, check box, radio group, combo box, slider, spin box, text field and a reorderable list of 1,000 items), a splitter, a status bar
and a modal About box, around a **viewport** that shows the Phase 5 lit, textured 3D scene through the deferred renderer. The viewport has an orbit camera (drag with the right
button, the pointer is locked where the display supports it; the wheel zooms). A second, small UI surface (a counter button and a label, with a theme of its own) is drawn to a texture
and shown on a lit quad standing in the 3D scene; clicking it in the viewport goes through a ray cast. Everything is drawn through `renderer::UiRenderer` and the render graph.

## Build and run

```
cmake --build build --target ui_demo
./build/game/ui_demo/ui_demo                                  # a window, until you close it
./build/game/ui_demo/ui_demo --backend x11                    # force a display backend (or wayland)
./build/game/ui_demo/ui_demo --scale 2                        # a 2x display scale (1, 1.25 or 2)
./build/game/ui_demo/ui_demo --screenshot out.ppm             # offscreen, no window: a 960x600 (logical units) PPM
./build/game/ui_demo/ui_demo --screenshot out.ppm --script "frame:2 theme:dark frame:4 openmenu:View"
```

## Things to try

- **Menus:** File, Edit, View, Help; keys: Alt+F opens File, F1 opens About, Ctrl+Q quits; View > Theme switches the whole tree between light and dark at run time (the status bar
  stays dark: a subtree theme override).
- **Left panel:** Tab walks the controls in order; drag a row of the list to reorder it; right-click the panel for a context menu; drop text or a file on the text field; Ctrl+V pastes.
- **Viewport:** right-drag orbits, the wheel zooms; left-click the in-world screen's Count button.
- **Settings:** View > Rendering settings... changes the present mode, the render scale and the shader quality while the window is open, with the cost class of each beside its control
  (`docs/guide/render-settings.md`); the `set:`, `reset` and `loss` script words do the same without a person.

## Options

| Option | Meaning |
|---|---|
| `--backend wayland\|x11\|win32` | display backend; default automatic. Same as `ENGINE_WINDOW_BACKEND` |
| `--frames N` | stop after N frames |
| `--screenshot FILE` | render offscreen with no window and write the last frame to FILE as a binary PPM (960x600 logical units times `--scale`) |
| `--script WORDS` | run steps without a person (below) |
| `--scale S` | pixels per logical unit: 1, 1.25 or 2; without it the window follows the display's scale (Win32: its DPI; Wayland and X11: 1), and `--screenshot` uses 1 |
| `--no-vsync`, `--validation`, `--gpu G`, `--rhi NAME` | as for `renderer_demo` |
| `--help` | print the options |

Script words (separated by spaces): `frame:N` wait until frame N; `click:name`; `type:text` (`_` is a space); `key:Name` (`Enter`, `Tab`, `Escape`, a letter, `F1`..`F12`, with `ctrl+`, `shift+`
or `alt+`); `resize:WxH` (offscreen only); `theme:dark|light`; `menu:File/About` (a submenu item is `View/Theme/Dark`); `openmenu:View`; `context`; `rendering`; `drag:name:dx,dy`; `wheel:name:n`;
`drop:text:T`, `drop:file:/path` and `paste:T` (a drag from another program and the clipboard, played by a fake OS peer); `shot:path`; `set:key=value`, `reset`, `loss` and `gpu:next` (in a window);
`quit`.

Exit codes: 0 success, 1 a runtime failure (no device, window or swapchain, a failed frame or script word), 2 a usage error, 3 the GPU device was lost.

## Settings options

Every program built on the settings system accepts these options, the settings of `--list-settings`, and the environment variables below. The list is generated from the registry; the ctest `ui_demo_readme_current` fails when this block differs from `--help`.

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

## Tests

`ctest -R ui_demo` runs the screenshot checks, the failure checks, the scripted window runs on each display backend (they skip without a session), the settings checks and the README check;
`rhi_tests "[ui_demo]"` holds the six golden pictures (`tests/rhi/golden/ui_demo_*.ppm`) and the reset and device-loss checks; `ui_tests "[ui_demo]"` runs the widget tree on the offscreen harness.
