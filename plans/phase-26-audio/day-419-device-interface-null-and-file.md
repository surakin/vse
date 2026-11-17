# Phase 26 / Day 419 — The Device Interface, the Null Device and the File Device

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Day 416.

The only per-platform seam, and two devices that need no hardware, which every test and the C API's host-driven mode use.

## Tasks

- [x] `audio/device.h`: `Device` (an interface, in the shape of the RHI and window layers): `Open(DeviceConfig)` (rate, layout, buffer frames, a device name or the default), `Start`, `Stop`, `Close`, `Info` (the rate and layout actually granted, the buffer frames, the latency in frames), a render callback `void(f32* interleaved, u32 frames)` called on the device's thread, an error callback for a lost device; `DeviceStatus` values for every failure.
- [x] `audio/device_list.h`: `EnumerateDevices` (id, name, channel layouts, rates, default flag) per backend; the null backend returns one fake device.
- [x] `NullDevice` (renders on demand: `Pump(frames)` calls the callback on the caller's thread, a clock the test advances) and `WavFileDevice` (renders as fast as asked and writes the result to a WAV through `io::File`: the offline renderer of the tools and the tests); both through the same interface so a test cannot tell.
- [x] Settings through `settings::Registry` (never ad hoc): `audio.device` (a name, `default`), `audio.sample_rate` (0 = the device's), `audio.buffer_frames`, `audio.channels`, `audio.master_volume`, `audio.max_voices`, `audio.resampler_quality`; each with its `env` and `flag` names and its classification (live or device restart); `config_access_check` clean.
- [x] The backend registration: `audio::CreateDevice(kind)` returns the platform's device (null on a platform with no backend yet) so mixer code never names a platform API; `BACKEND.md` in `engine/audio/` lists what a new backend implements, kept in step by an `audio_backend_lists` check like the other layers.
- [x] Tests: the null device calls back the requested frames in order, the file device writes a valid WAV that Day 417's parser reads back, a device that refuses a configuration reports why, the settings round-trip.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [x] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 440-441, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/audio.md` updated or created; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 26); only wrapped types, no `std`, no raw platform macros; the audio thread never allocates, locks or does I/O.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Code can open an audio device through one interface and run it with no hardware, offline or on demand.

## Outcome (2026-11-17, Linux)

- `audio/device.h` (`AudioDevice`, `DeviceConfig`, `DeviceInfo`, `DeviceStatus`, `DeviceKind`, the render and error callbacks, `CreateDevice`, `CreateWavFileDevice`), `audio/device.cpp` (the null and WAV-file devices over one on-demand base), `audio/device_list.h` (`EnumerateDevices`), `audio/audio_settings.h/.cpp` (the seven `audio.*` settings with `ENGINE_AUDIO_*` and `--audio-*`, `AudioSettings`, `DeviceConfigFromSettings`), `engine/audio/BACKEND.md` and the ctest `audio_backend_lists` (+ a planted-failure twin); tests `device_test.cpp` (7 cases); guide section "Devices".
- Decisions: the interface class is `AudioDevice`, not `Device`: docgen resolves an inherited summary by the unqualified base name, so an `audio::Device` made `rhi::NullDevice` and the other RHI devices lose their inherited documentation (the docs diff showed it). `Pump(frames)` and `FramesRendered()` are part of the interface (a hardware device returns `Unsupported` and counts its own calls); the render callback is a function pointer and a user pointer, not a closure; the file device collects the samples in memory and writes a 32-bit float WAV on `Close` (an offline tool, not the audio thread); the four device settings need a restart, the other three are live.
- Settings read: a device name longer than 95 bytes is ignored (the default is kept).

## Post-implementation code review

`/code-review high` over Days 419-420: see Day 420. One finding of mine on re-reading, fixed on Day 420's close: a `WavFileDevice` destroyed without `Close` lost its output.
