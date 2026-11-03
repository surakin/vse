# Building and Testing

> **Status:** skeleton — not yet written  
> **Audience:** contributors and users  
> **Covers:** Phases 0-2

## Source of truth

- `CMakeLists.txt`
- `engine/*/CMakeLists.txt`
- `tests/*/CMakeLists.txt`
- `STATUS.md`

## Already known — seed material (verify against the code when writing)

- Three sanitizer flavors: plain, ASan/UBSan (`*_asan`), ThreadSanitizer (`*_tsan`, label `tsan`)
- `ctest -L tsan` runs the TSan set; a deliberate-race canary proves TSan is active
- Catch2 v3 vendored; production flags `-fno-exceptions -fno-rtti -fno-stack-protector` do not apply to tests
- Known timing flake: an X11 test under heavy `ctest -j` load

## Outline

### Build targets and flavors

_TODO_

### Running subsets with labels and Catch2 tags

_TODO_

### Sanitizers: what each catches and when to use it

_TODO_

### Writing a test (Catch2 conventions in this repo)

_TODO_

### Stress tests and replaying a failing seed (`ENGINE_STRESS_SEED`)

_TODO_

### CI expectations

_TODO_

