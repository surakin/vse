# Linux

Status: written.

**Audience:** anyone who builds, runs or debugs the engine on Linux x86-64. Read [Getting started](getting-started.md) and
[Building and testing](building-and-testing.md) first: those pages describe Linux as the default, and this page collects what is specific to it in one place,
the way [Windows](windows.md) and [macOS](macos.md) do for their platforms.

Linux is the reference platform: every system is built and proven here first. It has one graphics backend (Vulkan), two window backends (Wayland, the
primary one, and X11 as the fallback) and three sanitizer flavors (plain, ASan with LeakSanitizer, and ThreadSanitizer, which only Linux has).

## At a glance

| I want to | Do this |
|---|---|
| build | `cmake --preset linux` then `cmake --build --preset linux` (tree `build`, Ninja, Clang) |
| build without LuaJIT's JIT | the `linux-nojit` preset (tree `build/linux-nojit`) |
| run tests | `scripts/test-phase.sh --affected` (add `--flavor asan` or `--flavor tsan`) |
| close a phase | `scripts/test-phase.sh --all --flavor all` |
| pick the window system | `--backend=wayland\|x11` (demos) or `ENGINE_WINDOW_BACKEND`; the default is Wayland when `$WAYLAND_DISPLAY` is set, else X11 |
| pick the graphics backend | `--rhi vulkan` (the default; `null` runs without a GPU); read at start, not changeable while running |
| pick the GPU | `--gpu <id, index or part of a name>`; `--list-gpus` lists them |
| turn on GPU validation | `--validation` (needs the Vulkan validation layers installed) |
| read a crash | `scripts/crash-report.sh <envelope> <program>` |
| open a core file | `scripts/open-core.sh <program>` (or `coredumpctl gdb <pid>`) |
| find user files | `$XDG_CONFIG_HOME/<app>` (settings), `$XDG_CACHE_HOME/<app>` (cache), `$XDG_STATE_HOME/<app>` (state, crashes) |

## Source of truth

- `BUILD.md` ("Linux"): the prerequisites with the version in use, what each is for, and the package names on Arch Linux.
- `CMakePresets.json`: the `linux` and `linux-nojit` presets.
- [Platform: windowing and input](platform-windowing-and-input.md): the Wayland and X11 backends.
- [Foundation: Debugging](foundation-debugging.md): the crash handler, minidumps and core dumps.

## Prerequisites

Install what `BUILD.md` lists as required: CMake 3.21 or newer, Clang (the compiler CMake picks first; GCC is the fallback and must build clean too),
Ninja, `pkg-config`, the headers of `libwayland-client`, `libxkbcommon`, `libX11`, `libXcursor`, `libXi` and `libXfixes`, `bash`, and at run time the
Vulkan loader (`libvulkan.so.1`) with a Vulkan 1.3 driver. For the checks: the Vulkan validation layers, `slangc` exactly v2026.18.2 (only if you change
shaders), `spirv-val`, `lldb` and `gdb`, `binutils`, a Wayland compositor and an X server (or Xwayland) for the window tests, and `clang-format` 18 or
newer to commit. The PipeWire headers are optional: without them audio uses ALSA.

Nothing else is linked: the engine takes no third-party convenience library, and the libraries above are the display systems' own client libraries.

## Building

```
cmake --preset linux
cmake --build --preset linux
```

The preset uses Ninja and a Debug build in `build/linux/`. The unity build is on by default; `-DENGINE_UNITY_BUILD=OFF` must build clean too. Warnings are
errors, and both compilers have to pass: Clang reports some things GCC does not, and GCC reports others (`-Wformat-truncation`,
`-Wsubobject-linkage`). To build with GCC, configure a tree with `-DCMAKE_CXX_COMPILER=g++` (or without Clang installed). The test suite also runs
cross-target syntax checks of the Windows and macOS code (the `*_cross_syntax_*` tests), so most platform mistakes show up here.

A shader checkout without `slangc` builds from the committed minted outputs; with the wrong `slangc` version the minted-output checks fail, because a
different version produces different bytes.

## Running tests

```
scripts/test-phase.sh --start-day            # records where the day starts
scripts/test-phase.sh --affected             # the current phase plus the phases the change touched
scripts/test-phase.sh --affected --flavor asan
scripts/test-phase.sh --affected --flavor tsan
scripts/test-phase.sh --all --flavor all     # the whole suite in every flavor
ctest --test-dir build -L docs
```

All three flavors run on Linux (`engine_tests_plain`, `_asan`, `_tsan`). AddressSanitizer includes LeakSanitizer here (macOS has none). The last line the
script prints says what ran (`ran X of Y tests ...`); quote it, and never call a reduced run "all tests pass". The details are in
[Building and testing](building-and-testing.md), "Test phases and scoped runs".

- **Window tests need a live, unlocked session.** A window never appears while the screen is locked, idle or switched to another user, so those tests fail
  for a reason that is not a defect; `KNOWN-ISSUES.md` lists them. Both window systems are exercised, so a Wayland compositor and an X server (or
  Xwayland) must be reachable.
- **GPU tests need a Vulkan device.** Without one the Vulkan tests fail at device creation (`vulkaninfo` finds no GPU). The non-GPU tests do not
  depend on it, and `--rhi null` runs the renderer without one.
- **ThreadSanitizer and the NVIDIA driver.** The proprietary driver's own threads crash inside ThreadSanitizer, so the tsan flavor of the RHI tests
  runs only the tests that need no GPU (`rhi_tests_tsan`).
- **Tests keep their files in the build tree** (`test-tmp/`), and the runner points `ENGINE_RHI_CACHE_DIR` there, so a test never writes to your home
  directory. When you run `rhi_tests` or `renderer_tests` by hand, set it yourself.

## Running the demos

```
build/linux/game/renderer_demo/renderer_demo
build/linux/game/renderer_demo/renderer_demo --backend=x11 --validation
build/linux/game/rhi_demo/rhi_demo --list-gpus
```

Programs that open a window choose Wayland when `$WAYLAND_DISPLAY` is set and X11 otherwise. A forced backend never falls back: `--backend=wayland`
on a machine with no compositor fails and says so. Under a Wayland session, `--backend=x11` runs through Xwayland, which is the way to test the fallback.
The window's size is a request on Wayland (the compositor may choose another), and the pointer lock, cursor shapes and clipboard need the protocols
listed in [Platform: windowing and input](platform-windowing-and-input.md).

## The display backends in short

| | Wayland | X11 |
|---|---|---|
| Role | the primary backend | the fallback, for systems that have not moved |
| Built on | `libwayland-client` and `libxkbcommon`, with the protocol code vendored under `engine/platform/linux/wayland/generated/` | `libX11`, with `libXcursor`, `libXi` and `libXfixes` |
| Key repeat | the backend synthesizes it | the server's detectable auto-repeat |
| Clipboard | a selection is accepted only after a recent key or button press in your window | owner notifications through XFixes |
| Pointer lock | `zwp_pointer_constraints_v1` with `zwp_relative_pointer_v1` | a pointer grab with XInput 2 raw motion |

`wayland-scanner` and `wayland-protocols` are needed only to regenerate the vendored protocol code.

## Validation and the debug layer

`--validation` (`renderer.validation`, `renderer.sync_validation`) turns on the Vulkan validation layers with synchronization validation. Messages reach
the `LogRhi` channel and count in the device's counters; the GPU tests fail on any (`BackendValidation`). The layers come from the Vulkan SDK or your distribution's package; without them there is nothing to report. [RHI](rhi.md) describes the validation capture hook.

## Audio

`CreateDevice(DeviceKind::Platform)` tries **PipeWire** first (its client library is loaded when the device opens, so a binary built with the headers
still runs where the library is missing) and then **ALSA** through raw `ioctl`s on `/dev/snd` (no `libasound`). `DeviceKind::PipeWire` and
`DeviceKind::Alsa` pick one. `tools/audio_probe` (`list`, `play`) plays a quiet tone through the real backend and prints what was granted. See
[Audio](audio.md), "The Linux backend".

## Crash dumps and symbols

The crash handler (sentry-native with Breakpad, no upload) is on by default in Debug builds and off in Release (`--crash-handler=on|off`). A crash leaves
an envelope with the minidump and the engine's crash report in `$XDG_STATE_HOME/<app>/crashes` (else `~/.local/state/<app>/crashes`). Then:

```
scripts/split-debug.sh <program>                              # keep the debug info of a release build
scripts/store-symbols.sh <program> symbols                    # file it under the build id
scripts/crash-report.sh <envelope> <program> [symbols]        # every thread, with function, file and line
```

`crash-report.sh` refuses a program whose build id differs from the dump's. For heap or object state ask for a core: `post_mortem_dumps` in the crash
handler's options raises the core size limit, and the report says where the core goes. Where it goes is the system's choice (`/proc/sys/kernel/core_pattern`):
with `systemd-coredump` use `coredumpctl list` and `coredumpctl gdb <pid>`, or `scripts/find-core.sh` and `scripts/open-core.sh <program>`. A dump and a core
hold process memory: keep them private. See [Foundation: Debugging](foundation-debugging.md), "Core dumps on Linux".

## Where things live

| What | Where |
|---|---|
| The user's settings | `$XDG_CONFIG_HOME/<app>/settings.cfg` (else `~/.config/<app>/settings.cfg`); `--save-settings` writes it |
| Data, cache and state | `$XDG_DATA_HOME`, `$XDG_CACHE_HOME` and `$XDG_STATE_HOME` under `<app>`, each when it is an absolute path, else under `$HOME` (`.local/share`, `.cache`, `.local/state`) |
| The pipeline cache | `$ENGINE_RHI_CACHE_DIR`, else `<cache>/<app>` |
| Crash dumps | `<state>/<app>/crashes` |
| The file layer | `engine/foundation/io/linux/` (`docs/guide/io.md`) |
| The socket backend | `engine/net/linux/` and `engine/net/posix/` (`docs/guide/net.md`) |
| The window backends | `engine/platform/linux/wayland/` and `engine/platform/linux/x11/` |
| The Vulkan backend | `engine/rhi/vulkan/` |

## Common mistakes

- **Expecting a window from a locked or remote session.** No window appears, and the window tests fail; unlock the session or skip the display group.
- **Expecting an X11 window to reach the compositor's features.** A program under Xwayland has X11's capabilities, not Wayland's.
- **Forcing `--backend=wayland` without a compositor.** It fails by design instead of opening an X11 window.
- **Expecting a run-time switch of the graphics backend.** `--rhi` is read at start; only `--gpu` can change while running.
- **Running GPU tests by hand without `ENGINE_RHI_CACHE_DIR`:** the pipeline cache lands in your home directory.
- **Reading a crash without the symbols of that build.** File them with `store-symbols.sh` for every build you ship; `crash-report.sh` refuses a different build.
- **Re-minting shaders with another `slangc` version.** Use v2026.18.2, or leave the committed outputs alone.
- **Calling the C library or the operating system around the wrappers.** Use `os::`, `io::` and `net::`; the gates (`file_access_check`, `net_access_check`,
  `std_usage_check`) refuse the rest.

## See also

[Building and testing](building-and-testing.md), [Foundation: Debugging](foundation-debugging.md), [Porting the debugging facilities](porting-debugging.md),
[Platform: windowing and input](platform-windowing-and-input.md), [RHI](rhi.md), [Audio](audio.md), [io](io.md), [Settings](settings.md),
[Windows](windows.md), [macOS](macos.md).

## Known gaps

`KNOWN-ISSUES.md` has the current list. The large ones: no IME composition on either window backend; the ThreadSanitizer limit with
the NVIDIA driver above; and a few interactive checks (dragging, real mouse input on both window systems) that have not been done by a person.
