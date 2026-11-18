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

## Choosing a backend

Backends register by name in a small fixed registry (`script/backend_registry.h`): `RegisterBackend({name, create})` (the same descriptor twice is accepted; a
clashing name, an empty or over-long name, a null factory or a full registry returns `false`), `FindBackend`, and `CreateBackend(name, &error)`, which
refuses an unknown name by value (`Status::Invalid` and a message that lists the registered names), never by abort. The setting `script.backend` (default
`luajit`; environment `ENGINE_SCRIPT_BACKEND`, flag `--script-backend`; restart required) names the language a world uses: declare it with
`RegisterScriptSettings` and read it with `ScriptBackendName`.

## The second language and the conformance suite

`script_expr` (`engine/script/expr/`) is a deliberately tiny language (`def`, `if`/`else`, `while`, locals and globals, numbers, strings, calls to script
functions and registered natives, `wait(seconds)`, `wait_frames(n)`, `wait_event(name)`) written against `backend.h` only, so an assumption of Lua that leaked
into the interface would not compile or would fail its tests. It advertises coroutines and instruction and time budgets; it does not advertise a memory budget
(it limits strings, call depth and the value stack instead), and a call given `memory_bytes` gets `Status::Unsupported`. Its cost is one stack-machine
interpreter, enough to run the whole suite.

`tests/script/backend_conformance.h` is the suite every backend must pass. A backend supplies a `ConformanceKit` (the prelude functions written in its
language, a source that does not compile, a hostile corpus); the suite checks loading and calling, marshalling of every `Value` kind, several results, errors
as values and recovery afterwards, native functions, instruction and time budgets, unhonored budgets reporting `Unsupported`, suspension and resumption,
independence of states, eight threads each with its own states, and hostile sources. A case needing a capability the backend does not advertise is skipped, and the report
says why. `tests/script/backend_conformance_test.cpp` runs the suite on `script_expr` and also runs it on backends with a planted fault (swallowed errors,
ignored budgets, dropped results) to prove the suite fails the right case.

## The LuaJIT backend

`script::luajit::CreateLuaJitBackend()` (`engine/script/luajit/`, registered as `luajit` by `RegisterLuaJitBackend`) gives each `script::State` its own `lua_State`. Only the
files of that directory include LuaJIT's headers (`luajit_boundary_check`).

- **Memory:** Lua allocates from the engine heap through a counting allocator, so the cap passed to `CreateState` (default 256 MiB) is exact for Lua objects; an allocation
  that would cross it is a Lua out-of-memory error, which comes back as `Status::OutOfMemory` and leaves the state usable (after `Collect`). Cleanup after such an error is
  given a small slack so it can run at a full cap. The machine code of the JIT is outside this count (Day 449 decides the budget mode).
- **Calls:** every `Call` runs the function as a coroutine, so `wait(seconds)`, `wait_frames(n)` and `wait_event(name)` suspend it; the call returns with
  `out.wait` and `out.coroutine`, and `Resume` passes the value `wait` returns inside the script. A path may be dotted (`game.on_update`).
- **Errors:** a script error comes back as a value: `RuntimeError` with the message (`chunk:line: text`; cut to 4 KiB) and a trace of function and line (cut to 16 KiB); a
  syntax error is `SyntaxError`. An error never unwinds through engine frames: every operation that can raise runs under `lua_cpcall`, `lua_pcall` or `lua_resume`, and
  engine-side problems (a value that cannot cross) are returned, not raised.
- **Natives:** `RegisterFunction("host.add", fn, user)` creates the table `host` on the way. The native gets copies of the arguments; a failure becomes a Lua error that the
  script can `pcall`.
- **Values:** `nil`, booleans, numbers, strings and tables cross as themselves. A Lua number is a double: an integer value outside +-2^53 is refused with `Invalid`, and a
  float with an integral value in that range comes back as an integer. Vectors, quaternions, entities, byte blobs, and empty arrays and maps are tables tagged by a
  metatable the backend keeps in the registry (`{x=, y=, z=}` and so on), so they round trip with their kind. An untagged table is an array when its keys are exactly 1..n
  and a map when all its keys are strings; a map comes back with its keys sorted by bytes (Lua tables have no order). A table that mixes the two, a function, a thread, a
  value nested deeper than 16 or shared in a way that makes more than 65,536 values is refused with `Invalid`.
- **Not yet:** bytecode and a `jit` capability beyond the budget mode (below).

### Budgets and the sandbox

`LuaJitOptions` chooses the mode of every state of a backend (setting `script.budget`, `budgeted` by default; `script.random_seed` seeds `math.random`).

- **Budgeted** (mods and untrusted scripts): the JIT is off (a count hook cannot stop compiled loops). A call can carry `Budget::instructions` (counted in blocks of
  1000) and `Budget::nanoseconds`; a count hook checks both and the call ends with `BudgetExceeded`. Once a budget has tripped the hook fires at every instruction,
  and the Lua `pcall`, `xpcall`, `coroutine.resume` and `coroutine.wrap` are wrapped to raise the budget error again, so `while true do pcall(...) end` stops. The top
  level of a loaded chunk runs under a default budget (50 million instructions, 2 seconds).
- **Trusted** (the game's own scripts): the JIT is on, and instruction and time budgets are refused with `Unsupported`; the machine code is bounded by LuaJIT's own
  `maxmcode` (512 KB per state).
- **Memory** is a budget in both modes: `Budget::memory_bytes` lowers the cap for the call, and an allocation past it is `OutOfMemory`; the state stays usable.
- **The environment** a script sees: `base` without the file and loader functions, `getfenv`, `setfenv`, `collectgarbage`, `require` and `module`; `string` (without
  `dump`), `math`, `table`, `bit` and `coroutine` (without `running`) as read-only views; no `io`, `os`, `package`, `debug`, `ffi` or `jit`. `print` writes to the log
  (channel `LogScript`). The engine's environment (these libraries, `wait`, registered natives) is read-only to scripts; each state has a script environment that is
  theirs to write and that `getmetatable` and `setmetatable` cannot change.
- **Patterns:** `string.find`, `match`, `gmatch` and `gsub` are the Lua 5.1 matcher with a step limit (4 million steps per call); a backtracking pattern ends with a
  `pattern too complex` error in milliseconds instead of running for a minute inside one C call.
- **Bounds on what leaves Lua:** a value copies at most 16 MiB of text, blobs and keys in total.
