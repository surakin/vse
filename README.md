# Visual Storm Engine (VSE)

Visual Storm Engine, VSE for short, is a game engine written from scratch in C++20. It opens its own windows (Wayland or X11 on Linux, Win32 on Windows, Cocoa on macOS), draws through its own graphics
abstraction (Vulkan, Direct3D 12, Metal), runs its work on its own job system, and brings its own ECS, physics, audio, networking, UI toolkit, asset pipeline and
scripting. Third-party code is kept to a short, justified list (FreeType, LuaJIT, SQLite, Catch2 and a few more), vendored under `third_party/` and built from source.

Two choices shape the code: **the C++ standard library is not used** (the engine has its own `Vector`, `String`, `Move`, `MemCopy`, ...), and **exceptions and RTTI are
off** (errors are values). The first page of the guide explains both.

> **Status:** under active development, one numbered phase at a time. [STATUS.md](STATUS.md) says where the project is, [KNOWN-ISSUES.md](KNOWN-ISSUES.md) lists what is
> broken or deferred, and [PLAN.md](PLAN.md) is the roadmap. Linux is the primary platform; Windows and macOS are supported (see [BUILD.md](BUILD.md)).

### Why "Visual Storm Engine"?

'Visual Storm Engine' was the name of the personal engine a dear friend of mine, Sergio Jesús de los Santos, was working on in his spare time. Sadly, he passed away at 35 (fuck cancer, yes, fuck you very much). Carrying this name is a humble homage to my friend. It is also a very cool name. He was THAT good.

## Build and run

You need a C++20 compiler (Clang is the reference), CMake 3.21 or newer, Ninja, and the platform's window libraries. [BUILD.md](BUILD.md) lists every
prerequisite per platform with the versions in use.

```bash
cmake --preset linux
cmake --build build
build/game/hello_window/hello_window
```

`hello_window` opens a window and prints keyboard and mouse input for six seconds. Windows and macOS use their own presets (`cmake --preset windows-msvc`,
`cmake --preset macos-clang-x86_64`); [Getting started](docs/guide/getting-started.md) has the commands.

Other demos live in `game/`: `rhi_demo` (a lit, textured cube), `renderer_demo` (a lit scene), `ui_demo` (a window built from the engine's widgets), `physics_demo`,
`net_demo`, `storage_demo`, `spatial_demo`, `script_demo`, `asset_demo`, `audio_bench` (an audio benchmark), and more. Each takes `--help`.

## Test

The suite is large, so tests run **by phase**:

```bash
scripts/test-phase.sh --affected      # what your change can have affected
scripts/test-phase.sh 27 --no-display # one phase, without tests that open a window
```

See [Building and testing](docs/guide/building-and-testing.md) for flavors (plain, asan, tsan), labels and how to write a test.

## Where to read next

| You want | Read |
|---|---|
| to go from a checkout to a running program, and learn the house rules | [Getting started](docs/guide/getting-started.md) |
| the guide pages in reading order | [docs/guide](docs/guide/README.md) |
| a page by what you want to do ("I want to draw text", "I want to save a world") | [I want to...](docs/guide/find-your-way.md) |
| what a word means (mint, flavor, gate, archetype, ...) | [Glossary](docs/guide/glossary.md) |
| step-by-step programs | [docs/tutorials](docs/tutorials/README.md) |
| the generated API reference | [docs/reference](docs/reference/README.md) |
| how the modules depend on each other | [ARCHITECTURE.md](ARCHITECTURE.md) |
| to use the engine from another program (a shared library with a C API) | [Engine as a library](docs/guide/engine-library.md) |
| the design reasoning behind a module | `docs/superpowers/specs/` |

## Repository layout

| Directory | Contents |
|---|---|
| `engine/` | the engine's modules (foundation, jobs, ecs, spatial, physics, audio, net, rhi, renderer, ui, asset, script, ...) |
| `game/` | demos and benchmarks |
| `tools/` | offline tools (the asset and shader minters, the documentation generator) and the checks that enforce the rules |
| `tests/` | the test suite, one directory per module |
| `shaders/` | Slang shader sources, compiled offline |
| `include/`, `abi/` | the public C API header and its ABI snapshot |
| `docs/` | the guide, tutorials and generated reference |
| `plans/` | the phase and day plans |
| `third_party/` | vendored libraries |

## Contributing

Read [CLAUDE.md](CLAUDE.md): it states the rules every change follows (warnings are errors, formatting by `scripts/format.sh`, no standard library, module layering
checked by a gate, a review at the end of each day). Install the pre-commit hook once per machine with `scripts/install-git-hooks.sh`.

## License

VSE is free software under the [GNU General Public License, version 3](LICENSE), or (at your option) any later version. The libraries vendored under `third_party/` keep their own licenses.
