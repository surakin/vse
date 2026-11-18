# Script API

Generated from the binding registry by `script_api_doc --write`; do not edit by hand.

Every function states the thread rule it follows: `pure` (any state, any thread), `world-read` (reads the world as of the tick start), `world-write` (changes the world only through the partition's command buffer) or `main-thread-only` (a partition job that calls it gets a script error). A wrong argument count or type is a script error that names the function and the argument; an entity handle that no longer exists is an error too.

## component

### `component.add`

`(Entity e, StringView name, Value fields) -> void`

Thread rule: **world-write**. Id: `0xaeea55b1`.

Adds the component at the sync point, starting from its defaults and then the values in `fields` (nil for none); an entity that has it already is an error (use component.set).

### `component.define`

`(StringView name, Value fields) -> void`

Thread rule: **world-write**. Id: `0x7fe6e025`.

Defines a plain-data component `name` with `fields`, a table of field name to type name (f32, f64, i32, i64, u32, bool, vec2, vec3, vec4, quat), while scripts load; it is registered with the engine and saved like any other. Defining it again the same way is fine, differently or after the first tick an error.

### `component.exists`

`(StringView name) -> bool`

Thread rule: **world-read**. Id: `0x546e48b2`.

Returns whether a stored component of this name is registered.

### `component.get`

`(Entity e, StringView name) -> Value`

Thread rule: **world-read**. Id: `0x86031fe6`.

Returns a table with a copy of the component's fields (floats as numbers, vectors as vec2/3/4, quaternions as quat, enums and resource ids as integers), or nil when the entity lacks it. Writing to the table changes nothing: use component.set.

### `component.has`

`(Entity e, StringView name) -> bool`

Thread rule: **world-read**. Id: `0xdc65b8e2`.

Returns whether the entity has the component, as this script sees it (its own adds and removes this tick included).

### `component.remove`

`(Entity e, StringView name) -> bool`

Thread rule: **world-write**. Id: `0x388fb52e`.

Removes the component at the sync point; returns whether the entity had it.

### `component.set`

`(Entity e, StringView name, Value fields) -> void`

Thread rule: **world-write**. Id: `0xb185380a`.

Changes the fields named in `fields` of a component the entity has, at the sync point; every value is checked against its field's type and range, and an unknown field, a wrong type or an engine component is an error naming the component and the field. This script sees the new values at once.

## entity

### `entity.destroy`

`(Entity e) -> void`

Thread rule: **world-write**. Id: `0xdaba351e`.

Destroys the entity and everything below it at the sync point.

### `entity.find`

`(StringView name) -> Value`

Thread rule: **world-read**. Id: `0x27ed4b2f`.

Returns the entity with this name, or nil. Costs a search of the scene: keep the result.

### `entity.name`

`(Entity e) -> Value`

Thread rule: **world-read**. Id: `0xffb30717`.

Returns the entity's name, or nil when it has none.

### `entity.self`

`() -> Entity`

Thread rule: **world-read**. Id: `0xb8abc3be`.

Returns the entity whose script is running.

### `entity.set_parent`

`(Entity child, Value parent) -> void`

Thread rule: **world-write**. Id: `0xd54010d1`.

Makes `parent` the parent of `child` (nil makes it a root) at the sync point; a cycle or too deep a hierarchy is refused there and counted.

### `entity.spawn`

`(Value prefab) -> Entity`

Thread rule: **world-write**. Id: `0x75ba4133`.

Spawns an entity at the sync point and returns its handle now (usable in this tick's other calls); `prefab` is a prefab name or nil for an empty entity with a transform.

### `entity.valid`

`(Value e) -> bool`

Thread rule: **world-read**. Id: `0xeaecb59e`.

Returns whether `e` is an entity that exists (as of the tick start) or that this partition spawned this tick.

## log

### `log.debug`

`(StringView text) -> void`

Thread rule: **pure**. Id: `0xd6ebc8b0`.

Writes `text` to the `LogScript` channel at debug level, tagged with the script and line. Buffered per partition and flushed in partition order at the sync point; a script that writes more than the per-tick limit (`script.log_limit`) loses the rest, counted in one warning.

### `log.error`

`(StringView text) -> void`

Thread rule: **pure**. Id: `0xffc4d479`.

Writes `text` to the `LogScript` channel at error level (buffered, rate limited as `log.debug`).

### `log.info`

`(StringView text) -> void`

Thread rule: **pure**. Id: `0x0444575d`.

Writes `text` to the `LogScript` channel at info level (buffered, rate limited as `log.debug`).

### `log.warn`

`(StringView text) -> void`

Thread rule: **pure**. Id: `0x993aa9a9`.

Writes `text` to the `LogScript` channel at warning level (buffered, rate limited as `log.debug`).

## mat4

### `mat4.from_trs`

`(Vec3 translation, Quaternion rotation, Vec3 scale) -> Mat4`

Thread rule: **pure**. Id: `0x26b785af`.

Returns the matrix that scales, then rotates, then translates.

### `mat4.identity`

`() -> Mat4`

Thread rule: **pure**. Id: `0x9bc67cb1`.

Returns the identity matrix (16 numbers, column-major).

### `mat4.look_at`

`(Vec3 eye, Vec3 target, Vec3 up) -> Mat4`

Thread rule: **pure**. Id: `0x9f90e63c`.

Returns the view matrix of a camera at `eye` looking at `target` (right-handed, looking down -Z); the identity when the view direction is zero or parallel to `up`.

### `mat4.mul`

`(Mat4 a, Mat4 b) -> Mat4`

Thread rule: **pure**. Id: `0x6a88eacf`.

Returns `a * b`: applies `b` first.

### `mat4.transform_direction`

`(Mat4 m, Vec3 d) -> Vec3`

Thread rule: **pure**. Id: `0x4a78781f`.

Applies the matrix to a direction (w = 0: no translation).

### `mat4.transform_point`

`(Mat4 m, Vec3 p) -> Vec3`

Thread rule: **pure**. Id: `0xae55698c`.

Applies the matrix to a point (w = 1).

### `mat4.transpose`

`(Mat4 m) -> Mat4`

Thread rule: **pure**. Id: `0x7d90ab12`.

Returns the transposed matrix.

## num

### `num.approach`

`(f32 current, f32 target, f32 step) -> f32`

Thread rule: **pure**. Id: `0x5938de77`.

Moves `current` toward `target` by at most `step` and never past it.

### `num.clamp`

`(f32 x, f32 lo, f32 hi) -> f32`

Thread rule: **pure**. Id: `0xa3b1eb5e`.

Limits `x` to the range `lo` to `hi`.

### `num.lerp`

`(f32 a, f32 b, f32 t) -> f32`

Thread rule: **pure**. Id: `0xaeba8f2e`.

Returns `a + (b - a) * t`.

### `num.sign`

`(f32 x) -> f32`

Thread rule: **pure**. Id: `0xc86c31a2`.

Returns -1, 0 or 1 by the sign of `x`.

### `num.smoothstep`

`(f32 edge0, f32 edge1, f32 x) -> f32`

Thread rule: **pure**. Id: `0xb2d709bf`.

Returns 0 below `edge0`, 1 above `edge1` and a smooth curve between.

### `num.sqrt`

`(f32 x) -> f32`

Thread rule: **pure**. Id: `0x5dbb00f1`.

Returns the square root of `x`, or 0 for a negative `x`.

## quat

### `quat.conjugate`

`(Quaternion q) -> Quaternion`

Thread rule: **pure**. Id: `0x70d0820a`.

Returns the opposite rotation of a unit rotation.

### `quat.from_axis_angle`

`(Vec3 axis, f32 radians) -> Quaternion`

Thread rule: **pure**. Id: `0x87d0abf4`.

Returns the rotation of `radians` about `axis` (identity for a zero axis).

### `quat.from_euler`

`(f32 x, f32 y, f32 z) -> Quaternion`

Thread rule: **pure**. Id: `0x8b79657a`.

Returns the rotation that turns `x` radians about X, then `y` about Y, then `z` about Z (uses `sin`, `cos`: not bit-reproducible across machines).

### `quat.identity`

`() -> Quaternion`

Thread rule: **pure**. Id: `0x64e05232`.

Returns the rotation that does nothing.

### `quat.mul`

`(Quaternion a, Quaternion b) -> Quaternion`

Thread rule: **pure**. Id: `0xb678002e`.

Returns the rotation that applies `b` first and then `a`.

### `quat.normalize`

`(Quaternion q) -> Quaternion`

Thread rule: **pure**. Id: `0xa802f6db`.

Scales the rotation to unit length (identity for a zero rotation).

### `quat.rotate`

`(Quaternion q, Vec3 v) -> Vec3`

Thread rule: **pure**. Id: `0xbceaa7cf`.

Rotates the vector `v` by `q`.

### `quat.slerp`

`(Quaternion a, Quaternion b, f32 t) -> Quaternion`

Thread rule: **pure**. Id: `0x96c20d3c`.

Interpolates between two rotations along the shorter arc (uses `acos`, `sin`: not bit-reproducible across machines).

## query

### `query.all`

`(Value names) -> Value`

Thread rule: **world-read**. Id: `0x250f6212`.

Returns every entity that has all the components in `names`, in stable id order, as of the tick start: the expensive call (it walks the whole world); up to 60000, else an error (use query.page).

### `query.each`

`(Value names) -> Value`

Thread rule: **world-read**. Id: `0xc672fd5e`.

Returns the entities of this partition that have every component in `names` (a list of up to eight), in stable id order, as of the tick start. The scripts of one entity's partition see the same entities on any thread count.

### `query.page`

`(Value names, Value after, i64 limit) -> Value`

Thread rule: **world-read**. Id: `0x92fed91c`.

Like query.all but returns at most `limit` entities (1 to 60000) with a stable id above `after` (an entity from the previous page, or nil for the first), so a world of any size can be walked in pages.

## random

### `random.float`

`() -> f32`

Thread rule: **pure**. Id: `0x14baa4b6`.

Returns the next number of the running entity's stream, from 0 up to but not including 1. The stream starts from the world seed, the entity's stable id and the tick, so it is the same for any thread count.

### `random.int`

`(i32 lo, i32 hi) -> i32`

Thread rule: **pure**. Id: `0x1d9e023d`.

Returns an integer from `lo` to `hi`, both included (swapped when `lo` is above `hi`), from the running entity's stream.

### `random.vec3_unit`

`() -> Vec3`

Thread rule: **pure**. Id: `0x101c92c6`.

Returns a vector of length 1 pointing in a random direction, from the running entity's stream.

## settings

### `settings.describe`

`(StringView key) -> Value`

Thread rule: **pure**. Id: `0xad37ffd3`.

Returns a table describing a setting: `key`, `type`, `default`, `min` and `max` (numbers), `names` (an enum), `restart_required`, `label`, `description`. An unknown or secret setting is an error.

### `settings.exists`

`(StringView key) -> bool`

Thread rule: **pure**. Id: `0xbc6df3ba`.

Returns whether a setting with this key is declared.

### `settings.get`

`(StringView key) -> Value`

Thread rule: **pure**. Id: `0x7827ff7e`.

Returns the value of a setting as of the tick start: a bool, an integer, a float or text (an enum is its name). An unknown or secret setting is an error.

### `settings.on_change`

`(StringView prefix, StringView function) -> void`

Thread rule: **world-write**. Id: `0x2e01d96c`.

Subscribes the running entity's script to changes of the setting `prefix` or anything below it; at the sync point after a change the global function named `function` is called (by the event delivery of Day 459) with the key. Removed when the entity is destroyed.

### `settings.set`

`(StringView key, Value value) -> void`

Thread rule: **world-write**. Id: `0xe6360fe2`.

Sets a setting at the sync point, if the script's policy allows the key (by default only `game.*`) and the registry accepts the value (type, range, enum name); otherwise an error and nothing is stored. Reads in the same tick still see the old value.

## time

### `time.dt`

`() -> f64`

Thread rule: **pure**. Id: `0x68338cee`.

Returns the simulation seconds one tick advances (0 outside a tick).

### `time.frame`

`() -> i64`

Thread rule: **pure**. Id: `0x8b45c84f`.

Returns the frame number.

### `time.now`

`() -> f64`

Thread rule: **pure**. Id: `0x6269290e`.

Returns the simulation clock in seconds at the tick start; deterministic, the same for every script in the tick.

### `time.real`

`() -> Value`

Thread rule: **pure**. Id: `0x6925216e`.

Returns the wall clock in seconds since 1970. NOT deterministic: nothing that reaches the world should depend on it. Raises an error while a replay is being recorded.

### `time.tick`

`() -> i64`

Thread rule: **pure**. Id: `0x8db56feb`.

Returns the tick number.

## transform

### `transform.get`

`(Entity e) -> Value`

Thread rule: **world-read**. Id: `0x2d04da49`.

Returns a table with the local `position`, `rotation` and `scale`, including this script's own writes this tick.

### `transform.position`

`(Entity e) -> Vec3`

Thread rule: **world-read**. Id: `0x89d4084c`.

Returns the local position.

### `transform.rotation`

`(Entity e) -> Quaternion`

Thread rule: **world-read**. Id: `0x9ad00671`.

Returns the local rotation.

### `transform.scale`

`(Entity e) -> Vec3`

Thread rule: **world-read**. Id: `0xe12be92f`.

Returns the local scale.

### `transform.set_position`

`(Entity e, Vec3 position) -> void`

Thread rule: **world-write**. Id: `0xe5d23079`.

Sets the local position at the sync point; this script sees it at once.

### `transform.set_rotation`

`(Entity e, Quaternion rotation) -> void`

Thread rule: **world-write**. Id: `0x7bf42264`.

Sets the local rotation (normalized) at the sync point; this script sees it at once.

### `transform.set_scale`

`(Entity e, Vec3 scale) -> void`

Thread rule: **world-write**. Id: `0xaa4c067c`.

Sets the local scale at the sync point; this script sees it at once.

### `transform.translate`

`(Entity e, Vec3 delta) -> void`

Thread rule: **world-write**. Id: `0x1345ba9f`.

Adds `delta` to the local position (as this script sees it).

### `transform.world`

`(Entity e) -> Mat4`

Thread rule: **world-read**. Id: `0x40f7006d`.

Returns the world matrix (16 numbers, column-major) as of the tick start.

## vec2

### `vec2.dot`

`(Vec2 a, Vec2 b) -> f32`

Thread rule: **pure**. Id: `0xbee9a7cc`.

Returns the dot product.

### `vec2.length`

`(Vec2 a) -> f32`

Thread rule: **pure**. Id: `0x9af0a139`.

Returns the Euclidean length.

### `vec2.lerp`

`(Vec2 a, Vec2 b, f32 t) -> Vec2`

Thread rule: **pure**. Id: `0x1fba5334`.

Interpolates linearly between two vectors.

### `vec2.new`

`(f32 x, f32 y) -> Vec2`

Thread rule: **pure**. Id: `0x11c8d82d`.

Makes a 2D vector.

### `vec2.normalize`

`(Vec2 a) -> Vec2`

Thread rule: **pure**. Id: `0xcf1953d8`.

Returns the vector scaled to unit length; the zero vector stays zero.

## vec3

### `vec3.cross`

`(Vec3 a, Vec3 b) -> Vec3`

Thread rule: **pure**. Id: `0x6ca1e95c`.

Returns the cross product `a x b` (right-handed).

### `vec3.distance`

`(Vec3 a, Vec3 b) -> f32`

Thread rule: **pure**. Id: `0xcf06392f`.

Returns the distance between two points.

### `vec3.dot`

`(Vec3 a, Vec3 b) -> f32`

Thread rule: **pure**. Id: `0x6716462b`.

Returns the dot product.

### `vec3.length`

`(Vec3 a) -> f32`

Thread rule: **pure**. Id: `0x323ba768`.

Returns the Euclidean length.

### `vec3.lerp`

`(Vec3 a, Vec3 b, f32 t) -> Vec3`

Thread rule: **pure**. Id: `0x3d9fc98d`.

Interpolates linearly between two vectors.

### `vec3.new`

`(f32 x, f32 y, f32 z) -> Vec3`

Thread rule: **pure**. Id: `0x984f8042`.

Makes a vector.

### `vec3.normalize`

`(Vec3 a) -> Vec3`

Thread rule: **pure**. Id: `0xf0bdc1ef`.

Returns the vector scaled to unit length; the zero vector stays zero.

## vec4

### `vec4.dot`

`(Vec4 a, Vec4 b) -> f32`

Thread rule: **pure**. Id: `0xf308a6d2`.

Returns the dot product.

### `vec4.length`

`(Vec4 a) -> f32`

Thread rule: **pure**. Id: `0xba2ed757`.

Returns the Euclidean length.

### `vec4.lerp`

`(Vec4 a, Vec4 b, f32 t) -> Vec4`

Thread rule: **pure**. Id: `0x6e06cc5e`.

Interpolates linearly between two vectors.

### `vec4.new`

`(f32 x, f32 y, f32 z, f32 w) -> Vec4`

Thread rule: **pure**. Id: `0x901decf7`.

Makes a 4D vector.

### `vec4.normalize`

`(Vec4 a) -> Vec4`

Thread rule: **pure**. Id: `0xc9862092`.

Returns the vector scaled to unit length; the zero vector stays zero.
