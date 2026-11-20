# Phase 0 / Day 1 — Repository & Build Skeleton

**Phase:** [Phase 0 — Foundation Layer](../phase-00-foundation.md)
**Depends on:** nothing.

**Note (added later, alongside the Day 9-adjacent MMap portability
fix):** `engine/foundation/CMakeLists.txt` grew two more targets not
mentioned below — `foundation_asan`/`engine_main_asan`, the same
sources built with AddressSanitizer/UndefinedBehaviorSanitizer via
`target_compile_options`/`target_link_options` (not a build-wide flag,
so both the normal and sanitized binaries exist side by side from one
configure). Run via `cmake --build build --target engine_main_asan`
then `build/engine/foundation/engine_main_asan`. Used to verify every
day's tests from Day 4 onward; not itself tied to any one day's task
list, so noted here where the rest of the CMake setup lives.

## Tasks

- [x] Create the `/engine` directory tree matching `PLAN.md`'s repo
  layout (`/engine/foundation`, `/engine/platform`, `/engine/jobs`,
  `/engine/rhi`, `/engine/renderer`, `/engine/ecs`, `/engine/physics`,
  `/engine/audio`, `/engine/asset`, `/engine/unreal_import`,
  `/engine/scripting`, `/engine/editor`), each currently empty aside
  from a placeholder so git tracks it.
- [x] Write the root `CMakeLists.txt`: `cmake_minimum_required`,
  `project(<placeholder-name> CXX)`, C++20, x86-64 Linux target. Link
  normally against libc — no `-nostdlib`/`-ffreestanding` (`PLAN.md`
  assumption #2, resolved). Compile flags: `-fno-exceptions -fno-rtti
  -fno-builtin -fno-stack-protector` (exceptions/RTTI stay banned
  regardless of the libc decision; `-fno-builtin` stops the compiler
  substituting its own builtins for the hand-written mem/string
  primitives Day 3 defines).
- [x] Add `/engine/foundation/CMakeLists.txt` defining a `foundation`
  static library target, and a placeholder `engine_main` executable
  target linking it.
- [x] Write a placeholder `int main() { return 0; }` in
  `/engine/foundation/main.cpp` — temporary; later days replace this
  with real startup logic.
- [x] Choose the placeholder engine name (`PLAN.md` assumption #9) and
  record it in exactly one place: the CMake `project()` name and the
  top-level C++ namespace, so renaming later is a find-and-replace.
  Chose `vse` for both the CMake project name and the
  top-level C++ namespace — deliberately unmistakable as a placeholder
  (not `re`/`reengine`, which would have coincidentally matched this
  repo's directory name and could be misread as an actual naming
  decision) and a single unique token to grep for when renaming later.
- [x] Build: `cmake -B build && cmake --build build`.
- [x] Run the resulting binary and confirm `echo $?` prints `0`.
- [x] Commit the skeleton.

**Done:** `cmake --build` produces an ELF binary; running it exits with
code 0 (verified via `echo $?`).
