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
