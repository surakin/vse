# Getting Started

> **Status:** skeleton — the Windows build and run commands are written (Phase 13), and `windows.md` is the full Windows page; the rest is not yet written  
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

_TODO_ (Linux). `BUILD.md` lists what each platform needs installed.

**On Windows** (Phase 13 Day 161), from any terminal: the toolchain files find MSVC, the Windows SDK and the Vulkan SDK themselves, so no
Developer Command Prompt is needed.

```
cmake --preset windows-msvc            # or windows-clang-cl; the build tree is build-msvc (build-clang-cl)
cmake --build --preset windows-msvc
ctest --preset windows-msvc
```

### Running the hello-window binary and its flags

_TODO_ (Linux).

**On Windows** the programs that open a window (`hello_window`, `rhi_demo`, `renderer_demo`) are Windows-subsystem programs: started from
Explorer they open no console window; started from a terminal, their output and `--help` go to that terminal.

```
build-msvc\game\hello_window\hello_window.exe
build-msvc\game\rhi_demo\rhi_demo.exe --frames 1000 --validation
build-msvc\game\renderer_demo\renderer_demo.exe --list-settings
build-msvc\game\renderer_demo\renderer_demo.exe --set renderer.fullscreen=true
```

A demo's settings file is `%APPDATA%\<demo>\settings.cfg` (`--save-settings` writes it) and its crash dumps go to
`%LOCALAPPDATA%\<demo>\crashes`. The demos draw with Direct3D 12 by default on Windows; `--rhi vulkan` chooses Vulkan when the program starts. The demos' READMEs (`game/rhi_demo/README.md`, `game/renderer_demo/README.md`) list every option.

### Running the tests

_TODO_

### Repository layout (`engine/`, `tests/`, `plans/`, `docs/`)

_TODO_

### Where to go next

_TODO_

