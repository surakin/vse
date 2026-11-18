# Script API

Generated from the binding registry by `script_api_doc --write`; do not edit by hand.

Every function states the thread rule it follows: `pure` (any state, any thread), `world-read` (reads the world as of the tick start), `world-write` (changes the world only through the partition's command buffer) or `main-thread-only` (a partition job that calls it gets a script error). A wrong argument count or type is a script error that names the function and the argument; an entity handle that no longer exists is an error too.

## num

### `num.clamp`

`(f32 x, f32 lo, f32 hi) -> f32`

Thread rule: **pure**. Id: `0xa3b1eb5e`.

Limits `x` to the range `lo` to `hi`.

### `num.lerp`

`(f32 a, f32 b, f32 t) -> f32`

Thread rule: **pure**. Id: `0xaeba8f2e`.

Returns `a + (b - a) * t`.

### `num.sqrt`

`(f32 x) -> f32`

Thread rule: **pure**. Id: `0x5dbb00f1`.

Returns the square root of `x`, or 0 for a negative `x`.

## quat

### `quat.from_axis_angle`

`(Vec3 axis, f32 radians) -> Quaternion`

Thread rule: **pure**. Id: `0x87d0abf4`.

Returns the rotation of `radians` about `axis` (identity for a zero axis).

### `quat.identity`

`() -> Quaternion`

Thread rule: **pure**. Id: `0x64e05232`.

Returns the rotation that does nothing.

### `quat.mul`

`(Quaternion a, Quaternion b) -> Quaternion`

Thread rule: **pure**. Id: `0xb678002e`.

Returns the rotation that applies `b` first and then `a`.

### `quat.rotate`

`(Quaternion q, Vec3 v) -> Vec3`

Thread rule: **pure**. Id: `0xbceaa7cf`.

Rotates the vector `v` by `q`.

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
