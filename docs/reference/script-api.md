# Script API

Generated from the binding registry by `script_api_doc --write`; do not edit by hand.

Every function states the thread rule it follows: `pure` (any state, any thread), `world-read` (reads the world as of the tick start), `world-write` (changes the world only through the partition's command buffer) or `main-thread-only` (a partition job that calls it gets a script error). A wrong argument count or type is a script error that names the function and the argument; an entity handle that no longer exists is an error too.

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

Returns the wall clock in seconds since 1970. NOT deterministic: nothing that reaches the world should depend on it. Returns nil while a replay is being recorded.

### `time.tick`

`() -> i64`

Thread rule: **pure**. Id: `0x8db56feb`.

Returns the tick number.

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
