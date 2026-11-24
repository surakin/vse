# Maintenance: scripts play, speed up and stop animation clips

Not a numbered phase, no day files. Request from a host project (2026-11-24). The host's robots switch between an `Idle` and a `Walk` clip and scale the walk clip's
speed to how fast the participant moves; today that is C++ (`animation_play`, `animation_set_speed` of the C API) because a script can do neither. The host wants this
in a Lua behavior on the robot, with its C++ knowing nothing about robots. The same pair of bindings is what any scripted character will need.

## Wanted

Four bindings, with the meaning and checks of the C calls of the same names (`animation_play`, `animation_set_speed`, `animation_stop`, `animation_time`):

1. `animation.clip(name)`: the clip resource called `name`, found among the animations of the loaded levels and imports (what the host finds with its
   resource lookup by type and name), as a value the other calls take; nil when there is none. If two have the name, the first mounted wins and the guide says so.
2. `animation.play(entity, clip, params)`: starts the clip on the entity that carries the skeleton instance. `params` is optional: `speed` (0 means 1), `start_time`
   (seconds), `fade_seconds` (> 0 blends from what plays now), `loop` (boolean, default true: the host's `ENGINE_PLAY_LOOP`). A world-write command applied at the
   merge; a call that the C function refuses (`INVALID_ARGUMENT`: a clip for another skeleton, no skeleton instance, a bad parameter) is a script error naming it.
   Calling it with the clip that plays now and the same parameters does nothing visible (no restart), so a script may call it every tick.
3. `animation.set_speed(entity, speed)`: speed >= 0, 0 pauses. A script error when nothing was started on the entity.
4. `animation.stop(entity)` and `animation.time(entity)` (world-read: the play position in seconds as of the tick's start, or nil when nothing plays).

`entity` may be the script's own entity or one it found.

## Tests

A C API test next to `host_script_scene.c`: a script on a skeleton entity plays one clip, then another with a fade, sets the speed, pauses, stops; the host
reads `skeleton_joint_transform` and `animation_time` (or the script's own `animation.time`) to see each effect; playing the same clip with the same parameters
twice does not restart it; a clip of another skeleton and a missing clip name give the errors above.

## What the host does with it

`assets/worlds/robot.lua` (a behavior on each robot) calls `animation.play(self.entity, animation.clip("Walk"), {speed = ...})` when the participant starts walking, the
`Idle` clip when it stops, and `animation.set_speed` as the pace changes. The host's C++ loses its `PlayAnimation` and `SetAnimationSpeed` code.

## Done

- [x] the bindings
- [x] C API test
- [x] guide text (scripting.md)
- [ ] checked on Windows and macOS (see `KNOWN-ISSUES.md`)
