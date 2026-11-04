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

### GPU tests and validation layers

Tests tagged `[gpu]` (CTest label `gpu`) need a Vulkan driver. Without one they skip; set
`ENGINE_RHI_REQUIRE_GPU=1` (do this on a development machine) to make a skip a failure so a broken driver cannot
hide. They run under `gpu_test::GpuFixture`, which enables the Khronos validation layer with synchronization
validation and fails the test on any validation error. `ENGINE_RHI_VALIDATION_FATAL=1` aborts on the first error
instead, for debugging a single test, and `ENGINE_RHI_VALIDATION=0` turns validation off for `Auto` instances. See
the validation section of the [RHI guide](rhi.md).

### The RHI boundary gate

`rhi_boundary_check` (label `boundary`, no GPU needed) enforces the rule that only `engine/rhi/vulkan/` may name the
Vulkan API. It is a CMake script, `tools/rhi_boundary_check/check.cmake`, that scans every `.h`, `.hpp`, `.cpp`, `.cc`,
`.c`, `.inl` and `.mm` file under `engine/`, `game/` and `tools/` for `vk*`/`Vk*`/`VK_*`/`PFN_vk*` identifiers and
`vulkan` includes (comments do not count), and checks that the backend-agnostic `engine/rhi/*.h` headers include
no Vulkan, Wayland, X11 or backend-directory header. To run it by hand:

```
cmake -DROOT=$PWD -DALLOWLIST=tools/rhi_boundary_check/allowlist.txt -P tools/rhi_boundary_check/check.cmake
```

A file that genuinely needs the exemption goes in `tools/rhi_boundary_check/allowlist.txt` as `path|reason`. An entry
without a reason fails, and so does an entry whose file no longer needs it or no longer exists, so the list cannot rot.
The gate tests itself: `rhi_boundary_check_planted*` run it over `tools/rhi_boundary_check/fixtures/bad` (an include, identifiers, one after a block comment, and a backend include in an agnostic header) and expect exactly
those five reports; the `_clean` case (block, trailing and inline comments naming Vulkan types) and the `_stale` and
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

Time budget for the RHI suite (`ctest -L rhi -j4`): about 15 seconds with a GPU, about one second on a machine without
Vulkan, where every `[gpu]` test skips (check with `ENGINE_VULKAN_LIBRARY=/nonexistent ctest -L rhi`). A GPU test that takes
more than a few seconds belongs in a stress label, not in the default run.

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
- **A golden alone is not trusted.** Each golden test also asserts pixels analytically (the background is the clear
  color; interior pixels have the barycentric blend of the vertex colors), so a wrong golden cannot hide a broken
  pipeline.
- **On failure** the test writes the amplified difference to `$TMPDIR/rhi_<pid>_diff_<name>.ppm` (`/tmp` when unset; the process id keeps parallel test runs apart) and
  prints its path.
- **Regenerating.** Run the test with `ENGINE_RHI_UPDATE_GOLDEN=1` (for example
  `ENGINE_RHI_UPDATE_GOLDEN=1 ./build/tests/rhi/rhi_tests "[golden]"`). It rewrites the golden instead of comparing and
  warns. Regeneration is deliberate: open the PPM and look at it, check the analytic assertions passed, and only then
  commit it. Never regenerate to make a failing run pass without understanding the difference.
- **Validation.** GPU tests fail on any validation error. With `ENGINE_RHI_VALIDATION_FATAL=1` the whole suite is clean
  except the validation-capture tests in `vulkan_instance_test.cpp`, which provoke errors on purpose and so abort.

### Stress tests and replaying a failing seed (`ENGINE_STRESS_SEED`)

_TODO_

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

### CI expectations

_TODO_

