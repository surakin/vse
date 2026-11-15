# The engine as a loadable library

The engine can be built as one shared library (`libengine.so.1`, `libengine.1.dylib` on macOS) that a host program loads at run time and drives through a flat C API, next to the static
libraries games link today (Phase 23; design in `docs/superpowers/specs/2026-11-16-engine-library-design.md`). This page grows with the phase; so far it holds
the rules of what the library exports and the first part of the C API.

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
&loader)` opens the library (`dlopen` with `RTLD_NOW | RTLD_LOCAL`; the Windows variant arrives with Day 354), finds `engine_get_api`, asks for the version
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
removes the handler if the host did not. The dump the crash test checks is in a directory of the run, so a dump of an earlier run cannot make a failing run pass.

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

## Importing a glTF at run time: the the host path (Day 351)

A program that fetches a glTF file from somewhere (the host gets 3D room models from a Matrix room) hands the bytes to the engine and gets resources it can
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


## Rendering into a window the host owns (Day 352)

An application with its own toolkit (a Qt, GTK or Electron shell, or a C program with a bare Xlib window) keeps its window and has the engine draw into it:
`surface_create`, `surface_resized`, `surface_rebind`, `surface_set_background`, `surface_state`, `surface_read_pixels`, `pixels_finish`, `pixels_destroy` and
`surface_destroy`, described in `engine_c.h` ("Day 352"). The engine **never creates, resizes, moves or destroys the host's window**; the host owns its size,
visibility and scale and reports them. In this order:

1. **Ask what the build can do.** `get_info`: `caps & ENGINE_CAP_SURFACE_X11` and `ENGINE_CAP_SURFACE_WAYLAND` say which window kinds `surface_create` accepts
   (today Linux with the Vulkan backend), `ENGINE_CAP_READBACK` that `surface_read_pixels` works. A build that cannot answers `UNSUPPORTED` with a sentence in
   `last_error`, never a crash.
2. **Create the surface** from the window's native handles in an `engine_surface_info`: for X11 the `Display*` (`display`) and the `Window` (`window_id`), for
   Wayland the host's own `wl_display*` (`display`) and `wl_surface*` (`surface`): the engine makes **no second connection**, it presents on the host's, so the
   host keeps dispatching its events as it did. The size is in pixels and the scale is the DPI factor (0 means 1). Each surface has a device of its own; the
   surface counts as an object of the engine until `surface_destroy`, and `destroy` of the engine answers `IN_USE` until it is gone. **Destroy the surface
   before the window.**
3. **Draw.** Every `frame` renders one frame into every surface: in this version a clear to the surface's background color (`surface_set_background`; the
   renderer is not behind the C API yet, so a scene such as the demo's cube has nothing to render it with: `KNOWN-ISSUES.md`). The host calls `frame` from its own
   thread, as everywhere in the API.
4. **Follow the window.** The host tells the engine every change of size or scale with `surface_resized` (what a toolkit does on `ConfigureNotify` or when the
   Wayland surface is configured; **the size reported must be the size the window really has**: on X11 the swapchain takes the server's extent when it is rebuilt).
   A size of 0 in either dimension means hidden or minimized: the engine draws nothing and keeps the surface (`frames_skipped` of `surface_state` counts the
   frames); the next real size resumes. Unmapping or covering a window needs no call.
5. **Recover.** `surface_state` reports `visible` (the last frame was presented), `lost` and `recoveries`. A surface whose window went away from under the
   engine draws nothing and does not crash the engine: on X11 the server reports the destroyed window as having no drawable area, so the surface reads as
   hidden (`visible` 0, `frames_skipped` counting up) and a read-back pending on it stays pending; when the swapchain reports the surface itself lost, `lost` is
   1 and the pending read-backs fail with `UNSUPPORTED`. Either way the host calls `surface_rebind` with the handles of a new window and the engine rebuilds
   the swapchain on the same device through the reset path of Phase 8 (`RecreateSurface`), `recoveries` goes up and the next frame is drawn there. If the
   device cannot present to the new window (`UNSUPPORTED`) the surface stays lost, and the host can try again or destroy it.
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
the install name is `@rpath/libengine.1.dylib`: the host's own rpath says where it lives. The Windows package follows with Day 354. A header that is only for the tests
(`engine_test_extension.h`) stays in `engine/engine_c/` and is not installed.


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
`destroy`; what they tolerate is the mapping (`tests/engine_library/process_probe.h`, `ENGINE_LIBRARY_UNLOADS`). A host that must get the memory of the
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
runs its UI rig on a Cocoa window there too. The Linux hosts run on the Mac unchanged except for the process probes (`process_probe.h`: Mach and dyld in
place of `/proc`) and the unload limit above.
