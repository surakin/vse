> **Answered by `script-handles.md` (2026-11-24): `animation.clip` returns a handle; `animation.play` takes a handle or a path.**

# Maintenance: a script can hold the id of a resource (`animation.clip` fails on ids above 2^53)

Not a numbered phase, no day files. Request from a host project (2026-11-24), a follow-up to `script-clips.md` (thank you, it is in, and it is what the host's robot
behavior is waiting for). Found when the host's `robot.lua` first ran against the real robot model.

## What happens

`animation.clip("/robot/Walk")` finds the clip, and then the call fails:

```
robot.lua:58: an integer outside +-2^53 cannot be represented as a Lua number
```

`AnimationClip` (`engine/engine_c/script_scene.cpp`) returns the resource id as `Value::FromInt(static_cast<i64>(id))`, and a resource id from an import made with
`id_seed` is a full 64-bit number (the host imports with a fixed seed so ids are stable between runs). Lua numbers are doubles, so `luajit_backend.cpp` refuses the
integer, and no script can ever play a clip of such an import. The same would hit any other binding that hands out or takes a resource id.

## Wanted

Any one of these, in the order the host likes them:

1. **`animation.play(entity, clip, params)` takes the clip's path (a string) as well as an id**, and a script never needs the id at all:
   `animation.play(self.entity, "/robot/Walk", {speed = 1.2})`. The path is looked up as `animation.clip` does (a name without a leading `/` is tried with one too);
   a path that names no clip is a script error naming it. `animation.clip` stays for scripts that want to test whether a clip exists, and may return `true`
   (or the path) instead of the id.
2. Or a resource id that survives a Lua number: ids made with `id_seed` limited to 53 bits (and said so in the C header), or `animation.clip` returning an opaque
   handle table the way entities and vectors are returned (a table tagged with a metatable, round-tripping as a `Value`).

Whatever is chosen, say in the guide that a script never sees a raw 64-bit id, and check the other bindings that name a resource (`mesh.set_slot` with a texture
resource id: the guide says "a texture resource id") for the same limit.

## Tests

A C API test next to `host_script_scene.c`: import a model with a seeded import whose clip id is above 2^53 (find one by trying seeds), and a script on its skeleton
entity plays the clip by path and by whatever `animation.clip` returns; the host reads the joint matrices to see it play. `animation.play` with a path that names
nothing is an error naming the path.

## What the host does with it

`assets/worlds/robot.lua` picks the `Idle` or `Walk` path out of the list of clip paths the host gives it (`params.animations`) and plays it with the speed of the
participant's pace. Nothing else changes on the host.

## Done

- [x] the binding change (script-handles.md)
- [x] C API test (`TestBigClipId`)
- [x] guide text
- [ ] checked on Windows and macOS (see `KNOWN-ISSUES.md`)
