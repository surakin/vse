# Getting Started

> **Status:** skeleton — not yet written  
> **Audience:** new users  
> **Covers:** Phases 0-2

## Source of truth

- `CMakeLists.txt`
- `STATUS.md`

## Already known — seed material (verify against the code when writing)

- Zero third-party convenience dependencies: only the compiler, libc, Wayland/X11 client libs and libxkbcommon at the OS boundary
- Linux first; Wayland primary, X11 fallback
- Build: `cmake -B build && cmake --build build`; test: `cd build && ctest`
- Compiler in use: clang++ with `-fno-exceptions -fno-rtti`

## Outline

### Prerequisites

_TODO_

### Getting the source and building

_TODO_

### Running the hello-window binary and its flags

_TODO_

### Running the tests

_TODO_

### Repository layout (`engine/`, `tests/`, `plans/`, `docs/`)

_TODO_

### Where to go next

_TODO_

