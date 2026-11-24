# The render features demo

`render_features_demo` shows the terrain, the sky, instanced meshes and instance hierarchies together, and `render_features_bench`
measures them. The scene: a 2048 x 2048 terrain (a height map from integer value noise, so every machine makes the same
samples), a cube map sky, a forest of 200,000 trees, 400 instanced rocks, a character that walks on the height field and twelve
spheres and boxes that fall and roll on it.

## The scene

- **Terrain.** One `renderer::TerrainObject` over the height map (grass, with rock on the steep faces), and the same samples as
  a `physics::HeightField` collider, so the character and the bodies stand on what is drawn. The terrain's space starts at its
  first sample, the collider's at its centre: the collider's body sits half a map from the origin.
- **Forest.** One `renderer::InstanceTree` of 1,200,000 nodes: each tree is a root with a trunk, two branches, a twig on each
  branch and a crown of leaf cards (six nodes). Trees are placed with `physics::TerrainRayCast` from above, skipping faces steeper
  than about 28 degrees and a clearing around the character. The trunk has a coarser mesh beyond 80 m and a stand-in
  (a box for the trunk and one for the crown) beyond 220 m; the crown has a blob beyond 60 m; branches and twigs are not drawn
  beyond 160 m and 110 m, and nothing beyond 700 m.
- **Rocks.** One `renderer::InstanceSet` filled by `renderer::Scatter`, its heights from the same ray casts.
- **Sky.** `renderer::SkyMode::Cube` with a 64-texel cube made from the procedural sky's own function when the program starts
  (the gradient and the sun, tone-mapped to 8 bits); `--sky procedural` draws the gradient instead.
- **Overlay.** The draw count, the instances drawn, the tree nodes the cull let through, the chunks tested and skipped, and
  the frame time.

The simulation runs in fixed steps of 1/60 s (a frame does the steps it owes, at most four). The character walks in eight
headings in turn.

## Running it

```
render_features_demo [--frames N] [--sky procedural|cube] [--backend B]
render_features_demo --screenshot out.ppm [--frames N] [--sky S]
render_features_demo --headless [--steps N] [--threads N]
```

| Option | Meaning |
|---|---|
| `--frames N` | Stop after N frames (a window: until closed; `--screenshot`: 240). |
| `--screenshot P` | Draw offscreen at 960 x 540 and write the last frame to `P` as a binary PPM. |
| `--sky S` | `cube` (the default) or `procedural`. |
| `--trees N`, `--rocks N` | How many to place (default 200,000 and 400). |
| `--threads N` | Threads of the job scheduler for `--headless` and `--screenshot` (default 1; a window uses every core). |
| `--reset-every N` | Reset the graphics device every N frames (a window only): everything that owns device objects is rebuilt. |
| `--headless` | No window and no device: step the physics and print the hash of its state and of the visible-instance list. |
| `--reference P`, `--tolerance N` | With `--screenshot`: compare the frame, reduced 8 times in each direction, with the image `P`; it matches when the mean channel difference is at most N thousandths (default 40). |
| `--write-reference P` | With `--screenshot`: write that reduced image to `P`. |
| `--backend B`, `--set key=value` | The display backend and any setting (`renderer.validation=on`, ...). |

Keys in a window: `C` switches the sky, `F1` hides the overlay, `Escape` quits.

`--headless` prints one line, for example

```
render_features_demo: 300 steps, 4 threads, 200000 trees, state hash 0x..., visible hash 0x..., 55895 tree nodes and 14 rock runs visible ok
```

The state hash is the physics world's hash after the steps; the visible hash is the FNV-1a hash of the list of tree nodes
(index, mesh, level) and rock runs that the cull finds for the camera that follows the character at the last step. Both are
the same at every thread count.

## The benchmark

```
render_features_bench [--scene cull|terrain|hierarchy|sky|memory|check|all]
```

`cull` culls 1,000,000 instances at 1 to 16 threads, `terrain` selects tiles on an 8192 x 8192 map, `hierarchy` moves 1% of
100,000 nodes and recomposes them at 1 to 16 threads, `sky` renders the scene at 1920 x 1080 with no sky, the procedural sky
and the cube sky and prints the difference (it needs a device and says so without one), and `memory` prints the bytes an
instance and a node cost. `check` is the regression check the tests run in an optimized build: it fails when a cost is more
than about three times the one measured, or when a result differs between 1 and 16 threads. The measured costs are in
[instancing](instancing.md) ("Cost") and [terrain](terrain.md) ("Cost").
