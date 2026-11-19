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

The registry derives everything from the signature: the argument checks and conversions (`f32`, `f64`, `i32`, `i64`, `bool`, `StringView`, `Vec2`, `Vec3`, `Vec4`, `Quaternion`, `Mat4` (an array of 16 numbers, column-major), `ecs::Entity`, `Value`; a result of
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

## Math, random, log and time

`RegisterStandardBindings` installs the bindings every script has; all are `pure` (they read their arguments and the running job's own `PartitionContext`, nothing shared), so a task state gets them too.

- **Math.** `num` (`clamp`, `lerp`, `sqrt`, `sign`, `smoothstep`, `approach`; the name `math` belongs to Lua's library), `vec2`, `vec3`, `vec4` (`new`, `dot`, `length`, `normalize`, `lerp`, `vec3` also `cross` and
  `distance`), `quat` (`identity`, `from_axis_angle`, `from_euler(x, y, z)` (x first), `slerp`, `mul`, `conjugate`, `normalize`, `rotate`) and `mat4` (`identity`, `from_trs`, `mul`, `transpose`, `transform_point`,
  `transform_direction`, `look_at(eye, target, up)`, a right-handed view matrix looking down -Z). A matrix is an array of 16 numbers, column-major; anything else is a script error. The
  vector and quaternion operators (`+ - *` `/`) come from the value metatables. The functions use `lang/scalar.h` (libm as is): `slerp`, `from_euler`, `from_axis_angle` and `look_at` call `sin`,
  `cos`, `acos` or `sqrt`, and their last bits are **not** reproducible across machines; a scene whose state hash must match across Linux, Windows and macOS avoids them (`+ - * /`, `dot`, `cross`,
  `length`, `normalize`, `mat4.mul` and the transforms are exact IEEE operations).
- **Random.** `random.float()` (0 up to but not including 1), `random.int(lo, hi)` (both included), `random.vec3_unit()`. The stream belongs to the running entity: `SeedPartitionRandom(context)` starts it
  from `RandomSeedFor(world_seed, entity_stable_id, tick)` (SplitMix64, integers only), once per entity per tick, so the numbers are the same on every machine and for any thread count (the test draws
  in 64 partitions on 8 threads and compares with one thread). Outside a job a per-thread stream stands in; its numbers are not tied to an entity.
- **Log.** `log.debug`, `log.info`, `log.warn`, `log.error` write `[script:line] text` to the `LogScript` channel. The script name and line come from the language backend
  (`script::CurrentCallerLocation`, which the LuaJIT trampoline publishes around each native call and answers only when asked). Inside a partition job the message goes to the partition's
  `LogBuffer` (`PartitionContext::log`) and is written by `FlushLogBuffers` at the sync point in partition order, so the log reads the same for any thread count. A script may add `script.log_limit`
  messages per tick (default 64, 0 for no limit); the rest are dropped and one warning per script counts them at the flush. Text is cut at 400 bytes.
- **Time.** `time.dt()` (the seconds a tick advances), `time.now()` (the simulation clock at the tick start, the same for every script), `time.tick()`, `time.frame()` come from the context the driver
  fills and are deterministic. `time.real()` is the wall clock in seconds and **not** deterministic; it raises a script error while `PartitionContext::replay_recording` is set, so a recorded run cannot depend on it.
  Outside a job they return 0.

## Settings

Scripts reach settings only through `settings.*` (`engine/script/settings_bindings.h`); a script has no `os`, no file access and no environment.

- **Reads** (`settings.get(key)`, `settings.exists(key)`, `settings.describe(key)`) are `pure`: they read the snapshot the driver took at the tick start (`SettingsAccess::snapshot`), so every partition
  sees the same values for the whole tick. A value is a bool, an integer, a float or text; an enum reads as its name. `describe` returns the declaration (`type`, `default`, `min`, `max`, `names`,
  `restart_required`, `label`, `description`). An unknown key is a script error; **a secret setting is not readable** (`get` and `describe` raise without showing the value).
- **Writes** (`settings.set(key, value)`) are `world-write`: the call checks the key against the script's policy and the value against the declaration (type, range with the registry's clamp or reject policy,
  enum name or index) and records one command; the merge applies it with `Settings::Set` at the sync point, in partition order, so listeners and the apply function run on the merging thread, never in a
  partition job. A refused call is an error that names the key, and stores nothing. Reads in the same tick still return the old value.
- **The allow list** is a `SettingsPolicy` the host fills: by default every script may set `game.*` only, and `script.*` and `renderer.*` are read-only. A prefix is a dotted prefix (`game` covers `game` and `game.volume`,
  not `gamepad.x`); `SetScriptWritable(chunk_name, prefixes)` gives one script its own list, which replaces the default for it.
- **Changes** (`settings.on_change(prefix, function_name)`) record a subscription in the partition (a function crosses as its global name, since a Lua function never leaves its state). After the merge the driver
  calls `CollectSettingChanges(registry, before, after, partitions)`, which lists, in the order partition, subscription, key, who is owed a call; the event delivery of Day 459 makes the calls in the next
  phase. `RemoveSettingsSubscriptions` drops an entity's subscriptions when it is destroyed.
- **Errors from bindings.** A binding reports a failure with `script::RaiseScriptError(message)`; the call's results are dropped and the script sees `name: message`.

## Entities and transforms

`entity.*` and `transform.*` (`engine/script/entity_bindings.h`) work on the Phase 17 components `LocalTransform`, `WorldTransform` and `Parent`.

- **Reads** (`entity.valid/self/name/find`, `transform.get/position/rotation/scale/world`) are `world-read`: the tick-start world, plus the script's own writes of this tick through the `Overlay` (a script that sets a
  position reads it back at once; another entity's read never sees it). `transform.world` is the matrix of the last propagation (the tick start). `entity.valid` accepts anything and returns false for a dead handle or a
  non-entity; every other call refuses a dead handle as an error before it runs.
- **Writes** (`entity.spawn/destroy/set_parent`, `transform.set_position/set_rotation/set_scale/translate`) are `world-write`: one command each, applied by the merge in the total order. `entity.spawn` returns a placeholder
  handle at once (usable by the other calls of the tick: `set_position`, `set_parent`, `valid`); the merge creates the entity and binds the placeholder. `entity.destroy` removes the entity and everything below it
  (`ecs::DestroyRecursive`). A rotation is normalized (a zero or non-finite one is an error), a position or scale must be finite. A cycle or too deep a hierarchy is refused by `set_parent` at the merge and counted
  in `MergeReport::skipped`.
- **Names and prefabs** are the scene's, not the script module's: a host fills `SceneServices` (`name_of`, `find`, `has_prefab`; the level's `asset::EntityName` is adapted by the host) and, for `entity.spawn("name")`, the merge's
  `MergeServices::prefabs` (`PrefabSource`). Without them `entity.name` and `entity.find` return nil and a prefab spawn is an error. `entity.find` costs a search of the scene: keep the result.
- **`self`:** `entity.self()` is the entity whose script runs (the `current_entity_key` of the job's context); the callbacks of Day 457 also pass it as their first argument.
- **The merge** applies the commands one at a time in `(partition, entity id, sequence)` order. A batched structural pass (one pass per component type) is not done: the Phase 17 structural path is used per command
  (recorded in `KNOWN-ISSUES.md`).
- The test `entity_bindings_test.cpp` runs 300 entities for 120 ticks (move, rotate, spawn children, parent, destroy): one world hash on 1 and 8 threads.

## Components and queries

`component.*` and `query.*` (`engine/script/component_bindings.h`) reach every stored component of the host's `storage::ComponentRegistry` by its registered name (`"gadget"`, `"animation_player"`, `"light_source"`), through the
registry's own encoder: a read encodes the component and shows its fields as script values, a write builds the new record from the values, checks it by decoding it into a scratch component (types, ranges, enum ranges, UTF-8)
and only then records a command. The host gives scripts a `ComponentCatalog` (`PartitionContext::components`, and `MergeServices::components` for the merge) over its registry.

- **Reads** (`component.get/has/exists`) are `world-read`: the tick-start component plus the script's own adds, sets and removes of this tick. `get` returns a table (floats and integers as numbers, `vec2/3/4`, `quat`,
  a matrix as 16 numbers, an enum or a resource id as an integer, text, arrays as arrays; entity-reference fields are left out) or nil when the entity lacks the component; changing the table changes nothing. A derived or
  runtime-only component (`world_transform`) is an error.
- **Writes** (`component.set/add/remove`) are `world-write`, one command each (kinds 20 to 22). `set` changes the fields named in the table of a component the entity has; `add` starts from the component's defaults, then the
  table (nil for none); `remove` returns whether it had it. A wrong type, an out-of-range number or enum, a non-finite float, a missing array length, a field that does not exist, or a component of the storage module itself
  (ids below 20: transform, parent, identity, spatial; they have `transform.*` and `entity.*`) is a script error naming the component and the field, and nothing is recorded. Placeholders from `entity.spawn` work as targets.
- **Scripts define components:** `component.define("health", {hp = "f32", max = "f32", dead = "bool"})` while scripts load. Types are `f32 f64 i32 i64 u32 bool vec2 vec3 vec4 quat`; text and entity references are not supported
  yet (`KNOWN-ISSUES.md`). The fields are laid out in name order, so every state gets the same layout whatever order its table was walked in, and the stored id is derived from the name (`0x80000000 | FNV-1a`; a collision
  with another component is an error that asks for a rename). The component is registered in the host's storage registry (`ComponentRegistry::RegisterDynamic`) and in the ECS registry, so a save writes and a load reads it like
  an engine component. Defining the same component again succeeds (every partition's state runs the same script); a different definition under the name, an engine component's name, or any new component after
  `ComponentCatalog::Freeze()` (called at the first tick) is an error. A component is at most 32 fields and 256 bytes (padded to 16, 32, 64, 128 or 256), and a catalog lets scripts define at most 32 components (the ECS holds 128 component types for the whole process, and a script must not use them up). The registries copy the names, so a component outlives its catalog.
- **Queries** are `world-read` and return arrays of entities in stable id order as of the tick start, so the answer never depends on the thread count. `query.each({"a", "b"})` returns the entities of the calling partition
  (`PartitionOf(id, partitions)`, with `PartitionContext::partitions`), the common case: a partition touches only its own entities and the cost shrinks with the partition count. `query.all` returns every match and is the
  expensive call: it walks the whole world. A call returns at most 60,000 entities (a script value holds 65,536 values); more is an error, and `query.page(names, after, limit)` walks any world in pages (`after` is the last
  entity of the previous page, or nil). `query.each` over all partitions covers `query.all`, each entity once. A callback form (`query.each(names, fn)`) waits for the callbacks of Day 459.
- **Render components:** `light_source`, `camera` and `hidden` are registered by `scene::RegisterSceneComponents` (`engine/scene/scene_components.h`) and work through the same calls. `MeshRenderer` holds a shared GPU mesh,
  which has no stored form; a script names a mesh by its asset id with the resource bindings of Day 463 (`KNOWN-ISSUES.md`).
- **Cost** (Linux x86-64, plain build, 100,001 entities with one tag): `query.each` of one of 16 partitions (6,250 entities) about 6.5 ms, `query.page` of 60,000 about 54 ms, a `query.all` that overflows (100,001 matches,
  refused) about 14 ms. Each call walks the archetypes and sorts the ids; a world with fewer archetypes and an `each` per partition is the fast path.
- The tests `component_bindings_test.cpp` (reflection, validation, definitions, a save and a load through `WorldStorage`, queries, 40 ticks of queries and writes: one hash on 1 and 8 threads) and
  `tests/scene/scene_components_test.cpp`.

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

## The world view and the merge

Scripts on many threads read one world and change it through ordered command buffers (design A of the scripting design: no tick-start copy).

- **Reads.** During the script phase the ECS is read-only: `script::ReadOnlyPhase` begins the ECS parallel phase, so a structural call aborts, and every partition reads the live world, which holds
  the tick-start values. A binding finds the world, the partition's command buffer and the running entity's overlay in `script::PartitionContext`, which the job publishes for its thread with
  `ScopedPartitionContext` (`CurrentPartitionContext()` is null outside a job).
- **Writes.** A binding records a command (`CommandBuffer::Record(kind, order_key, target, payload)`; plain bytes, copied into the buffer, storage kept between ticks). The `order_key` is the
  stable id (`Entity::ToU64`) of the entity whose script runs. A script's own reads after its own writes come from the `Overlay`, which the driver clears before each entity runs.
- **Spawning.** `CommandBuffer::Reserve()` returns a placeholder entity (`generation` = `kPlaceholderGeneration`, `index` = `partition * 2^22 + n`), so a script can name an entity it spawns
  before the merge creates it. Placeholders depend only on the partition and the script's own order, not on threads. `ContextEntityCheck()` treats a reserved placeholder as alive.
- **Order.** `Merge` applies the buffers in partition order and each buffer in `(order_key, sequence)` order, sorting a buffer that is not already in that order, so the result does not depend on
  the thread count or on the order a partition ran its entities in. A creating kind binds its placeholder to the new entity (`MergeContext::Bind`); a later command naming it is resolved, and
  one naming a placeholder that was never created is counted as `dangling`.
- **Conflicts.** Two partitions using a `writes` kind on one entity in one tick conflict. `script.conflicts` is `last_wins` (default: the higher partition's command applies; counted) or `error`
  (the later command is refused and counted in `refused`).
- **Destroyed entities.** A read of an entity that a command destroys this tick still returns the tick-start value; after the merge the handle fails the entity check ("no longer exists").
- **Cost** (Linux x86-64, one thread, 200,000 `set_vel`-style commands of 12 payload bytes): about 23 ns to record one command and 100 ns to merge it (including the component write).

The test `world_view_test.cpp` runs 10,000 entities whose scripts move, spawn and destroy each other: the state hash is equal at 1, 2, 7 and 16 threads and over 100 runs, and a binding that reads
a racing counter is caught by the same comparison.

## Behaviors and the script system

A script reaches an entity through a **behavior**: a table of callbacks registered by name, attached with the `ScriptComponent`.

```lua
behavior('door', {
  on_create = function(self) end,          -- once, when the instance is made
  on_enable = function(self) end,          -- when the component is enabled (also the first time)
  on_start = function(self) end,           -- once, before the first update of an enabled instance
  on_fixed_update = function(self, dt) end, -- once per physics step of the tick
  on_update = function(self, dt) end,      -- once per tick
  on_disable = function(self) end,         -- when the component is disabled, and before on_destroy
  on_destroy = function(self) end,         -- when the entity or the component goes away
})
```

- **The instance.** `self` is a table made for the entity, with `self.entity` (its handle) and `self.params` (a copy of `ScriptComponent::params`); its own fields persist between callbacks. It
  lives in the entity's partition's state and nothing else can reach it. A callback that is not defined is skipped.
- **The component.** `ScriptComponent{behavior, params, enabled}` (`script_runtime.h`). The script system makes the instance on the first tick after the component appears. Changing `enabled`
  calls `on_enable` or `on_disable` at the next tick. Changing `behavior` closes the old instance at the next tick and makes the new one on the tick after.
- **The system.** `script::MakeScriptSystem()` is an `ecs::SystemDesc` that runs `ScriptRuntime::Tick` on the world's `ScriptRuntime` resource. It declares `ReadsAllComponents()` and
  `WritesAllComponents()` (scripts name components at run time), so the schedule runs it alone, and it uses `SystemContext::ExclusiveWorld()`. A tick is: reconcile the instances with the
  entities that have the component; per partition (in this version one after another) and per entity in id order, `on_create`, `on_enable`/`on_disable`, `on_start`, `on_fixed_update` times the
  host's `SetFixedSteps` count, `on_update`; the **merge** (the sync point); then a short second phase that closes the instances whose entity the merge destroyed.
- **Destroys.** `on_destroy` runs for an entity the host destroyed, a component the host removed, and an entity a script destroyed (in the second phase, so the entity is already gone: the callback can
  still change other entities, but reading `self.entity` fails). `on_disable` runs first only when the entity still exists (a removed component). The instance is dropped either way.
- **Errors.** A callback that fails (a script error, the instruction budget, an unknown behavior name) is reported once per distinct error per instance ("Debugging" below). The setting `script.on_error`
  says what then happens: `disable` (default: the instance gets no more callbacks, including `on_destroy`; the entity keeps the component), `retry` (the next tick calls again) or
  `abort_in_debug` (asserts in builds with slow checks, otherwise `disable`). A partition whose state ran out of memory and was reset disables its instances. The other instances are not
  affected. Counts are in `ScriptRuntimeStats`.
- **Determinism.** Instances run in entity id order inside a partition and partitions are merged in partition order; the random stream of an entity (`random.*`) is seeded from the world seed, the
  entity id and the tick.

The test `script_system_test.cpp` checks the callback order through a `tracer` component (`123445` for one tick with two fixed steps), enable and disable, errors and `retry`, the three ways
an instance is destroyed, a behavior change, 10,000 instances for three ticks, and the system inside an `ecs::Schedule`.

## The parallel script system

`ScriptRuntime::Tick(world, dt, scheduler)` runs each phase as one job per partition on the Phase 2 scheduler (`jobs::ParallelForBlocking` with a chunk of one partition, so the work-stealing
scheduler balances uneven partitions; the main thread helps while it waits). `MakeScriptSystem()` passes `SystemContext::JobScheduler()`, which is set when the system is alone in its wave (it
always is: it declares every component) and the schedule has a scheduler with several participants. With no scheduler, one participant, or a call from inside a job, the partitions run one after
another and the result is the same.

- **What runs on the workers.** The callbacks of one partition's instances (each partition is used by exactly one job at a time: the lease). The reconcile, the merge, the second destroy phase and
  the stats sum run on the calling thread between the phases; each partition counts into its own stats, summed after the jobs finish.
- **Independence from the thread count.** Entities run in id order within a partition, each partition has its own command buffer, the buffers are merged in partition order, and an entity's random
  stream depends on the world seed, its id and the tick. `parallel_script_test.cpp` runs 20,000 scripted entities (a read, two component writes, a transform write and `random.int` per update) for
  three ticks at 1, 2, 3, 7 and 16 threads: the world hash and the counts are equal.
- **Evenness of the partition hash.** `PartitionOf` over entity ids (index low, generation 1 high): at 1,000 ids and 16 partitions the busiest partition holds under 1.5 times the mean and the
  emptiest over 0.6 times; at 100,000 and 1,000,000 ids (64 and 256 partitions) within 10% of the mean. A heavy partition is therefore not expected, so no sub-assignment of a partition is made.
- **Scaling** (Linux x86-64, 32 hardware threads, plain build, 100,000 entities with the test's `mixer` behavior, three ticks after the first; entity updates per second, merge included):

  | threads | 1 | 2 | 4 | 8 | 16 |
  |---|---|---|---|---|---|
  | `P` = 64 | 119,000 | 241,000 | 434,000 | 700,000 | 904,000 |

  At 8 threads `P` = 16 gives 644,000 and `P` = 256 gives 700,000. The curve flattens because the merge (one thread) is about a fifth of the tick at 16 threads. The test is hidden
  (run `script_tests "[cost]"` by name) so the sanitizer flavors do not run it.
- **Not done this day** (see `KNOWN-ISSUES.md`): a hot-script detector for a partition job that runs long (the instruction budget of each callback bounds it for now), and script systems that
  declare their reads and writes so they can overlap other systems (the one script system declares every component and runs alone).

## Events

Scripts and native systems exchange named events with `Value` payloads. An emit never runs a handler inside the emitter's call.

```lua
behavior('door', {
  on_create = function(self)
    event.on('button_pressed', function(me, payload, sender) --[[ me is self, sender the emitting entity ]] end)
    event.once('level_loaded', function(me, payload) end)
  end,
  on_update = function(self)
    event.emit('door_opened', { id = 3 })            -- every subscriber
    event.emit_to(self.params.light, 'turn_on', nil) -- only that entity's script
  end,
})
```

- **Calls.** `event.emit(name, payload)`, `event.emit_to(entity, name, payload)` (bindings, `event_bindings.h`), and `event.on(name, fn)`, `event.once(name, fn)`, `event.off(name[, fn])` (added by
  the behavior host: `on` and `once` work inside a behavior callback, the handler is called as `fn(self, payload, sender)`; `off` without `fn` removes every handler of the name). A name is 1 to 64
  bytes; one partition emits at most 65,536 events a tick.
- **Delivery.** An emit goes into the partition's outbox. At the sync point the outboxes move into the `EventBus` in `(partition, emitting entity id, sequence)` order (native `EventBus::Emit` calls
  enter at the call), and the next tick delivers them: first the native subscribers on the calling thread, then each partition's job calls its subscribers' handlers, events in bus order and
  subscribers in entity id order, before the tick's lifecycle callbacks. So the latency is always one tick, a handler never runs concurrently with the emitter's partition, and the order does not
  depend on the thread count.
- **Chains.** An event emitted by a handler has a chain depth one more than the event it answers, and is delivered one tick later again. Past `script.event_chain_limit` (default 8, setting
  `ENGINE_SCRIPT_EVENT_CHAIN_LIMIT`) it is dropped and counted (`EventBus::Dropped`), so two scripts cannot answer each other forever.
- **Lifetime.** Subscriptions belong to the instance: they end when it is destroyed, and a failed or disabled instance gets no events (an event is not kept for it). `emit_to` checks that the target is a live entity when the script calls it.
- **Native code.** `ScriptRuntime::Events()` is the bus: `Emit`, `Subscribe(name, handler, user)` (an empty name receives everything) and `Unsubscribe`. The engine's own events (`on_collision_*` and
  `on_trigger_*` from physics (Day 467), `on_input`, `on_asset_loaded`) are native emits with these names, added by the days that own them; `on_setting_change` stays a settings subscription for now.
- **Cost** (Linux x86-64, 8 threads, 100 subscribers, 10,000 events a tick, no payload work): about 5.6 million handler calls per second, emit and delivery included.
- **For Phase 32 (Blueprint-shaped events).** Event dispatchers map to named events with a target; "call an event on another actor" maps to `emit_to`. An interface call that returns a value is
  deliberately absent: it would need a synchronous call into another entity's partition, which the threading model forbids. The shape that fits is a request event answered by a reply event
  one tick later (`event.emit_to(other, 'get_health', { reply_to = self.entity })`), which the importer can generate for interface calls whose result is used in a later node. Latent nodes map to
  the waits of Day 460.

The test `event_test.cpp` checks the one-tick latency, `once` and `off`, targeted events and the sender, the chain limit, cleanup on destroy, native emit and subscribe, a handler error, and equal
state at 1, 2, 7 and 16 threads.

## Latent waits

A callback or a handler may wait without blocking any thread: `wait(seconds)`, `wait_frames(n)` and `wait_event(name)` (which returns the payload and the sender) suspend the call, and the engine
resumes it in a later tick.

```lua
behavior('lamp', {
  on_create = function(self)
    wait(0.5)                              -- resumes at the first tick whose simulation time is 0.5 or more
    self.on = true
    local payload, sender = wait_event('switch')
    after(2.0, function(me) me.on = false end)
    every(1.0, function(me) --[[ once a second ]] end)
    tween(0, 1, 0.5, 'out_quad', function(me, v) --[[ v from 0 to exactly 1 ]] end)
  end,
})
```

- **How it resumes.** A call that waits is a coroutine of the instance's partition state. The runtime keeps a wait list per partition; at the start of a partition's work it resumes the due timers in
  `(wake time, entity id, creation order)`, then delivers the tick's events (a `wait_event` resumes when its event is delivered, with the payload and sender), then runs the lifecycle callbacks. Time
  is the simulation clock (`sim_time`, the sum of the `dt` of the ticks run), so a wait never depends on real time or on the thread count. `wait_frames(n)` counts ticks.
- **Cancel rules.** Destroying the entity (or removing its script component) drops its waiting coroutines without running another step; `on_destroy` and `on_disable` of a closing instance may not wait
  (a wait there is dropped). A disabled instance's timers do not run down: they resume with the time they had left once it is enabled again. An event wait of a disabled instance is not kept. A state
  reset (out of memory) drops the coroutines; there is no `on_cancel` call because the state is gone.
- **Library helpers** (plain code over `wait`, in the behavior host): `spawn(fn)` starts `fn(self)` as a coroutine of the instance at the start of the next tick (through a private event, so it takes one
  tick) and returns a handle; `stop(handle)` ends it the next time it would continue from a wait; `after(seconds, fn)`, `every(seconds, fn)` and `tween(from, to, seconds, easing, fn)` (easings `linear`,
  `in_quad`, `out_quad`, `in_out_quad`; one call per tick, the last exactly `to`) are built on `spawn` and return its handle.
- **Limits.** One partition keeps at most `ScriptRuntimeConfig::coroutine_limit` waiting coroutines (65,536; a call that would suspend past it fails with "too many waiting coroutines" and the instance
  is disabled by `script.on_error`). A coroutine that never waits runs against the instruction budget like any call. A handler that waits delays the instance's other handlers for that event until it
  finishes. Not built: `wait_all`, `wait_any` (use `spawn` and events), and the timeout of `wait_event`. `wait_task` is in the next section.
- **Cost** (Linux x86-64, plain build): 100,000 entities each holding one waiting coroutine take about 160 ms a tick to run, nearly all of it the 100,000 `on_update` dispatches; the waits add the scan
  of the wait list. About 2.7 KB of script state per such entity (instance table, coroutine and bookkeeping).

The test `latent_test.cpp` checks timed and frame waits, `wait_event`, `after`, `every`, `stop` and `tween`, destroy, disable, the cap, the instruction budget of a coroutine that never waits, and equal
state at 1, 2, 7 and 16 threads.

## Script tasks

A script can start parallel work of its own: pathfinding, generation, anything that only computes. A task is a function given as **source text** (a Lua closure cannot cross into another state; passing one is an
error that says so), run on a **task state**: a pooled language state that has only the `pure` bindings (math, `random`, `log`, `num`, `vec3`, the string, table and bit libraries). It has no world, no
events, no settings, no clock (`time.real` is refused), and none of `task.*` or `wait`.

```lua
local h = task.run('return args.a + args.b', { a = 1, b = 2 })          -- the body of function(args)
local sum = wait_task(h)                                                 -- 3, one tick later

local doubled = wait_task(task.map('return item * 2 + index', array, 256))   -- function(item, index), in index order
local parts = wait_task(task.parallel_for(1000000, 'local s = 0 for i = first, last do s = s + i end return s', nil, 4096))
-- function(first, last, args) once per chunk of indices (counted from 1, both included); one result per chunk
```

- **Calls.** `task.run(source, args, seed)`, `task.map(source, array, chunk, seed)` and `task.parallel_for(count, source, args, chunk, seed)` return a handle, or `nil` and a message when the request is
  refused (queue full, arguments too large, a bad chunk size). `wait_task(handle)` returns `result, nil` or `nil, message`; `task.result(handle)` does the same without waiting and answers `nil, 'pending'`
  in the tick the task was asked for. A result is **taken once** (the second take says "no such task") and only by the instance that asked.
- **When it runs.** The tasks asked for in a tick run at the end of that tick (after the merge), on the job scheduler, and a waiting coroutine resumes at the start of the next tick, in `(entity id,
  creation order)`. A long task therefore lengthens its tick; split big work into chunks, which run on every state at once. The results are the same for any thread count and any number of states.
- **Chunks.** `map` and `parallel_for` cut the work into chunks of `chunk` elements (default 256, never derived from the thread count) and run them on the pool's states; the answer is assembled in index
  order. A `map` array holds at most 65,536 elements (a value container's limit), and a call makes at most 65,536 chunks, so a million-index loop uses `parallel_for` with a chunk of 4,096 or more and
  reduces the per-chunk results itself. A failing chunk fails the whole task: the message names the first failing chunk (`chunk starting at index 5: ...`). A `map` function may not return `nil`.
- **Determinism.** A task's result depends only on its source, its arguments and its seed. Before each task, and before each element of a `map`, `math.random` is seeded again from the seed (plus the
  index); the `random` table in a task (`random.float`, `random.int`) is built on it. Whatever a task does to the globals (adds, removes or replaces one) is put back when it ends, so a task leaves
  nothing for the next one on the same state. Do not change the standard libraries' contents: they are read-only views.
- **Limits** (settings): `script.task_states` (0: one per scheduler thread), `script.task_queue_limit` (1,024 tasks a partition may have queued or finished and not taken), `script.task_arg_bytes` (16 MiB of
  source and arguments a partition may queue in a tick). The budget of a chunk is `ScriptRuntimeConfig::task_budget` (500 million instructions); a chunk that exceeds it fails like any call. A task state
  that runs out of memory is replaced. A task whose owner is destroyed is dropped (queued, running or finished), and a result never reaches a destroyed owner.
- **Not built:** a task that spans several ticks (it holds the tick it runs in), cancelling a queued task by handle, tasks started from native code (the pool is `script::TaskPool`, used by the runtime),
  and minted script assets as task functions (Days 462 and 463 add the asset kind; today the function is source text, cached by a hash of it).

The test `task_test.cpp` checks `run`, `map`, a million-index `parallel_for` against its known sum, errors as values, the globals and the world being out of reach, a closure refused, random numbers that depend
only on the seed, the queue and size limits, taking a result once and only by its owner, dropping with the owner, and equal results at 1, 2, 7 and 16 threads with 1, 3 and 16 states.

## Minted scripts

A script reaches the engine as a `SCRP` asset (`asset/script_blob.h`, `docs/guide/assets.md`, "Scripts"): `asset_mint mint-script` validates the Lua source (syntax, banned libraries found statically, literal `require` names, lifecycle callbacks that are functions), rewrites `require("assets.weapon")` to an id lookup (`__require("<hex id>")`), refuses a missing dependency or a cycle, lists the behaviors the source defines, and can add a bytecode chunk keyed by the language, its version and the processor.

**Loading** (`script/script_assets.h`, Day 463). `ScriptAssets assets(world, manager); assets.Add(id)` loads script `id` through the resource manager (`asset::RegisterCpuLoaders` registers the `ScriptView` loader), which loads the whole dependency closure or fails with its own status (`MissingDependency` and the chain in `LoadFailure`, `DependencyCycle`, `DependencyTypeMismatch`, `Corrupt`): nothing of a closure that did not load is added to the world. Each script is added after the scripts it requires, once; adding an id twice does nothing. The text stays in the manager's loaded buffer and every partition's state compiles from that same shared range. `assets.Release()` (or the destructor) lets the manager unload the scripts when nothing else holds them; a state keeps what it compiled until it is reset. `ScriptOf(name)` names the script that defines a behavior, from the entry points the minter found.

**Modules.** The world adds a minted script with `ScriptWorld::AddModule`. A state compiles the text without running it and gives the function to the module host (`State::DefineModule`: no source is spliced into a wrapper; the host's functions are pinned in the registry by `State::LoadHost` and called by `@name`, so a script cannot reach or replace them), and then runs each script once, at its place in the load order, so a script's requires have run before it. A script's value is what its chunk returns (`true` when nothing, as in Lua). `__require("<key>")` works only while a script loads and only for a key in that script's declared dependencies (the `dep` lines of its source); anything else, and a `require` from a function that runs after the load, is an error naming the rule. A script that fails to compile or run makes the checkout of the partition fail with that outcome (by value); the other scripts of that closure are not run in that state.

## Hot reload

A development facility (`script/script_reload.h`, Day 464): edit a script and the running world picks it up between ticks. It is off unless `script.hot_reload` is on (restart required, default off); a disabled `ScriptReloader::Reload` does nothing and says so in its report.

**What happens.** `ScriptReloader::Reload(changes, tick, &report)` first compiles each new text in a scratch state: a text that does not compile is reported (key, line, message) and its old version keeps running, while the others go on. Then `ScriptWorld::ReloadModules` replaces the texts in two phases, in every partition at once and on the calling thread, which must be between ticks with no partition held (an ownership check fails the call with `Invalid` otherwise). Phase one runs the changed scripts, and every script that requires one of them, again in load order, with the old behavior classes and module values still in place; a script that fails there, in any partition, aborts the whole reload and every partition stays on the old version. Phase two commits: each instance of a behavior the new text redefined moves to the new class, after the old class's `on_reload_save(self)` (returning a table) and before the new class's `on_reload_restore(self, saved)`; without hooks the instance keeps its own fields and just gets the new callbacks. A hook that fails is counted in the report (`hook_errors`, the first message) and the reload goes on. A tick never sees two versions.

**What carries over.** An instance's own fields, its pending waits and spawned coroutines (they are closures of the old text and finish as written; the next call goes to the new class), its subscriptions and its tasks. A behavior the new text no longer defines keeps its old class until it is destroyed.

**Limits.** A script is reloaded at most once per tick (`tick` is the number of the tick about to run; a second request in the same tick is in `report.deferred` and is asked again later), a key given twice in one call takes the last text, so an editor that saves twice reloads once.

**Watching files.** `ScriptDevWatcher(script.dev_directory)` watches `<hex id>.lua` for the ids it is told to `Watch` (`asset_mint mint-script SRC OUT --dev-out=DIR` writes the stored text there). It listens through `io::DirectoryWatcher` (`docs/guide/io.md`, "Watching a directory"), reads only the files an event names (all of them after an overflow, or when no watcher could start) and compares content hashes: `Poll(&changes)` lists what differs, and `Applied(change)` records one that was applied, so a failed or deferred one is found again.

```
auto changes = watcher.Poll();          // the files that changed
reloader.Reload(replacements, tick, &report);
if (report.applied) watcher.Applied(...);
```

The tests `script_reload_test.cpp` check the hooks, a reload without hooks, a syntax error and a run error leaving the old version in every partition, a wait across a reload, a dependency and its dependents, the setting off, the once-per-tick rule, the identity of a callback not leaking out of the commit, 100 reloads under 16 threads, and the watcher.

## Debugging

Day 465 (`script/script_debug.h`, `script/script_runtime.h`): what a script author sees when something goes wrong, and how to stop the world and look.

**Errors.** A callback that fails is reported once per distinct error per instance (the same callback with the same message again is a repeat). The log gets one line, `behavior 'door' on entity 4294967296 ('Hero'): on_update failed: errors:3: boom` (the message is the language's own `script:line: message`; the entity's name comes from `SceneServices::name_of` and is left out when there is none), and a second line with the script's stack trace. A repeat is only counted; the log shows the count at every power of ten (`... (repeated 10 times)`). Every distinct error is also an engine event, `on_script_error`, sent at the end of the phase in partition order with the payload `{behavior, entity, entity_name, callback, message, trace, partition, tick}` (a script subscribes with `event.on('on_script_error', fn)`; a native subscriber through `ScriptRuntime::Events()`), a record in `ScriptRuntime::RecentErrors()` (the last 64, with the running `count`), and a count in `ScriptRuntimeStats::distinct_errors` (`errors` counts every failure).

**The debugger.** `ScriptRuntime::AttachDebugger(&transport)` (between ticks) makes a `ScriptDebugger` that owns the breakpoints and the stop state and gives each partition's state a `DebugSession`; the backend reports every line to it while a breakpoint, a step or a pause is pending, and a stop blocks the thread that runs the script inside the language's hook until a command lets it go. The wall-clock budget of the call does not run while it waits. A backend says it can with `Capabilities::debug` (LuaJIT: `lua_sethook` with the line, call and return events, and `lua_getinfo`/`lua_getlocal` for the stack and the locals; the three hooks share one dispatcher, so a state with a budget, a debugger and a profiler at once costs one hook).

The debugger talks through a `DebugTransport`: commands come in as `Value` maps, events go out as `Value` maps; `InProcessDebugTransport` is the in-process one (the tests and a UI console use it; a network adapter for an external debugger is not in this phase). Commands (`cmd`): `set_breakpoints` (`breakpoints`: an array of `{script, line}`; the script is the chunk name), `pause` (stop at the next line of any partition), `continue`, `step_over`, `step_in`, `step_out` (each for the `partition` that is stopped, the lowest one when absent), `stack`, `locals` (`frame`; `self` and the parameters come first), `detach` (drop every breakpoint and let everything run). Events (`event`): `stopped` (`partition`, `script`, `line`, `depth`, `reason` = `breakpoint`, `step` or `pause`), `continued`, `breakpoints` (`count`), `stack` (`frames`: `{function, script, line}`), `locals` (`variables`: `{name, value}`, a table shown field by field to a few levels and cut) and `error` (`message`). A bad command is an `error` event; it never stops the engine. `InProcessDebugTransport` keeps the newest events and counts the ones it dropped.

**What a stop holds.** The policy is explicit and is the setting `script.debug_pause_all` (default true). With it, a stop holds the world: every other partition finishes the callback it is in and then waits at a gate before its next call, so a tick does not end and the world is the one you stopped in; `continue` releases them. Without it only the stopped partition waits and the others run on (their breakpoints still stop them). A step lets the other partitions run until the next stop. Several partitions may be stopped at once (each at its own breakpoint); answer each with its `partition`. The main thread is never held by the debugger itself, only by the tick it is inside (the tick waits for its jobs), so a UI thread that talks to the transport stays responsive. A transport must outlive the debugger; `DetachDebugger()` (or destroying the runtime) lets stopped partitions run on first.

**The console.** `ScriptRuntime::ConsoleRun(partition, chunk, &result)` runs a chunk in a partition's state between ticks, first as an expression (`return <chunk>`) and then as statements, and returns the values or the script's own error (`ConsoleResult`). `kConsoleScratch` runs it in a bare state with only the language's standard library. `ConsoleSubmit(partition, chunk)` queues a chunk from any thread: it runs at the start of the next tick, before any callback (the sync point), in the order queued, and `ConsoleTake(id, &result)` takes the result. A console chunk is not an entity's callback, so a binding that needs one (`event.on`, `spawn`) fails with its own message, and a chunk cannot wait.

**The profiler.** `script.profile` (off by default; `ScriptRuntime::SetProfiling(on)` changes it at run time) times every Lua function call and return in the hook, so it slows scripts down: look for a slow script, then turn it off. `ProfileReport(&entries, top)` adds up the partitions that are not held, one entry per function (chunk, name, line, calls, nanoseconds including what it called), longest first; a call that did not return normally (an error, a yield) adds its count and no time.

The tests `script_debug_test.cpp` check the error text and the repeat counts, the event, the console (partition, scratch, queued), the profiler's counts, the transport queue, a breakpoint with stack, locals and step over and out, a bad command and `detach`, and the policy on eight threads: a held world does not move while a partition is stopped, an unheld one does, and 48 instances stopping and stepping in 16 partitions finish without a deadlock (also under asan and tsan).

## Physics

Day 466 (`script_physics/physics_bindings.h`, `script_physics/physics_layers.h`): scripts drive and question the physics world. The bindings are the separate target `script_physics` (layer 14, above `script` and `physics`), so `script` still never includes `physics`. A host wires it in these steps:

```cpp
script_physics::RegisterPhysicsScriptSettings(&settings_registry);  // script.physics_layers (the layer names)
script_physics::RegisterPhysicsComponents(&registry);           // rigid_body, velocity, collider in the storage registry
script_physics::RegisterPhysicsValidators(&catalog);            // their Validate() functions run at component.set/add
script_physics::RegisterPhysicsBindings(&bindings);             // the physics.* functions, before the runtime is made
script_physics::RegisterPhysicsCommandKinds(&runtime.Kinds());  // kinds 100 to 114, before the first tick
script_physics::CharacterSet characters(&physics_world);        // optional: `character.*` (installs the pre-step hook)
script_physics::PhysicsScriptServices services{&physics_world, &layers, &characters};
runtime.Services().extensions[script::kServicePhysics] = &services;
```

`script` offers only opaque pieces for this: the service slots (`MergeServices::extensions`, copied into `PartitionContext::extensions`) and `ScriptRuntime::Kinds()`.

**Components.** `component.add/set/get(e, "rigid_body" | "velocity" | "collider", {...})` work like any component, with fields `type` (0 static, 1 kinematic, 2 dynamic), `flags`, `mass`, `linear_damping`, `angular_damping`, `gravity_scale`; `linear`, `angular`; and for a collider `shape` (0 sphere, 1 box, 2 capsule, 3 hull, 4 mesh, 5 compound), `radius`, `half_height`, `half_extents`, `hull`, `mesh`, `compound`, `offset`, `rotation`, `friction`, `restitution`, `density`, `friction_rule`, `restitution_rule`, `layer`, `trigger`, `group`, `mask`. The physics module's own `Validate()` runs on the new values before the command is recorded, so `{mass = -1}` or `{layer = 40}` is an error at the call, with the physics reason. `physics::SyncBodies` makes the body at the next sync.

**Writes** are `world-write` commands applied in the merge, through the body's `BodyLink`: `physics.add_force(e, v)`, `add_force_at(e, v, point)`, `add_impulse`, `add_impulse_at`, `add_torque` (they add up, in the merge order, so forces from every partition sum to the same bits at any thread count), `set_velocity`, `set_angular_velocity` (they overwrite, and count as writes for the conflict policy), `set_gravity_scale` (changes the `rigid_body` component, which the sync follows), `wake`, `sleep` (a body and its island), `add_joint(owner, {type = "ball" | "distance" | "fixed" | "hinge", a = e, b = e, ...})` and `remove_joint(owner)` (put a `physics::Joint` on any entity, or take it off; `physics::SyncJoints` makes or destroys the physics joint). A kinematic body is moved the usual way: `transform.set_position(e, p)`, and `physics::DriveKinematicBodies` turns the change into a velocity so the body carries what stands on it. `set_gravity_scale` changes the component at the merge, so a `component.get(e, "rigid_body")` in the same tick still shows the old scale (the write is not part of the script's own-entity view; `component.set` is). A call that cannot work is an error where it is made: a non-finite number, a force on a body that is not dynamic, an entity with no body yet (a body exists after the next sync, so a script that creates a body asks for forces in a later tick).

**Reads** are `world-read` against the physics world as of the tick start: `get_velocity`, `get_angular_velocity`, `is_sleeping`, `raycast(origin, direction, max, layers)` (nil, or `{entity=, point=, normal=, distance=}`), `raycast_ex(..., ignore)`, `shape_cast(shape, position, rotation, direction, distance, layers)` with `shape = {kind = "sphere", radius = r}`, `{kind = "box", half_extents = v}` or `{kind = "capsule", half_height = h, radius = r}`, `overlap_sphere(center, radius, layers)` and `overlap_box(center, half_extents, rotation, layers)` (arrays of entities, each once), and `closest_point(point, max, layers)`. The physics world is not stepping while scripts run and the queries only read it, so every partition can query at once.

**Layers by name.** `layers` is nil (every layer), a layer name or an array of names. The names are the setting `script.physics_layers` (`default,world,player,enemy`: a name's position is its layer number, 0 to 31; the default is `default`). `physics.layer("enemy")` gives the number for a collider's `layer` field and `physics.layer_mask({"world", "enemy"})` the bits for its `group` and `mask`; an unknown name is an error.

**The frame order** is fixed, and a script sees the last step's results:

1. the script phase: scripts read the world and the physics world as of the tick start and record commands;
2. the merge applies the commands in the fixed order (forces, impulses and velocities into the physics world; gravity scale and joints into the components);
3. `physics::SyncBodies` and `physics::SyncJoints`;
4. `physics::DriveKinematicBodies`;
5. the physics step, the write-back of the transforms, transform propagation, the render sync.

The test `script_physics_test.cpp` runs this order and checks that an observer script reads velocity `N - 1` in tick `N` while an impulse recorded in tick `N` is in the body when the step runs; it also checks that 256 scripts pushing one body from 64 partitions leave the same physics state hash at 1, 2, 3, 7 and 16 threads, and that 300 scripts raycasting and overlapping from 16 threads at once agree (under tsan too).

Not here yet: contact and trigger events and the character controller are Day 467.

**Contact and trigger events (Day 467).** After each physics step the host calls `script_physics::EmitPhysicsEvents(physics_world, &runtime, options)`; the events reach the scripts that listen at the start of the next script phase, through the event bus:

```lua
behavior('crate', {
  on_create = function(self)
    event.on('on_collision_begin', function(me, hit) -- hit.other, hit.point, hit.normal, hit.depth, hit.impulse, hit.points
      if hit.impulse > 5 then log.info('hard hit by ' .. tostring(hit.other)) end
    end)
  end,
})
behavior('pad', { on_create = function(self)
  event.on('on_trigger_enter', function(me, info) --[[ info.other entered ]] end)
  event.on('on_trigger_exit', function(me, info) end)
end })
```

The names are `on_collision_begin`, `on_collision_stay` (only when `StepConfig::stay_events` is on), `on_collision_end`, `on_trigger_enter` and `on_trigger_exit`. Both entities of a contact get their own event (the one with the smaller stable id first), with `normal` pointing from `self` toward `other`; a trigger event goes to the trigger's entity, `other` being the body that entered or left. The order is the physics step's fixed event order, so a log of the deliveries is the same at any thread count (tested at 1 and 16 threads, with equal `StateHash`). **Filtering:** an event is built only for an entity that has a handler (`ScriptRuntime::HasHandler`: one lookup per receiver, so 100,000 events with no listener put nothing on the bus), and `PhysicsEventOptions::layer_mask` keeps out events whose other collider is on a layer not in the mask; `contacts` and `triggers` turn a kind off. A handler that runs several times in one tick sees the world's value of its own components each time, so count in a field of `self` and write the total.

**Characters (Day 467).** `character.create(e, config)` makes a kinematic capsule at the entity's transform (`{radius, height, step_height, min_ground_y, skin_width, gravity_scale}` or nil for the defaults); `character.move(e, velocity)` sets the horizontal velocity it walks at until set again, `jump(e, speed)` gives it an upward speed, `set_height(e, h)` crouches with the feet kept, `destroy(e)` removes it. Reads: `exists`, `is_grounded`, `ground_normal` (nil in the air), `ground_velocity` (the platform's velocity) and `vertical_speed`, as of the last step. The calls are merge commands; the moves themselves run inside the physics step: `script_physics::CharacterSet` (made by the host over its `PhysicsWorld`, in `PhysicsScriptServices::characters`) installs a pre-step hook (`PhysicsWorld::SetPreStepHook`) that moves every character at the start of each fixed step, so a frame with three steps moves it three times. The host calls `CharacterSet::Prune(ecs)` at the sync point (a character whose entity is gone goes away) and `CharacterSet::WriteBack(ecs)` after the step (a kinematic body is not written back by `WriteBackTransforms`). The events of a character's body name its entity when `PhysicsEventOptions::characters` is set. Raycast vehicles, ragdolls and the like are library scripts built from these primitives (the demo, Day 475), not engine features.

## Audio and input

Day 468 (`script_media/audio_bindings.h`, `script_media/audio_services.h`, `script_media/input_bindings.h`, `script_media/input_snapshot.h`): scripts play sounds and read the player's input. The bindings are the separate target `script_media` (layer 14, above `script`, `audio` and the platform's header-only input types), so `script` still includes neither `audio` nor a platform header. The service slots are `script::kServiceAudio` and `script::kServiceInput`. A host wires it in these steps:

```cpp
script_media::RegisterAudioComponents(&registry);           // audio_listener in the storage registry
script_media::RegisterAudioValidators(&catalog);            // gain 0 to 4 is checked at component.set/add
script_media::RegisterAudioBindings(&bindings);             // audio.*
script_media::RegisterInputBindings(&bindings);             // input.*
script_media::RegisterAudioCommandKinds(&runtime.Kinds());  // kinds 120 to 127, before the first tick
script_media::VoiceTable voices;
script_media::AudioScriptServices audio;                    // mixer, clip and stream lookups by name, music player, voices
audio.mixer = &mixer;  audio.voices = &voices;  audio.clips = {&FindClip, &clip_table};  audio.sample_rate = 48000;
script_media::InputSnapshot snapshot;                       // captured by the host at the tick start
script_media::InputScriptServices input{&snapshot};
runtime.Services().extensions[script::kServiceAudio] = &audio;
runtime.Services().extensions[script::kServiceInput] = &input;
```

**Audio is `world-write`.** Every call is a command in the partition's buffer; the merge posts it to the mixer in the fixed `(partition, entity, call)` order, so the mixer receives the same commands in the same order at any thread count (tested: 128 scripts starting, adjusting and stopping voices give the same hash of the rendered blocks at 1, 2, 3, 7 and 16 threads). Every argument is given: pass `nil` for no options.

- `audio.play(clip, options)` starts a clip by name (the host's `ClipSource` finds it; an unknown name is an error at the call) and returns the voice's **handle**, a number. `options` is nil or `{gain = 0..16, pitch = 1/16..16, pan = -1..1, bus = "name", loop = bool, priority = n, fade_in = bool}`; an unknown key, a wrong type or an out-of-range value is an error that names it. Spatial placement (`position`, `entity`) is not here: it belongs to the `AudioSource` system (see KNOWN-ISSUES).
- `audio.stop(voice, fade_seconds)`, `pause(voice)`, `resume(voice)`, `set_gain(voice, g)`, `set_pitch(voice, p)`, `set_bus_gain(bus, g)`, and for music `play_music(stream, fade_seconds)`, `crossfade_music(stream, seconds)`, `stop_music(fade_seconds)` (the host's `StreamSource` and `MusicPlayer`). The reads `audio.is_playing(voice)` and `audio.exists(clip)` are `world-read`. Fades are in seconds, 0 to 600.
- **Handles.** A handle is made of the tick and the partition's reserved placeholder number, so it depends only on the script's own calls, not on the thread count; the merge binds it to the mixer's `VoiceId` in the `VoiceTable`. The handle of a voice started in the same tick can be used at once (the commands apply in order). Using the handle of a voice that was stopped, or that has ended, is a script error ("the voice was stopped", "the voice has ended"), and so is a number that is not a handle. A `Play` the mixer refuses (the pool is full of better voices) leaves a handle with no voice, which then reports "has ended".
- **`on_voice_end`.** The host calls `script_media::PumpAudioEvents(&mixer, &voices, &runtime)` once a frame, after the audio blocks that frame rendered: for each voice that finished, was stopped or was stolen, the entity that started it gets `on_voice_end` with `{voice = handle, reason = "finished" | "stopped" | "stolen"}` at the start of the next tick, and the handle is forgotten. An event is built only for an entity with a handler. The events arrive in block order from an offline mixer; with a device they follow the audio thread, so a game that needs repeatable event timing drives an offline mixer.

**`audio_listener`** is a registered component (`gain`, `active`); `AudioSource` is not (it holds shared clip pointers, which a stored component cannot).

**Input is a snapshot.** The host calls `InputSnapshot::Capture(tick, os::InputState, text, map, externals)` once at the start of each tick, before the script phase; every script, in every partition, reads that one copy, so the input is identical whatever the thread count and however long the tick runs. The reads are `world-read`: `input.key_down(key)`, `key_pressed`, `key_released` (keys by lower-case name: `"a"` to `"z"`, `"0"` to `"9"`, `"f1"` to `"f12"`, `"space"`, `"enter"`, `"escape"`, `"left"`, `"leftshift"`, ...), `modifier("shift" | "ctrl" | "alt" | "super")`, `mouse_position()`, `mouse_delta()`, `scroll()` (vec2), `mouse_down(button)`, `mouse_pressed`, `mouse_released` (`"left"`, `"right"`, `"middle"`, `"back"`, `"forward"`), `pointer_inside()`, `text()` (what was typed since the previous capture), and the named inputs the host defined: `input.axis(name)` (-1 to 1), `input.action(name)`, `action_pressed(name)`, `action_released(name)`. An unknown key, button, action or axis name is an error. A press and a release between two captures are not seen (the snapshot compares states).

The platform has no action-mapping system, so the host fills an `InputMap`: `AddAction("jump", {Key::Space, mouse left})` is down while any binding is, `AddAxis("horizontal", {A, Left}, {D, Right})` is -1, 0 or 1, and `ExternalAxis` values the host read itself (a gamepad stick) are added to the axis of that name. `EmitInputEvents(snapshot, &runtime)` also puts the changes on the bus as `on_input` events (`{kind = "key_down" | "key_up" | "mouse_down" | "mouse_up" | "scroll" | "text", key = , button = , dx = , dy = , text = }`) for every subscriber.

**Replay.** `InputSnapshot::Serialize(&writer)` writes the snapshot (little endian, versioned) and `Deserialize(&reader)` reads it back, replacing the snapshot: a log of the snapshots of a session, fed back tick by tick to a fresh world, gives the scripts the input they had (the test `script_media_test.cpp` replays 24 recorded ticks and compares the scripts' decisions). `Deserialize` refuses a short record, a wrong version, an oversized name or text, and non-finite numbers. Day 469 stores the per-tick snapshot in the replay log.

## Saving scripted state

A script marks the fields of `self` a save keeps with `persist("hp", "inventory")`, called from a behavior callback (usually `on_create`). Everything else is by design
not saved: closures, userdata, coroutines and any field not marked; a script rebuilds them in `on_load`.

- **What is saved.** Numbers, strings, booleans, vectors, quaternions, byte blobs, entity handles, and plain tables of those (within the `Value` limits: depth 16, 65,536
  items per container). Inside a table, entries that cannot be saved (functions, threads, userdata) are left out; a field whose value holds such an entry inside a table
  with a metatable is skipped as a whole (`PersistReport::fields_skipped`) and the instance's other fields are still saved. A field that is `nil` is not saved.
- **Two components carry it.** `script` (`ScriptComponent`: behavior name, parameters, enabled; id 130) and `script_persist` (`ScriptPersist`: the saved fields as one value
  record; id 131), both registered by `RegisterScriptComponents(&registry)` and saved by `storage::WorldStorage` like any component: full and incremental saves, versioned
  (version 1), every length held to the registry's limits. A record that does not decode is refused as `Corrupt` by value; a persistent record must be a map.
- **Before a save**, between ticks and after `KeyIndex::Sync`, call `ScriptRuntime::CapturePersistent(&world, index, &report)`. It reads each running instance's marked fields
  from its partition's state and writes them as `script_persist` components; entity handles inside values are written as stable keys. The report counts what it did and
  carries `state_hash` (a hash of the saved state, equal for equal state at any thread count), `tick` and `sim_time`.
- **After a load**, call `ScriptRuntime::ResolveLoaded(&world, index, &report)` once, then `ScriptRuntime::RestoreClock(tick, sim_time)` with the values the save
  reported. `ResolveLoaded` decodes each `script_persist` (entity keys resolved through the index), removes the component and holds the fields until the entity's instance is
  made. The instance then runs `on_create`, gets the saved fields back (they replace what `on_create` set), runs `on_load`, and only then `on_enable` and `on_start`. A
  record that does not decode is counted (`LoadReport::corrupt`) and its instance starts without it. A save taken before the first tick after a load writes the held fields
  back.
- **Entities.** Entity handles inside persistent fields are written as stable keys and found again after the load (a handle whose entity is not in the world reads as an
  invalid entity). Handles inside a `script` component's `params` are written as packed ids and are not remapped.
- **Not saved.** Coroutines and timers waiting at the time (`PersistReport::coroutines_not_saved` counts them) and events queued but not yet delivered: a restored session
  starts without them. `persist_coroutine` and `on_cancel` are not built (`KNOWN-ISSUES.md`).
- **Equal runs.** A scripted entity's random stream is seeded from the runtime seed, the entity id and the tick; `RestoreClock` restores the tick, and a world loaded in key
  order into an empty world gets its entities in that order. A session saved at tick N and continued on any thread count reaches the same hash as the uninterrupted one
  (`tests/script/script_persist_test.cpp`).
- **Replay.** `script_media::ReplayLog` records, for every tick, the `InputSnapshot` the scripts read and the world's hash after the tick (`Record`), writes and reads them
  (`Serialize`, `Deserialize`: a version, a count, rising ticks and every snapshot are checked) and gives snapshots back (`Snapshot`). Feed them to a fresh world; a session
  recorded at 16 threads replays to the same hashes at 1, 2, 3, 7 and 16, and the reverse (`tests/script_media/script_media_test.cpp`).

## JIT or interpreter

LuaJIT can compile hot loops to machine code. The engine treats that as a platform and trust choice, not a language feature scripts rely on: every script gives the same results
in every mode, and only the speed differs (`tests/script/script_jit_test.cpp`; the whole phase also runs on a build without the compiler, below).

- **Budget mode decides first.** A *budgeted* world (`script.budget = budgeted`, the default) runs the interpreter with a count hook, so every call can be limited in instructions and time;
  it never uses the JIT. A *trusted* world (`script.budget = trusted`) may use the JIT and refuses instruction and time budgets with `Unsupported` (a count hook cannot stop compiled
  loops); memory is limited in both.
- **`script.jit`** (default true; env `ENGINE_SCRIPT_JIT`, flag `--script-jit`, restart required) turns the compiler off for a trusted world: it then runs the interpreter with no
  instruction budget. `luajit::OptionsFromSettings` reads it with `script.budget`, `script.jit_code_kb` and `script.random_seed`.
- **`ENGINE_LUAJIT_JIT`** (CMake option, default ON) builds LuaJIT without the compiler (`LUAJIT_DISABLE_JIT`) for platforms that forbid run-time code generation. `luajit::JitCompiledIn()`
  tells which build this is; `Backend::Caps().jit` and `State::Caps().jit` report what a state really does (budgeted: false; `script.jit` off: false; compiled out: false).
- **Machine code per state.** Every partition is its own compiler. `script.jit_code_kb` (default 0, LuaJIT's 2048) caps the machine code of one state (LuaJIT's `maxmcode`); a state always
  has one 64 KB area, so values below 64 act as 64, and a state that reaches the cap flushes its traces and goes on correctly. `State::JitReport(&stats)` gives one state's `JitStats` (enabled,
  traces, code bytes) and `ScriptRuntime::JitTotals(&stats)` adds up the partitions that are not held. In the measurement below a state holding 300 hot loops keeps about 159 KB, so 256 partitions
  keep about 40 MB uncapped.
- **What it costs** (the spec's loop of 2e8 iterations, Debug build of the engine, Linux x86-64, `script_tests "[.jit_bench]"`): the JIT 467 ms; the interpreter without a budget 865 ms; the interpreter with
  the count hook 1,262 to 1,703 ms between runs. Recommendation: shipped game scripts run trusted with the JIT (`script.budget = trusted`); mods and other untrusted scripts run budgeted;
  a platform with no JIT runs trusted with `script.jit` off or a build with `ENGINE_LUAJIT_JIT=OFF`.

| Platform | Mode | Notes |
|---|---|---|
| Linux x86-64 | JIT available, budgeted by default | built and tested (plain, asan, tsan, and the no-JIT build) |
| Windows x64, macOS x86-64 | JIT available | Days 473 and 474 build and test them |
| Android | JIT allowed | decided in Phase 29 |
| iOS, consoles | interpreter only | `ENGINE_LUAJIT_JIT=OFF` (Phases 29 and 30) |

**The `script_nojit` flavor.** The preset `linux-nojit` (tree `build-nojit`) configures `-DENGINE_LUAJIT_JIT=OFF`; the phase runs on it with
`scripts/test-phase.sh 27 --no-display --build-dir build-nojit`. Tests that expect the compiler check `luajit::JitCompiledIn()` first.
