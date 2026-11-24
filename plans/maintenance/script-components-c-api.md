# Maintenance: scripts of a C API world can add components (a body and a collider)

Not a numbered phase, no day files. Request from a host project (2026-11-24), the last piece the host's robot behavior needs after `script-clips.md`,
`script-material-slots.md` and `script-joint-rotation.md`.

## What happens

A behavior on an entity of a world made through the C API calls

```lua
component.add(self.entity, 'rigid_body', { type = 1 })
component.add(self.entity, 'collider', { shape = 2, radius = 0.25, half_height = 0.81, offset = vec3.new(0, 1.06, 0) })
```

and gets

```
robot.lua:63: component.add: no components are available to this script
```

`scripting.md` ("Components and queries", "Physics") says a host gives its scripts a `ComponentCatalog` and calls `script_physics::RegisterPhysicsComponents`,
`RegisterPhysicsValidators` and so on; `engine/engine_c` does neither (a search of `engine_c/*.cpp` for `ComponentCatalog` and `RegisterPhysicsComponents` finds
nothing), so `component.*` and `query.*` never work for a script a C API host loads, although `character.*`, `physics.*` and `terrain.*` do.

## Wanted

`component.get/has/exists/set/add/remove` (and `query.*`) work in the worlds the C API makes, at least for the components the C API itself can set: `rigid_body`,
`collider`, `velocity` (the physics ones, with their `Validate()`), and the render components (`light_source`, `camera`, `hidden`); the others (the ones the C
API names with `ENGINE_COMPONENT_*`) as far as is cheap. If wiring the whole catalog is too much, the narrow form is enough for the host: the physics components
only.

## Tests

A C API test next to `host_script_scene.c`: a script on an entity adds a kinematic `rigid_body` and a capsule `collider`; the host steps and sees the entity block a
dynamic body that falls on it (or `physics_raycast` hit it); a bad field (`{type = 7}`, `{radius = -1}`) is a script error naming the physics reason; `component.get`
returns what was set.

## What the host does with it

`assets/worlds/robot.lua` makes each robot solid in `on_create`, so the host's C++ loses its `ENGINE_COMPONENT_RIGID_BODY` / `ENGINE_COMPONENT_COLLIDER` code and its
knowledge of how big a robot is. Until this lands the robot script wraps the two calls in `pcall` and robots are not solid.

## Done

- [x] the wiring (`ScriptHost::Impl` owns a registry and a frozen catalog: builtin + physics components and validators; `ScriptTickWorld` sets `physics_used` when a script added a body or a collider)
- [x] C API test (`TestBodies` in `host_script_scene.c`)
- [x] guide text
- [ ] checked on Windows and macOS (see `KNOWN-ISSUES.md`)

## Post-implementation code review

`/code-review high`: 2 nits, both declined on purpose (two cheap queries per frame in a world that has no body yet; audio and host components are not in the catalog, noted in the guide and `KNOWN-ISSUES.md`).
Scoped runs: `ran 1421 of 12484 tests (phases: 3 6 9 22 23 27 28; flavors: plain; ...)` and `ran 1213 of 12484 tests (... flavors: asan; ...)`, both passing; `ctest -L docs` passes.
