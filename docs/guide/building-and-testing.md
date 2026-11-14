# Building and Testing

Status: written (Phases 0-17; Windows from Phase 13; macOS from Phase 21).

**Audience:** everyone who changes the engine. [Getting started](getting-started.md) has the first build; this page is what you need to *check your work*:
the flavors, how to choose and run tests, what each sanitizer catches, and how to write a test.

## Build targets and flavors

One configure produces three *flavors* of every library and test program, so the same code is checked in three ways:

| Flavor | Target names | Compiled with | Catches |
|---|---|---|---|
| plain | `ecs_tests`, `foundation_base`, ... | the normal optimizer and warnings | wrong behavior; this is what ships |
| ASan + UBSan | `*_asan` | AddressSanitizer and UndefinedBehaviorSanitizer | out-of-bounds, use-after-free, leaks, overflow, misaligned and null access |
| TSan | `*_tsan` | ThreadSanitizer (Linux and macOS; not Windows) | data races |

Useful targets: `cmake --build build` builds everything; `cmake --build build --target ecs_tests` builds one test program; `--target docs` regenerates the reference
pages. Production code is compiled with `-fno-exceptions -fno-rtti` and **warnings as errors**; tests are not (Catch2 uses exceptions), but still build warning-free.
The Windows build has plain and ASan only (MSVC has no ThreadSanitizer).

### Libraries and layers

The foundation is built as one static library per layer of `ARCHITECTURE.md` (section 1), each linking only the ones below it, and the windowing code is a
library of its own. Link the **lowest** one that has what you use; a file that reaches up fails to link in a test of its own layer, which is how the layering
is proved (the include rules are checked by the `module_deps_check` gate).

| Library | Layers | Link it for | Notes |
|---|---|---|---|
| `foundation_base` | 0-5 | `types.h`, `string_view.h`, memory, log, containers, strings, math, `os/` (clocks, environment, dynamic libraries, command line, session) | Needs no other library; `${CMAKE_DL_LIBS}` is PUBLIC. The sanitizer flags of the `_asan`/`_tsan` flavors are PUBLIC here, so every layer above inherits them. |
| `foundation_io` | 6 | `io::File`, `io::Path`, `io::Directory` | Links `foundation_base`. |
| `foundation_debug` | 7 | asserts with traces, crash handling, `StartProgramDebugging`, death tests | Links `foundation_io`; the only library that links libbacktrace and sentry-native (PRIVATE). Without it an assert prints one line and aborts (the hooks of `log/diagnostics_hooks.h` are not installed); a program or test that wants traces calls `debug::InitDebugFacilities`. |
| `foundation_settings` | 8 | `settings::Registry`, `Setting<T>`, `Settings` | Links `foundation_debug`. |
| `foundation` | 0-8 | everything above | An `INTERFACE` aggregate of the four; what the programs, `jobs` and `rhi` link. |
| `platform` | 9 | `platform/window.h`, `input.h`, `window_host.h` | Links `foundation_debug` (crash state); the only library that links the display libraries (Wayland, xkbcommon and X11 on Linux; user32 on Windows; AppKit and QuartzCore on macOS). |

Each library has an `_asan` and a `_tsan` variant (`foundation_io_asan`, `platform_tsan`, ...) and a test links the variant of its own flavor. Tests: `tests/types`,
`tests/foundation_sanitized` and the core files of `tests/foundation` (the executable `foundation_base_tests`) link `foundation_base`; `tests/io` links
`foundation_io`; `tests/debug` links `foundation_debug`; `tests/settings` links `foundation_settings`; the file and window tests of `tests/foundation`
(`foundation_tests`) link `platform` and `foundation`.

### Running subsets with labels and Catch2 tags

Each test has labels (its phase, its flavor, and groups such as `gpu`, `display`, `docs`, `crash`), and Catch2 cases have tags (`[ecs]`, `[world]`). Use whichever
narrows the run to what you changed:

```bash
ctest --test-dir build -L '^phase17$'            # every test of one phase (the anchors stop phase1 matching phase17)
ctest --test-dir build -L '^phase4$' -L gpu      # labels combine: both
ctest --test-dir build -R 'World:'               # by name (a regular expression)
build/tests/ecs/ecs_tests "[world]"              # one executable, one Catch2 tag
build/tests/ecs/ecs_tests "World: create*"       # one executable, by name pattern
build/tests/ecs/ecs_tests --list-tests           # what is in it
```

Running the test executable directly is the fastest loop while you work; use `scripts/test-phase.sh` (below) before you commit.

### Sanitizers: what each catches and when to use it

- **ASan/UBSan** (`--flavor asan`): run it on **any change that touches memory, lifetimes, containers or raw pointers**. It turns silent corruption into an immediate report
  naming the line. UBSan also reports signed overflow, misaligned access, bad shifts and null dereference.
- **TSan** (`--flavor tsan`): run it on **any change that touches threads, atomics, locks, the job system or anything shared**. It reports a data race even when the
  program happened to work. A deliberate-race canary test proves TSan is really active.
- **Plain** is always run; it is the only flavor that exercises the optimized code.

A failure in a sanitizer flavor is a real bug until proven otherwise; do not suppress it. Sanitizer runs are slower, so run them on the part of the suite you changed
(`scripts/test-phase.sh --affected --flavor asan`), and the whole suite in every flavor at the end of a phase.


### Writing a test (Catch2 conventions in this repo)

Tests use the engine's own types, like the code they test: `String`, `StringView`, `Vector`, `UniquePtr`, `Span`, `byte`, `u8`..`u128`
(see "Which type do I use?" in `docs/guide/foundation-types.md`). A test never needs `std::string`, `std::vector` or `std::function`:

- `tests/support/test_text.h` has `Text` (an owned heap string), `MakeText`, `Repeated`, `Format`, `Concat`, `Chars` (a mutable buffer),
  and two small recorders, `TextList` (ordered) and `TextSet` (sorted, no repeats), with `SameAs({...})` to compare against a list.
- `tests/support/catch_engine_types.h` teaches Catch2 to print `StringView`, `String`, `io::Path`, `io::Status`/`io::Error`, `u128`,
  `i128` and `Span`, so `CHECK(path == other)` and `CHECK(text == "abc")` show readable values. `String` compares with a `StringView`
  or a string literal directly. `INFO` takes a stream expression, so pass `Catch::Detail::stringify(value)` for an engine type.
- Pass a callable with a template parameter or a function pointer plus a `void*`, not `std::function`.
- Use the wrappers of `sync/`: `Thread`, `Mutex`, `ConditionVariable` and `Atomic<T>` (Phase 16; [Synchronization](foundation-sync.md)), not the `std::` ones.
- Where Catch2 itself needs `std::` (the text it returns from a printer, `GENERATE` ranges), the use stays local and has a comment.

The `std_usage_check` gate (ctest `std_usage_check`; Phase 16 Day 220, it replaced `std_container_check`) fails on any `#include` of a standard header, any `std::` name, any unqualified C library call (`strlen(`, `memcpy(`, `printf(`, ...) and any raw `unsigned char`/`uint8_t` spelling in `engine/`, `game/`, `tools/` and `tests/`. Its allow list (`tools/std_usage_check/allowlist.txt`) has one line per exception, `path|headers|reason`: the wrapper modules (`lang`, `sync`, `cstr`, the `os` console, error-number and signal headers, `jobs/coroutine.h`) with the standard headers each may include, the documented boundaries (`types.h`, the nlohmann/json files, `tests/support/catch_engine_types.h`) and the oracle tests of the wrappers. An entry that covers no use, or lists a header nobody includes, is stale and fails, so the list only shrinks. A test never needs the standard library: use the wrappers (Windows-only files are listed until Phase 16 Day 219).

The `cmdline_access_check` gate (ctest `cmdline_access_check`) fails on `argv[i]` in `engine/`, `game/`, `tools/` and `tests/`: a program reads its command line with `os::CommandLine`, which accepts `--name value` and `--name=value` for the options it declares and refuses unknown ones (`docs/guide/platform-os.md`). Its allow list (`tools/cmdline_access_check/allowlist.txt`, `path|reason`) names the implementers of the command line, the settings and debugging layers that read the raw arguments before anything exists, the code that builds a child process's `argv` and the test programs that echo a raw one; a stale entry fails.

### Test phases and scoped runs

Every test carries exactly one phase label: `phase0` (foundation), `phase1` (Linux platform), `phase2` (jobs),
`phase3` (documentation tooling), `phase4` (RHI), `phase5` (renderer), and so on for later phases. Select a phase
with `ctest -L '^phase5$'`; labels combine as in `ctest -L '^phase4$' -L gpu` (both), `-L 'phase4|phase5'` (either)
and `-LE gpu` (exclude). The anchors keep `phase2` from matching `phase3`.

`tests/phases.txt` is the only place a test's phase is decided: a line per source file or directory (`tests/rhi/cube_test.cpp  3`),
or `test:<name>` for a test that is not a Catch2 case. Add a line when you add a test file; the build stops,
naming the file, if a Catch2 test has none. A test that is not a Catch2 case (`add_test`) gets its label from
`engine_label_tests(<name>)`, called after `add_test`. `test_phase_labels` fails, naming the tests, if any test in the
build has no phase label or more than one. Catch2 tests are registered through `engine_discover_tests`
(`cmake/EngineTestPhases.cmake`), a wrapper of `catch_discover_tests` that adds the label.

Tests per phase and flavor on 2026-11-06 (`plain` is neither `asan` nor `tsan`; recount with
`ctest -N -L ^phase5$ -L ^asan$`):

| Phase | plain | ASan | TSan |
|---|---|---|---|
| 0 foundation | 87 | - | - |
| 1 Linux platform | 63 | - | - |
| 2 jobs | 77 | 77 | 78 |
| 3 documentation tooling | 172 | 168 | - |
| 4 RHI | 226 | 211 | 143 |
| 5 renderer | 211 | 178 | 148 |
| 6 test scoping | 35 | - | - |
| 7 debugging facilities | 193 | 154 | 154 |
| 8 RHI reset | 132 | 118 | 104 |
| 9 file handling | 64 | 53 | 53 |
| 10 standard types and containers | 61 | 26 | 26 |
| 11 settings system | 164 | 127 | 127 |
| 12 module layering | 19 | - | - |
| 13 Windows port | 21 on Windows (2026-11-07, Day 150; none on Linux) | 16 on Windows | - |

The `flavors` lines in `tests/phases.txt` state which flavors each phase has, and `test_phase_labels` fails if one
appears or disappears.

**Tests that crash on purpose.** The Phase 7 suite (`tests/debug`) makes processes fault deliberately, and the kernel hands each
fault to `systemd-coredump`, which makes a desktop crash reporter (Plasma's systray) raise a notification. Those tests carry the
ctest label `crash` and `scripts/test-phase.sh` leaves them out of every run but `--all` (which closes a phase), unless you pass
`--crashes`. The summary line says so (`crash tests: excluded`), so a reduced run is never mistaken for a full one. The script also
sets a soft core limit of 0 for the tests it starts. Plain `ctest` runs them.

#### The runner

`scripts/test-phase.sh` builds, selects tests by label and runs them. It is how day-to-day work runs tests:

```
scripts/test-phase.sh                  # the phase in CURRENT_PHASE, plain flavor, without the window tests
scripts/test-phase.sh --affected       # that phase plus the phases of every module touched since the phase started
scripts/test-phase.sh 3 --flavor asan  # all of phase 4 under AddressSanitizer
scripts/test-phase.sh --all --flavor all   # the whole suite in every flavor: the closing run of a phase
scripts/test-phase.sh --affected --dry-run # show the selection only
```

Every run ends with how much it ran, for example `ran 35 of 1874 tests (phases: 5; flavors: plain; display: excluded)`,
and says so loudly when it left tests out. Quote that line when reporting a reduced run; "all tests pass" means the
`--all --flavor all` line. `--help` lists the options. A scoped run with `--flavor asan` or `tsan` needs ctest 3.29 or newer (it combines two label filters); the script stops with a message on an older one.

`--affected` reads the paths changed since the last commit that changed `CURRENT_PHASE` (the commits of the phase and the
working tree, plus new files) and adds the phases listed for each directory in `tests/module_map.txt`: `engine/rhi` selects
the RHI and renderer tests and the docs checks, `engine/foundation` almost everything. It runs everything when the root or a module's
`CMakeLists.txt`, `cmake/`, `third_party/`, a header under `engine/foundation`, `tests/phases.txt` or `tests/module_map.txt` changed,
and it says so, with the reasons, before the build starts. A `CMakeLists.txt`, `tests/phases.txt` or `tests/module_map.txt` whose change
only **adds** lines (a new library, a new test file, a new map line; in the root `CMakeLists.txt` only `add_subdirectory` calls and
comments) does not widen the run, so a day that adds a module or a test costs its own phases, not the suite. Editing or removing an
existing line there still runs everything. The `CMakeLists.txt` of a directory under `tests/`, `tools/` or `game/` never widens it (the
map line of the directory names the phases; `tools/docgen`'s goes to phase 3). Because the diff reaches back to the start of the
phase, one non-additive edit to a module's CMake widens every later day of the phase too: keep such edits to the day that needs them.

**A changed path that is in no line of the module map is an error**, not a reason to run everything: the script stops before it
builds, names the paths and asks for the map line (`path  phases`, the phases whose tests exercise the directory). The ctest
`test_phase_script_module_map_paths` checks the other direction as well: a source directory directly under `engine/`, `game/`,
`tools/` or `tests/` that holds a file and has no line in the map fails it, so a new module is mapped the day it is created.

#### What it saves

Measured on 2026-11-06 (Ryzen 9 7950X3D, 16 threads, `-j16`; the suite is 1874 tests): the whole suite in plain, ASan
and TSan takes 66 s (341 s with `-j1`). One phase, wall time, plain / all flavors: phase 0 0.1 / 0.1 s, phase 1 8.8 / 8.8 s
(window tests), phase 2 1.8 / 9.3 s, phase 3 1.6 / 1.9 s, phase 4 17 / 47 s, phase 5 13 / 44 s, phase 6 1.4 / 1.4 s. A change
in `engine/foundation` selects every phase, so `--affected` runs 871 tests in 38 s plain; one in `engine/rhi` and
`engine/renderer` selects 638 tests, 26 s. The saving is large for work inside one phase and small for work in the
foundation, which everything links.

#### What a scoped run can miss

A behavior change can reach a finished phase through a dependency nobody edited (the renderer starts calling an RHI
function differently, and only an RHI test notices). `--affected` covers the modules a change touches, and the module map
lists everything that links a directory, but it cannot see this, so the whole suite in all three flavors closes every
phase. The same goes for a changed build flag, which is why build files force everything. A shared test helper counts as
touching its module's tests. The window tests (`display`) are skipped by default in `--current`/`--affected` unless phase 1
is selected or `engine/foundation` changed; `--display` includes them.

#### Side phases

A phase the user runs beside the main line on another machine is a **side phase** (the user's rule, confirmed 2026-11-11;
today Phase 21, the macOS port, on the Mac). It does not change `CURRENT_PHASE`; its tests carry its own label (`phase21`), and the `SIDE_PHASES` file lists
it (one number per line). `scripts/test-phase.sh --affected` adds every side phase the build has tests of, and the summary marks it:
`ran 29 of 494 tests (phases: 19 26 (side); ...)` on the Mac, while the Linux machine, which builds no phase-26 tests, leaves it out. `--current`
and a phase number never add it; `scripts/test-phase.sh 26` runs it alone. `test_phase_labels` fails on any phase of `tests/phases.txt` above
`CURRENT_PHASE` that `SIDE_PHASES` does not list, so a mistyped phase number cannot hide a test. The side phase's last day removes its line. See CLAUDE.md ("Tests run by phase") and `docs/superpowers/specs/2026-11-11-macos-port-design.md`, section 6.

#### Adding a test

Add a line for the new source file to `tests/phases.txt` (`tests/rhi/vulkan_foo_test.cpp  4`); the build stops, naming the
file, if a Catch2 test has none. A test that is not a Catch2 case gets a `test:<name>` line and an
`engine_label_tests(<name>)` call after its `add_test`. `ctest -R test_phase` checks the rules (labels, flavors, runner).
Run one flavor with `--flavor asan|tsan|plain`; plain means neither sanitizer label.

Window tests also carry the label `display` (they share the one desktop and run one at a time).

### Where tests keep their files

What a test makes while it runs goes under one folder of the build tree, `test-tmp/` (`ENGINE_TEST_TMP_DIR` in the root `CMakeLists.txt`), safe to
delete at any time: `profile/` (the demo tests' `APPDATA` and `LOCALAPPDATA`, or `XDG_CONFIG_HOME` and `XDG_STATE_HOME` on Linux, so your own settings are never read or written),
`pipeline-cache/<test>/` (each GPU test's `ENGINE_RHI_CACHE_DIR`), `screenshots/` (what the demos' `--screenshot` tests write), `cli/<demo>_<hash>/` (the demos' command-line checks; the suffix is the first 8 hex
digits of an MD5 of what the check runs, so parallel checks never share a folder and a rerun reuses its own), `crash_extract/`,
`renderer_demo_assets_check/` and, on Windows, `windows_discovery-<compiler>/`. A new test that needs a working folder puts it there.

### Window tests and the session

A window never appears while the screen is locked, idle or switched to another user, so a window test run then fails for a reason that is not a defect. A test that opens a real window starts with
`SKIP_IF_NO_DISPLAY_SESSION()` (`tests/support/display_session.h`): it asks `os::QuerySessionState()` (`engine/foundation/os/session.h`) and skips with the state as the reason unless the session is `Active`. An `Unknown`
state (no session manager, a container) does not skip, so such a machine still runs and can fail. Tag the test `[x11]` or `[wayland]` so it also holds the ctest `display` resource. On Linux the query asks systemd-logind
through `libsystemd.so.0`, loaded at run time; `os/windows/SESSION.md` and `os/macos/SESSION.md` say what the other ports call.

### GPU tests and validation layers

Tests tagged `[gpu]` (CTest label `gpu`) need a GPU backend: a Vulkan driver on Linux and Windows, Metal on macOS (where the
Vulkan-only tests skip). Without one they skip; set
`ENGINE_RHI_REQUIRE_GPU=1` (do this on a development machine) to make a skip a failure so a broken driver cannot
hide. They run under `gpu_test::GpuFixture`, which enables the Khronos validation layer with synchronization
validation and fails the test on any validation error. `ENGINE_RHI_VALIDATION_FATAL=1` aborts on the first error
instead, for debugging a single test, and `ENGINE_RHI_VALIDATION=0` turns validation off for `Auto` instances. See
the validation section of the [RHI guide](rhi.md).

### Testing a reset

Phase 8 made replacing the device a normal operation, so a test of anything that owns device objects should survive one. Write it like
this:

1. Use `tests/renderer/reset_rig.h`: a `Rig` holds a `DeviceHost`, a `DeviceResources` registry, an offscreen target (itself a registered
   owner) and the deferred demo frame and scene, all registered. `rig.Create(rhi::Backend::Null)` needs no GPU; for a `[gpu]` test
   run the body once per backend with `FOR_EACH_GPU_BACKEND(backend)` (`tests/rhi/backend_fixture.h`) and pass `backend` (it skips
   without a device). `rig.Read(&image)` reads the target into a `gpu_test::Image`.
2. Make the scene or owner under test, render, then `rig.ResetDevice(sync_validation)`, which flips a device-class setting and so replaces
   the device through `Renderer::ApplySettings`; render again.
3. Assert with the counts, not with timing: `device->Quiesce()` then `device->Resources()` gives the live objects per kind, which must equal
   those before the reset; `rig.host.LeakedObjects()` is 0 when every owner released before the device was destroyed; compare pictures
   byte for byte on one adapter.
4. To lose the device instead, set `info.faults` (`lose_device_at_frame`, `_at_submit`, `_at_present`) on the `create_info` and limit the
   strikes with `strikes_left` so the recovery's new device is not lost again.

The phase's tests: `tests/renderer/reset_soak_test.cpp` (100 resets on the null backend, resets during a 50-frame sequence, what each
cost class leaves alone, a reset with the job scheduler running), `tests/rhi/reset_golden_gpu_test.cpp` (the picture before and after a
reset is byte-identical and matches the blessed golden; 100 resets on the real device, 20 under the sanitizers, with memory and descriptor
drift reported), `tests/renderer/device_loss_recovery_test.cpp`, `tests/renderer/apply_settings_test.cpp`,
`tests/renderer/recreatable_renderer_test.cpp`, `tests/renderer/device_resources_test.cpp`. The GPU ones carry the `gpu` tag and run in the
plain and ASan flavors, not in TSan. **The cross-adapter comparison** (a discrete against an integrated GPU, golden tolerance only) and
**switching adapters on real hardware** need a machine with two GPUs; the development machine has one, so those checks are not written as
skipped tests but filed in `KNOWN-ISSUES.md`.

**Measuring a leak across resets.** The memory and handle checks read [[os::ResidentMemoryKiB]] and [[os::OpenHandleCount]] (the resident set
and the file descriptors on Linux, the working set and the kernel handles on Windows). Compare the lowest reading of an early stretch of resets
with the lowest of a late one, not two single readings: on Windows (AMD Radeon 860M, Phase 13 Day 161) the process holds one to four more 64 MiB
device-memory blocks after some resets or losses, while the device's own count (`ResourceCounts::device_memory_bytes`) does not change, and
gives them back later. The checks allow four such blocks on Windows.

### The RHI boundary gate

`rhi_boundary_check` (label `boundary`, no GPU needed) enforces the rule that only `engine/rhi/vulkan/` may name the
Vulkan API. It is a CMake script, `tools/rhi_boundary_check/check.cmake`, that scans every `.h`, `.hpp`, `.cpp`, `.cc`,
`.c`, `.inl` and `.mm` file under `engine/`, `game/`, `tools/` and `tests/renderer/` for `vk*`/`Vk*`/`VK_*`/`PFN_vk*` identifiers and
`vulkan` includes (comments do not count), and checks that the backend-agnostic `engine/rhi/*.h` headers include
no Vulkan, Wayland, X11 or backend-directory header. To run it by hand:

```
cmake -DROOT=$PWD -DALLOWLIST=tools/rhi_boundary_check/allowlist.txt -P tools/rhi_boundary_check/check.cmake
```

A file that genuinely needs the exemption goes in `tools/rhi_boundary_check/allowlist.txt` as `path|reason`. An entry
without a reason fails, and so does an entry whose file no longer needs it or no longer exists, so the list cannot rot.
The gate tests itself: `rhi_boundary_check_planted*` run it over `tools/rhi_boundary_check/fixtures/bad` (an include, identifiers, one after a block comment, a backend include in an agnostic header, a Vulkan include in `engine/renderer/` and an identifier in `tests/renderer/`) and expect exactly
those seven reports; the `_clean` case (block, trailing and inline comments naming Vulkan types) and the `_stale` and
`_noreason` cases cover the passing tree and the allow-list rules. Names inside string literals still count, because
`dlsym("vkFoo")` is Vulkan API use too.

### Failure injection, sanitizers and the time budget

`tests/rhi/vulkan_fault_test.cpp` (tag `[fault]`) drives the error paths with `rhi::FaultInjection`: a device lost at a
chosen frame, device creation and later resource creation failing for lack of memory, and a 200-step resize storm with
zero and one-pixel sizes on both Wayland and X11. Each ends with the device destroyed, the live-object counts at zero
and the validation layer silent. `rhi_demo_*` ctests (label `demo`) run the demo itself with `--inject-device-loss` and
`--inject-oom`, and with `ENGINE_VULKAN_LIBRARY=/nonexistent`, and check the exit code and message; those that need a GPU
skip without one.

`rhi_tests_asan` (label `asan`) includes the GPU tests when a device exists, except the X11 presentation test; the NVIDIA
driver's Xlib path leaks inside a library it unloads before exit. `rhi_tests_tsan` (label `tsan`) covers the GPU-free
multithreaded tests, since the NVIDIA driver's own threads crash inside ThreadSanitizer. Leaks that belong to the driver
or to a test that leaks on purpose are suppressed in `tests/rhi/lsan.supp`, which gives a reason for every entry; the test
binaries load it through `__lsan_default_options`, so direct runs honour it too. A leak in engine code is never covered:
add a suppression only with a reason and a reviewer.

The asan flavor builds with `-fno-sanitize=vptr` (the engine libraries are `-fno-rtti`; GCC's vptr check would reference
typeinfo they do not define), and the asan test binaries keep the Vulkan driver loaded after the instance is destroyed
(an environment setting made in `lsan_options.cpp`), because a leak in an unloaded driver is reported without a module
name and cannot be suppressed.

Time budget for the RHI suite (`ctest -L rhi -j4`): about 15 seconds with a GPU, about one second on a machine without
Vulkan, where every `[gpu]` test skips (check with `ENGINE_VULKAN_LIBRARY=/nonexistent ctest -L rhi`). A GPU test that takes
more than a few seconds belongs in a stress label, not in the default run.

### GPU tests per backend

The golden-image tests and the checks around them (`golden_test.cpp`, `compute_test.cpp`, `cube_test.cpp`, `textured_cube_test.cpp`,
`lit_cube_test.cpp` in `tests/rhi/`) run once per GPU backend the machine has: Vulkan on Linux, Vulkan and Direct3D 12 on Windows
(Phase 13 Day 170), Metal on macOS (Phase 21 Day 306). They use only the RHI interface, through `tests/rhi/backend_fixture.h`:

```
FOR_EACH_GPU_BACKEND(backend) {
    gpu_test::BackendValidation validation(backend);           // before the device: fails on its validation errors
    rhi::DevicePtr device = gpu_test::CreateTestDevice(backend); // validated; skips (or fails) when it cannot be made
    ...
}
```

Each backend is a Catch2 section named after it (`vulkan`, `dx12`, `metal`), so a failure names the backend, and one backend runs alone with
`-c`: `rhi_tests "[golden]" -c dx12`. `gpu_test::GpuBackends` lists the registered backends but null that are available; the test "The
per-backend GPU tests run on every GPU backend this machine has" checks the list against the registry (that Windows has Direct3D 12, and that only macOS has Metal),
so a test cannot quietly run one backend. Shaders come from the minted blob sets (`CreateShaderFromSet`), so each backend gets its own
format. `BackendValidation` counts Vulkan's validation errors, and Direct3D 12's debug-layer errors and warnings; Metal's validation stops the process at the first error. Tests of one backend's
internals (the loader, the instance, memory policy, SPIR-V checks) stay in the `vulkan_*`, `dx12_*` and `tests/rhi/macos/metal_*` files.

- **Another adapter:** `ENGINE_RHI_GPU` picks it on either backend (an id from `--list-gpus`, an index, or part of the name):
  `ENGINE_RHI_GPU="Basic Render" rhi_tests "[golden]" -c dx12` runs the goldens on WARP, Direct3D 12's software rasterizer.
- **GPU-based validation** on Direct3D 12: `ENGINE_RHI_VALIDATION_SYNC=1`.
- **How far from the golden:** `ENGINE_RHI_GOLDEN_REPORT=1` makes each golden check warn with its largest difference from the golden
  and from the other backend's image of the same test, which tells a driver's differences from a backend's.
- **Regenerating** with `ENGINE_RHI_UPDATE_GOLDEN=1` writes only from the Vulkan section: the goldens were made through it, and the
  other backends are checked against them.

### Golden-image rendering tests

Rendering tests draw offscreen, read the pixels back and compare them with a golden image, so no window or
person is needed. `tests/rhi/render_target.h` is the offscreen target (RGBA8 color texture with `Readback`);
`tests/rhi/image_compare.{h,cpp}` compares and reads and writes the goldens, which are binary PPM files in
`tests/rhi/golden/` (64x64 for the triangle).

- **Tolerance.** An interior pixel may differ from the golden by `tolerance` (2) per channel: GPUs agree on
  interpolated colors to within rounding. A pixel is an *edge pixel* when the golden steps by more than `edge_step`
  (16) to a neighbour (a smooth gradient ramps by about 5 per pixel, so it is not an edge); edge pixels are compared
  with `edge_tolerance` (64), and up to `max_edge_miss` (25%) of them may exceed even that, because vendors differ in
  the rasterization rules for pixel centers on an edge. Nothing else may differ.
- **Vendor differences in shading.** The goldens come from an NVIDIA GPU. The lit and textured cube tests also let 0.5%
  of interior pixels exceed `tolerance` (`max_interior_miss`), but never by more than 16 levels
  (`interior_miss_limit`); AMD's radv differs by up to 10 levels on a handful of pixels.
- **A golden alone is not trusted.** Each golden test also asserts pixels analytically (the background is the clear
  color; interior pixels have the barycentric blend of the vertex colors), so a wrong golden cannot hide a broken
  pipeline.
- **On failure** the test writes the amplified difference to `$TMPDIR/rhi_<pid>_diff_<name>.ppm` (`/tmp` when unset; the process id keeps parallel test runs apart) and
  prints its path.
- **Regenerating.** Run the test with `ENGINE_RHI_UPDATE_GOLDEN=1` (for example
  `ENGINE_RHI_UPDATE_GOLDEN=1 ./build/tests/rhi/rhi_tests "[golden]"`). It rewrites the golden instead of comparing and
  warns. Regeneration is deliberate: open the PPM and look at it, check the analytic assertions passed, and only then
  commit it. Never regenerate to make a failing run pass without understanding the difference.
- **The numbers of a passing run.** `ENGINE_RHI_GOLDEN_REPORT=1` prints one line per comparison, passed or not: the
  worst channel difference and the interior and edge pixels over their tolerances (how a backend's differences are
  recorded, Phase 21 Day 309).
- **Validation.** GPU tests fail on any validation error. With `ENGINE_RHI_VALIDATION_FATAL=1` the whole suite is clean
  except the validation-capture tests in `vulkan_instance_test.cpp`, which provoke errors on purpose and so abort.

### Testing a UI

A widget test needs no window and no GPU: `ui::Harness` (`engine/ui/testing/harness.h`) is an offscreen surface with a font, an atlas, input helpers and a software rasteriser; the sample in `docs/guide/ui.md` ("The widget tree, the surface and the test harness") is the pattern, and `tests/ui/` has one file per widget family. A test of the toolkit on a real device is a golden picture (`tests/rhi/ui_golden_gpu_test.cpp`, `ui_demo_gpu_test.cpp`; the pictures are `tests/rhi/golden/ui_*.ppm`, Vulkan on Linux, both backends on Windows, Metal on macOS) and, for anything that owns device objects, the reset check of "Testing a reset". `ui_demo` runs scripted (`--script`, `--screenshot`) under the ctests `ui_demo_*`, including `ui_demo_script_wayland|x11` on a live display. The CPU cost of the toolkit is `renderer_bench --section ui`, which asserts nothing.

### Stress tests and replaying a failing seed (`ENGINE_STRESS_SEED`)

Tests that use random data take a **fixed seed**, so a run is repeatable: the same seed gives the same sequence on every machine. The job-system stress test
(`tests/jobs/stress_test.cpp`) runs many random task graphs; when one hangs or fails, the message prints the seed, for example
`graph seed 17 (replay: ENGINE_STRESS_SEED=17)`. Re-run that single case with the environment variable set:

```bash
ENGINE_STRESS_SEED=17 build/tests/jobs/jobs_tests "[stress]"
```

Randomized tests of other modules (the ECS model tests, for instance) hard-code their seeds in the test; to replay a failure, run the test directly and
read the failing assertion. If you write a randomized test: use a small deterministic generator with a fixed seed (`tests/ecs/test_rng.h`), make
the failure message name the seed or the step, and never seed from the clock.



### Documentation checks

`docgen` (built from `tools/docgen`) keeps the documentation honest. One
target runs everything; `ctest` runs the four checks without regenerating:

```
cmake --build build --target docs     # regenerate docs/reference, then run all checks
ctest --test-dir build -L docs        # docs_check, docs_links, docs_samples, docs_lint
```

| Check | Fails when |
|---|---|
| `docs_check` | `docs/reference/` differs from what `docgen reference` generates from the headers (run the `docs` target and commit the result) |
| `docs_links` | a Markdown link, `#anchor`, image or `[[Symbol]]` reference is broken, or a page under `docs/` is not reachable from `docs/README.md` |
| `docs_samples` | a tagged code block does not compile, exits non-zero, times out or prints something other than its `output` block |
| `docs_lint` | a new undocumented public symbol, a missing required label in a strict module, or lower coverage than `docs/reference/lint-baseline.txt` |

A whole run over the engine takes well under a second, except for the sample
compilations (about a tenth of a second each, run four at a time). The lint
baseline lists the findings that are tolerated for now and only shrinks:
after documenting symbols, refresh it with `docgen lint --baseline
docs/reference/lint-baseline.txt --update-baseline` and commit the smaller file.
The conventions the checks enforce are in [Documentation Conventions](../DOC-CONVENTIONS.md);
the tool's design is in `tools/docgen/DESIGN.md`.

All file and directory access of the tools goes through the engine's file layer ([io](io.md)): `docgen` writes each generated page atomically (a
failed write keeps the previous file), `shader_mint` stages every output of one minting as temporary files and renames them together, and
`crash_extract` writes its output atomically. Output must not depend on the order the file system lists a directory in: `docgen` sorts what it
collects, and a test (`SetNameOrderSeedForTesting` in `tools/docgen/src/source_file.h`) runs the whole reference build under shuffled
directory orders and compares the bytes.

A sample in this guide, built and run by `docs_samples` (it uses the standard
prelude, so the engine namespace is already in scope):

```cpp compile run
ArenaAllocator arena(1 << 16);
StringBuilder<ArenaAllocator> sb(arena);
sb.Append(StringView("answer = "));
sb.AppendInt(42);
std::puts(sb.CStr());
```

```output
answer = 42
```

### Building and testing on Windows

Since Phase 13 every layer builds and is tested on Windows x64, with the Vulkan and Direct3D 12 backends; `BUILD.md` lists what to install
and `docs/guide/windows.md` is the Windows page (building, the demos, validation, crash dumps, where things live). CMake finds MSVC, the Windows SDK, LLVM's `clang-cl` and the Vulkan SDK
itself (vswhere, the registry, the default install folders), so any terminal works: no Developer Command Prompt and no `vcvars`.

- **Presets.** `cmake --preset windows-msvc` configures `build-msvc` (MSVC, the primary compiler, plain and ASan flavors);
  `cmake --preset windows-clang-cl` configures `build-clang-cl`, the second compiler: it builds every day and its full plain suite runs at a
  phase's close (no ASan flavor). Build and test with
  `cmake --build --preset <name>` and `ctest --preset <name>`. On Linux the preset is `linux` (`build`).
- **`scripts/test-phase.sh` runs under Git for Windows' `bash`.** It finds the platform from `uname -s`, defaults to `build-msvc`, and
  takes the same options as on Linux. `--flavor tsan` stops with a message (MSVC has no ThreadSanitizer), and `--flavor all` runs
  plain and asan and says so: `flavors: all (plain and asan, no tsan on Windows)`. Git Bash rewrites an argument that looks like an
  absolute path, so a `/flag` meant for `cl` or `clang-cl` becomes a file name; a script or test that passes such flags sets
  `MSYS2_ARG_CONV_EXCL='*'` or calls the tool through CMake.
- **The phase map per platform.** File patterns in `tests/phases.txt` need no platform: every source file is in both checkouts, and only
  the built tests differ. What differs is which phases have tests in which flavors, so every phase has a `flavors <phase> windows: ...`
  and a `flavors <phase> macos: ...` line (`none` where the platform has no tests of it), and a change that adds or removes a phase's tests on
  a platform updates its line. `test_phase_labels`
  checks the line for the platform it runs on, and only the flavors the configuration builds (`clang-cl` is not asked for ASan tests).
  The unused-line check runs on Linux only: a `test:` line for a test Windows does not build (a Wayland or X11 test) matches nothing
  there without being stale.
- **The `display` group.** `SKIP_IF_NO_DISPLAY_SESSION()` asks `os::QuerySessionState()`, which on Windows asks the Terminal Services
  API whether this session is the active one and checks that the input desktop is `Default` (a locked workstation or a screen saver
  switches it; `engine/foundation/os/windows/session.cpp`), so a locked screen skips the window tests, as on Linux.
- **The source gates** (`*_check`) are CMake scripts and run unchanged on all three machines; they also know the Windows rules: `windows.h`
  only through `engine/foundation/os/windows/win32.h` and the Windows SDK headers only in the Windows directories
  (`platform_neutral_check`), no platform system header in neutral code (`module_deps_check`), D3D12 and DXGI only in
  `engine/rhi/dx12/` (`rhi_boundary_check`), and the Win32 and C runtime file, environment and command-line calls
  (`file_access_check`, `config_access_check`) and heap, lock and formatting calls in signal-safe code (`signal_safety_check`).
  A gate prints each problem on one line (`message(NOTICE)`), so its self-tests do not depend on the console width or the
  checkout's path.
- **Two machines.** During Phase 13 the Windows machine ran the Windows build every day, and the shared changes (CMake, the phase map, the
  gates, `scripts/test-phase.sh`, shared tests) were checked on Linux once, when the phase closed (Day 174); `KNOWN-ISSUES.md` lists what
  waits for a Linux run.

### Building and testing on macOS

Since Phase 21 every layer builds and is tested on macOS (x86-64, Apple clang), with the Cocoa window and the Metal backend in place of
Vulkan; [macOS](macos.md) is the platform's page. `BUILD.md` lists what to install: the Command Line Tools (Apple clang), CMake and
Ninja; no Xcode.

- **Preset.** `cmake --preset macos-clang` configures `build-macos` (Ninja, Debug, x86-64, deployment target macOS 13.3); build and test with
  `cmake --build --preset macos-clang` and `ctest --preset macos-clang`. The configure takes Apple clang through `xcrun`, so a MacPorts or
  Homebrew clang first on `PATH` is not picked up, and stops with a message without the Command Line Tools. Everything the Linux build has is
  built, with the Metal backend in place of Vulkan (Day 311).
- **Apple silicon, compile-only.** `cmake --preset macos-clang-arm64` configures `build-macos-arm64` for arm64 with `ENGINE_COMPILE_ONLY` (Day
  327): every target builds and nothing built runs, so there is no test discovery and no shader minting (the committed outputs are used). It
  is how the Intel Mac checks arm64 code paths; run it, unity and `-DENGINE_UNITY_BUILD=OFF`, on every macOS day that changes macOS code.
- **Flavors.** plain, asan (ASan and UBSan) and tsan, as on Linux. Apple clang has no LeakSanitizer: ASan aborts when asked for
  `detect_leaks=1` (`KNOWN-ISSUES.md`), so leak checks run on Linux.
- **Objective-C++.** `.mm` files (only in macOS directories) compile with ARC and the warnings of every target. `tests/macos` checks that
  every neutral foundation header compiles beside AppKit, Metal and QuartzCore in either order (`macos_header_apple_first`, `_last`).
- **The test script** runs under the system's `/bin/bash` 3.2 (`scripts/test-phase.sh`, defaulting to `build-macos` there), so it keeps to
  what that bash has: no associative arrays, `mapfile` or `${x,,}`. `--flavor all` is plain, asan and tsan. `--affected` adds phase 21 as a
  side phase.
- **The gates** run here as on Linux and Windows. Rule 7 of `platform_neutral_check` keeps Apple's headers (the frameworks, `<mach/...>`,
  `<dispatch/...>`, `<objc/...>`, `<sys/sysctl.h>`, `<copyfile.h>`, `<libproc.h>`) and `.mm` files in the macOS directories (a `macos/`
  folder, `engine/rhi/metal/`); `std_usage_check` and `module_deps_check` read `.mm` files and count `#import` as `#include`.
- **Documentation** (`docgen`, the reference, `ctest -L docs` with its samples) builds and runs on the Mac as on Linux (the samples from
  Phase 21 Day 311).

### Formatting and clang-tidy

The style is `.clang-format` (Allman braces, every control-flow body braced, 120 columns; `CLAUDE.md`, "Code style"). `scripts/format.sh` formats every tracked C and C++ file, or
the files you name; `scripts/format.sh --check` changes nothing and lists what is not formatted. `scripts/install-git-hooks.sh` (once per machine; from Git Bash on Windows) points git at
`scripts/git-hooks/`, whose pre-commit hook formats the staged files and stages them again. A file with unstaged edits as well is refused (staging the formatted file would stage
those edits): run `scripts/format.sh FILE` and stage what you mean. clang-format also wraps comments at 120 columns; doc-comment labels are list items (`/// - Fails: ...`) so that wrapping leaves them alone. Vendored code, generated code and test fixtures are listed in `.clang-format-ignore`. The installer also tells
`git blame` to skip the formatting-only commits listed in `.git-blame-ignore-revs`.

clang-tidy is configured in `.clang-tidy` and run by `scripts/tidy.sh` on a separate non-unity build tree (`build-tidy`, which exports `compile_commands.json`). It is not part of the
commit hook. A finding is fixed, or suppressed on its line with `// NOLINT(check-name): the reason`.

### What "done" means before you commit

There is no hosted continuous-integration service: the checks are run by you, on your machine(s), by the same scripts a CI job would call. Before a change is
committed:

1. It builds with **no warnings** in unity and in `-DENGINE_UNITY_BUILD=OFF`, with Clang and, on Linux, also GCC.
2. `scripts/test-phase.sh --affected` passes (plain), and the `asan` and `tsan` flavors of the same selection when you touched memory or threads. Quote the
   `ran X of Y tests` line when you report it.
3. `ctest -L docs` passes if you changed a public header or a guide page.
4. The gates (`std_usage_check`, `module_deps_check`, `platform_neutral_check`, ...) pass; they run with the normal tests.
5. `STATUS.md` and, if a test fails or a check is deferred, `KNOWN-ISSUES.md` are updated in the same commit.
6. At the end of a phase, `scripts/test-phase.sh --all --flavor all` runs everything, on every platform the project supports.

Some tests need a live desktop and fail while your session is locked; `KNOWN-ISSUES.md` lists them, so a failure there is not automatically your change.

## Debugging scripts (Linux and macOS)

`scripts/find-core.sh` and `scripts/open-core.sh` locate the newest core dump of a program, check its build id and open it in
the debugger (`gdb` on Linux, `lldb` on macOS); they are shell scripts and are described in the "Core dumps on Linux" section of `docs/guide/foundation-debugging.md`.
Their test (`The newest core of a crashed program is found by script...`) runs in the plain flavor only and skips, with the
reason printed, when the debugger is missing or the system keeps no core.

`scripts/crash-report.sh`, `scripts/store-symbols.sh` and `scripts/split-debug.sh` turn crash dumps into traces and keep the symbols
of release builds ("Turning a dump into a trace" and "Keeping symbols for release builds" in the same guide). Their tests
(`crash-report.sh ...`, `A stripped program resolves through a symbol store...`) skip with a printed reason when `lldb`,
`objcopy` or `strip` is missing. `-DENGINE_SPLIT_DEBUG_INFO=ON` builds with `-g` and splits the debug information of the demos and benches.

