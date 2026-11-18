# Scripting: a language behind an interface, many threads in front

Status: in progress (Phase 27). Built: the design and the spikes (Day 444), LuaJIT vendored (Day 445), the backend interface and the value type (Day 446). Next: a second backend and the conformance suite (Day 447), the LuaJIT backend (Days 448-449).

**Audience:** engine programmers who write or review script code, and programmers who add a language backend. Design and the measurements behind it:
`docs/superpowers/specs/2026-11-18-scripting-design.md`.

The script module (`engine/script`, namespace `script`) asks of a language only what this page lists. Nothing here names Lua: LuaJIT is one implementation of
the interface, the tiny test language of Day 447 is another, and the engine, the bindings and the conformance suite see only `script/backend.h`,
`script/value.h` and `script/status.h`.

## The rules

1. **A state has one owner thread at a time.** A `State` is created owned by the thread that created it. `HandOver()` gives it up as the last use before a
   synchronization point (a job boundary) and `Acquire()` takes it on the next thread. Every other call checks the owner in builds with debug checks and aborts
   on a use from another thread. There is no lock: a partition's state is used by one job at a time (Day 451).
2. **Only values cross.** A `Value` is copied deeply; nothing in it points into a state, so it can move to another thread or another state. A script never holds
   an engine pointer and the engine never holds a script object.
3. **Errors are values.** Every call returns an `Outcome`: a `Status` (`Ok`, `SyntaxError`, `RuntimeError`, `BudgetExceeded`, `OutOfMemory`, `Invalid`,
   `Unsupported`), a message and a stack trace as text. A hostile script never aborts the process.
4. **A limit a backend cannot honor is refused, not ignored.** `Budget` has instruction, wall-clock and memory limits (0 is unlimited); `Capabilities` says
   which a backend honors, and a call with a limit it cannot honor returns `Unsupported`.
5. **Waiting is a request, not a block.** A backend with coroutines lets a call suspend: the call returns with `CallOutput::wait` set (seconds, frames, an event
   name or a task id) and an id; the scheduler resumes it later with `State::Resume`, or drops it with `State::Abandon`. A backend without coroutines says so in
   its capabilities.

## Values

`script::Value` is one of: nil, a boolean, a 64-bit integer, a 64-bit float, text, `Vec2`, `Vec3`, `Vec4`, a rotation, an entity id, a byte blob, an array or a
map. A map keeps its entries in insertion order (so iteration is the same on every machine and in every run) and `Set` on an existing key replaces the value in
place. Containers nest at most 16 deep (`kMaxValueDepth`) and hold at most 65,536 items (`kMaxContainerItems`); `Push` and `Set` return `false` and leave the item
alone when a limit would be crossed, so copying, comparing and destroying a value use bounded recursion whatever came in.

A value that comes from a script or the network is checked once at the boundary: `Value::Check(ValueLimits)` returns `Ok`, `TooDeep`, `TooManyNodes`,
`TextTooLong` (a map key counts), `BlobTooLarge` or, when asked for finite numbers only, `NotFinite`. `Equals` compares floats by bit pattern (a NaN equals
itself, `+0` differs from `-0`), and `Hash` is a 64-bit FNV-1a over a canonical encoding, the same on every machine for the same value, so a recorded session can
be compared across them.

## Calling

`State::Load(chunk_name, source)` compiles and runs a chunk; `State::Call(path, args, budget, &out)` calls a global function or a dotted path
(`game.on_update`) with copies of the arguments and fills `out.results`; `State::RegisterFunction(name, function, user)` makes a native `NativeFunction`
callable from scripts (it gets copies of the arguments and fills the results; a failure becomes a script error). `Collect`, `MemoryUsed` and `Close` manage the
state's memory. A `Backend` names a language, reports its version and capabilities, and creates states with a memory cap.
