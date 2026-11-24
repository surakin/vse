# Maintenance: scripts read and write binary data (`bytes.unpack`, `bytes.pack`)

Not a numbered phase, no day files. Request from a host project (2026-11-24). The host wants its C++ to know nothing about its world: today it decodes the
32-byte frames of its network protocol into numbers and applies the rules (interpolation, send rate, collisions) in C++. The rules should be a Lua behavior; the
host would only hand the script the raw bytes of a received frame and send the bytes the script gives back. Scripts run LuaJIT (Lua 5.1 style, `string.byte`,
`string.char`, `bit`, no `string.pack`), so reading a little-endian `f32` out of a string means decoding IEEE 754 by hand; that is slow to write and to run.

## Wanted

1. **Bytes cross the event bus both ways, intact.** A host event whose payload is (or contains) `ENGINE_VALUE_BYTES` arrives in a script as a Lua string, binary-safe
   (zero bytes included). A script's `event.emit(name, value)` with a Lua string inside reaches the host's subscribers as `ENGINE_VALUE_BYTES` (or `STRING` when
   it is valid UTF-8 and the host asked for that; choose one rule and document it). If this already works, only the tests and the guide text are missing. Payload
   limits as for any value (the host's frames are at most 1 KiB).
2. **`bytes.unpack(data, format [, offset])`**: reads values from the string `data` starting at `offset` (1-based, default 1) as `format` says and returns them, then
   the offset after the last one read. Little-endian, no padding. Format letters, repeated as needed and separated by spaces (or not):
   `B` u8, `b` i8, `H` u16, `h` i16, `I` u32, `i` i32, `f` f32, `d` f64, `c<n>` n raw bytes as a string (`c4`), `s1` a string with a one-byte length in front.
   Reading past the end is an error naming the offset and the format letter. `NaN` and infinities come out as such.
3. **`bytes.pack(format, ...)`**: the inverse; returns the string. A value that does not fit its letter (a number above 255 for `B`, a non-number, a string
   longer than `c<n>` or than 255 for `s1`) is an error naming the argument. `f` rounds to `f32`.
4. **`bytes.size(format)`**: the length of a fixed format (nil when it has an `s1`), for a script that checks a frame's size before reading it.
5. Cost: a single call that reads or writes many fields is one native call; the instruction budget counts it as one plus a unit per 64 bytes. Document it in
   the budgets section.
6. Guide text in `scripting.md` ("Binary data"), with the pose frame of a host (a 32-byte record: `B B H I I f f f H h H H`) as the example.

## Tests

A C API test next to `host_script_scene.c`: the host emits a 32-byte payload with every format letter at known values (including `NaN`, `-0`, the largest u32, the
smallest i16, a zero byte in the middle) and the script unpacks it and emits the numbers back; the host compares. The reverse: the script packs the same record and
emits it as bytes; the host reads back identical bytes. The error cases of 2 and 3, a read at the exact end (fine) and one byte past it (an error), and a
round trip through `bytes.pack` then `bytes.unpack` of random values for each letter.

## What the host does with it

`assets/worlds/world.lua` (the behavior of the room: the others' poses, what is sent about the local one, collisions) would receive `frame` events with the
sender's id and the payload string, unpack them with `bytes.unpack`, and emit `send` events with a packed string. The host's `WorldState` and the frame codec go;
C++ forwards bytes between the call's data stream and the script.

## Done

- [x] bytes through events both ways (or the test that shows they already work)
- [x] `bytes.unpack`, `bytes.pack`, `bytes.size`
- [x] C API test
- [x] guide text
- [ ] checked on Windows and macOS (see `KNOWN-ISSUES.md`)
