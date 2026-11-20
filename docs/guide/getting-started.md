# Getting Started

Status: written (Linux, Windows and macOS).

**Audience:** a new developer. This page takes you from a fresh checkout to a running window and a passing test, then tells you how the repository is
organized and what the rules of the house are. Everything after it in the [guide](README.md) assumes you have read it.

## What this engine is

A game engine written from scratch in C++20: it opens its own windows (Wayland or X11 on Linux, Win32 on Windows, Cocoa on macOS), talks to the GPU through its
own graphics layer (Vulkan, Direct3D 12 and Metal), runs its work on its own job system, draws its own text and user interface, and stores game objects in its own
entity-component system. It has almost no third-party code: the operating system's libraries, and a short list of vendored libraries for jobs that are
solved problems (FreeType for fonts, Catch2 for tests, and a few more, all under `third_party/` and built from source).

Two choices shape everything you will read:

- **The standard library is not used.** `std::vector`, `std::string`, `std::move`, `memcpy`, `printf` all have engine replacements (`Vector`, `String`, `Move`,
  `MemCopy`, `out::Print`). They are small, predictable and the same on every platform. [Standard types](foundation-types.md) is the dictionary.
- **No exceptions, no run-time type information.** Errors are values (a `bool`, a status, a `nullptr`) or, for bugs, a failed assertion. See
  [Logging and assertions](foundation-logging-and-assert.md).

## Prerequisites

You need a C++20 compiler (Clang is the reference; GCC and, on Windows, MSVC and `clang-cl` also work; on macOS, Apple clang from the Command Line Tools), CMake 3.21 or newer, Ninja or Make and the
platform's window libraries. `BUILD.md` in the repository root lists every tool and library per platform with the versions in use; on Linux you also need
a Wayland compositor or an X server to open windows, and a Vulkan 1.3 driver to run the graphics demos.

Third-party libraries are **vendored**: do not install FreeType, Catch2 or anything else yourself.

## Getting the source and building

On Linux, from the repository root:

```bash
cmake --preset linux
cmake --build build
```

The preset makes `build/`, a Debug tree built with Ninja. A tree made by an older configure with Make has to be removed first: CMake refuses to
change the generator of an existing tree.

The first build compiles everything, so expect it to take a while. Our own targets are built as *unity builds* (several source files merged into one translation unit) to be fast;
`-DENGINE_UNITY_BUILD=OFF` compiles each file alone, and both must keep working. Warnings are errors on every compiler: a clean build is the rule, not a goal.

**On Windows** (Phase 13 Day 161), from any terminal: the toolchain files find MSVC, the Windows SDK and the Vulkan SDK themselves, so no
Developer Command Prompt is needed.

```
cmake --preset windows-msvc            # or windows-clang-cl; the build tree is build-msvc (build-clang-cl)
cmake --build --preset windows-msvc
ctest --preset windows-msvc
```

**On macOS** (Phase 21), with the Command Line Tools (no Xcode), CMake and Ninja; the graphics backend is Metal. [macOS](macos.md) has the rest.

```
cmake --preset macos-clang             # the build tree is build-macos
cmake --build --preset macos-clang
ctest --preset macos-clang
```

## Running your first program

`hello_window` is the smallest program: it opens a window, prints the keyboard and mouse state when it changes, and closes itself after six seconds.

```bash
build/game/hello_window/hello_window
build/game/hello_window/hello_window --run-seconds=30 --log-level=info
```

| Flag | Meaning |
|---|---|
| `--backend=auto\|wayland\|x11\|win32\|cocoa` | which window system to use (the default: Wayland, then X11 on Linux; Win32 on Windows; Cocoa on macOS) |
| `--run-seconds=N` | how long to run (default 6) |
| `--frame-times` | print the time of every frame |
| `--log-level=error\|warn\|info\|debug\|trace` | how chatty the engine's logging is |

Move the mouse over the window and press keys: you will see lines such as `input: mouse=(412.0,230.5) ... keys_down=1`. Closing the window ends the program.
The source is `game/hello_window/main.cpp`; reading it is the next step ([Windowing and input](platform-windowing-and-input.md) explains it).

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

### The demos

| Program (`build/game/<name>/<name>`) | What it shows |
|---|---|
| `hello_window` | a window and the input state |
| `input_probe` | prints the input event stream; keys try the pointer lock (`L`), cursor shapes (`C`) and the clipboard (`Y`) |
| `rhi_demo` | a textured, lit cube drawn through the graphics abstraction ([RHI](rhi.md)) |
| `renderer_demo` | a lit scene through the renderer ([Renderer](renderer.md)); `--list-settings` shows what can be changed |
| `ui_demo` | a window built only from the engine's widget toolkit, around a 3D viewport ([UI](ui.md)) |

Every demo has `--help`. Their settings come from the settings system ([Settings](settings.md)): `--set key=value`, a settings file, or an environment
variable all work.

## Running the tests

The test suite is large (thousands of cases), so the project runs it **by phase**. Day to day you run only what your change can have affected:

```bash
scripts/test-phase.sh --affected          # the current phase plus the phases of every module you touched
scripts/test-phase.sh --affected --flavor asan   # the same under AddressSanitizer
```

The last line it prints says exactly how much ran, for example `ran 1992 of 5121 tests (phases: ...)`. When you report results, quote that line:
"all tests pass" is only true of `scripts/test-phase.sh --all --flavor all`. A few tests need a desktop session (they open real windows) and are
reported as failing when your session is locked; `KNOWN-ISSUES.md` lists them. [Building and testing](building-and-testing.md) has the whole story.

## Repository layout

| Path | What is there |
|---|---|
| `engine/foundation/` | the base: types, memory, containers, strings, math, logging, files, settings, debugging |
| `engine/platform/` | windows and input (Wayland, X11, Win32, Cocoa) |
| `engine/jobs/` | the job system |
| `engine/rhi/` | the graphics abstraction and its backends (`vulkan/`, `dx12/`, `metal/`, `null/`) |
| `engine/renderer/` | render graph, materials, passes |
| `engine/text/`, `engine/ui/`, `engine/ui_window/` | fonts and text layout, the widget toolkit, the glue between a window and the UI |
| `engine/ecs/` | the entity-component system |
| `game/` | demo and benchmark programs |
| `tools/` | offline tools and the project's own checks (`std_usage_check`, `module_deps_check`, ...) |
| `tests/` | the tests, mirroring `engine/` |
| `shaders/` | shader sources (Slang) and their committed compiled outputs |
| `third_party/` | vendored libraries |
| `docs/` | this documentation: `guide/` (concepts), `reference/` (generated API), `tutorials/`, `superpowers/specs/` (design records) |
| `plans/`, `PLAN.md`, `STATUS.md` | the roadmap, day-by-day plans and the current state |

Modules are arranged in **layers**; a module may use only the layers below it. `ARCHITECTURE.md` shows them and the `module_deps_check` test enforces them,
so you cannot accidentally make the renderer depend on the UI toolkit.

## House rules in five minutes

1. **Use the engine's types.** `Vector`, not `std::vector`; `u32`, not `unsigned int`; `Move`, not `std::move`. If a wrapper you need is missing, add it to
   the right module first. ([Standard types](foundation-types.md))
2. **Memory comes from allocators and containers.** No `new`, `delete` or `malloc`. ([Memory](foundation-memory.md), [Containers](foundation-containers.md))
3. **Files go through `io::File`, `io::Path` and `io::Directory`**, never `fopen` ([Files](io.md)). **Anything the user can configure is a setting**
   ([Settings](settings.md)), not an ad hoc flag.
4. **Ask `build_defs.h` for the platform**, never `_WIN32` or `__linux__` directly: write `#if ENGINE_PLATFORM_WINDOWS`.
5. **Warnings are errors.** Fix the code; do not silence the warning.

The rules are enforced by tests (the "gates"), so a violation fails the build or `ctest` with a message naming the file.

## Where to go next

Read the guide in this order (the [guide index](README.md) has the same list with one line per page):

1. [Standard types](foundation-types.md), [Memory](foundation-memory.md), [Containers](foundation-containers.md), [Strings](foundation-strings.md),
   [Logging and assertions](foundation-logging-and-assert.md): the foundation you use in every file.
2. [Building and testing](building-and-testing.md) and [Debugging](foundation-debugging.md): how to check and diagnose your work.
3. [Windowing and input](platform-windowing-and-input.md), then [Jobs](jobs.md): programs and parallel work.
4. [RHI](rhi.md), [Renderer](renderer.md), [Text](text.md), [UI](ui.md): drawing.
5. [ECS](ecs.md): the game-object model.

Then try a tutorial from [tutorials](../tutorials/README.md).
