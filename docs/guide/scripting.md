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

## Bindings

`script::BindingRegistry` (`engine/script/bindings.h`) is how engine functions become script functions. A binding is one typed C++ function:

```cpp
f32 Clamp(f32 x, f32 lo, f32 hi);
registry.Add<&Clamp>("num", "clamp", script::ThreadRule::Pure, "x, lo, hi", "Limits `x` to the range `lo` to `hi`.");
```

The registry derives everything from the signature: the argument checks and conversions (`f32`, `f64`, `i32`, `i64`, `bool`, `StringView`, `Vec2`, `Vec3`, `Vec4`, `Quaternion`, `ecs::Entity`, `Value`; a result of
any of them or `void`), the error messages, the signature text and the documentation page [`script-api.md`](../reference/script-api.md), which `tools/script_api_doc` renders and the ctest
`docs_script_api` keeps current. The function's stable id is a hash of `ns.name`; two bindings with the same id are refused. Names are lower case (`num.clamp`), and a namespace must not be the name
of one of Lua's libraries (`math`, `string`, `table`): registering it would replace the library.

- **Bad calls are script errors.** A wrong argument count or type is a `RuntimeError` naming the function and the argument (`test.add3: argument 2 (b): expected f32, got text`); the function
  is not called, and the script can `pcall` it.
- **Entity handles are checked.** An `Entity` is an index and a generation, never a pointer. The state's `EntityCheck` says whether the world still holds it; a dead handle fails every call
  (`the entity 7:1 no longer exists`), including one the script kept in a table or a global. A handle nested inside a `Value` is not checked; the function that uses it must.
- **Every binding states a thread rule:** `Pure` (any state, any thread), `WorldRead` (the tick-start view), `WorldWrite` (the command buffer), `MainThreadOnly`. `Add` refuses an unmarked binding.
- **Installing:** `registry.Install(state, profile, &error)` registers the bindings the profile allows. `InstallProfile::Main` installs all; `Partition` installs all, but a `MainThreadOnly` function
  raises `... is main-thread-only and cannot be called from a partition job`; `Task` installs only `Pure` ones (the task state of Day 461 has no world access). Installing only reads the
  registry, so many threads may install into their own states at once. The returned `Installation` holds the records the state's native functions point at and must outlive the state.
- **Value types in scripts.** A vector, quaternion or entity is a table tagged with a metatable (the Day 448 representation), not a userdata or cdata. The reason: `ffi` is removed from the sandbox,
  userdata cannot be built from Lua without it, and a tagged table round trips as a `Value` without a registry of handles. The metatables carry `+`, `-`, unary `-`, `*` (by a number, componentwise,
  quaternion times quaternion, quaternion times vec3 rotates), `/` (by a number), `==` and `tostring` (`vec3(1, 2, 3)`); `getmetatable` returns `false`, so a script cannot change them.
  The cost is a table per value; a script that makes millions of vectors a frame should use the `vec3.*` functions on scalars or the engine's batch bindings (later days).

## Partitions

`script::ScriptWorld` (`engine/script/script_world.h`) is a set of `P` partitions (setting `script.partitions`, 1 to 1024, default 64), each with one language state. An entity's scripts run in
`PartitionOf(stable_id, P)`, a fixed 64-bit mix (splitmix64's finalizer) reduced modulo `P`; the value depends on nothing but its two arguments, so it does not change with the thread count, the
load or the order entities were made in. Committed values are in `tests/script/script_world_test.cpp`.

- **One holder at a time.** A job takes a partition with `TryCheckout` (or `Checkout`, which treats a held partition as a program error) and gets a `PartitionLease`; an atomic owner token makes the
  second taker get `Busy`, and the lease's destructor (or `Release`) hands the partition back. Taking and releasing also acquire and hand over the state (`State::Acquire`, `HandOver`), so the debug
  owner check of Day 446 follows the job's worker thread. A lease belongs to the thread that took it: do not hold one across a suspension point of a job, which may resume on another thread.
- **States are lazy and identical.** A partition's state is made at its first checkout: the bindings installed with the world's `InstallProfile`, then every script in the order it was added. A script
  added later is loaded into the existing states at their next checkout. A script that does not load makes the checkout `Failed` (with its outcome) and leaves the partition free.
- **Memory.** Each state has the cap `script.partition_memory` (default 64 MiB; the cap is a limit, not a reservation). `PartitionLease::Call` runs a call and counts it; a call that ends
  `OutOfMemory` replaces that partition's state with a fresh one holding the same scripts, and no other partition is touched. `ScriptWorld::Total().memory_bytes` is the world's total at the last
  handback of each partition.
- **Counters** (`Counters(p)`, `Total()`): calls, nanoseconds, budget hits, out-of-memory results, resets and bytes held, read with relaxed atomics from any thread.

### What is shared between states

| Thing | Rule |
|---|---|
| A Lua object (table, function, coroutine) | Never leaves its state; only `script::Value` copies cross. |
| Script source text | Immutable bytes shared by `SharedBytesPtr`; every state reads the same copy and compiles its own. |
| The binding registry | Read-only once filled; installing is safe from many threads (`Pure` bindings can be called from any state). |
| Entity handles | Index and generation, checked on every call by the state's `EntityCheck`; the check must be safe to call from the job's thread. |
| A binding that touches an engine singleton | Must be `main-thread-only`, or the singleton needs its own lock; a `Pure` binding touches nothing but its arguments. |
| The backend | `CreateState` is thread-safe; a state is used by one thread at a time. |
