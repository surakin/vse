# The engine as a loadable library

The engine can be built as one shared library (`libengine.so.1`) that a host program loads at run time and drives through a flat C API, next to the static
libraries games link today (Phase 23; design in `docs/superpowers/specs/2026-11-16-engine-library-design.md`). This page grows with the phase; so far it holds
the rules of what the library exports.

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

1. Declare and define the function in `engine/engine_c/` as `extern "C" ENGINE_API ...` (and in `engine_c.h` from Day 346).
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
  that is still alive. The frame pool's per-thread cache and the crash handler's alternate stack were the two offenders on the engine's API path; both are
  now plain data, handed back explicitly: `jobs::FramePool::ReleaseThreadCache()` and `debug::UnregisterThreadForCrashHandling()` (through
  `NotifyThreadEnd`, which the job workers and the I/O thread call last, and, for the creating thread, the end of the scheduler's pool). A thread that
  ends without calling them strands at most 64 cached blocks per size class and one 128 KiB, mostly untouched, alternate stack; the C API (Day 346)
  gives a host thread `engine_thread_detach` for it. Still on the list: the Vulkan backend's per-thread pool slot holder (Day 346 or 348).
- **The crash handler is installed once, by the engine, and removed at shutdown;** the host's own handlers are back afterwards.

The tests (`tests/engine_library`, Linux, `-DENGINE_BUILD_SHARED=ON`) are small C programs that `dlopen` the library: `engine_dup_singletons` (a plugin that
links only the C API sees the engine's allocator, counters, log and frame pool) with its planted failure (`engine_dup_singletons_planted*`: a plugin that
links the static foundation is detected, by name), `engine_unload_after_jobs` (plain, asan, tsan: jobs on the calling thread and on a joined host thread,
the crash handler installed and removed, then `dlclose`: the library must be gone), `engine_owner_*` (plain, asan) and `engine_crash_across_boundary` (the
host's `SIGSEGV` handler is restored; a crash inside a job leaves an envelope with a minidump). Until Day 346 they read the library through the table
`engine_shared_state_probe` returns (`engine/engine_c/engine_state_probe.h`), which `engine_get_api` replaces.
