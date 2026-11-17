# Phase 26 / Day 419 — The Device Interface, the Null Device and the File Device

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Day 416.

The only per-platform seam, and two devices that need no hardware, which every test and the C API's host-driven mode use.

## Tasks

- [ ] `audio/device.h`: `Device` (an interface, in the shape of the RHI and window layers): `Open(DeviceConfig)` (rate, layout, buffer frames, a device name or the default), `Start`, `Stop`, `Close`, `Info` (the rate and layout actually granted, the buffer frames, the latency in frames), a render callback `void(f32* interleaved, u32 frames)` called on the device's thread, an error callback for a lost device; `DeviceStatus` values for every failure.
- [ ] `audio/device_list.h`: `EnumerateDevices` (id, name, channel layouts, rates, default flag) per backend; the null backend returns one fake device.
- [ ] `NullDevice` (renders on demand: `Pump(frames)` calls the callback on the caller's thread, a clock the test advances) and `WavFileDevice` (renders as fast as asked and writes the result to a WAV through `io::File`: the offline renderer of the tools and the tests); both through the same interface so a test cannot tell.
- [ ] Settings through `settings::Registry` (never ad hoc): `audio.device` (a name, `default`), `audio.sample_rate` (0 = the device's), `audio.buffer_frames`, `audio.channels`, `audio.master_volume`, `audio.max_voices`, `audio.resampler_quality`; each with its `env` and `flag` names and its classification (live or device restart); `config_access_check` clean.
- [ ] The backend registration: `audio::CreateDevice(kind)` returns the platform's device (null on a platform with no backend yet) so mixer code never names a platform API; `BACKEND.md` in `engine/audio/` lists what a new backend implements, kept in step by an `audio_backend_lists` check like the other layers.
- [ ] Tests: the null device calls back the requested frames in order, the file device writes a valid WAV that Day 417's parser reads back, a device that refuses a configuration reports why, the settings round-trip.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 440-441, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/audio.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 26); only wrapped types, no `std`, no raw platform macros; the audio thread never allocates, locks or does I/O.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Code can open an audio device through one interface and run it with no hardware, offline or on demand.
