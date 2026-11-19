# script_demo

A game written in Lua that runs on every core (Phase 27 Day 475). `scripts/demo.lua` gives 2,000 physics bodies a small behavior each (`wander`,
`flock`, `flee`), walks a player around a square, counts the bodies that touch 16 trigger pickups, and has a planner search a field of cells with
`task.parallel_for`. The program runs the full frame (scripts and merge, physics, physics events) with no window and prints the script and physics
time per frame, the pickups and the state hash.

```
script_demo [--threads N] [--partitions P] [--entities N] [--frames N] [--report N]
```

The hash is the same for every `--threads` (`tests/script_demo` runs 1, 2 and 16 and compares). The measured numbers are in `docs/guide/scripting.md`
("The script demo"). The windowed view with its overlay and the screenshot test are not built yet (`KNOWN-ISSUES.md`).
