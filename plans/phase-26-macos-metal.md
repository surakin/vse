# Phase 26 — macOS Port + Metal Backend

**Depends on:** Phase 13 (the Windows port: the "port an already-working stack" process, the RHI backend registry, bytecode blob sets, the object
index stream) and everything that exists on `master` when each day runs. Phases 0-18 today; see "Runs on the side" below.
**Goal:** everything that runs on Linux and Windows runs on macOS (x86-64), with Metal as the native RHI backend.
**Definition of done:** on macOS, the foundation, file-layer, settings, job-system and debugging suites pass in the plain, asan and tsan flavors; the
CPU modules' suites (text, UI, ECS, spatial) pass; the Cocoa window and input pass the window layer's tests (event stream, cursor, lock, clipboard, drag and
drop, fullscreen, session); the Phase 4 golden images and the Phase 5 scene tests, the text and UI golden images and the scene bridge tests pass through the
**Metal** backend with Metal API validation silent; the Metal backend passes the lifecycle conformance suite and recovers from an injected device loss; the demos run
on Metal; every gate and `ctest -L docs` pass on macOS; and the Linux machine and the Windows PC still pass their full suites in every flavor they have.

**Status: started (2026-11-10), Days 282-311: Day 282 (the `.metallib` spike; interim route, pending), Day 283 (spec, inventory, setup), Day 284 (CMake, Apple clang, the base layer) and Day 285 (the `os` backend, `os/posix/`) done on the Mac; next Day 286.** Day numbers continue after Phase 25 (Day 281). Phases 19-24 have no day files yet;
when they get them, Phases 25 and 26 renumber in the same commit (CLAUDE.md, "Phases are whole numbers").

**Hardware (checked 2026-11-10).** The developer's 2019 MacBook Pro 13" (`MacBookPro15,4`): quad-core Intel Core i5 at 1.4 GHz, 8 GB RAM, **Intel Iris Plus
Graphics 645, integrated and the only GPU** (Metal 3), 2560x1600 Retina display; macOS 15.8.1 (24H32); Command Line Tools only (Apple clang 17.0.0,
`clang-1700.0.13.5`), no Xcode; MacPorts in `/opt/local` (CMake 4.4.4 as `cmake-devel`, Ninja, Python 3.14); `/bin/bash` is 3.2.57. **Corrections to the
earlier version of this file:** the GPU is integrated, not discrete, so `hasUnifiedMemory` is expected to be true and the `Managed` storage path is
exercised only by forcing it (Day 301); there is one adapter, so the run-time GPU switch has no second Metal device to switch to (the null backend covers
the switch logic, as on Linux). The `slangc` on `PATH` (`/Applications/kitty.app/Contents/MacOS/slangc`) is kitty's own program, not the Slang compiler.
It does **not** exercise Apple silicon (unified memory with tile-based rendering, memoryless attachments, arm64 code); that stays unverified and is recorded
in `KNOWN-ISSUES.md` (Day 310 adds an arm64 compile-only configuration).

## Runs on the side (the user's request, 2026-11-10)

This phase is done on the Mac **alongside** the main line (Phase 18 and onward on the Linux machine and the Windows PC), out of the roadmap's numerical
order. The user asked to start with the foundation (Days 284-293) and then plan the whole phase. What follows from it:

- **`CURRENT_PHASE` does not change** for this phase: it keeps naming the main line's phase. Phase 26's tests carry the label `phase26`. Day 286 teaches
  `scripts/test-phase.sh` a side phase (a `SIDE_PHASES` file listing `26`, added to `--affected` runs), and Day 283 adds the rule to CLAUDE.md
  ("Tests run by phase"); the user confirmed it on 2026-11-11.
- **It ports what exists on `master` when each day runs.** Each day starts by pulling `master`. A module a main-line phase adds before this phase
  closes gets its macOS port inside the day of its layer if that day has not run yet, or on Day 310 if it has. After this phase closes, every phase gets
  a macOS day the way it gets a Windows PC day today; Day 311 writes that answer into `PLAN.md` assumption #11 (proposed; the user confirms).
- **Shared code is checked on the other machines by the next session there**, not at this phase's close, because the main line keeps working on those
  machines in the meantime and must not sit on a broken `master` for weeks. A Phase 26 day that changes shared files (anything outside the macOS-only
  directories listed under "Where macOS code goes") files the Linux and Windows runs it needs in `KNOWN-ISSUES.md`, and the next day on that machine runs
  them first (the same "first task" rule the main line's Windows PC days follow today). A macOS day never pushes a shared change it has not built on macOS
  in both unity modes.

## Days

282. [Spike: Minting `.metallib` Files Without Xcode](phase-26-macos-metal/day-282-spike-metallib-without-xcode.md)
283. [Design Spec, Inventory, Toolchain & Phase Setup](phase-26-macos-metal/day-283-design-spec-and-setup.md)
284. [CMake, Apple Clang and Objective-C++ on macOS](phase-26-macos-metal/day-284-cmake-and-compiler.md)
285. [The `os` Backend, Console and Log; Base Tests Pass](phase-26-macos-metal/day-285-os-backend.md)
286. [Test Tooling, Gates and Docs on macOS](phase-26-macos-metal/day-286-test-tooling-and-gates.md)
287. [File Layer I: Paths, Errors, Local Files](phase-26-macos-metal/day-287-file-layer-files.md)
288. [File Layer II: Directories, File-System Operations, Asynchronous I/O](phase-26-macos-metal/day-288-file-layer-directories-async.md)
289. [The Job System on macOS](phase-26-macos-metal/day-289-job-system.md)
290. [Debugging I: Stack Traces, Symbols, Crash Context](phase-26-macos-metal/day-290-debug-stack-traces.md)
291. [Debugging II: Crash Handler, Death Tests, Fault Injection](phase-26-macos-metal/day-291-debug-crash-handler.md)
292. [Debugging III: Dumps, Symbol Store and Scripts](phase-26-macos-metal/day-292-debug-dumps-and-scripts.md)
293. [Settings on macOS: the User Defaults Store](phase-26-macos-metal/day-293-settings-user-defaults-store.md)
294. [Text, UI, ECS and Spatial on macOS (the CPU Modules)](phase-26-macos-metal/day-294-cpu-modules.md)
295. [Cocoa I: Application, Entry Point, Window, Session, Fullscreen](phase-26-macos-metal/day-295-cocoa-window.md)
296. [Cocoa II: Keyboard, Text, Pointer and the Event Stream](phase-26-macos-metal/day-296-cocoa-input.md)
297. [Cocoa III: Cursor, Pointer Lock, Clipboard, Drag and Drop](phase-26-macos-metal/day-297-cocoa-cursor-clipboard-dragdrop.md)
298. [RHI, Renderer and Demos on the Null Backend](phase-26-macos-metal/day-298-null-backend-on-macos.md)
299. [The Shader Path for Metal: `.metallib` Blobs, Entry Names, Clip Space, Object Index](phase-26-macos-metal/day-299-metal-shader-path.md)
300. [Metal I: Backend Library, Adapters, Device, Validation, Events](phase-26-macos-metal/day-300-metal-device.md)
301. [Metal II: Buffers, Textures, Storage Modes, Upload and Readback](phase-26-macos-metal/day-301-metal-resources.md)
302. [Metal III: Bind Groups, Samplers and Slots from Reflection](phase-26-macos-metal/day-302-metal-bindings.md)
303. [Metal IV: Pipelines and the Pipeline Cache](phase-26-macos-metal/day-303-metal-pipelines.md)
304. [Metal V: Command Lists, Encoders, Barriers, Mips](phase-26-macos-metal/day-304-metal-command-lists.md)
305. [Metal VI: `CAMetalLayer` Swapchain and Presentation](phase-26-macos-metal/day-305-metal-swapchain.md)
306. [Metal Golden Images: the Phase 4 Suite](phase-26-macos-metal/day-306-metal-golden-images.md)
307. [Metal Renderer: the Phase 5 Suite](phase-26-macos-metal/day-307-metal-renderer.md)
308. [Text, UI and the Scene Bridge on Metal](phase-26-macos-metal/day-308-metal-text-ui-scene.md)
309. [Metal Lifecycle, Device Loss and Resets](phase-26-macos-metal/day-309-metal-lifecycle.md)
310. [Demos, Benchmarks, Catch-up and the arm64 Compile Check](phase-26-macos-metal/day-310-demos-benches-arm64.md)
311. [Guides, Closing Full Runs on Three Machines & Wrap-up](phase-26-macos-metal/day-311-docs-and-closing-run.md)

Order reasoning: the spike comes first because its answer (driver-minted `.metallib` or Xcode) changes `BUILD.md`, the prerequisites and Day 299, and it
needs no build of the engine. Then the stack bottom-up, one layer per day as Phase 13 did (`ARCHITECTURE.md`): the build, `os`, the test tooling (so every
later day is scoped and gated like Linux), the file layer, jobs, debugging, settings — **Days 284-293 are the foundation port the user asked for first**,
and they end with the whole `foundation` aggregate and `engine/jobs` passing on macOS. The CPU modules (text, UI, ECS, spatial) need nothing above the
foundation and jobs, so they come before the window. Cocoa follows (three days, as Win32 needed the Phase 13 days plus Phase 15 Day 205). Unlike Windows,
there is no Vulkan-on-macOS stepping stone (decision 8), so Day 298 proves the RHI, renderer and demos on the null backend instead. Then the shader path and
the Metal backend in the order the DX12 backend was built (Days 164-172), then its suites, the demos and the close.

## Rules for every day of this phase

- **Three machines.** The Mac does the day; the Linux machine and the Windows PC check the shared files it changed at their next session (above). Each day
  names, in its result and in `KNOWN-ISSUES.md`, the shared files, the flavors to run and the docs to regenerate. Day 311 confirms none is left open.
- **Flavors.** macOS has all three: `plain`, `asan` (with UBSan, as on Linux) and `tsan`; Apple clang supports both sanitizers on x86-64. Apple clang has no
  LeakSanitizer on macOS, so leak checks stay on Linux (`KNOWN-ISSUES.md`, Day 285 records what was measured). Concurrency, lifetime and memory days run asan
  and tsan of the same selection on the Mac.
- **Compiler.** Apple clang from the Command Line Tools is the only macOS compiler; MacPorts' clang is not used (one compiler keeps the minted outputs and
  warnings in step with what a macOS developer has). Unity on and off on every day that changes code (CLAUDE.md).
- **Where macOS code goes.** Cocoa, AppKit, Foundation, QuartzCore, Metal, CoreGraphics and IOKit headers only under `*/macos/` directories and
  `engine/rhi/metal/`, in Objective-C++ (`.mm`) files; the Mach, `dispatch`, `copyfile` and `sysctl` C headers likewise. Code identical on Linux and macOS
  moves to a `posix/` directory beside `linux/` (the inventory on Day 283 decides per file). Neutral headers never see an Objective-C type: they hold
  opaque pointers. `platform_neutral_check`, `module_deps_check` and `rhi_boundary_check` learn these rules on Day 286 (Metal on Day 300).
- **Labels.** A shared test keeps its phase when it starts running on macOS; a new macOS-only test is labeled phase 26 (`*_macos_test.cpp`, `.mm` tests).
- **The rest of CLAUDE.md applies unchanged:** end-of-day `/code-review high`, public API documented, known issues filed, unity and non-unity builds both
  pass, the standard library only through the wrappers (`std_usage_check`), file access through the file layer, configuration through settings,
  warnings as errors.

## Design decisions made while planning (2026-11-10; confirmed or amended on Day 283: `docs/superpowers/specs/2026-11-11-macos-port-design.md`, section 2)

1. **Toolchain:** Apple clang (Command Line Tools), CMake 3.21+ and Ninja, a CMake preset `macos-clang` (tree `build-macos`; asan and tsan as the
   existing sanitized targets), native build on the Mac. **Deployment target macOS 13.3** (amended on Day 283: Apple's libc++ has floating-point `std::to_chars` from 13.3; Metal 3 needs 13.0; the
   cross-syntax gates keep their `arm64-apple-macos13` triple) on x86-64. **No Xcode** unless the Day 282 spike rejects the driver route. Flags: `-std=c++20
   -fno-exceptions -fno-rtti`, `-fobjc-arc` for Objective-C++, the warning set of CLAUDE.md; frameworks linked by name (`-framework Metal` ...) only into
   the libraries that use them.
2. **The C library is allowed** (`PLAN.md` assumption #2); macOS's libc and libc++ are the system's. POSIX calls are shared with Linux where they behave
   the same (`posix/`), and macOS-only calls (Mach clocks, `_NSGetEnviron`, `copyfile`, `F_FULLFSYNC`, `pthread_threadid_np`) sit in `macos/`.
3. **Objective-C++ with ARC, no bridging library.** AppKit and Metal are called from `.mm` files with ordinary Objective-C message syntax; ARC owns
   the Objective-C objects; a C++ class that holds one keeps it in an Objective-C++-only member (or a `__bridge_retained` pointer released in its
   destructor) behind an opaque pointer in the neutral header. Not used: raw `objc_msgSend` calls (the earlier plan; brittle and no safer),
   `metal-cpp` (a separate download, not part of the OS), Swift. **Amended on Day 283:** Apple's headers clash with no engine header in either order (243 headers checked), so no
   `unity/apple_enable.h` / `apple_disable.h` pair up front; one is added when a real conflict appears (CLAUDE.md "Unity builds");
   `__ASSERT_MACROS_DEFINE_VERSIONS_WITHOUT_UNDERSCORES=0` stays as a guard for later SDKs.
4. **Symbols are DWARF in dSYM bundles.** A post-link `dsymutil` step for every executable in debug-info builds; libbacktrace uses `macho.c` and reads
   the dSYM next to the binary; the symbol store holds dSYMs by `LC_UUID`. Crash capture is sentry-native's Breakpad backend with no transport, as
   elsewhere (assumption #20); the report is written from its `on_crash` hook, which receives the user context on macOS.
5. **One Cocoa window backend** (`WindowBackend::Cocoa`; `auto` is Cocoa on macOS). AppKit runs on the main thread, so `ENGINE_WINDOWED_PROGRAM`
   expands to a `main` that sets up `NSApplication` (activation policy `Regular`, no nib, no bundle needed) and calls the program's entry on the main
   thread; `Window::PumpEvents` drains `nextEventMatchingMask:` without blocking. Windowed programs embed an `Info.plist` (`-sectcreate __TEXT
   __info_plist`: bundle id, `NSHighResolutionCapable`). Tests and tools stay plain `main` programs.
6. **Input:** key events through `interpretKeyEvents:` (`NSTextInputClient`, committed text only, as on Windows: no marked-text composition),
   `keyCode` translated to `os::Key` by a table, repeat from `isARepeat`, `scrollingDeltaX/Y` (precise deltas become fractional notches), pointer lock
   through `CGAssociateMouseAndMouseCursorPosition` and raw deltas from the mouse events, the clipboard through `NSPasteboard`, drag and drop through
   `NSDraggingDestination`/`NSDraggingSource` on the view.
7. **Session state** through `CGSessionCopyCurrentDictionary` (`engine/foundation/os/macos/SESSION.md`), fullscreen through native fullscreen
   (`engine/platform/macos/FULLSCREEN.md`).
8. **Metal is the only GPU backend on macOS; Vulkan is not built there.** Vulkan on macOS means MoltenVK, a third-party translation layer that does not
   ship with the OS: the zero-third-party-convenience rule excludes it. The null backend builds everywhere and carries the renderer and demos until Metal
   exists (Day 298). `auto` is Metal on macOS. **Amended on Day 283:** the GPU driver probe (`os/gpu_driver_probe.h`, Vulkan-only, included in
   `foundation_base`) is not built on macOS, and `rhi_backends` gains an `ENGINE_RHI_HAVE_VULKAN` guard (Day 298).
9. **Metal baseline:** `MTLGPUFamilyMac2` and Metal 3, the default hazard-tracking mode (so the render graph's barriers become no-ops or encoder
   splits), one `MTLCommandBuffer` per command list with encoders split around render passes, copies and dispatches, `MTLSharedEvent` as the fence,
   `Shared` storage for upload, readback and the uniform ring, `Private` for GPU-only resources, `Managed` with `didModifyRange` for CPU-written
   resources on a non-unified device (`FlushMappedBuffer`). **Bind groups map to direct `set*Buffer`/`set*Texture`/`set*SamplerState` calls at
   slots read from reflection**, not argument buffers (the Iris Plus 645 reports argument-buffer tier 1, Day 282; Day 302 measures and may amend). Pipeline cache: `MTLBinaryArchive`, named by adapter identity. Validation: `MTL_DEBUG_LAYER=1` with `MTL_DEBUG_LAYER_ERROR_MODE=assert`
   and `MTL_SHADER_VALIDATION=1` in the validation test runs, so any message fails the test; the `validation` setting is advisory (set before the
   device exists, as the lifecycle review says).
10. **Shaders: minted `.metallib` blobs, never compiled at run time** (assumption #18). Slang's `-target metal` output is minted to an AIR-only
    `.metallib` (**Interim, pending (the user's decision, 2026-11-10, Phase 26 Day 282): `.metallib` files are minted on the Mac through the driver's own compiler with the undocumented `-[MTLLibrary serializeToURL:error:]`, until the user has a new Mac;** then the route is decided again (Xcode's `metal`/`metallib`, or Apple's Metal Developer Tools for Windows). The documented driver route (`MTLDynamicLibrary`) was rejected by the spike: a dynamic library's entry points cannot be used as pipeline functions. Limits in `KNOWN-ISSUES.md`.) committed as `*.metallib.inc` beside `*.spv.inc` and `*.dxil.inc`, deterministically. `ShaderFormat::Msl`
    (reserved, refused everywhere today) becomes `MetalLib`; `newLibraryWithSource:` appears only in the minting tool. The Metal build uses
    `ENGINE_CLIP` for the Y flip and `ENGINE_OBJECT_INDEX_STREAM` for the per-object index, as the D3D12 build does; entry points named `main` are
    renamed `main_0` by Slang, and the blob records its real entry name; `fill.comp` and `saxpy.comp` get their element count as a constant.
11. **Golden images are not re-blessed for macOS.** The existing images and tolerances are the oracle on the Iris Plus 645; a difference outside
    tolerance is investigated, and any tolerance change is recorded with the measured numbers (as Phase 13 decision 11).
12. **The checkout is on a case-insensitive APFS volume.** Checked on Day 283: no paths differ only in case, and no gate or test assumes a
    case-sensitive file system.

## Prerequisites the developer installs (Day 283 checks each and records the version in `BUILD.md`)

Present on 2026-11-10: the Command Line Tools (Apple clang 17.0.0), CMake 4.4.4 and Ninja (MacPorts), Python 3.14 (MacPorts), `lldb`, `dsymutil`,
`git`. Missing:

- **`slangc` v2026.18.2** for macOS x86-64 (the release archive from the Slang GitHub page, same version as Linux and Windows), on `PATH` ahead of kitty's
  `slangc`, or named by `ENGINE_SLANGC`.
- **A bash newer than 3.2** only if Day 286 finds `scripts/test-phase.sh` needs one (MacPorts `bash`); the preference is to keep the scripts 3.2-clean.
- **Xcode: not needed for now.** The Day 282 spike rejected the documented driver route; the user chose the undocumented driver route as an interim (pending until the new Mac), so Xcode (`xcodebuild -downloadComponent MetalToolchain` for `metal`/`metallib`) is the likely later route, decided then.
- Optional: Xcode's Instruments or the Metal debugger for GPU captures (only with Xcode).

## Inputs collected from earlier phases (each is a task in the day named)

- **Shader toolchain spike (added 2026-11-10; done the same day, result in its day file: interim route chosen, pending)** — Day 282: the probe result and its five sub-tasks (load on Apple silicon and another macOS version, strip
  to AIR, determinism, dynamic versus ordinary library, the decision). Probe: on 2026-11-10, on this Mac with Command Line Tools only, MSL compiled with
  `MTLCompileOptions.libraryType = .dynamic` and an `installName`, wrapped with `MTLDevice.makeDynamicLibrary(library:)` and written with
  `MTLDynamicLibrary.serialize(to:)`, gave a 27 KB fat container (magic `cbfebabe`, an AIR slice `__AIR_DATA` and an `INTEL_*` native slice) that loaded
  back through `makeLibrary(URL:)` with its vertex entry usable. The undocumented `serializeToURL:error:` of an ordinary library is not to be used.
- **From the Phase 4 portability review** (`docs/superpowers/specs/2026-11-04-rhi-portability-review.md`, section 5) — Days 301-305: encoder splitting
  around passes, copies and dispatches; barriers as no-ops, `memoryBarrier` or an encoder split by tracking mode; threadgroup size from shader reflection
  at pipeline creation; `Managed` storage with `FlushMappedBuffer` as `didModifyRange`; `ClampToBorder` rejected with a log on iOS (Phase 27; supported on
  macOS); `nextDrawable()` as `Acquire`; the clip-space Y flip in the shader path.
- **From Phase 5** (`docs/superpowers/specs/2026-11-04-renderer-portability-review.md`, Day 84) — Days 299, 307: `fill.comp` and `saxpy.comp` call
  `GetDimensions` on a buffer (pass the count); `main` becomes `main_0`; slangc flattens sets to `[[buffer(n)]]` slots, read from reflection; the per-object
  index; graph barriers as no-ops on tracked resources; the G-buffer's tile-memory concerns are Phase 27's (Apple silicon), not this phase's.
- **Debugging facilities backend (Phase 7, made ready on Day 101)** — Days 290-292: everything in `engine/foundation/os/macos/debug/BACKEND.md`;
  re-vendor Breakpad's macOS private headers (`KNOWN-ISSUES.md`, Phase 7 Day 95); the conformance groups of `tests/debug/BACKEND-CONFORMANCE.md` pass or skip
  with a reason; `debug_backend_lists` and `debug_cross_syntax_macos` still pass; the stack-trace and crash-handler sources the cross-syntax check does not
  cover (`KNOWN-ISSUES.md`, Phase 7 Day 101) are built for real.
- **Adapter enumeration (Phase 8 Day 105)** — Day 300: `MTLCopyAllDevices()`; the id is `registryID` as lowercase hex; `name`; `kind` (`Integrated` for a
  unified-memory or low-power GPU, `Discrete` for a non-unified or removable one); `dedicated_memory_bytes` from `recommendedMaxWorkingSetSize` (0 when
  unified); `driver` is the OS version; `can_present` for a device that can drive the window's `CAMetalLayer`; `suitable` requires the feature family of
  decision 9; enumeration creates no command queue. External-GPU removal (`MTLDeviceRemovalRequestedNotification`) feeds `Device::DeviceLost`.
- **Device lifecycle (Phase 8 Day 114)** — Day 309: `engine/rhi/BACKEND-LIFECYCLE.md` (fifteen functions), a Metal factory in
  `tests/rhi/lifecycle_conformance_test.cpp`, generation seeding, `CAMetalLayer` properties for `Swapchain::Reconfigure` (EDR for HDR), loss only through
  removal and `InjectDeviceLoss`, validation advisory; `docs/superpowers/specs/2026-11-04-rhi-portability-review.md` section 6.
- **Backend registry and blob sets (Phase 13 Day 162)** — Days 299-300: Metal registers a factory and an adapter lister; `ShaderBlobSet` gains the
  `.metallib` blob.
- **Fullscreen and session state** — Day 295: `engine/platform/macos/FULLSCREEN.md`, `engine/foundation/os/macos/SESSION.md`.
- **The window layer's backend list** (`engine/platform/BACKEND.md`, section `macos`) — Days 295-297: event stream, cursor, pointer lock, clipboard, drag and
  drop; `KNOWN-ISSUES.md` items of Days 186-188 and 205 that say "open on macOS until Phase 26".
- **File layer (Phase 9)** — Days 287-288: `engine/foundation/io/BACKEND.md` for macOS (case-insensitive volumes, `dispatch_io`, the standard directories,
  sandbox containers), the `File` conformance suite; `io_cross_syntax_macos` keeps passing.
- **Settings stores (Phase 11 Day 139)** — Day 293: `engine/foundation/settings/BACKEND.md` for macOS (`NSUserDefaults` per application suite,
  `NSUserDefaultsDidChangeNotification`, the bundle's defaults as the `System` layer, `_NSGetEnviron` for the environment snapshot), the store conformance
  suite; `settings_cross_syntax_macos` keeps passing.
- **Standard types (Phase 10) and wrappers (Phase 16)** — Days 284-285: native `__int128` on x86-64 macOS (the portable path is already tested by MSVC);
  the wrapper modules' platform files (`os/`, `sync/`, `cstr/`) on libc++ and Darwin's libc.
- **GPU driver probe (Phase 13 Day 149)** — not built on macOS, nor its test (decision 8 as amended on Day 283; Days 284-285); `os::LoadLibrary` is tested by `tests/macos/os_macos_test.cpp` there.

## Notes / open questions

- **Pending, the user's decision (2026-11-10):** the `.metallib` minting route is the undocumented `-[MTLLibrary serializeToURL:error:]` until the user has a new Mac; then it is decided again, and the new Mac also runs the spike's open task (loading on Apple silicon and another macOS version).
- **Confirmed by the user (2026-11-11):** the side-phase rule (CLAUDE.md, written on Day 283; the tooling is Day 286). **Still proposed:** the answer
  to `PLAN.md` assumption #11 (a macOS day in every later phase, Day 311); not building Vulkan on macOS (decision 8).
- The iOS platform layer (UIKit, touch) and the LuaJIT interpreter-only build for iOS are Phase 27, which reuses this phase's Metal backend.
- **HDR:** EDR (`wantsExtendedDynamicRangeContent`, `RGBA16Float`, extended linear sRGB) maps `HdrMode::ScRgbLinear`; the MacBook's built-in display is not
  an EDR display, so HDR stays checked by the policy tests only (Day 305 records it).
- **Not in this phase:** argument buffers and residency sets (unless Day 302 measures a need), `MTLHeap` placement, indirect command buffers, mesh
  shaders, MetalFX, Apple silicon tile features (memoryless attachments, imageblocks: Phase 27), notarization and code signing beyond ad hoc, app
  bundles for the demos, IME marked-text composition.
