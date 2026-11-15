# The engine as a loadable library

The engine can be built as one shared library (`libengine.so.1`) that a host program loads at run time and drives through a flat C API, next to the static
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

1. `engine/engine_c/engine_abi_version.h`: the version line of the list and `ENGINE_ABI_VERSION_MAJOR` and `_MINOR` must be the same numbers;
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

`engine/engine_c/engine_c.h` is the whole surface a host sees: plain C11, one exported function and a table. The convention comment at the top of the header is
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

`engine/engine_c/engine_loader.h` is header-only plain C that needs nothing but `engine_c.h`: a host copies the two files. `engine_load(path, major, minor,
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

A language binding (Phase 26's Lua, any other) binds through this table and may rely on: functions are only ever appended to `engine_api`, and a field
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

**Audit.** The things an engine registers, and who removes them: the settings listeners and the `log.*` application (the settings service, at destroy); the log observer (destroy); the log channels' state (destroy restores it); the job scheduler's threads (the scheduler that made them, which joins them when it is destroyed: the engine starts none of its own until Day 349); the crash handler (the host installs and removes it; an engine installs it on Day 349 and removes it at destroy); the per-thread caches (the thread's own `thread_detach`); the global frame pool (the library's destructor). No static destructor touches another static.
