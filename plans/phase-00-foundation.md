# Phase 0 — Foundation Layer

**Depends on:** nothing (freestanding).
**Goal:** the language-level building blocks every later layer needs —
memory, containers, strings, math, logging/assert — with no CRT/stdlib.
**Definition of done:** custom allocators/containers/string/math all pass
the custom test harness (`ctest`, all green).

See root `PLAN.md` for guiding principles and scope assumptions.

## Tasks

- [ ] **Day 1 — Repository & build skeleton.** Set up `/engine` directory
  structure. Build system starts on CMake (a dev-time dependency, not a
  runtime one — see PLAN.md assumption 8) for Linux x86-64 using
  `-nostdlib -ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector
  -fno-builtin`. A custom entry stub that does nothing but exit(0) via raw
  syscall. CMake is a placeholder: once build needs (cross-compiling to
  later platform targets, asset cooking steps, custom toolchain flags) are
  well understood, it gets replaced by our own build tool under `/tools`.
  The engine name is still undecided (see PLAN.md assumption 9), so use a
  single placeholder name for the CMake `project()` and top-level
  namespace, kept to as few spots as possible so it's easy to rename later.
  **Done:** `cmake --build` produces an ELF binary; running it exits with
  code 0 (verified via `echo $?`).

- [ ] **Day 2 — Raw syscall layer (x86-64 Linux).** Inline-assembly wrappers
  for `exit`, `write`, `read`, `open`, `close`, `mmap`, `munmap`, `mprotect`.
  **Done:** the `write` wrapper prints `"OK\n"` to fd 1 with no libc involved.

- [ ] **Day 3 — Base types & mem primitives.** Fixed-width int typedefs,
  bool, hand-written `memcpy`/`memset`/`memmove`/`strlen` (the compiler will
  emit calls to these for struct/array ops even in freestanding mode).
  **Done:** custom assert-based tests validate each against hand-computed
  cases.

- [ ] **Day 4 — Arena allocator.** Linear allocator backed by raw `mmap`,
  with alloc/reset/free-all.
  **Done:** allocate 1,000 objects, checksum-verify no overlap/corruption.

- [ ] **Day 5 — Pool & stack allocators.** Fixed-size free-list pool;
  push/pop-marker stack allocator.
  **Done:** alloc/free churn test shows no leaks or corruption.

- [ ] **Day 6 — Dynamic array container.** Resizable array over a chosen
  allocator: push/pop/insert/remove/reserve.
  **Done:** stress test pushes 100k elements, contents verified.

- [ ] **Day 7 — Hash map container.** Open-addressing map with custom hash
  functions for common key types.
  **Done:** insert/lookup/delete correctness test against a known key set.

- [ ] **Day 8 — String type & builder.** Length-prefixed string view + owning
  string; a formatter (int/float/string → buffer) since there's no printf.
  **Done:** formatted output written via the raw `write` syscall matches
  expected text exactly.

- [ ] **Day 9 — Logging & assertions.** Log system on top of the Day 8
  formatter, designed against Unreal's log-spam problem from the start
  (see PLAN.md assumption 10): named channels/categories (e.g. `LogCore`,
  `LogRender`), each with its own verbosity level (Error/Warn/Info/
  Debug/Trace) and independent enable/disable; a global default verbosity
  with per-channel overrides. Channels default to a conservative verbosity
  (Warning+), not "log everything." Settings are overridable at startup
  without recompiling — per-user configuration — via a simple config
  source (exact format TBD, see open question below); the log API is
  built to support that from day one rather than bolting it on later. An
  assert macro on top prints file/line/message and calls raw `exit(1)` (no
  libc `abort`).
  **Done:** an intentionally-failing assert prints the right message and
  process exit code; toggling a test channel's enable flag and verbosity
  via config changes what reaches the `write` syscall, verified for both a
  suppressed and an emitted message.

- [ ] **Day 10 — Math: vectors & matrices.** Vec2/Vec3/Vec4, Mat3/Mat4: dot,
  cross, normalize, multiply.
  **Done:** results cross-checked against hand-calculated reference values.

- [ ] **Day 11 — Math: quaternions & transforms.** Quaternion type, slerp,
  quaternion↔matrix conversion, Transform (position/rotation/scale)
  composition.
  **Done:** rotating a known vector by a known quaternion matches the
  expected result.

- [ ] **Day 12 — Foundation test harness.** A minimal custom test runner
  (no external framework) that runs and reports every foundation test.
  **Done:** `ctest` builds and runs the suite, all green.

## Notes / open questions

- **Log config format/source is undecided.** Day 9 needs per-user,
  no-recompile control over channel verbosity/enable state, but Phase 0
  has no config-file parser yet (Day 8 only gives a string type, not a
  parser, and the asset pipeline with its own format conventions doesn't
  land until Phase 7). Leading candidate is a minimal hand-rolled
  `key=value` text format read via the Day 2 syscalls, kept intentionally
  dumb — revisit if that proves too limited once real usage shows up.
