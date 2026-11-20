# The engine as a loadable library

The engine can be built as one shared library (`libengine.so.1`, `libengine.1.dylib` on macOS) that a host program loads at run time and drives through a flat C API, next to the static
libraries games link today (Phase 23; design in `docs/superpowers/specs/2026-11-16-engine-library-design.md`). The page is the reference for the whole phase: building, what is exported, the C API's conventions, versioning, loading, embedding on each platform, the cost
of the shared build and the limits. To write a host step by step, start with the [tutorial](../tutorials/loading-the-engine.md).

## Building it

`cmake -DENGINE_BUILD_SHARED=ON` (Linux from Day 343; see `building-and-testing.md`) adds the targets `engine`, `engine_asan` and `engine_tsan`. The option
also compiles every target position independent with hidden symbols, so nothing leaves the library unless it is exported on purpose.

## What the library exports

Only the C API: `extern "C"` functions marked `ENGINE_API` (`foundation/build_defs.h`). `ENGINE_API` is `__attribute__((visibility("default")))` (or
`__declspec(dllexport)` on Windows) in the sources that define the exports (the build defines `ENGINE_BUILDING_LIBRARY` for `engine/engine_c`) and empty
everywhere else, so a static build is unaffected. Nothing else is exported: no C++ type, no function with a C++ signature, no variable.

The list that counts is `abi/engine.exports`: the linker's version script is generated from it (`engine_shared.map` in the build tree, a node named
`ENGINE_<major>`, everything else local), and the gate `abi_check` (`tools/abi_check`, ctest `abi_check*`) compares it with three things:

1. `include/engine/engine_abi_version.h`: the version line of the list and `ENGINE_ABI_VERSION_MAJOR` and `_MINOR` must be the same numbers;
2. the built library's dynamic symbol table (`nm -D --defined-only`): the library exports every listed symbol and nothing else;
3. the last commit's list (`git show HEAD:abi/engine.exports`): **a symbol added needs a higher minor version, a symbol removed needs a higher major version**,
   and the version never goes down. A change of the ABI is a decision made in the same diff as the code, not an accident.

The C header `engine_c.h` (Day 346) is checked too: it must compile as C11 with `-pedantic -Werror` and hold no C++ token outside a `#ifdef __cplusplus` block.

### Adding an export

1. Declare and define the function in `engine/engine_c/` as `extern "C" ENGINE_API ...` (from Day 346 only `engine_get_api` is exported: a new function is a new member at the end of the `engine_api` table in `engine_c.h`, not a new export; a new
   export is for a second entry point, which needs a reason).
2. Add its name to `abi/engine.exports` and raise the minor version there (and in `engine_abi_version.h`); a removal raises the major version.
3. Build with `-DENGINE_BUILD_SHARED=ON` and run `ctest -R abi_check`: it names every symbol that differs.
4. Update the ABI snapshot (Day 347) and the guide when the function is part of the C API's documented surface.

## One of everything (Day 345)

The engine library is the only place the engine's singletons live. A plugin or a second library that links the static foundation or job system carries its
own copy of each under hidden visibility: another log (ring, line count, channel list), another `HeapAllocator::Default()`, another set of job counters,
another frame pool and another set of thread-locals. **A plugin links the C API only** (it never includes an engine header that defines state, and never
links an engine static library); what it needs it gets through the function table.

The rules, and what enforces them:

- **No mutable static state in a public header.** The gate `shared_state_check` (`tools/shared_state_check`, ctest `shared_state_check*`) fails on a
  `static` non-const object (function scope or `static inline` member), a `thread_local` or a non-const `inline` variable in any header under `engine/`.
  Move the object into the module's `.cpp` (one definition, inside the library) or list the header in `tools/shared_state_check/allowlist.txt` with the reason
  it is safe (`path|reason`; a stale entry fails). Moved on Day 345: the log channel registry head, `jobs::Counter::PathStats()` and
  `ecs::detail::SystemFn::Fallbacks()`. Allowed with a reason: `HeapAllocator::Default()` (no data), the `ENGINE_ENSURE` site record, `ecs::World`'s component tag
  (the C API forbids C++ component access) and `ENGINE_DEATH_TEST_FN` (expanded in test programs only).
- **Nothing is freed by the side that did not allocate it.** A buffer the engine hands out comes back through the engine's release function, never the
  host's `free`. The host tests count the blocks the engine still holds after a release (`engine_owner_*`).
- **No `thread_local` destructor on a thread the engine does not own.** `ld.so` refuses to unload a library that registered a thread-exit destructor on a thread
  that is still alive. The three per-thread things on the engine's API path were the frame pool's cache, the crash handler's alternate stack and the Vulkan
  backend's command-pool slot; each is plain data now, handed back through `NotifyThreadEnd()` (`log/diagnostics_hooks.h`), which runs the functions added
  with `AddThreadEndHook` (the first use of each registers its own). The job workers, the I/O thread and the end of the scheduler's pool call it; **any other
  thread that used the engine must call it before it ends** (the C API gives a host thread `engine_api.thread_detach`, Day 346). A thread that does not strands at
  most 64 cached blocks per size class and one 128 KiB, mostly untouched, alternate stack, and keeps its Vulkan slot (there are `kThreadSlots` of them, so a
  program that records on many short-lived threads must call it).
- **The crash handler is installed once, by the engine, and removed at shutdown;** the host's own handlers are back afterwards.

The tests (`tests/engine_library`, Linux, `-DENGINE_BUILD_SHARED=ON`) are small C programs that `dlopen` the library: `engine_dup_singletons` (a plugin that
links only the C API sees the engine's allocator, counters, log and frame pool) with its planted failure (`engine_dup_singletons_planted*`: a plugin that
links the static foundation is detected, by name), `engine_unload_after_jobs` (plain, asan, tsan: jobs on the calling thread and on a joined host thread,
the crash handler installed and removed, then `dlclose`: the library must be gone), `engine_owner_*` (plain, asan) and `engine_crash_across_boundary` (the
host's `SIGSEGV` handler is restored; a crash inside a job leaves an envelope with a minidump). Since Day 346 they reach the library through
`engine_get_api` and read the state table through the extension `engine.test_state` (`engine/engine_c/engine_test_extension.h`, below).

## The C API (Day 346)

`include/engine/engine_c.h` is the whole surface a host sees: plain C11, one exported function and a table. The convention comment at the top of the header is
the specification; this is a summary of the rules and their reasons.

**One entry point, one table.** The library exports `engine_get_api(major, minor, table_size, &table)`. It returns a pointer to a static `engine_api`, a
struct of function pointers. The host asks for the version it was compiled against; the library serves the same major and any minor up to its own, and
refuses anything else with `ENGINE_ERROR_UNSUPPORTED_VERSION` (`*out` is NULL then). New functions are appended to the table in a new minor; a host that
wants one checks `table->struct_size` before it reads the pointer, so an older library never has its table read past the end. The table names the library's own
version and the sanitizer it was built with (`sanitizer`: a sanitized library loads only into a host built with the same one).

**Status.** Every function that can fail returns an `engine_status`; zero is success; a failing call changes nothing it was asked to change. The numbers are
part of the ABI. `status_name` gives the name of a code (`stale_handle`). The text of the most recent failure on the calling thread is `last_error`, a message for
people (the code is what a program branches on). The codes come from one table (`engine/engine_c/status_map.h`): `asset::ErrorCode` and
`settings::SetError` each map onto them by a `switch` with no default, so a new engine error is a compile error until it is mapped, and the test
`engine_c_tests` checks every mapping and that every status has a unique name.

**Strings and ownership.** A string is a pointer and a length (UTF-8, not NUL-terminated). A string the host passes in is borrowed for the call. Memory the
library produces comes back in an `engine_buffer` with its own `release` function: the host reads it and calls `engine_buffer_release` once, and the
library's allocator frees it. Nothing allocated by one side is freed by the other.

**Structs that grow.** Every struct that may gain fields starts with `uint32_t struct_size`. In: the host sets its `sizeof`; the library reads that many
bytes and treats the rest as zero, and refuses a size below the struct's first version (`ENGINE_ERROR_STRUCT_SIZE`). Out: the host sets its `sizeof`, the
library writes at most that many bytes and reports how many.

**Handles.** An object is an `engine_handle`: 64 bits, the low half a slot and the high half the slot's generation, zero meaning none. A handle of a destroyed
object is `ENGINE_ERROR_STALE_HANDLE`; one that was never issued is `ENGINE_ERROR_INVALID_ARGUMENT`; neither crashes.

**The engine object.** `create` takes an `engine_create_info`: the host's settings (`key`/`value` text entries), the command line, an optional log
callback and a flag. The settings go through the engine's settings system and nothing else is read: **not the environment, not the user's own settings
file, not a policy**. Layers, lowest first: the host's entries, the engine's own writable layer (what `setting_set` changes), the command line (`--set
key=value` and the declared flags; `args[0]` is the program name and is skipped). A key the engine does not declare is `ENGINE_ERROR_NOT_FOUND`; a value
of the wrong type, a command-line error or a note from the load (an unknown key on the command line) is `ENGINE_ERROR_INVALID_ARGUMENT`, and either creates
nothing. `setting_get` returns a setting's current value as text in a buffer.

**The log callback** receives each line the engine emits, after the channel's own filter (the `log.<channel>.verbosity` settings decide what arrives), on the
thread that logged, possibly from several threads at once. `ENGINE_CREATE_LOG_SUPPRESS_CONSOLE` keeps the lines off the console. It is made with
`SetLogObserver` (`log/log.h`), which waits for callbacks in flight when it is removed, so `destroy` returns only when none is running. A callback may call
the other API functions on its own thread, must not call `create` or `destroy`, and must not throw.

**One engine per process.** The engine's singletons are the process's, so a second `create` while one exists is `ENGINE_ERROR_LIMIT_EXCEEDED`. A change to
`log.*` settings is applied to the process's log channels and is not undone by `destroy` (the next engine starts from the channels as they are).

**Extensions.** `get_extension(name)` returns a static struct of functions beyond the core, or NULL. The one the library has today is `engine.test_state`
(`engine_test_extension.h`): the addresses of the engine's singletons and a few operations (run jobs, install a crash handler, allocate and release) that the
boundary tests of Day 345 need. It is compiled in only when `ENGINE_C_TEST_EXTENSION` is ON (the default while the phase is under way); a release build
returns NULL for it, and the C API never needs it.

**Threads.** Calls are thread-safe unless a comment says otherwise. `thread_detach` is called by a host thread that used the engine, once, just before the
thread exits (see "One of everything").

### Building and testing it

`engine_c` is a static library (plain, asan, tsan; Linux) built always, so a C program can link the C API without the shared library. `host_c`
(`tests/engine_library/host_c.c`, ctest `engine_c_host*`) is such a program: it creates an engine with settings, a command line and a log callback, reads and
changes a setting, destroys the engine, and makes every bad call the API documents (null and wrong-size structs, a stale and a never-issued handle, a version
too new, a second engine, a bad key and a bad value); each must return its documented status. The same C API is what the shared library exports (Day 347
loads it with `dlopen`).

## Loading the library (Day 347)

`include/engine/engine_loader.h` is header-only plain C that needs nothing but `engine_c.h`: a host copies the two files. `engine_load(path, major, minor,
&loader)` opens the library (`dlopen` with `RTLD_NOW | RTLD_LOCAL`; on Windows `LoadLibraryExW`, Day 354: below), finds `engine_get_api`, asks for the version
the host was compiled against and checks that the table says the same; `loader.api` is then the function table. Every refusal leaves a sentence in
`loader.message` and nothing loaded: a file that is missing or not a library (`ENGINE_ERROR_IO`), a library without the entry point
(`ENGINE_ERROR_UNSUPPORTED`), another major or a newer minor than the library has, or a table that does not say what it should
(`ENGINE_ERROR_UNSUPPORTED_VERSION`). `engine_unload` closes the library; destroy every engine and release every buffer first.

`tests/engine_library/host_dl.c` (`engine_host_dl`, plain, asan, tsan) is the reference host: it checks each refusal against stand-in libraries
(`fake_engine.c`) and the real library asked for a newer minor and another major, then loads the real one, creates an engine, runs jobs through the test
extension (the frame loop arrives on Day 349), destroys it, unloads, and checks that the library left the address space.

### The ABI snapshot and what a binding may rely on

`abi/engine_api.snapshot` records the value of every status and the size and every field's offset of every struct of `engine_c.h`, in order; it is written by
`abi_snapshot_gen` (compiled against the header) and checked by the test `engine_abi_snapshot`. The test passes when the generated text is identical, or when
the minor version went up and only appends happened (every status and field line of the snapshot still present unchanged; a new field or struct is
allowed); anything else fails and names the line. After a legitimate append, run `ninja engine_abi_snapshot_update` and commit the new snapshot. A new
struct member or table function is also added to the lists in `abi_snapshot_gen.c`; the struct size line catches one that is missed. The layout is that of a
64-bit target.

A language binding (Phase 27's Lua, any other) binds through this table and may rely on: functions are only ever appended to `engine_api`, and a field
only to the end of a struct; a status number never changes; a function's meaning never changes within a major version; `struct_size` says how much of a
struct is real. It may not rely on the text of a message (`last_error`, `loader.message`), on the order of log lines, or on the extensions (`get_extension`
names other than the ones the guide documents).

## Shutdown, unload and reload (Day 348)

**What `engine_destroy` guarantees.** When it returns `ENGINE_OK`:

- the engine's handle is stale, and the log callback is gone: it is not running on any thread and will not run again (destroy waits for a callback that is running);
- the engine's settings service, registry, layers and listeners are destroyed (they belong to the engine object);
- the process's log channels have the verbosity and enabled state they had before `engine_create` (the `log.*` settings changed them for the engine's life);
- a new engine may be created.

What a host must do, and what the engine does not do for it:

- **Give objects back first.** The engine counts the objects its API hands out (the worlds, loaded resources and imports of Days 349 to 351 register with `engine_c/engine_objects.h`). `engine_destroy` with one outstanding returns `ENGINE_ERROR_IN_USE` ("n object(s) of the engine are still outstanding"), changes nothing and may be called again once the host has released them. There is no `force`: cancelling a host's loads behind its back would leave it with handles to nothing.
- **Do not unload while a thread is inside the library.** `engine_destroy` waits for a log callback that is running, but the library cannot stop a host from `dlclose`-ing a library a thread is still executing in. Unload only after destroy has returned and every host thread that called the engine has called `thread_detach` and left it. While destroy waits, the handle is already stale (calls fail with `ENGINE_ERROR_STALE_HANDLE`) and `engine_create` fails with `ENGINE_ERROR_LIMIT_EXCEEDED` until the old engine is gone (`host_inside.c`).
- **Do not unload while the engine is alive.**

**Unload.** The library is not linked `-z nodelete`: it unloads when its last `dlclose` runs, and the test `engine_reload_cycles*` proves it by loading, creating, running jobs, destroying and unloading 50 times in one process (plain, asan, tsan), checking after every cycle that the library is gone from `/proc/self/maps` and from the dynamic linker, that the process has no thread it did not have, and after the last that the resident size has not grown (plain: within 8 MiB of its size after the fifth cycle; the sanitizers' allocators get 64 MiB). Under asan the leak report at exit is the check for what is really lost. What an unload still has to give back is done by the library's destructor (`EngineLibraryUnloaded`, in the shared library only): the job system's process-wide frame pool, immortal in a program, frees its slabs (`FramePool::DestroyGlobalAtUnload`); without it each cycle kept about 2 MiB.

**Audit.** The things an engine registers, and who removes them: the settings listeners and the `log.*` application (the settings service, at destroy); the log observer (destroy); the log channels' state (destroy restores it); the job scheduler's threads (the scheduler that made them, which joins them when it is destroyed: the engine starts its job system on the first job submitted, Day 349, and the world and counter objects below keep `destroy` from running while any job can still run); the crash handler (`crash_handler_install` and `_uninstall`; destroy removes it if the host did not); the per-thread caches (the thread's own `thread_detach`); the global frame pool (the library's destructor). No static destructor touches another static.

## The frame loop, worlds, jobs and the crash handler (Day 349)

The rest of the runtime surface of version 1.0, all reached through the table (`engine_c.h`; `host_runtime.c` is the C program that drives every function).

**The frame.** The host drives the loop: `frame(engine, &info, &stats)` runs, on the calling thread, one frame of every world's systems (today the transform propagation: it writes
each entity's world matrix from its local transform) and returns. Nothing in it waits for the host, and no engine thread starts a frame by itself. `info` has the time step
(`delta_seconds`) and an optional budget in microseconds: when it is spent, a world that has not started is left for the next frame (counted in `stats.worlds_skipped`), and the
next frame begins with the first world it left, so no world starves. One thread runs frames at a time, the host's frame thread; the other calls may run beside it.

**Worlds and entities.** `world_create` makes an empty world; it is an object of the engine, so `engine_destroy` refuses (`IN_USE`) while one exists, and `world_destroy` gives it back.
`entity_create` returns a 64-bit key: its generation part makes the key of a destroyed entity fail with `STALE_HANDLE` even after its slot is reused. A world, an entity and a counter
never expose a pointer: components are read and written by value.

**Components are a typed, size-prefixed property API.** There are four kinds (`engine_component_kind`; the numbers are the ABI): `TRANSFORM` (`engine_transform`: position, a unit rotation
quaternion that the engine normalizes, scale), `WORLD_TRANSFORM` (`engine_world_transform`: the column-major matrix; read only: the frame writes it, so a transform set on an entity shows
as a matrix after the next frame), `NAME` (up to 63 bytes, set and read with `entity_name_set/get`) and `MESH_REFERENCE` (`engine_mesh_reference`: a mesh identifier the host chooses,
which Day 350's loaded resources name, and a material). A struct goes in with its `struct_size` first (a size below the first version is `STRUCT_SIZE`); a struct comes out the same way. Setting a
transform adds the world transform with it, and removing it takes both. `query(world, mask, keys, capacity, &total)` finds the entities that have every kind of `mask` (`ENGINE_COMPONENT_BIT`),
writes up to `capacity` keys and reports the total, so a host that is told `total > capacity` calls again with a bigger array; the order is not specified.

**Jobs.** `counter_create` makes a counter (an object of the engine), `jobs_submit(engine, fn, user, counter)` queues `fn(user)` on an engine worker thread and counts it on the counter, and
`counter_wait(counter, timeout_ms)` sleeps the calling thread until the counter is zero (`NOT_READY` on a timeout, `UINT64_MAX` waits for ever). The engine starts its job system, a scheduler on a thread of its own with
its workers, on the first submit, and stops it at `engine_destroy`; the host's threads never become participants, so a host thread that waits sleeps and does not run jobs. A counter cannot be
destroyed while a job submitted with it has not finished or a thread waits on it (`IN_USE`).

**Thread rules** (the same text is at `engine_job_fn` in `engine_c.h`): every function of the table may be called from any host thread. The world, entity, component and query functions and `frame` are serialized with each
other by one lock, so a call waits for a running frame and a frame waits for a running world call; counters have a lock of their own. A job callback runs on an engine thread, never on the thread that submitted it, and may call
`status_name`, `last_error`, the world, entity, component and query functions, `counter_value`, `counter_create`, `counter_destroy` and `jobs_submit`. It must not call `frame`, `create`, `destroy`,
`counter_wait` (refused with `INVALID_ARGUMENT` inside a job: it would sleep a worker the counter may need), `crash_handler_install` or `crash_handler_uninstall`. A job never throws or longjmps, and the host must not destroy the engine while one of
its own threads is inside a call (the library does not wait for it).

**The crash handler.** `crash_handler_install(engine)` installs the engine's handler for the process: a crash on any thread, a job included, writes a dump (an `.envelope` with a minidump) to the directory of the
setting `crash.dump_directory` (empty: the user's crash directory). `crash_handler_uninstall` restores the handler that was there before (the test installs its own `SIGSEGV` handler first and finds it again), and `engine_destroy`
removes the handler if the host did not.

- **Where the dump goes:** give `crash.dump_directory` at creation, before `crash_handler_install`: an `engine_setting_entry` `{"crash.dump_directory", "/path/to/dir"}` in `engine_create_info.settings`. The directory is made (private to the user) if it is
  missing; the report is `crash-report.txt` in it, and each crash's `.envelope` (the minidump with the report attached) and `.crash` file go in a `<run id>.run` directory below it.
- **A host with its own crash handler** (Linux, macOS and Windows): a handler installed before `crash_handler_install` still runs, after the engine's report and dump, once, in signal context on the faulting thread,
  then the original fault ends the process (`foundation-debugging.md`, "If your program has its own signal handler"). `crash_host.c` checks it: its child installs the host's `SIGSEGV` handler first, crashes in a job, and finds both the
  host's marker and the envelope. The dump the crash test checks is in a directory of the run, so a dump of an earlier run cannot make a failing run pass.
- **On Windows** the host's handler is its unhandled-exception filter, set before `crash_handler_install`: it runs after the report and the dump, once, on the faulting thread, with the original exception, and the process still ends with
  the original code; a `SIGABRT` handler set before runs after them for an `abort()`, which raises no exception (a crash inside a job can end that way on Windows: the job's assert aborts). `foundation-debugging.md`, "If your program has its
  own unhandled-exception filter (Windows)". `crash_host.c`'s Windows child installs both, and one of them leaves the marker.

**Tests.** `host_runtime` (ctest `engine_c_runtime*`, plain, asan and tsan; static link) creates a world with a thousand entities with transforms, names and mesh references, queries by every combination, runs frames and reads the
matrices, runs a frame with a budget over two worlds, submits 500 jobs and waits, runs jobs that create entities through the table, waits with a timeout, destroys a counter with a pending job (refused), makes every bad call (a stale
world and entity, a counter used as a world, a wrong struct size, a name too long, a non-finite transform), runs two host threads each with a world and a counter beside a third that runs frames (the thread-safe subset, under tsan), and,
in a child process that starts before any thread exists, crashes inside a job and counts the envelope the handler left.

## Host-provided content and loading (Day 350)

A host that has its content in memory (it downloaded it, unpacked it, generated it) hands the minted blobs to the engine and loads them through the table:
`resource_add`, `resource_replace`, `resource_remove`, `resource_mounted`, and `resource_load` / `resource_load_async` with their handles and tickets
(`engine_c.h`, "Day 350"). The engine never mints: the host adds blobs that `asset_mint` made (decision of Day 342).

- **One mount.** The engine mounts its `HostMemoryBackend` at `/` at the priority `assets.memory_priority` (default 1000; the mounts of `assets.mounts` have
  the priorities 0, 1, 2, ...), so host content wins over files. The budget is `assets.memory_backend_bytes` and counts lent bytes too; an add past it is
  `LIMIT_EXCEEDED` (back-pressure, never eviction). The resource settings are read when the engine is created.
- **Copied or lent.** Without `ENGINE_RESOURCE_BORROW` the bytes are copied and the host frees its buffer when the call returns. With it the engine reads the
  host's memory in place and calls `release(user, bytes, size)` exactly once, on an engine worker thread, after the last reader of those bytes is gone: the
  resource was replaced or removed and no load reads it any more, or the engine was destroyed (`destroy` returns after every pending release ran). A call
  that fails never calls `release`: the host keeps its buffer. A lent buffer must stay valid and unchanged until its release; a buffer that was overwritten
  no longer opens as a blob and the load fails with `CORRUPT`, and AddressSanitizer reports one that was freed.
- **Checked as a whole.** An add or replace opens the blob (container, type, version), checks that its own id is the id given and that its content hash
  verifies; `ENGINE_RESOURCE_CHECK_HASH` also compares the hash with the one the host promises.
- **Dependents.** Replacing or removing a resource that a loaded resource depends on is refused with `IN_USE` and the ids of the dependents come back in a buffer
  (an array of `uint64_t` values). `ENGINE_RESOURCE_FORCE` unloads them first, atomically, and their handles read `loaded` 0 in `resource_status`. A
  replace of a loaded resource nothing depends on rebuilds it in place: its handles stay valid.
- **Loading.** `resource_load` names the resource by path (when `path.length` is not 0) or by id and the type the host expects; it loads the whole closure
  or fails (`MISSING_DEPENDENCY`, `TYPE_MISMATCH`, ...). The handle keeps it loaded and counts as an object of the engine (`destroy` refuses with `IN_USE`
  until it is released). Nothing of the loaded data is exposed: there is no pointer into a blob. Rendering uses the handle through the frame loop (later days).
- **Tickets.** `resource_load_async` returns a ticket at once; the blobs are read on the engine's workers and the objects are built by the host's own calls
  (`frame`, `ticket_poll`, `ticket_wait`, `ticket_finish`), never by an engine thread on its own, so a program's GPU objects can stay on its render thread
  when later days add them. `ticket_finish` gives a handle (each call its own) or the load's error; `ticket_cancel` stops the load; `ticket_destroy` ends the
  ticket and cancels what has not finished.
- **Tested** by `host_resources.c`: blobs minted by `asset_mint`, the bad calls of every function, the dependents refused and forced, a texture replaced 24 times
  while a handle is held and another thread runs frames, every release counted (once, off the host's thread), the engine destroyed holding lent buffers, a
  lent buffer overwritten (`CORRUPT`), and the tickets (poll, wait, finish twice, failure, cancel, destroy before the end).

Not here: paks and loose files are not mounted by the C API yet (`assets.mounts` is declared but only the memory mount is made), and GPU resources come
with the renderer's C functions.

## Importing a glTF at run time (Day 351)

A program that fetches a glTF file from somewhere (a 3D room model from a server, say) hands the bytes to the engine and gets resources it can
load: `import_gltf` (synchronous) and `import_gltf_async` with its ticket calls (`import_poll`, `import_wait`, `import_finish`, `import_cancel`,
`import_destroy`), described in `engine_c.h` ("Day 351"). The importer is `engine/asset_import` (Phase 22); the C API is a thin skin over `ImportGltf`,
`ImportGltfAsync` and `ImportIntoBackend`. The whole path from a C host, in the order a host makes the calls:

1. **Create the engine** and ask `get_info`: `caps & ENGINE_CAP_IMPORT_GLTF` says the library has the importer. A library built with
   `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` (the option defaults to ON; it leaves `asset_import`, the vendored zlib, libpng and the JPEG decoder out of the
   library) has no such bit and answers every import call with `UNSUPPORTED`; nothing else changes.
2. **Import.** `import_gltf(engine, bytes, size, &options, &result)` reads a `.gltf` or a `.glb` from memory (the call borrows `bytes`) and adds
   what it made (meshes, skeletons, animation clips, textures, materials) to the engine's memory backend **atomically**: when one resource is refused,
   none is added. A glTF that names external files (a `.bin`, an image) gets them from the host's `read_file` callback (name, the limit, an
   `engine_import_file` the engine copies and then releases); without a callback such a file is `NOT_FOUND`. `data:` URIs and the `.glb` chunk need nothing.
3. **Read the result.** `result.resource_count`, and buffers the host releases: `ids` (`uint64_t` values), `types` (`uint32_t` tags), `paths` (one per line),
   `warnings` (one line each: features the importer dropped or approximated) and `manifest` (text). The resources load like any others with
   `resource_load` by id or by path (`/gltf/...`, or the options' `path_prefix`).
4. **Keep the ids.** The manifest names the id of every object by kind, index and name. Pass it as `options.manifest` on the next import of the file (the file
   changed, or the room was entered again) and the objects that match keep their ids, so what the host stored about them (a saved reference, an entity's
   component) stays valid; `id_seed` makes new ids reproducible. An import whose ids or paths are already in the backend fails with `ALREADY_EXISTS` and
   changes nothing: the host removes the old resources (`resource_remove`, with `ENGINE_RESOURCE_FORCE` when something loaded depends on them) and imports
   again with the manifest.
5. **Asynchronously.** `import_gltf_async` copies the bytes and runs the import on an engine worker; `import_poll` reports the steps done of the five, `import_wait`
   waits, `import_finish` adds the resources and gives the result (once; a second call is `NOT_FOUND`), `import_cancel` stops the job at its next step (the
   outcome is `CANCELLED`, or the result if the import had finished already), `import_destroy` ends the ticket and waits for its job. Each ticket counts as an
   object of the engine. The host's `read_file` runs on a worker thread then: it must be thread-safe and live until the ticket is destroyed.
6. **Hostile files.** The file may come from anywhere. Every count and size is checked against the limits before anything is allocated: the defaults are the
   `assets.import.*` settings, and each import may set its own (`max_file_bytes`, `max_json_bytes`, `max_buffer_bytes`, `max_output_bytes`, `max_pixels`,
   `max_nodes`, `max_accessors`, `max_images`; 0 takes the setting). A file that is not valid, or over a limit, comes back as a status
   (`INVALID_ARGUMENT`, `TOO_LARGE`, `LIMIT_EXCEEDED`, `UNSUPPORTED`, `NOT_FOUND`, ...) and a sentence in `last_error` that names the glTF object
   (`meshes[0].primitives[0].attributes: ...`); `result` is zeroed and nothing is added. Nothing aborts.

The library reaches the importer only through `engine/engine_c/import.cpp`; the gate `asset_boundary_check` lists that file and the CMake file that links it,
and fails when any other file of the runtime includes the importer.

**Tested** by `host_import.c` (in the static `engine_c` and, against the shared library through `dlopen`, in `engine_c_import_shared`; every flavor): the
fixture of `tests/asset` imported from memory, every mesh, skeleton, clip, texture and material loaded by id and by path, the second import refused
(`ALREADY_EXISTS`, nothing changed), remove and import again with the manifest (same ids), external files served by the host (and each released once), a
refusing callback, the asynchronous import (poll, wait, finish twice, 24 cancels, destroy while running), every bad argument, and 450 hostile files
(truncations, changed bytes, a GLB whose header lies, over every limit): each refused with a message or still a valid glTF, none a crash.
`host_import_off.c` is the library without the importer. ThreadSanitizer cannot follow libpng's `longjmp` (`KNOWN-ISSUES.md`), so its flavor of the hostile
run leaves the fixture's PNG uncorrupted.


### Keeping what an import made: a host's own disk cache (maintenance, 2026-11-18)

Importing a large `.glb` can take a second or more, so a host that shows the same file again (a world or an avatar it saw before) keeps what
the import made and adds it back next time instead of importing again. The supported way, and the only state the host has to keep:

1. **Import**, with `ENGINE_IMPORT_EMIT_LEVEL` when the file is a scene. Key the cache by the file's own hash (SHA-256 of the `.glb`; the host has it) **and by
   `import_format_version()`**, a `uint64_t` that changes whenever a re-import of the same file could give other blobs: the importer's output version
   (`asset::kGltfImporterOutputVersion`, raised by every change of what it writes), the blob container version and the version of every resource type. It needs
   no engine, so the cache directory can be named before `engine_create`. Compare it for equality only. `abi_major` / `abi_minor` do not serve: they track the API,
   not the importer's output. A library built without the importer has a fingerprint of its own (the formats only): the cache of one is not valid for the other.
2. **Store, per resource of the result**, in the host's index: the id (`ids`), the type (`types`), the path (one line of `paths`) and the blob that
   `resource_read(engine, id, max_size, &buffer)` returns (byte for byte what `resource_add` takes: container, type, id, content hash), plus the id of the level (the
   last resource, the one of type `ENGINE_RESOURCE_LEVEL`) and the `summary` text. `resource_mounted` gives the size and `content_hash` before and after. Only a
   resource of the memory backend can be read (what `resource_add` and the imports put there); one of a pak or a file is `NOT_FOUND`. No blob is larger than the
   setting `assets.memory_backend_bytes`, and the blobs of one import together at most `assets.import.max_output_bytes` (2 GiB by default); `max_size` makes
   `resource_read` refuse a bigger blob (`TOO_LARGE`) before copying a byte.
3. **Facts about the file** that no engine call reads from a loaded level (the bounds of a level, a material by name, the length of a clip) are in `result.summary`,
   UTF-8 JSON (`asset_import/gltf_summary.h`): the level's nodes with their names and world positions (the `Spawn` and `Eye` nodes a host may name),
   the bounding box of the meshes (the walkable area of a world, the height of an avatar), the material names (`Screen`) and the animation names with their
   durations (`Walk`, `Idle`). Store it beside the blobs; it is written once, by the import.
4. **Load again** with `resource_add_many(engine, infos, count, &failed_index)`: every blob is checked (as `resource_add` checks it, `ENGINE_RESOURCE_CHECK_HASH` with
   the stored `content_hash` included), every id and path is found free (also among the items) and the sum of the sizes is found to fit the budget **before the
   first is installed**, and the resources then appear together, in the given order: a failure leaves the backend as it was, and `failed_index` is the item that
   was refused (or `count` when the batch as a whole was: the budget). The bytes are copied (`BORROW` is `INVALID_ARGUMENT` here). Adding does not look at
   dependencies (a load does: `MISSING_DEPENDENCY`), so the order of `ids`, dependencies first, is enough but not required. Then `resource_load` the level by
   its id and `level_spawn` it. Two participants who use the same file share one set of resources: add it once (`resource_mounted` tells whether it is there).
5. **Without stalling the frame:** `resource_add_many_async` returns a ticket at once and the checking and copying run on an engine worker (`resource_add_wait`,
   `resource_add_finish` for the outcome and `failed_index`, `resource_add_destroy` to drop it). The engine reads the host's bytes in place until the ticket is
   finished or destroyed, so they must stay valid until then; a ticket counts as an object of the engine. Measure before using it: a synchronous add is a
   check of the blob hashes and one copy.
6. **A library without the importer** (`-DENGINE_LIBRARY_WITH_IMPORTER=OFF`) still has all of this (`ENGINE_CAP_RESOURCE_CACHE` is set in every build): a cache-only host
   adds blobs it stored from another build with the same `import_format_version()` rules.

`ENGINE_CAP_RESOURCE_CACHE` says the library has these functions; `engine_api.struct_size` says the host's header and the library agree on the table.
**Tested** in `host_import.c` (the round trip through two engines, atomicity of a corrupt, a repeated and a live id and of the budget, the limits of
`resource_read`, the asynchronous form, a host built against the first `engine_import_result`), `host_import_off.c` (the same functions in the library without
the importer), `host_cpp.cpp` (the C++ layer) and `tests/asset/memory_backend_test.cpp` (the backend's `AddMany`).

## Rendering into a window the host owns (Day 352)

An application with its own toolkit (a Qt, GTK or Electron shell, or a C program with a bare Xlib window) keeps its window and has the engine draw into it:
`surface_create`, `surface_resized`, `surface_rebind`, `surface_set_background`, `surface_state`, `surface_read_pixels`, `pixels_finish`, `pixels_destroy` and
`surface_destroy`, described in `engine_c.h` ("Day 352"). The engine **never creates, resizes, moves or destroys the host's window**; the host owns its size,
visibility and scale and reports them. In this order:

1. **Ask what the build can do.** `get_info`: `caps & ENGINE_CAP_SURFACE_X11`, `ENGINE_CAP_SURFACE_WAYLAND`, `ENGINE_CAP_SURFACE_WIN32` and `ENGINE_CAP_SURFACE_COCOA`
   say which window kinds `surface_create` accepts (Linux with the Vulkan backend; Windows with Direct3D 12 and Vulkan, Day 354; macOS with Metal, Day 355), `ENGINE_CAP_READBACK` that `surface_read_pixels` works. A build that cannot answers `UNSUPPORTED` with a sentence in
   `last_error`, never a crash.
2. **Create the surface** from the window's native handles in an `engine_surface_info`: for X11 the `Display*` (`display`) and the `Window` (`window_id`), for
   Wayland the host's own `wl_display*` (`display`) and `wl_surface*` (`surface`): the engine makes **no second connection**, it presents on the host's, so the
   host keeps dispatching its events as it did. The size is in pixels and the scale is the DPI factor (0 means 1). Each surface has a device of its own; the
   surface counts as an object of the engine until `surface_destroy`, and `destroy` of the engine answers `IN_USE` until it is gone. **Destroy the surface
   before the window.**
3. **Draw.** Every `frame` renders one frame into every surface: a clear to the surface's background color (`surface_set_background`), and, from Day 359, the world bound with `surface_set_world` (next section). The host calls `frame` from its own
   thread, as everywhere in the API.
4. **Follow the window.** The host tells the engine every change of size or scale with `surface_resized` (what a toolkit does on `ConfigureNotify` or when the
   Wayland surface is configured; **the size reported must be the size the window really has**: on X11 the swapchain takes the server's extent when it is rebuilt).
   A size of 0 in either dimension means hidden or minimized: the engine draws nothing and keeps the surface (`frames_skipped` of `surface_state` counts the
   frames); the next real size resumes. Unmapping or covering a window needs no call.
5. **Recover.** `surface_state` reports `visible` (the last frame was presented), `lost` and `recoveries`. A surface whose window went away from under the
   engine draws nothing and does not crash the engine: on X11 the server reports the destroyed window as having no drawable area, so the surface reads as
   hidden (`visible` 0, `frames_skipped` counting up) and a read-back pending on it stays pending; when the swapchain reports the surface itself lost, `lost` is
   1 and the pending read-backs fail with `UNSUPPORTED`. Either way the host calls `surface_rebind` with the handles of a new window and the engine rebuilds
   the swapchain on the same device through the reset path of Phase 8 (`RecreateSurface`), `recoveries` goes up and the next frame is drawn there. When the
   device itself was lost (a driver reset, the GPU removed), `surface_rebind` makes a new device on the surface's adapter instead (Day 369, below). If the
   device cannot present to the new window, or no new device can be made yet (`UNSUPPORTED`), the surface stays lost, and the host can try again or destroy
   it.
6. **Read pixels.** `surface_read_pixels` returns a ticket at once; the next frame that draws the surface fills it; `pixels_finish` takes the RGBA8 image (an
   `engine_pixels`: width, height, `stride` = width × 4, top row first, in the encoding of the swapchain image, so a clear to 0.5 reads 188 on an sRGB surface)
   as an `engine_buffer` the host releases with `engine_buffer_release`. Before a frame is drawn `pixels_finish` answers `NOT_READY`; after the surface is
   destroyed, `CANCELLED`; a second take is `NOT_FOUND`. `pixels_destroy` ends the ticket (256 at most; each counts as an object of the engine). It is the screenshot
   and test path, and is not meant for every frame.

The engine's own window code is not involved: `os::ForeignWindow` (`engine/platform/foreign_window.h`) is an `os::Window` over the host's handles with no events,
cursor or clipboard of its own (the host owns input; Day 353 forwards it and takes the requests back through callbacks), so the RHI's swapchain code is the same as for the engine's windows. The Vulkan backend creates its
surface from the handles on the host's connection.

**Tested** by `host_surface.c` (a C program with an Xlib window; plain and asan, the tsan flavor is a skipped stub because ThreadSanitizer cannot run the GPU
driver's threads): every bad argument, a frame of a known color read back and compared with the golden pixels, another color (sRGB encoding), a second take of a
ticket, the host's resize (the image follows), a hidden window and the return, an unmapped window, a window destroyed under the engine (no crash, no X error
raised by the engine) and `surface_rebind` on a new window, a ticket cancelled by the surface's destruction and `destroy` refused while the surface exists; and by
`surface_wayland_test.cpp` (the same on a Wayland surface, a window opened with the platform layer and handed to the C API: the golden frame, resize, hidden,
and rebind to a second window). Both need an unlocked session (`KNOWN-ISSUES.md`) and skip without a display.


## Input from the host, requests back, and the host-driven loop (Day 353)

A surface has no window of the engine's own, so the host feeds it input and answers what the engine asks for. Everything is on the surface: `surface_input`
takes one `engine_input_event` (size-prefixed, with `type`, `time_ns`, `modifiers`, and the fields of its type), and `engine_surface_info.callbacks` points to
an `engine_surface_callbacks` the surface copies at `surface_create` and `surface_rebind`.

**Events.** Types (`ENGINE_INPUT_*`): key (`engine_key`, `down`, `repeat`), pointer move, enter and leave, pointer button (`ENGINE_BUTTON_*`), scroll, text (UTF-8, at most 256 bytes), focus, and touch (`touch_id`; the first finger acts as the pointer).
Positions are in window pixels, as the host's toolkit reports them. The key values are the engine's own enumeration (layout-aware: the symbol the layout
produced), so a toolkit maps its key codes once. An event with a bad size, type, key, button, coordinate or malformed UTF-8 is refused with `INVALID_ARGUMENT`
and a sentence in `last_error`; nothing reaches the queue. The queue holds 1024 events; `surface_state.input_dropped` counts those lost when the engine's UI did
not drain it (the UI drains once per frame).

**Requests back.** `set_cursor` (`engine_cursor`: arrow, I-beam, hand, the resize shapes, not-allowed, hidden), `set_pointer_lock`, `clipboard_set`,
`clipboard_get` (text only: the answer is valid until the next callback), `text_input_rect` (the caret, for an input method) and `request_focus`. Any may be
NULL, and the request is then refused. They run on the thread that drives the UI of the surface (the host's frame thread), never from an engine worker, and
**must not call back into the engine**. The engine opens no display, input or clipboard connection of its own for the surface: `host_surface.c` counts the
`XOpenDisplay` calls made by the program and its statically linked engine, and there is exactly the host's one (the graphics driver's own connection inside
Vulkan is the driver's, not the engine's).

**Present mode.** `engine_surface_info.present_mode` asks for FIFO, mailbox or immediate; the swapchain takes it when the surface allows it and falls back to
FIFO otherwise, and `surface_state.present_mode` says what it got.

**The host's duties.**
- *Threads:* `surface_input` and `frame` are called from one thread, the host's frame thread; the callbacks come back on it. The engine blocks on nothing of the
  host's loop and runs no frame of its own: the host calls `frame` from its timer or display link, and sets `engine_frame_info.budget_us` if it wants a cap.
- *Ordering:* forward the events of a tick, then call `frame`; the UI sees them in that frame, and a callback made in the frame answers the events before it.
  Forward `focus` when the window gains or loses it, and a pointer move on entering a widget so the cursor request follows.
- *DPI:* positions and sizes are pixels; report the scale with `surface_resized` and `engine_surface_info.scale`; the engine scales its UI, the host does not
  scale the events.
- *Lifetime:* the callbacks' `user` pointer stays valid until `surface_destroy` returns.

**A worked example** (the shape of `host_surface.c`, C with Xlib):

```c
static int SetCursor(void* user, uint32_t shape) { ((Host*)user)->cursor = shape; return 1; }

engine_surface_callbacks cb = { sizeof cb, 0, &host, SetCursor, NULL, ClipSet, ClipGet, NULL, NULL };
engine_surface_info info = { sizeof info, ENGINE_SURFACE_X11, 0, 640, 360, 1.0f, ENGINE_PRESENT_MAILBOX };
info.display = host.display; info.window_id = host.window; info.callbacks = &cb;
api->surface_create(engine, &info, &surface);

for (;;) {                                       /* the host's own event loop */
    XEvent e;
    while (XPending(host.display)) {
        XNextEvent(host.display, &e);
        engine_input_event ev = { sizeof ev };
        if (e.type == MotionNotify) { ev.type = ENGINE_INPUT_POINTER_MOVE; ev.x = e.xmotion.x; ev.y = e.xmotion.y;
                                      api->surface_input(surface, &ev); }
        else if (e.type == ButtonPress) { ev.type = ENGINE_INPUT_POINTER_BUTTON; ev.button = ENGINE_BUTTON_LEFT; ev.down = 1;
                                          ev.x = e.xbutton.x; ev.y = e.xbutton.y; api->surface_input(surface, &ev); }
    }
    api->frame(engine, &frame_info, NULL);       /* the callbacks run in here */
}
```

**Tested** by `surface_input_test.cpp` (Catch, an X11 window: forwarded pointer and keys click a button whose counter is read back through the world API, a text
field receives text and a paste, the cursor callback fires when the pointer enters a text field, the clipboard callbacks round-trip, the UI frame compared with a
golden, overflow counted) and `host_surface.c` (argument validation of every event field, queue overflow, present-mode fallback, the callbacks' size check, and
the count of display connections). They need an unlocked session and skip without a display.


## The SDK: linking a pre-built engine (include directory and install)

A host that uses a pre-built engine needs the C API's headers and the library, and nothing of the engine's source tree. The public headers live in their own
directory, **`include/engine/`**: `engine_c.h`, `engine_loader.h` and `engine_abi_version.h`. They are plain C11, include each other and the C library only, and
are the only headers a host includes (`#include "engine/engine_c.h"`); the engine's own headers (`engine/engine_c/*.h` and the modules) are not part of the
interface. Inside the repository `ENGINE_PUBLIC_INCLUDE_DIR` is that directory, and the `engine_c` targets pass it on to what links them.

`cmake --install <build> --prefix <dir> --component engine_sdk` installs `include/engine/*.h` and, in a build with `-DENGINE_BUILD_SHARED=ON`, `lib/libengine.so.1`
with the package `lib/cmake/Engine` (`EngineConfig.cmake`, `EngineTargets.cmake`, a version file that accepts the same major version). A host's CMake file:

```cmake
find_package(Engine REQUIRED)               # with -DCMAKE_PREFIX_PATH=<dir>
add_executable(app main.c)
target_link_libraries(app PRIVATE engine::engine)
```

A host that loads the library with `dlopen` needs only the headers (`engine_loader.h`). **Tested** by `engine_install_consumer`: it installs to a scratch prefix,
checks that exactly the three headers were installed, and builds and runs `tests/engine_library/consumer` (a program outside the tree: `find_package(Engine)`,
public headers only) against that prefix. On macOS (Day 355) the package installs `lib/libengine.1.0.dylib` with the links `libengine.1.dylib` and `libengine.dylib`;
the install name is `@rpath/libengine.1.dylib`: the host's own rpath says where it lives. On Windows (Day 354) the install rule puts `engine.dll` in `bin/` and its
import library in `lib/`; the consumer test does not run there (`KNOWN-ISSUES.md`). A header that is only for the tests
(`engine_test_extension.h`) stays in `engine/engine_c/` and is not installed.

## Windows: engine.dll, loading it, and embedding into an HWND (Day 354)

**Building it.** `-DENGINE_BUILD_SHARED=ON` on the `windows-msvc` and `windows-clang-cl` presets builds `engine.dll` (and `engine_asan.dll` in the MSVC tree;
MSVC has no ThreadSanitizer), the same aggregate of the module libraries as on Linux, whole-archived through CMake's `WHOLE_ARCHIVE` feature (`/WHOLEARCHIVE`).
A DLL exports only what `__declspec(dllexport)` or its module-definition file names, so the export list of `abi/engine.exports` becomes a `.def` file
(`engine_shared.def`, generated beside the version script) and `ENGINE_API` is the `dllexport` on `engine_get_api`; the C header's declaration carries it too in
the library's own sources (`ENGINE_C_API`, set by `engine/engine_c/CMakeLists.txt`: MSVC wants both to agree). `abi_check` reads the DLL's export table with
`dumpbin /exports` (the toolchain files find it with MSVC) and holds it to the list, as it holds `nm -D` on Linux; it also compiles `engine_c.h` as C11 with
`cl /std:c11 /W4 /WX`. The linker writes an import library beside the DLL; hosts do not use it.

**Loading it.** `engine_loader.h` is the same header on every system: on Windows `engine_load` converts the UTF-8 path to UTF-16 and calls `LoadLibraryExW`
(with `LOAD_WITH_ALTERED_SEARCH_PATH` when the path has a folder in it, so the DLLs `engine.dll` needs, such as the AddressSanitizer runtime beside it, are found
there first), finds `engine_get_api` with `GetProcAddress`, and `engine_unload` calls `FreeLibrary`. The system's message for a refusal is in `loader.message`
with its error number. Delay-loading is not used.

**`DllMain`.** `engine.dll` has none of its own. Loading it runs the C runtime's start and the constructors of the engine's statics, none of which starts a
thread or writes a line (`engine_load_runs_nothing`: the process has the threads it had before the load; the job system starts its workers when it is first
used). The one piece of work on unload is the same as on Linux, the immortal frame pool giving its memory back: a static object's destructor, which the C runtime
runs when the DLL is detached (`FreeLibrary`, or process exit).

**One C runtime each.** The engine and the host may use different C runtimes, and on Windows they often do (a host built with the static `/MT`, the engine with
the DLL `/MD`): two heaps, two `stdio`, two `errno`. Nothing of one runtime crosses to the other: **no `FILE*`, no allocation, no `errno`** passes the boundary.
The host's bytes are copied in (or lent with a release callback the engine calls), the engine's memory comes back as an `engine_buffer` the host releases with
`engine_buffer_release`, which calls the engine's own release function. `engine_other_crt` is such a host: built `/MT` (it depends on `KERNEL32.dll` alone), it
runs frames, the memory test and the resource test of Day 350 through the loader.

**Unload and reload.** `engine_reload_cycles` runs the 50 load-run-destroy-unload cycles on Windows too (plain and asan): after each `FreeLibrary` the DLL is
gone from the module list, the thread count is back, and the working set does not grow. The crash handler gives the host's own unhandled-exception filter back
when it is uninstalled (`FinishCrashEventHookRemoval`, `engine/foundation/os/windows/debug/BACKEND.md`), so no filter points into the DLL after it is unloaded.

**Rendering into an HWND.** `ENGINE_SURFACE_WIN32` takes the window's `HINSTANCE` (the module of its window class; `GetModuleHandleW(NULL)` for a class of the
program) in `display` and the `HWND` in `surface`. `engine_surface_info.flags` chooses the graphics API: `ENGINE_SURFACE_FLAG_D3D12`, `ENGINE_SURFACE_FLAG_VULKAN`,
or neither for the platform's own (Direct3D 12 on Windows, Vulkan on Linux, Metal on macOS); both at once is `INVALID_ARGUMENT`, one the platform has not is `UNSUPPORTED`, and a
surface keeps its API across `surface_rebind`. Everything else is as on Linux (steps 1-6 above). **Report the window's real client size**: Vulkan draws the
client area the window has, Direct3D 12 the size the host gives (DXGI scales its buffers to the window), so the two differ when the host's numbers are wrong
(Windows gives a captioned window a minimum width: a 200-pixel client area came out 232 wide). A destroyed window reads as hidden on Direct3D 12 and as
presented on Vulkan; either way the frames go on and `surface_rebind` recovers on a new window.

**The C hosts on Windows.** The hosts of `tests/engine_library` reach the operating system through `host_os.h` (atomics, threads, sleeping, loading a library, a
directory, a crash in a child process, the host's own crash handler, process facts), with a POSIX side (`posix/host_os_posix.h`: what the hosts called directly
before, and the process probes of Day 355) and a Windows side (`windows/host_os_windows.h`: Win32, Toolhelp, psapi). `host_quiet_failures()` comes first in every host's `main`: on Windows it sends
the debug C runtime's asserts and `abort()` to standard error instead of a dialog box. The crash child is a second run of the program on Windows (there is no
`fork`); a fault inside a job ends it through the coroutine's `unhandled_exception` as an abort, with the dump written (`KNOWN-ISSUES.md`). `host_win32.c`
(`engine_c_surface_win32`, plain and asan) is the Windows counterpart of `host_surface.c`: its own window class, window procedure and message loop, run with
Direct3D 12 and with Vulkan, forwarding what its window receives through `surface_input` and answering the cursor and clipboard requests with Win32;
`surface_input_test.cpp` runs on a Win32 window too (the UI's click, text, cursor and clipboard requests through the host's callbacks).

## macOS: the dylib and embedding into an `NSView` (Day 355)

**The library.** `-DENGINE_BUILD_SHARED=ON` on `macos-clang` builds `libengine.1.dylib` (and `libengine_asan.1.dylib`, `libengine_tsan.1.dylib`): every module whole-archived as on
Linux, hidden symbols, and in place of the version script an exported-symbols list generated from `abi/engine.exports` (`engine_shared.exported_symbols`
in the build tree: `_engine_get_api`, with the C symbol prefix). Its install name is `@rpath/libengine.1.dylib`, so a host that links it finds it through
its own rpath (`-Wl,-rpath,@executable_path/../Frameworks` in an application bundle), and a host that loads it with `engine_load` passes the path. `abi_check`
reads the dylib's defined external symbols with `nm -gU` and strips the prefix (`NM_STYLE=macho`); the gate's own cases include a Mach-O listing and a
leaked C++ symbol. The arm64 configuration (`macos-clang-arm64`) is meant to compile and link the same library (nothing runs there); it has not been built yet.

**Hardened runtime and library validation.** A host signed with the hardened runtime and library validation (an application from the App Store, or any
notarized application that did not opt out) loads only libraries signed by Apple or by its own team: sign `libengine.1.dylib` with the host's identity
(`codesign --sign "<identity>" --options runtime --timestamp libengine.1.dylib`), or give the host the entitlement
`com.apple.security.cs.disable-library-validation`. An ad hoc signature (what the linker puts on a development build) is refused by such a host. A hardened
process also ignores `DYLD_LIBRARY_PATH` and the other `DYLD_*` variables, so the rpath, or the path given to `engine_load`, is the way to find the library.
The tests run unsigned, unhardened hosts.

**Unloading.** `dlclose` succeeds, but dyld keeps an image that holds Objective-C classes (the Cocoa window, the Metal backend) loaded for the life of the
process: `libengine.dylib` stays mapped after its last `dlclose`. The library's destructor (`EngineLibraryUnloaded`) therefore does not run, and the
engine's process-wide state (the frame pool, the log channels) stays and is reused by the next `engine_create`. What the tests check on macOS is what
still holds: `dlclose` returns 0, the threads the engine started are gone, the resident size stays flat over 50 cycles, and the engine holds no block after
`destroy`; what they tolerate is the mapping (`tests/engine_library/host_os.h`, `HOST_LIBRARY_UNLOADS`). A host that must get the memory of the
code back runs the engine in a helper process.

**Autorelease pools.** Every entry of the C API that touches AppKit or Metal drains a pool of its own: the Metal backend's entry points and the Cocoa surface
code (`engine/engine_c/macos/cocoa_surface.mm`) wrap their bodies in `@autoreleasepool`, so a host may call from a thread, or a part of its main loop,
that has none (`MetalSwapchain`'s creation, `Apply` and `Acquire` did not until Day 355; the Cocoa host's test found them). The Cocoa host test runs with
`OBJC_DEBUG_MISSING_POOLS=YES` and fails on a missing pool on the host's main thread, where it calls the engine outside every pool of its own
(`macos/run_cocoa_host.cmake`); Apple's own queues (Metal's completion queue, Core Animation) report missing pools too, with no engine frame in their
stacks, and are counted, not failed.

**A Cocoa surface.** `ENGINE_SURFACE_COCOA` (caps `ENGINE_CAP_SURFACE_COCOA` and `ENGINE_CAP_READBACK`) renders with Metal into a view the host owns:

- `display` is the `NSView*` (required); `surface` is a `CAMetalLayer*` the host made for it, or NULL. With NULL, a view whose own layer is a `CAMetalLayer` (an
  application that draws with Metal set it up so) is drawn into; any other view gets a `CAMetalLayer` of the engine's own as a sublayer that fills the view's
  bounds by autoresizing (the view is made layer-backed if it was not, and given back as it was), removed again by `surface_destroy` and `surface_rebind`.
  Nothing else of the view changes.
- **The main thread.** `surface_create`, `surface_rebind`, `surface_resized` and `surface_destroy` of a Cocoa surface must be called on the main thread
  (`INVALID_ARGUMENT` otherwise), and `frame` draws a Cocoa surface only on the main thread: a frame on another thread counts it as skipped and fails its
  pending read-backs with `INVALID_ARGUMENT`. The swapchain reads the screen through AppKit, and the engine's sublayer is part of the view's layer tree.
- **Sizes are pixels**: the view's bounds times `window.backingScaleFactor`, and `scale` is that factor. Report a change with `surface_resized` from the view's
  `setFrameSize:` and `viewDidChangeBackingProperties` (moving to a display of another density changes the scale and the pixels, not the points); the engine's
  sublayer follows the view's bounds by itself and takes the new `contentsScale`, a host's own layer is the host's to size.
- **The clock.** The host drives frames. With a display link, its callback runs on a CoreVideo thread: dispatch to the main queue and call `frame` there.

```objc
static CVReturn Tick(CVDisplayLinkRef link, const CVTimeStamp* now, const CVTimeStamp* out, CVOptionFlags in,
                     CVOptionFlags* flags, void* user)
{
    Host* host = user;
    dispatch_async(dispatch_get_main_queue(), ^{ host->api->frame(host->engine, &host->frame_info, NULL); });
    return kCVReturnSuccess;
}

engine_surface_info info = { sizeof info, ENGINE_SURFACE_COCOA };
NSView* view = window.contentView;
info.width = view.bounds.size.width * window.backingScaleFactor;
info.height = view.bounds.size.height * window.backingScaleFactor;
info.scale = window.backingScaleFactor;
info.display = (__bridge void*)view;          /* surface NULL: the engine adds its CAMetalLayer */
info.callbacks = &callbacks;                  /* NSCursor, NSPasteboard */
api->surface_create(engine, &info, &surface); /* on the main thread */
CVDisplayLinkCreateWithActiveCGDisplays(&link);
CVDisplayLinkSetOutputCallback(link, Tick, &host);
CVDisplayLinkStart(link);
```

- **Input** is forwarded as everywhere (`surface_input`): positions in pixels from the top left, so a host flips AppKit's `y` (`bounds.height - y`) and
  multiplies by the scale. The callbacks map to `NSCursor` and `NSPasteboard`.
- **Recovery.** A closed window does not make the surface lost: the engine holds its layer, which keeps taking drawables, so frames go on into a layer nobody
  sees. The host calls `surface_rebind` with its new view (a view that is not an `NSView`, or a layer that is not a `CAMetalLayer`, is refused and changes
  nothing). Metal reports no lost swapchain, and a lost device cannot be forced on Metal, so the `lost` branch is not exercised on the Mac
  (`KNOWN-ISSUES.md`).

**iOS** (Phase 29) links the engine statically: no application may load a library of its own there. `engine_c.h` compiles for `arm64-apple-ios`
(`engine_c_header_ios`, as C11 and C++20, freestanding: the Mac has no iOS SDK); `engine_loader.h` is not for iOS.

**Tested** by `tests/engine_library/macos/host_cocoa.m` (Objective-C with ARC; `engine_c_cocoa*`, statically linked in plain, asan and tsan, and
`engine_c_cocoa_shared*` against the dylib through `engine_loader.h`, under Metal's validation): the caps, the bad arguments (no view, not a view, not a
Metal layer, off the main thread, another platform's kind), a golden frame and an sRGB grey read back, the engine's sublayer added and removed, the host's
resize and a change of scale, a hidden window, 30 frames from a display link on the main thread, `NSEvent`s converted and forwarded, the host's cursor and
pasteboard callbacks (called by the host there; the engine's UI calling them is `surface_input_test.cpp`'s part), a frame and a `surface_destroy` off the main
thread (the frame skips the surface and fails its read-back; the destroy is refused), the view's `wantsLayer` given back, a rebind to a view with its own `CAMetalLayer` and to an explicit layer, a refused rebind that changes nothing, the window closed under
the engine and the rebind to a new one, a read-back cancelled by `surface_destroy`, and `destroy` refused while the surface exists. `surface_input_test.cpp`
runs its UI rig on a Cocoa window there too. The Linux hosts run on the Mac through the POSIX side of `host_os.h`, with the process probes (`process_probe.h`: Mach and dyld in
place of `/proc`) and the unload limit above.

## A world on a surface (Phase 24 Day 359)

`surface_set_world(surface, world)` binds a world to a surface (`ENGINE_CAP_WORLD_ON_SURFACE` in `caps`; pass a null world to unbind). Every `frame` then draws, on that surface, the entities of the
world that have an `engine_mesh_reference` and a transform: the mesh and the material are resources the host added or loaded (Day 350), looked up by id; a material id of 0 is the surface's default grey material.
The picture is lit by a default sun and seen by a default camera until Day 360 adds `surface_set_camera`.

- **Per surface, not per engine.** Each surface owns a `renderer::SceneFrame` (the frame object the renderer demo used, moved into `engine/renderer`), a `RenderScene` and a resource manager over the engine's
  resource host, so two surfaces draw independent pictures and a surface lost with its window loses only its own GPU content, which is rebuilt from the resources on `surface_rebind`.
- **Order.** `frame` runs the world's systems first, then renders every surface: what a system changed this frame is what the picture shows.
- **What it reports.** `engine_surface_state` gains `world_status` (`ENGINE_OK`, or `STALE_HANDLE` once the bound world was destroyed: the surface draws the background and keeps going),
  `objects_drawn` (meshes the last frame drew) and `entities_unresolved` (entities whose mesh or material id did not resolve: they are skipped, never drawn wrongly, and counted).
- **Mesh layout.** The G-buffer templates read the full vertex layout; a mesh minted with the packed default (`quality packed`) is refused by the template with a log line, so
  `asset_mint` sources for meshes a surface draws say `quality full`.
- **Destroy order.** Destroy the surface before the world; a world destroyed first leaves the surface drawing its background with `world_status` `STALE_HANDLE`.

**Tested** by `host_scene.c` (a C program with a window of its own: Xlib on Linux, plain and asan; a Cocoa `NSView` drawn with Metal on macOS, every flavor, Metal's validation on, Day 370): a minted mesh and material added from memory, an entity bound to them, the counts of `surface_state`, an unresolved
material, the default material, the world destroyed under the surface and the image compared with the golden `golden/scene_rock_160x120.rgba` (no channel off by more than 4 and at most 0.5% of the values off by more than 1).

## Cameras (Phase 24 Day 360)

`ENGINE_COMPONENT_CAMERA` (`engine_camera`: `fov_y`, `near_plane`, `far_plane`, `flags`, `aspect`) makes an entity a perspective camera. It looks along the entity's world matrix's -Z with +Y up (scale ignored), so the host's
whole camera control is setting the entity's transform each frame. `surface_set_camera(surface, entity)` names the camera of a surface (0: the default camera); two surfaces may name two cameras of one world.
The aspect follows the surface unless `ENGINE_CAMERA_FIXED_ASPECT` is set. A camera entity that is destroyed or loses its component or transform makes the surface draw through the default camera and report `camera_status`
`NOT_FOUND` in `surface_state`; the name is kept, so a camera that returns is used again.

`surface_pixel_ray` and `surface_project` turn a surface pixel into a world ray and a world point into a pixel with the camera **of the last frame that drew the surface** (they answer `NOT_READY` before one has). The ray starts at the camera
position; the projection reports the pixel and the distance along the view direction (negative behind the camera). Orthographic cameras are not built (`KNOWN-ISSUES.md`).

## Lights and the environment (Phase 24 Day 361)

`ENGINE_COMPONENT_LIGHT` (`engine_light`: kind, colour, intensity, radius, cones) makes an entity a light: a **directional** light shines along the entity's world -Z axis (rotate the entity to aim it; the way a camera looks), a
**point** light sits at the entity's position and reaches nothing beyond `radius`, and a **spot** light (`ENGINE_LIGHT_SPOT`, maintenance 2026-11-20) is a point light that shines along the entity's -Z only: full strength inside
`inner_cone`, nothing beyond `outer_cone` (half-angles in radians, `0 <= inner_cone < outer_cone <= pi/2`). The two cone fields were added at the end of the struct: a host built before them (a `struct_size` that ends before
`inner_cone`) still sets and reads directional and point lights, and gets `STRUCT_SIZE` for a spot light. A surface lights its world with up to 64 lights (the ambient term takes one slot); the rest are counted in `lights_refused` of `surface_state`.
`world_set_environment(world, &engine_environment)` sets the world's ambient colour and intensity, the sky colour behind the scene (before the tone map) and the exposure; every surface that shows the world uses it.

A world on which the host never calls `world_set_environment` and that has no light entity is drawn with the default sun of Day 359 (and ambient 0.2); once either exists the default sun is gone, so a host that wants a dark scene
sets an environment with ambient 0. **Not built:** shadows (the renderer has none) and a sky gradient (a flat sky colour only); both are in `KNOWN-ISSUES.md`.

## Hierarchy, names, visibility and material overrides (Phase 24 Day 362)

- **Hierarchy.** `entity_set_parent(world, child, parent)` makes a child's transform relative to its parent's (the frame's transform system composes the world matrices); a parent of 0 makes it a root again; a cycle or the entity itself is
  refused with `INVALID_ARGUMENT`. Destroying a parent does not destroy its children: they become roots at the next frame. `entity_get_parent` reads the link.
- **Names.** `entity_find` (the first entity with the name) and `entity_find_all` (into a caller's array, with the total). Names are unique only by the host's discipline; "first" is the order of the world's storage.
- **Visibility.** `ENGINE_COMPONENT_VISIBILITY` (`engine_visibility.hidden`) hides an entity **and every entity below it**; clearing it (or removing the component) draws them again at the next frame. Nothing else about the entity changes.
- **Material overrides.** `entity_set_slot_override(world, entity, "base_color", &engine_slot_override)` gives one entity its own value of one slot of its material (a colour, a float or a texture resource): the entity draws with a private
  copy of the material (`MaterialSystem::CloneMaterial`: same template, permutation, values and textures) and every other entity that shares the material is untouched. Slots are those of the G-buffer material: `base_color`,
  `roughness`, `metallic`, `emissive_color` (a colour, the strength in alpha), `alpha_cutoff`, `occlusion`, `occlusion_strength`, the textures `albedo_tex`, `normal_tex`, `metallic_roughness_tex`, `occlusion_tex`, `emissive_tex`, and the albedo's coordinate transform `albedo_uv` and `albedo_uv_outside` (below); an unknown name or a value of the wrong kind is refused at the call with the slot named in `last_error`.
  **Fitting a picture to a quad** (maintenance, 2026-11-18): `albedo_uv` (`ENGINE_SLOT_VEC4`: scale u, scale v, offset u, offset v; the albedo is sampled at `uv * scale + offset`, default `(1, 1, 0, 0)`) and `albedo_uv_outside` (`ENGINE_SLOT_COLOR`, linear RGBA) let a host show a video frame of any aspect on a 16:9 screen without copying pixels: for a frame of aspect `a` on a screen of aspect `s`, `a > s` scales v by `a / s` and offsets v by `(1 - a / s) / 2`, `a < s` scales u by `s / a` and offsets u by `(1 - s / a) / 2`. With the outside colour's alpha above 0 the part of the quad outside the picture shows its rgb (the shader masks it, the same on every graphics API) and the coordinates inside are kept half a texel from the edge, so filtering never reads across it; with alpha 0 the texture's address mode applies as before. Only the albedo uses the transform. Set it when the frame size changes, not every frame; it costs one material copy, like any override.
  An entity holds up to 8 overrides; `entity_clear_slot_override` removes one or all. The copy is a material of the surface's material system, so it counts against its limit, and it is made again when the base material or the overrides change.

## Skinned meshes and animation (Phase 24 Day 363)

- **Skeleton and player.** `ENGINE_COMPONENT_SKELETON_INSTANCE` (`engine_skeleton_instance.skeleton`, a resource id) makes an entity a skinned model: setting it loads the skeleton (`NOT_FOUND` when it is not mounted) and keeps it loaded. A mesh minted with `quality full` and a skin
  section, referenced by the same entity, is drawn skinned (with the surface's default skinned material when the reference names no material); an entity with no clip is drawn in the bind pose, and a skinned mesh on an entity with no skeleton instance is not drawn.
  `ENGINE_COMPONENT_ANIMATION_PLAYER` (`engine_animation_state`) is read only: it shows the clip, the time, the speed and whether a fade or the end is reached.
- **Morph weights** (maintenance, 2026-11-20). `ENGINE_COMPONENT_MORPH_WEIGHTS` (`engine_morph_weights`: `count` and up to `ENGINE_MORPH_WEIGHTS_MAX` (64) weights) sets how far an entity's morphed mesh moves toward each of its
  targets; the surface draws it each frame with the entity's current weights, and an entity without the component is drawn in its base shape. A weight is any finite number (0 to 1 is usual).
- **Playing.** `animation_play(world, entity, clip, &engine_play_params)` (the params may be NULL: play once at speed 1) starts a clip (`ENGINE_PLAY_LOOP`, `speed`, `start_time`, `fade_seconds`); a clip made for another skeleton is `INVALID_ARGUMENT`.
  A clip of morph weights only (a glTF animation whose channels are all `weights`) has no skeleton and plays on any entity, skinned or not; a clip with weight tracks gives the entity the morph weights component when it has none
  and writes it every frame. A clip that moves joints, on an entity without a skeleton instance, is `INVALID_ARGUMENT`. With `fade_seconds` above 0 the old clip keeps playing and fades out while the new one fades in.
  `animation_set_speed` (0 pauses), `animation_stop` and `animation_time` read and steer what plays. The frame advances every player before the transforms are propagated, so a frame's pose and its world matrices agree.
- **Joints.** `skeleton_find_joint(world, entity, "arm", &index)` and `skeleton_joint_transform(world, entity, index, matrix16)` (column-major, in the skeleton entity's space, as the last frame left it; the bind pose before any clip). `entity_attach_to_joint(world, entity,
  skeleton_entity, joint)` makes an entity follow a joint (its own transform becomes relative to the joint); the entities below the attached one lag by a frame. A `skeleton_entity` of 0 detaches.

## A glTF scene as a level (Phase 24 Day 364)

- **Making the level.** Import with `ENGINE_IMPORT_EMIT_LEVEL` (the tool: `asset_mint import-gltf --emit-level`) and the default scene's node tree becomes one more resource of the result, of type `ENGINE_RESOURCE_LEVEL`, the last of `ids`: one entity per node reachable from the
  scene's roots, with its name (made valid, at most 48 bytes, made distinct with `_<index>`), its translation, rotation and scale, its parent, the mesh and the material of the mesh's first primitive and, for a skinned node, the skeleton instance. Nodes that
  share a glTF mesh give entities that share one mesh resource. A node with a shear in its matrix, a hierarchy deeper than 63, a scene root that is also a child, or more nodes than `max_nodes` is refused with a message that names the node. Without the flag no level is made.
- **Spawning.** `level_spawn(world, level, &engine_spawn_params, &root, keys, capacity, &total)` makes a root entity at the transform of the params (an uniform scale, a rotation, a position; `name_prefix` puts `prefix/` before every name) and the level's entities below it. The
  level is read and every resource it names is checked before the world is touched, and a failure part way undoes what was made, so the spawn is whole or nothing. Spawning twice makes two independent copies. Find a node by name with `entity_find`
  (`"prefix/name"` for a prefixed spawn) and its skeleton instance, mesh reference and transform are ordinary components. A node's morph weights (`node.weights`, or its mesh's) arrive as `ENGINE_COMPONENT_MORPH_WEIGHTS`,
  and a node's `KHR_lights_punctual` light as `ENGINE_COMPONENT_LIGHT` (directional, point or spot, with the importer's light settings applied; maintenance, 2026-11-20). A level's ambient light (none comes from glTF) has no C kind
  and is left out.
- **Limits.** The spawn runs on the calling thread under the world lock, in time proportional to the entities; a very large level is better spawned between frames. A mesh with several primitives gets the material of its first. Levels that include other levels are not spawned (`UNSUPPORTED`).

## Host textures (Phase 24 Day 365)

- **What one is.** A host texture is a picture the host keeps painting (a video frame, a canvas, a UI layer): `texture_create(engine, &engine_texture_info, &texture)` makes one of a width and a height (RGBA8, `ENGINE_TEXTURE_SRGB` marks it as colour), `texture_update(texture, pixels, stride, info)` gives it new pixels from any thread, `texture_resize` changes its size, `texture_destroy` ends it. Create and destroy count as engine objects: the engine is not destroyed while one lives.
- **Latest wins.** An update copies the rows (the stride may be larger than a row; the padding is not read) under a short lock into one kept buffer, so the host's memory is free as soon as the call returns. An update that replaces one no device uploaded yet is counted in `engine_frame_stats.updates_replaced`; a producer faster than the frames costs only the copies.
- **Resize** drops the pixels until the next update (a surface keeps the material's own value meanwhile); an update that carries another `width` and `height` resizes and updates in one step. `texture_read_pixels` returns a copy (`NOT_READY` before the first update). Sizes above `assets.host_texture.max_size` (8192) are `TOO_LARGE`; strides above 16 MiB too.
- **Settings.** `assets.host_texture.max_size`, `assets.host_texture.upload_budget` (bytes a surface uploads per frame, default 8 MB; used from Day 366).
- **Not yet.** Nothing draws a host texture before Day 366 (a material slot that names one); `textures_uploaded` and `bytes_uploaded` stay 0 until then.

## Host textures on a surface (Phase 24 Day 366)

- **Binding.** `entity_set_slot_override(world, entity, "albedo_tex", &o)` with `o.kind = ENGINE_SLOT_HOST_TEXTURE` and `o.host_texture` set makes that entity draw the texture's newest pixels in place of the slot (`"normal_tex"` too). Entities that share the material keep theirs; the handle must name a live texture (`STALE_HANDLE` otherwise).
- **Upload.** Each surface keeps, for each host texture an entity of its world names, one device texture and a ring of three mapped staging buffers; every frame it copies the newest pixels (rows padded to the device's pitch) and uploads them in one submission before the frame is drawn, at most `assets.host_texture.upload_budget` bytes per frame (the first texture of a frame always goes). A texture drawn by two surfaces is uploaded to each device. `surface_state.textures_uploaded` and `texture_bytes_uploaded` count the uploads of the surface; `frame_stats.textures_uploaded` and `bytes_uploaded` those since the previous frame call.
- **Reverting.** A texture that was destroyed, has no pixels (never updated, or just resized) or did not fit the budget on its first frame leaves the slot at the material's own value for that frame; each such slot of each frame is counted in `frame_stats.slots_reverted`.
- **Resize** makes a new device texture at the next update, after the device is idle; the material of the entity is rebuilt to point at it.

## Labels: text in the world (Phase 24 Day 367)

- **The font.** The engine ships none: give it a font file's bytes once with `label_font_set(engine, data, size)` (TrueType or OpenType, copied, up to 32 MiB). A surface opens it at the first frame that has a label and a font; until then its labels are counted in `surface_state.labels_refused`.
- **A label** is `ENGINE_COMPONENT_LABEL` (`engine_label`) on an entity with a transform: up to 256 bytes of UTF-8 text (line breaks, combining marks and malformed bytes are laid out by the text layer; a glyph the font lacks shows its `.notdef`), an RGBA colour, a size, an offset in world axes and an anchor (centre, start or end of the text on the point). `size` is pixels, or with `ENGINE_LABEL_SIZE_METRES` the height of the em square in metres, from which the surface computes the pixel size by the distance and the camera's field of view and rounds it to one of 8, 10, 12, 14, 16, 20, 24, 32, 48, 64, 96, 128, 192, 256 (the glyph atlas holds a few rasterizations); every size is clamped to 6..256 pixels.
- **Where it is drawn.** At the entity's world position plus the offset projected through the frame's camera, over the picture after the tonemap, with no depth test (geometry never hides a label), facing the viewer by construction (constant orientation on screen). A hidden entity's label is not drawn; an entity attached to a joint carries its label with it. A label behind the camera or far off the screen is skipped. A surface draws up to 1024 labels per frame (the text renderer's budget is 8192 glyphs); the rest go to `labels_refused`.
- **Not here.** A background plate, depth-tested labels and billboard quads (later); text above 256 pixels (Phase 28's outline meshes).

## Surviving rebinds, losses and several surfaces (Phase 24 Day 368)

- **What a surface keeps and what it remakes.** A surface's scene (frame object, resident meshes and materials, device textures of host textures, the label font and the text atlas) belongs to the surface's device and is remade by `surface_rebind` from what the engine keeps: the resource host's content, the host textures' newest pixels (kept as the CPU source), the label font's bytes, the world itself. The host recreates nothing: the same world, camera, textures and labels draw on the new window, and the picture is the one before.
- **Several surfaces.** One world may be bound to several surfaces, each with its own camera (`surface_set_camera`); each uploads the host textures its entities name to its own device (`surface_state.textures_uploaded` per surface), so destroying one surface leaves the others drawing. A world destroyed under a surface leaves it at the background colour with `world_status` `STALE_HANDLE`. A host texture may be destroyed before or after the entities that name it (the slots revert), and the engine refuses to be destroyed while any texture, world or surface lives (`IN_USE`).
- **A lost device** (Day 369). When the device of a surface is lost, the next frame draws nothing on it (`lost` 1, a pending read-back fails with
  `UNSUPPORTED`), and `surface_rebind` makes a new device and the scene on it, with the same picture as before. The new device is on the adapter the surface
  started on, never another one. On Direct3D 12 an adapter has one device for the whole process (`D3D12CreateDevice` hands back the one that exists), so
  every surface on the adapter loses its device at once, and a new device exists only once none of them holds the removed one: the first `surface_rebind`
  answers `UNSUPPORTED` while another lost surface still holds it (had the adapter been picked anew, the engine would have fallen back to WARP, the software
  adapter). The host rebinds each lost surface, then again those that were refused. A world bound to the surface stays bound through the refused rebinds
  (`world_status` is `UNSUPPORTED` meanwhile) and draws again after the rebind that works; `surface_set_world` refuses a lost surface. The test hook is the
  test extension's `surface_lose_device` (`engine_test_extension.h`); `host_scene.c` loses one surface's device, then one of two surfaces', on both backends.
- **Not covered by a test yet.** A fault-injection sweep through the frame path and the 5000-frame flat-count run are closing-pass items (`KNOWN-ISSUES.md`).

## Windows: scenes on an `HWND` (Phase 24 Day 369)

The scene features of Days 359-368 run on Windows with Direct3D 12 and with Vulkan. The scene hosts' window (`tests/engine_library/host_window.h`, below) is
`windows/host_window_win32.c` there: a window class of the host's, borderless windows of the exact client size (a captioned window has a smallest width), a
message loop turned by the polls, and the walk-around's key posted as a `WM_KEYDOWN` message to the window. `HOST_SURFACE_API` (`d3d12` or `vulkan`, read by
`host_surface_api_flags`) chooses the graphics API, so each scene test runs once per backend (`engine_c_scene_d3d12`, `engine_c_scene_vulkan`,
`engine_c_walk_around_d3d12`, `engine_c_walk_around_vulkan`, plain and asan).

- **Golden images.** On the PC (AMD Radeon 860M), Direct3D 12 and Vulkan draw the golden scene byte for byte the same, and both differ from the golden image
  made on Linux (NVIDIA, Vulkan) by at most 2 in a channel, 40 of 76 800 values by more than 1: inside the rule of Day 358.
- **One device per adapter.** On Direct3D 12 the surfaces of one adapter share one `ID3D12Device`: each surface's device is its own `rhi::Device`, but
  `D3D12CreateDevice` returns the one that exists. The RHI counts the users of a D3D12 device and only the last one reports the objects still alive at its
  destruction (before Day 369, destroying a second surface reported the first one's objects as leaks). Losing it loses every surface on the adapter (above).
- **The walk-around sample** builds and runs on Windows (`walk_around.exe`; `HOST_SURFACE_API=vulkan` for Vulkan).
- **Off-screen surfaces and multi-material meshes (Day 381)** run on both Windows backends: `engine_c_offscreen` and `engine_c_acceptance_scene` take the
  platform's own Direct3D 12, `engine_c_offscreen_vulkan` and `engine_c_acceptance_scene_vulkan` Vulkan (`HOST_SURFACE_API`, read by
  `tests/engine_library/host_surface_api.h`, which the window and the off-screen hosts share), and `engine_c_walk_around_offscreen_d3d12` and `_vulkan`
  show each picture in the window (`host_window_show` is a GDI blit there: the RGBA rows converted to a top-down 32-bit DIB and drawn with
  `SetDIBitsToDevice`). The acceptance picture (1280x720) differs between the two backends in 2 of 2 764 800 values, by 1. A lost device on
  Direct3D 12 loses both surfaces of the adapter, so `host_offscreen.c` rebinds the second when the first's rebind answers `ENGINE_ERROR_UNSUPPORTED`,
  then the first again (the rule of Day 369, above).

## The walk-around sample (Phase 24 Day 371)

`tests/engine_library/walk_around.c` is the phase's driving case as a plain C host: three skinned robots (one walks, two stand), three screens that show a pattern each, painted by a thread of their own through `texture_update`, name tags over the robots, a directional light with an ambient term, and a first-person camera (W A S D, the mouse looks, Escape quits). It builds the scene through the C API alone; `--frames N` makes it a test that draws N frames, stops the painters, rebinds the surface to a new window and checks the picture is the same and that no entity was unresolved. In test mode it also posts a W key press to its own window through the window system (held for ten frames) and checks that the camera moved forward: the host's event loop drives the scene. The walk-through is the tutorial [A 3D Scene in Your Window](../tutorials/a-3d-scene-in-your-window.md).

**Its window, on each platform (Day 370).** The two scene hosts reach their window through `tests/engine_library/host_window.h` (open the window system, open and close a window of a size in pixels, describe it as an `engine_surface_info`, poll and post the few events the walk-around reads): `linux/host_window_x11.c` is Xlib, `windows/host_window_win32.c` Win32 (Day 369, above), `macos/host_window_cocoa.m` an `NSWindow` whose plain content `NSView` gets the engine's `CAMetalLayer` (Day 355). On a Retina display the window is half as many points as the pixels asked, so the golden image of `host_scene.c` is the same 160x120 picture on every platform; on the Mac it is within the golden rule with Metal (seen: a largest difference of 2, 96 of 76,800 values off by more than 1). Host textures reach Metal through the RHI as everywhere (`engine_c/live_textures.*`: a ring of three persistently mapped staging buffers per texture and one buffer-to-texture copy per update): nothing Metal-specific was needed (the walk-around's `textures_uploaded` counts every screen on the Mac). Every call is on the main thread there, as a Cocoa surface requires (the screens' painter threads call only `texture_update`).

## Meshes with several materials (Phase 24 Days 374-377)

A mesh may have **slots**: index ranges of one mesh, each its own draw (the primitives of an imported glTF mesh, in the
file's order). The renderer draws one object per slot (`RenderObjectDesc::first_index` and `index_count`), instancing only
draws of the same mesh, material, pipeline and range; a skinned entity's slots share one joint palette. In a scene,
`MeshRenderer::slot_materials` gives each slot a material (`kInheritMaterial` keeps the entity's own); a level stores them
in the additive component `asset::MeshSlotMaterials` (no level-version change; the importer writes it only when the
primitives' materials differ). A host sets them through the table:

- `entity_set_mesh_slot_material(world, key, slot, material)` and `entity_set_mesh_slot_material_by_name(world, key, name,
  material)` (the importer names a slot after its material, else `primitive<N>`); `entity_clear_mesh_slot_materials`
  draws the mesh whole again.
- A slot whose material is not mounted is not drawn and counts in `entities_unresolved`; `engine_slot_override`s (material
  parameters) apply to every slot. At most 16 slots; a mesh with more draws whole.
- Known limit: a glTF primitive without a material inherits the entity's base material (the first primitive's).

## Off-screen surfaces (Phase 24 Days 378-380)

`ENGINE_SURFACE_OFFSCREEN` (capability `ENGINE_CAP_SURFACE_OFFSCREEN`) makes a surface with no window and no window
system: only `kind`, `flags`, `width`, `height` and `scale` are read. The engine draws into an RGBA8 target of its own
(sRGB-encoded as a window shows it, straight alpha) on the platform's graphics API, and `surface_resized` changes its size
(0 draws nothing). A world, a camera, a background, `surface_input`, `surface_pixel_ray` and `surface_read_pixels` work
as on a window surface. Several off-screen surfaces, and a window surface beside them, share the engine's device.

- **The picture each frame:** `surface_acquire_frame` returns the newest finished frame as a copy the engine keeps for the
  host (tightly packed RGBA8, `stride = width * 4`): it never waits for the GPU, returns `NOT_READY` when nothing new
  finished, and a host slower than the frames skips pictures (`frames_dropped`, `frames_skipped_slow_host` in
  `engine_surface_state`). At most two frames are held; `surface_release_frame` gives one back. A held frame stays valid
  until released or the surface is destroyed, across a resize and a device loss; read it from one thread at a time.
  Under it a ring of three GPU read-back buffers with a fence each; the copy of a frame is skipped when all three are in
  flight.
- **Loss and reset:** after a lost device the surface reports `lost`; `surface_rebind` (with an off-screen info) makes a new
  device, target and ring and binds the same world again.
- `engine.h`: `Surface::AcquireFrame` and `Surface::ReleaseFrame`.

### Overrides of one mesh slot, and the acceptance scene (Day 383)

`entity_set_mesh_slot_override(world, key, mesh_slot, slot, value)` is `entity_set_slot_override` for one mesh slot: a
robot's `Screen` slot shows a host texture while its body keeps the material's own. It makes the entity draw one part per
mesh slot, like slot materials; `entity_clear_slot_override` removes a slot's overrides of every mesh slot.
`tests/engine_library/host_acceptance_scene.c` is the acceptance scene: it imports the meadow and the robot
(`tests/engine_library/fixtures/`, see its README) at run time, spawns the meadow and three robots (one walks), gives each
robot's screen its own host texture, draws 1280x720 into an off-screen surface and takes the pictures, through a resize,
a lost device and a slow host. The glTF importer writes meshes in the full vertex layout, the one the G-buffer shaders
read (a packed mesh's attributes do not feed them).

## Scenes: conventions and the host's duties (Phase 24 Day 372)

| What | Convention |
|---|---|
| Axes | right-handed, +Y up; a camera and a directional light look along their entity's -Z |
| Units | metres; angles in radians; `engine_transform.rotation` is a unit quaternion (x, y, z, w) |
| Matrices | column-major, 16 floats (`engine_world_transform.matrix`) |
| Colours | linear RGB for lights and material values, RGBA 0..1 as sRGB for labels; host texture pixels are RGBA8 (sRGB when `ENGINE_TEXTURE_SRGB`) |
| Pixels | origin at the top left of the surface, x right, y down; `surface_project` and `surface_pixel_ray` use the last frame's camera |
| Sizes | a surface draws up to 4096 objects, 64 lights and 1024 labels; the rest is counted in `surface_state` |

The host's duties: call `frame` from one thread (each call runs the animation system, the transform and attachment systems of every world, then draws every surface with a bound world, in that order); give `resource_add` content before an entity names it (an entity naming a missing resource draws nothing and is counted in `entities_unresolved`); let any thread call `texture_update`, but not `texture_destroy` while a frame is running on another thread without its own care (a destroyed texture only reverts the slot); give the surface's `scale` (the window's DPI scale) so labels and the UI are the size the user expects; destroy things in the order textures, surfaces, worlds, engine (`IN_USE` otherwise). What was built against the plan: no shadows, no orthographic cameras, no zero-copy GPU textures from the host, no per-vertex morph targets; labels have no depth test and no plate (`KNOWN-ISSUES.md`).

## Physics: bodies, colliders and queries (Phase 25 Day 408)

Two components make an entity physical: `ENGINE_COMPONENT_RIGID_BODY` (`engine_rigid_body`: static, kinematic or dynamic, the mass in kilograms or 0 for "from the shape and its density", dampings, a gravity scale) and `ENGINE_COMPONENT_COLLIDER` (`engine_collider`: a sphere, box or capsule, its offset and rotation on the body, friction, restitution, density, a layer and the group and mask that select what it meets, a trigger flag). Both are size-prefixed, validated before anything is stored (`INVALID_ARGUMENT` for a number that is not finite or out of range, with the reason in `last_error`), and read back with `component_get`. **A triangle mesh** (glTF colliders, 2026-11-20): `ENGINE_SHAPE_MESH` with `mesh_resource`, an `MCOL` the host added or an import made (`ENGINE_RESOURCE_MESH_COLLIDER` among the import's results), for static bodies only (a mesh collider on a body that is not static, or a body made non-static under a mesh collider, is `INVALID_ARGUMENT`, whichever is set second); it is made at the end of the next frame at the entity's world scale (as a spawned level's), so `component_has` and queries see it from then on, while `component_get` returns the host's values at once. `mesh_resource` and the `reserved` field before it were appended: a host of the first `engine_collider` still sets and reads the primitives, and gets `STRUCT_SIZE` for a mesh. Convex hulls and compounds are refused with `UNSUPPORTED`. An entity with a transform, a body and a collider and no parent is simulated (a static one with a parent too, placed by its world transform: glTF colliders): the frame makes its body (before it steps, in the order animation input, body sync, physics step, transform system) and writes the body's pose back to the entity's transform, so a host reads motion with `component_get` of the transform. A world that never had a body component pays nothing for physics. `engine_frame_stats` reports `physics_us` (making the bodies), `physics_bodies` and `physics_steps`; a host built before them passes the older `struct_size` and is served.

The table's physics functions take the world and the entity's key: `physics_set_gravity`, `body_apply_force` and `body_apply_impulse` (at a world point, or at the centre of mass when it is NULL), `body_set_velocity` and `body_get_velocity` (either vector may be NULL), `body_teleport` (the body and the entity's transform move at once), `physics_raycast` (the closest hit: the entity's key, the point, the normal facing against the ray and the distance; triggers are not hit) and `physics_overlap_sphere` / `physics_overlap_box` (keys into a host array with a capacity and a total, as `query`). A body is made by the frame or by the first call that needs it, so a call before any frame works; an entity that has no body answers `NOT_FOUND`, a force on a static body `UNSUPPORTED`. Nothing of the engine's memory crosses the boundary. The step rate, gravity, substeps, sleeping and the rest are the `physics.*` settings (`physics.gravity_y`, `physics.step_hz`, ...), read when the engine is created and refused like any setting when unknown or out of range.

## Physics II: joints, characters, events and snapshots (Phase 25 Day 409)

Two more components: `ENGINE_COMPONENT_JOINT` (`engine_joint`) and `ENGINE_COMPONENT_CHARACTER` (`engine_character`); the query mask is now `0x3FFF` (a bit above it is refused). The entity that has a joint component is the joint: `entity_a` and `entity_b` are the keys of two entities that are bodies (0 on one side ties the other to the world), `kind` is ball, distance, fixed or hinge, the anchors and the hinge axis are in world space at the time the joint is made, and `flags` carry collide-connected, the hinge limit and the hinge motor. The frame makes the physics joint when both bodies exist (a joint waiting for a body is not an error), a joint whose numbers change (the component set again) is made again, and a joint that breaks (`break_force` or `break_torque` above zero) loses its component by the end of that frame and is reported as an event. `joint_reaction` reads the force and torque the joint carried in the last step and fails with NOT_FOUND for an entity that has no joint in the physics.

A character is a transform plus `engine_character` (radius, height, step height, the least ground normal Y, the skin width and a gravity scale). The engine keeps its capsule in the physics world, `character_move(world, key, velocity, dt, &state)` sets the velocity it walks at (the wish is horizontal; gravity, slopes, steps and moving platforms are the controller's), `character_jump` asks for a jump when it stands on the ground, and `engine_character_state` says whether it stands, on which entity, which wall it ran into and its vertical speed. After each frame the entity's transform takes the capsule's centre (the world matrix follows on the next frame); removing the component or destroying the entity gives the capsule back at the next frame.

**The held move (since 2026-11-20).** The move holds until it is set again, and each fixed physics step carries it out: a frame that runs two steps moves the character twice, one that runs none moves it not at all, so the distance walked in a second is the same at 30, 60 or 120 frames a second. `dt` is checked and otherwise unused. A jump starts at the next fixed step. The entity's transform takes the capsule's centre interpolated between the last two steps when `physics.interpolate` is on (the default; the last step's position otherwise), so a camera that follows the entity moves smoothly; `on_ground` and `vertical_speed` are the last step's. A host that used to call `character_move` once per frame keeps working; a host that stopped calling it to stop the character now sets a zero velocity. The world's characters are one set shared with its scripts (below, "Scripts with physics").

`physics_events` copies the events of the last frame (every step it ran: contacts, then triggers, then joint breaks) into an array of `engine_physics_event` with the entities, the point, the normal and the depth; the array is the host's, the count comes back in `*total` so a capacity of 0 asks only the count, and the next frame replaces them. No callback crosses the boundary. `physics_snapshot` returns the physics state as a buffer (release it with `engine_buffer_release`) and `physics_restore` puts it back; a restore is refused with CORRUPT (not a snapshot, truncated or damaged) without changing the world, and the continued run is bit for bit the one the snapshot was taken from. The snapshot also holds the characters' own state (vertical speed, ground, held move, a pending jump), so a rewound character continues as it did.

The sample is `tests/engine_library/physics_sample.c` (a stack of boxes, a pendulum on a ball joint, a walking and jumping character, a trigger; 600 frames, a rewind at 300; the printed checksum is the same on every build), and `host_physics_joints.c` checks each refusal. The walk-through is the tutorial [Physics in Your Window](../tutorials/physics-in-your-window.md). Not here yet: hull, mesh and compound colliders (the `engine_collider` shapes are the three primitives) and a body made from a resource id; they need the collision assets to be addressable through `resource_add`.

## A solid level and a character (glTF colliders, 2026-11-20)

An artist's `.glb` brings its own collision (`docs/guide/assets.md`, "Collision from glTF": `-col`/`-colonly` object names, or `OMI_physics_*`
shapes). Import it with `ENGINE_IMPORT_EMIT_LEVEL`, spawn the level, run two frames, and the world is solid: a character dropped above the ground lands
on it.

```c
engine_import_options options = { sizeof options };
options.flags = ENGINE_IMPORT_EMIT_LEVEL;
api->import_gltf(engine, glb, glb_size, &options, &result);      /* the level is the result's ENGINE_RESOURCE_LEVEL */
api->level_spawn(world, level, NULL, &root, NULL, 0, NULL);      /* static bodies for every collider of the file */

uint64_t player; api->entity_create(world, &player);
engine_transform t = { sizeof t, { 0, 3, 0 }, { 0, 0, 0, 1 }, { 1, 1, 1 } };
api->component_set(world, player, ENGINE_COMPONENT_TRANSFORM, &t);
engine_character c = { sizeof c, 0.3f, 1.8f, 0.3f, 0.64f, 0.01f, 1.0f };   /* radius, height, step, slope, skin, gravity */
api->component_set(world, player, ENGINE_COMPONENT_CHARACTER, &c);

for (;;) {
    const float wish[3] = { input_x * 3.0f, 0, input_z * 3.0f };            /* metres per second */
    engine_character_state s = { sizeof s };
    api->character_move(world, player, wish, 1.0f / 60, &s);
    if (jump_pressed && s.on_ground) api->character_jump(world, player, 5.0f, &s);
    api->frame(engine, &frame_info, &stats);                               /* stats.colliders_made on the first frame */
}
```

What to expect:
- **Two frames** before the level is solid: the colliders are made at the end of the first frame that propagates the level's world matrices (at that
  world scale), the bodies in the next. Spawn the level before the player, or let the player fall a frame.
- **The character** climbs steps up to `step_height`, treats a surface whose normal's y is below `min_ground_y` as a wall, slides along walls, and falls
  off edges; `character_state.on_ground` says whether it stands.
- **The ground has no thickness rule for characters**: a triangle mesh has no inside, but the character's move is a swept capsule, so it never passes
  through a single-sided ground at walking or falling speeds. A fast *dynamic* body can tunnel through a thin mesh (`docs/guide/physics.md`).
- **Moving the level**: setting the root's transform moves its bodies a frame later; scaling it afterwards is counted
  (`engine_frame_stats.collider_scale_changes_ignored`) and not followed: spawn it again at the new scale.

**Tested** by `host_character_world.c` (a ground, a wall, a step of `step_height`, one 5 cm higher, a ramp steeper than `min_ground_y`, a ledge: landing,
stopping, climbing, falling, and two worlds moving the same), `host_import.c` (`TestColliders`, `TestMeshShape`) and `level_spawn_test.cpp`.

## The C++ layer: `engine.h`

A host written in C++ may include `include/engine/engine.h` beside `engine_c.h` (it includes nothing else: no standard library, no exceptions, no RTTI).
It is the table's own functions with three guards the C header cannot give: **owners** (`Engine`, `World`, `Counter`, `Resource`, `Ticket`, `ImportTicket`,
`Surface`, `PixelsTicket` and `Texture`: move-only, destroyed once, the status of `Destroy()` available, `Release()` hands the handle back), **result and buffer
holders** (`Buffer`, `ImportResult` and `Pixels` release what the engine handed out, once; `Fill()` releases what the object held first) and **`Init<T>()`** (a
zeroed struct with its `struct_size` set; the layer also sets it on the out-structs and on every element of the physics-event array it passes). `Status` is
`[[nodiscard]]` and converts to `true` on success.

**Every function of the table has a member** (or, for `get_extension` and `thread_detach`, a free function) named for what it does, with the table's arguments
minus the handle the object holds: `Engine` has the settings, `Frame`, the crash handler, jobs, the resource registry, glTF import, surfaces, textures and the
label font; `World` has entities, components, names, parents, queries, the environment, slot overrides, levels, animation, skeletons and all of physics (gravity,
forces, impulses, velocities, teleport, ray cast, overlaps, joint reactions, the character, events, snapshot and restore); `Surface`, `Texture`, `Ticket`,
`ImportTicket` and `Counter` have their own. The ctest `engine_h_covers_table` reads the table in `engine_c.h` and fails when a member is not called by
`engine.h`, so a function added to the table without a wrapper cannot pass. The layer adds no ABI: `owner.Get()` still gives the raw handle. Declare a world
after its engine so it is destroyed first (the engine refuses with `IN_USE` while one exists). Test: `host_cpp.cpp` (`engine_c_host_cpp`; it drives components,
names, parents, queries, physics, a counter and a job through the layer; the surface, texture, import and resource wrappers are compiled by it but not yet run
through the layer: only the C programs run those functions).

## Writing a host: the short path (Days 346-357)

1. Include `engine/engine_c.h` (and `engine_loader.h` to load the library at run time). Nothing else of the engine is a header a host may use: the C++ interfaces have no stable ABI.
2. Ask for the table: `engine_get_api(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, sizeof(engine_api), &api)`; a newer library serves an older host (the table only grows), an older library refuses a newer host with a status.
3. Zero every struct, set its `struct_size`, and pass it. Handles are generation-checked: a stale handle is `ENGINE_ERROR_INVALID_HANDLE`, never a crash.
4. Drive the engine from your loop (`frame`), or hand it a window (`surface_create`) and forward your events (`surface_input`); results the engine wants to tell you arrive through the callbacks you registered.
5. Destroy in reverse order: surfaces and worlds, then the engine; unload the library only after `destroy` (the unload and reload tests prove this works any number of times).

## What the shared build costs (Day 356)

Measured in Release on Linux x86-64 (figures and method: the spec's "Day 356 results"): `libengine.so.1.0` is 7.7 MB; a call through the function table costs 15-100 ns
(an empty `frame` 90-99 ns, an entity create and destroy about 33-36 ns, a component get or set 15-28 ns), the same whether the host linked the C layer statically
or loaded the shared library; the static default, which most programs use, is not slower for the option existing. A resource load is dominated by file I/O,
which the C layer does not change.

## Limits and what is deliberately not here

- **No stable C++ ABI.** Only the flat C table is stable; the C++ headers change with the engine.
- **No bindings beyond the table.** A binding for another language is written over `engine_c.h` and the ABI snapshot (`abi/engine_api.snapshot`), by the host's author.
- **No plugin hot reload.** A game as a library reloaded while the engine runs is not in this phase; the C ABI makes it possible (the engine itself unloads and reloads cleanly), and what it needs is listed in the spec.
- **No shared GPU textures yet.** A host that composites the engine's output itself reads pixels back (`surface_read_pixels`); importing the engine's texture into the host's own device (Vulkan external memory, a D3D12 shared handle, `IOSurface`) is not built (reasons in the spec).
- **One engine per process is the tested case;** several engines in one process share the allocator and the job system by design (`One of everything`).
- **Platform notes** are in the sections above: the Linux and POSIX loader, `engine.dll` and the HWND on Windows, the dylib and the `NSView` on macOS; iOS links statically.

## Audio: a host-driven mixer (Phase 26 Days 438-439)

`audio_open` (`engine_audio_info`: rate, channels, voice count, limiter) makes a mixer that has **no device and no thread**: the host calls `audio_render(frames, out)` from its own audio callback (or from any thread for an offline render) and gets interleaved floats. `audio_render` takes no lock, allocates nothing and never blocks; one thread at a time may call it, and every other audio function may be called while it runs. The same bytes come out whatever the callback size.

`audio_clip_create` copies the PCM the host gives (8-bit, 16-bit, 24-bit, 32-bit integer or float), `audio_play` starts a voice (gain, pitch, pan, loop, a bus), `audio_voice_set` and `audio_voice_stop` change it, `audio_bus_create`, `audio_bus_set`, `audio_bus_effect_set` (filter, delay, reverb), `audio_bus_send` and `audio_bus_duck` build the bus graph, and `audio_events` polls what voices did (finished, looped, stolen). Clips, voices and streams are 64-bit handles with a generation; a stale one fails with `STALE_HANDLE`.

Long sounds are streams: `audio_stream_open_clip` or `audio_stream_open` (the host's `read` and `seek` callbacks, called from the engine's job threads, never two at once and never from the audio thread). The host calls `audio_update` once per frame (with `wait` 1 for an offline render, so the same samples come out whatever the timing). `audio_music_play`, `audio_music_crossfade` (now, or at the next bar), `audio_music_layers`, `audio_music_layer_gain` and `audio_music_stinger` drive the music system on the audio clock.

**Live voices** (maintenance, 2026-11-18; `ENGINE_CAP_AUDIO_PUSH_STREAMS`, a maintenance item). A call's participants speak in blocks the host receives as they arrive, with no clip to give. `audio_stream_open_push(engine, &engine_audio_push_info, &stream)` opens a stream the host feeds: `audio_stream_push` (floats) or `audio_stream_push_i16` from the thread the audio arrives on, one thread per stream, any number of streams. There is no callback and no refill job: the push writes into a ring and the audio thread reads it, with no lock and no allocation on the audio thread; the push takes the control lock only for the lookup of the stream (a control call that runs long, such as the first fill of `audio_stream_open`, makes it wait). The frames are interleaved at the mixer's sample rate (`audio_state`; a push stream is not resampled). Set the stream as the `stream` of an entity's `engine_audio_source` (`clip` 0) and the voice is placed in the world like a clip's: spatial flag, distances, rolloff, cone, Doppler, bus, gain, priority. The voice follows the entity; destroying the entity, removing the source or setting another stream on it stops the voice, and the stream stays open until `audio_stream_close` (which also ends a voice that still plays it), so a participant who leaves and comes back gets the same stream on a new entity. A stream has one reader: a second source of the world with it, or a source with both a clip and a stream, is `INVALID_ARGUMENT`.

A live stream never ends. When the ring is empty the voice plays silence for the rest of the mixer block and goes on with the next frames, counted as an underrun in `audio_stream_stats` (it is not a `FINISHED` event). When the host pushes faster than the audio thread reads, the ring keeps the newest `capacity_frames` and drops the oldest, counted in `frames_dropped`. `prefill_frames` is a jitter buffer: the voice waits until that many frames are collected, and again after it ran dry (0 plays whatever is there); it is the delay a voice adds. A voice that plays a live stream cannot be saved: `audio_snapshot` fails with `UNSUPPORTED` until it is stopped.

*Delay.* The mixer works in blocks of 256 frames, so a frame pushed just before an `audio_render` call is heard in that call or the next one: measured on Linux (`engine_c_audio_live`, 48 kHz, the host calling `audio_render` for 480 frames after each 480-frame push, `prefill_frames` 0, eight phases of the block against the call) the marker reaches the output after at most 256 frames (5.33 ms). To that add `prefill_frames`, the host's own buffering between its callbacks, and the player's. A call that keeps the whole voice delay under 60 ms has about 50 ms for the rest. The Windows and macOS figures are the PC's and the Mac's to take (`KNOWN-ISSUES.md`). The 16-bit form saves the host one conversion per block; the engine converts in pieces of 256 frames on the stack.

`audio_snapshot` writes the state of the mixer (voices, buses, effects, limiter) and `audio_restore` puts it back into a mixer set up the same way, with the clips and streams it named open; a render after the restore replays the original bit for bit. The snapshot does not hold the music player's schedule: restore at a moment when no music action is pending. The sample `tests/engine_library/audio_sample.c` renders 600 blocks, snapshots at block 300 and checks the replay.

**Sounds on entities.** `ENGINE_COMPONENT_AUDIO_SOURCE` (`engine_audio_source`: a clip handle, a bus, gain, pitch, flags for loop, play on start, spatial, Doppler and occlusion, a priority, a distance model with its range and a cone) and `ENGINE_COMPONENT_AUDIO_LISTENER` (`engine_audio_listener`: a gain and an active flag) go on entities that have a transform. Each `frame` call places the world's sounds on the engine's mixer (`audio::SpatialAudio`: the loudest sources get real voices, the rest are virtual, the listener's velocity gives the Doppler shift, a removed component or a destroyed entity stops its voice); a world with no audio component pays nothing. A source needs the clip to exist (`audio_clip_create`) when it is set, and a clip handle read back from `component_get` is the table's handle for the same clip. Closing audio and opening it again keeps the sources: the world remakes its voices on the new mixer. `audio_close` waits for no one but keeps the mixer alive for any table call still in flight, and `audio_render` finds the engine without a lock. `audio_bus_effect_set` retunes the effect of the same kind in a slot instead of making another (the mixer makes at most 64 over its life).

Not in the C API (recorded in `KNOWN-ISSUES.md`): occlusion of sources through the physics world (the `occlusion` flag is kept but nothing answers it yet) and device selection (the host owns the device).

## Scripts: loading, calling and host functions (Phase 27 Day 471)

A library built with the script module (`ENGINE_CAP_SCRIPT` in `engine_info.caps`; Linux, Windows and macOS x86-64; a library built without the script module answers `UNSUPPORTED`) lets a host put scripts in a world.
The world's script runtime is made by the first `script_load`, the first script component or the first host function, from the `script.*` settings read when the engine was created
(`script.partitions`, `script.budget`, `script.jit`, `script.random_seed`, ...; see [Scripting](scripting.md)).

- **`script_load`** adds a script (its text and a chunk name) to the world: every state of the world loads it in the order of the calls. A script that fails to compile does not fail the
  call; its error is in the log and counted in `script_stats().errors` when it runs. A script resource id (a minted script) is not loadable through the C API yet.
- **`ENGINE_COMPONENT_SCRIPT`** attaches a behavior (the name given to `behavior(name, table)` in a loaded script) to an entity through `component_set`; `params` is a map the instance reads as
  `self.params`. The frame runs the callbacks, in the world's script runtime, before the bodies follow the entities; `enabled` 0 keeps the instance from running.
  `component_get`, `component_has` and `component_remove` work as for every component (`params` is NULL on a read).
- **`script_call`** calls a global function (`name` or `table.name`) of the world's scripts at once, on the calling thread, with `args` in and `results` out. It is refused with `NOT_READY` when a
  frame or another call is running on another thread (it never waits), and a function that waits (a coroutine) is an error. A call runs in one state of the world: a global it changes is seen by
  the later calls of that state, not by the behaviors' instances in other partitions.
- **`script_set_budget`** limits one `script_call` (instructions, nanoseconds, memory; 0 is no limit); set before the runtime exists it also sets the limit of the frame's callbacks.
  With `script.budget` false (a trusted world with the JIT) an instruction limit is `UNSUPPORTED`.
- **`script_register_function`** makes `namespace.function` callable from scripts, with a fixed arity. Register before the first script of the world (`NOT_READY` after). The callback runs on an
  engine worker thread in whichever partition made the call, several at once, while the frame holds the world: it must be thread-safe, short, and must call no function of the engine API (a
  `script_call` from inside it, even on another thread, is refused as busy). `ENGINE_SCRIPT_FUNCTION_PURE` also installs it in the states of script tasks.
- **Values** cross as an `engine_value_pool`: an array of `engine_value` (nil, bool, int, number, string, bytes, array, map, entity) and a byte pool, with containers and strings named by index and
  offset, so no pointer into the engine and no allocation crosses the boundary. An input pool is checked before anything runs (limits: 65,536 values, depth 32, text 1 MiB, blob 16 MiB; an index outside
  the pool, an unknown tag or a map key that is not a string is `INVALID_ARGUMENT`). An output pool that is too small is `TOO_LARGE` with `item_count` and `byte_count` telling how much it needs.
  A script's `vec2`, `vec3`, `vec4` and `quat` come out as arrays of numbers.
- **`script_stats`** reports the partitions, ticks, live instances, updates, errors, calls and host calls, and how many scripts and host functions the world has.

### Events and tasks (Phase 27 Day 472)

- **`script_event_emit`** sends an event from the host (a name of 1 to 64 bytes, the first root of a pool as the payload, a target entity id or 0 for every subscriber). It enters the world's event bus at
  once and is delivered at the start of the world's next script tick, in the bus order (host emits in call order; events the scripts emit follow at the sync points). It needs the runtime
  (`NOT_READY` before the first script) and, like `script_call`, does not wait for a running frame.
- **`script_event_subscribe`** calls a host function for the events with a given name (an empty name: every event) that scripts or the host emit. The call happens on the thread that runs `frame`,
  at the start of the script tick, while the world is held: it must not call the engine API. It may be made before the first script (the runtime takes it over when it is made); at most 256
  per world. **`script_event_unsubscribe`** removes it. The payload is a pool with one root, valid for the call only.
- **`script_task_run`** runs one pure task (`task.run`, `task.map` or `task.parallel_for`: mode `ENGINE_SCRIPT_TASK_RUN`, `_MAP`, `_FOR`) in the world's task states and fills `results`. Only functions registered
  `ENGINE_SCRIPT_FUNCTION_PURE` exist there. It runs now, on the calling thread (a host thread is not a job-system participant, so the chunks run one after another); the result is cut into chunks
  by index and does not depend on the thread count. Register the functions first (`NOT_READY` after the first script or task).
- **Threads.** One thread at a time runs `frame` for an engine; the other calls (`script_call`, `script_task_run`, `script_event_emit`) from other threads beside it get `NOT_READY` and are retried by
  the host. `tests/engine_library/script_sample.c` shows all of it: 1,000 entities moved by a behavior for 600 frames, a host event in, script events out, host functions, tasks, and three host
  threads with worlds of their own beside the frame thread; the state hash is the same for 1, 2, 4 and 8 script partitions.

### Scripts with physics (2026-11-20)

A world's scripts have the physics and character bindings of [Scripting](scripting.md), "Physics" (`physics.*` and `character.*`), on that world's own physics world; the
same world's C API functions (`ENGINE_COMPONENT_CHARACTER`, `character_move`, `character_jump`) work beside them.

- **One set of characters per world.** A character made by `character.create` and one made by `ENGINE_COMPONENT_CHARACTER` are the same kind of object in the same set: `character_jump(world, key, 0, &state)`
  reads a script-made character, and `character.is_grounded` reads a C-made one. An entity has at most one: `character.create` on an entity that already has a character fails ("has a character"),
  whichever door made it, and setting the component again puts the capsule back at the entity's transform with the new config.
- **Timing.** A frame runs the script tick (its commands are applied at the merge), then 0, 1 or several fixed steps (`physics.step_hz`, `physics.max_substeps`), then the write-back. The held move
  (`character.move`, `character_move`) is carried out once per fixed step, so the distance walked in a second does not depend on the frame rate; a jump starts at the next fixed step (and waits for one
  when the frame makes none). The entity's transform takes the capsule's centre, interpolated between the last two steps when `physics.interpolate` is on (the default): a first-person camera that
  follows the entity moves smoothly. `character.is_grounded` and `character.vertical_speed` are the last step's.
- **Teleport.** `character.set_position(e, vec3)` puts the capsule's centre there at the merge, with the vertical speed and the body's speed reset and a pending jump dropped; the held move stays.
  `transform.set_position` on a character's entity does not move the character (the write-back replaces it).
- **A spawned entity.** `entity.spawn` then `character.create` on the returned entity in the same callback works: the merge makes the entity, then the character, so the character exists after that
  tick and moves in that frame's steps. The handle `entity.spawn` returns names the new entity only within that tick's merge: a call with the stored handle in a later tick does not reach
  it (`KNOWN-ISSUES.md`), so do the spawned entity's set-up in the spawning callback. `character.exists` is true from the next tick.
- **Events.** After the frame's steps the scripts get the steps' contacts and triggers (`on_collision_*`, `on_trigger_*`; a character is named by its entity), delivered at the next tick.
- **Layers.** The names scripts give physics layers are the engine's `script.physics_layers` setting (`default` when it is not set).
- **Snapshots.** `physics_snapshot` holds the characters' state with the bodies, so a rewound character continues as it did.

**Tested** by `host_script_character.c` (an imported `-col` ground; a behavior makes a character on its entity and on one it spawned in the same tick, jumps it at 5 m/s: it leaves the ground and
lands, the rise is the fixed step's `v^2 / (2 g)`, the C API's state agrees with the script's, and a teleport resets the vertical speed; then a held `character_move` walks 1.475 m in a second at 30,
60 and 120 frames a second).

**A note for hosts (not a requirement).** A room could be a world whose objects carry a behavior: the host emits `player_entered` events and subscribes to what the room's scripts emit, and runs
`script_task_run` for work it wants the engine to do off its own thread. Nothing here depends on it.

What is not here yet is listed in `KNOWN-ISSUES.md`: unloading a script, calling a behavior of one entity, loading a minted script by resource id, and running the partitions on the engine's jobs from the host's frame call.
