# script_demo

A game written in Lua that runs on every core (Phase 27 Day 475). `scripts/demo.lua` gives 2,000 physics bodies a small behavior each (`wander`,
`flock`, `flee`), walks a player around a square (the arrows or W, A, S, D take it over), chimes when the player walks into one of 16 trigger
pickups (which also count every body that enters), and has a planner search a field of cells with `task.parallel_for`.

```
script_demo [--threads N] [--partitions P] [--entities N] [--frames N] [--headless [--report N]]
            [--screenshot out.ppm] [--backend wayland|x11|win32|cocoa] [--audio platform|null] [--built-in]
```

- **In a window** (the default): the world drawn by the deferred renderer, with an overlay of the script time per frame, the speedup over one
  thread (press T to run the scripts on one thread, again to go back), the last frame's partition load and physics time, the pickups and the
  reloads; P adds the three hottest Lua functions (the profiler), F1 hides the overlay, Q and E turn the camera.
- **`--screenshot P`:** the same, offscreen, written as a binary PPM (`ctest -R script_demo_screenshot`).
- **`--headless`:** no window; prints the script and physics time, the pickups and the state hash every `--report` frames. The hash is the same
  for every `--threads` (`tests/script_demo` runs 1, 2 and 16 and compares) and on every machine (`script_demo --headless --frames 120`:
  `e1f5c33055210a41` on Linux, the Windows PC and the Mac).
- **The script is minted by the build:** `scripts/demo.script.src` goes through `asset_mint mint-script` into `minted/demo.blob` (the demo mounts
  that directory and loads the script by its id) and, with `--dev-out`, into `dev/5c27de0000000475.lua` under the build directory.
  `--built-in` runs the embedded copy of the same text instead (the tests use it).
- **Hot reload:** in a window the demo watches `dev/5c27de0000000475.lua` (or the directory the setting `script.dev_directory` names,
  `--script-dev-directory DIR`): save an edit and the running game takes it between frames, keeping every instance's state; a text that does not
  compile is refused and the old one runs on.
- **Sound:** the platform's device (`--audio null` for none; the null device is used when the platform's cannot open). The chime is made in
  code.

The measured numbers are in `docs/guide/scripting.md` ("The script demo").
