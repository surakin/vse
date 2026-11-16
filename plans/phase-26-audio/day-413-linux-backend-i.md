# Phase 26 / Day 413 — The Linux Backend I: Opening the Device

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Days 402, 407.

The platform seam for Linux, in the form Day 402's spike chose (raw ALSA or PipeWire's native protocol); the days below name the choice generically and are amended with the spike's outcome.

## Tasks

- [ ] `engine/audio/linux/` with the backend's files and `BACKEND.md` entries; raw system calls and the reference protocol only (no third-party convenience library; `libasound` only if the spike and the user chose it as the distro library, like `libwayland-client`).
- [ ] Device enumeration: the cards and PCM devices (or the PipeWire graph's sinks), the default device, names and capabilities (rates, layouts, formats) into `DeviceInfo`; a machine with no sound device reports `NoDevice` by value and every test that needs one skips with a printed reason.
- [ ] Opening: negotiate the rate, the channel count and the sample format closest to the request (prefer `f32` where the device takes it, else `S16` with the conversion of Day 404 at the device boundary), the buffer and period sizes from `audio.buffer_frames`; report what was granted in `DeviceInfo`.
- [ ] Error mapping: every failure becomes a `DeviceStatus` with the OS error in the message; busy, permission, unsupported configuration and disconnected are distinct.
- [ ] A `audio_probe` tool (`tools/audio_probe`): lists devices, plays a test tone for a duration through the real backend (a program, not a test; the user listens), prints the granted configuration and the underrun count.
- [ ] Tests that need no hardware: the negotiation logic against a recorded capability table (a fake kernel interface or fake protocol peer), the format conversion at the boundary, the error mapping; a hardware test that is skipped without a device.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 428-429, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/audio.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 26); only wrapped types, no `std`, no raw platform macros; the audio thread never allocates, locks or does I/O.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** `audio_probe` lists the devices of this machine and opens one with the granted configuration reported.
