# Phase 0 / Day 1 — Repository & Build Skeleton

**Phase:** [Phase 0 — Foundation Layer](../phase-00-foundation.md)
**Depends on:** nothing.

## Tasks

- [ ] Create the `/engine` directory tree matching `PLAN.md`'s repo
  layout (`/engine/foundation`, `/engine/platform`, `/engine/jobs`,
  `/engine/rhi`, `/engine/renderer`, `/engine/ecs`, `/engine/physics`,
  `/engine/audio`, `/engine/asset`, `/engine/unreal_import`,
  `/engine/scripting`, `/engine/editor`), each currently empty aside
  from a placeholder so git tracks it.
- [ ] Write the root `CMakeLists.txt`: `cmake_minimum_required`,
  `project(<placeholder-name> CXX)`, C++20, x86-64 Linux target. Link
  normally against libc — no `-nostdlib`/`-ffreestanding` (`PLAN.md`
  assumption #2, resolved). Compile flags: `-fno-exceptions -fno-rtti
  -fno-builtin -fno-stack-protector` (exceptions/RTTI stay banned
  regardless of the libc decision; `-fno-builtin` stops the compiler
  substituting its own builtins for the hand-written mem/string
  primitives Day 3 defines).
- [ ] Add `/engine/foundation/CMakeLists.txt` defining a `foundation`
  static library target, and a placeholder `engine_main` executable
  target linking it.
- [ ] Write a placeholder `int main() { return 0; }` in
  `/engine/foundation/main.cpp` — temporary; later days replace this
  with real startup logic.
- [ ] Choose the placeholder engine name (`PLAN.md` assumption #9) and
  record it in exactly one place: the CMake `project()` name and the
  top-level C++ namespace, so renaming later is a find-and-replace.
- [ ] Build: `cmake -B build && cmake --build build`.
- [ ] Run the resulting binary and confirm `echo $?` prints `0`.
- [ ] Commit the skeleton.

**Done:** `cmake --build` produces an ELF binary; running it exits with
code 0 (verified via `echo $?`).
