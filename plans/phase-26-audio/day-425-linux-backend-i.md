# Phase 26 / Day 425 — The Linux Backend I: Opening the Device

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Days 414, 419.

The platform seam for Linux. **Day 414's recommendation (the user confirms before this day starts): PipeWire through `libpipewire` (dynamically loaded, `ENGINE_AUDIO_HAVE_PIPEWIRE`) as the primary backend, and a raw-kernel ALSA ioctl backend (no library, about 50 lines to open and play, spike 1) as the fallback when PipeWire is absent**; `audio.backend` picks `auto`, `pipewire` or `alsa`. If the user chose one backend, drop the other's tasks.

## Tasks

- [x] `engine/audio/linux/` with the backend's files and `BACKEND.md` entries; raw system calls and the reference protocol only (no third-party convenience library; `libasound` only if the spike and the user chose it as the distro library, like `libwayland-client`).
- [x] Device enumeration: the cards and PCM devices (or the PipeWire graph's sinks), the default device, names and capabilities (rates, layouts, formats) into `DeviceInfo`; a machine with no sound device reports `NoDevice` by value and every test that needs one skips with a printed reason.
- [x] Opening: negotiate the rate, the channel count and the sample format closest to the request (prefer `f32` where the device takes it, else `S16` with the conversion of Day 416 at the device boundary), the buffer and period sizes from `audio.buffer_frames`; report what was granted in `DeviceInfo`.
- [x] Error mapping: every failure becomes a `DeviceStatus` with the OS error in the message; busy, permission, unsupported configuration and disconnected are distinct.
- [x] A `audio_probe` tool (`tools/audio_probe`): lists devices, plays a test tone for a duration through the real backend (a program, not a test; the user listens), prints the granted configuration and the underrun count.
- [x] Tests that need no hardware: the negotiation logic against a recorded capability table (a fake kernel interface or fake protocol peer), the format conversion at the boundary, the error mapping; a hardware test that is skipped without a device.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [x] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 440-441, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/audio.md` updated or created; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 26); only wrapped types, no `std`, no raw platform macros; the audio thread never allocates, locks or does I/O.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** `audio_probe` lists the devices of this machine and opens one with the granted configuration reported.

## Outcome (2026-11-17, Linux)

- The user chose PipeWire plus the raw ALSA fallback. `audio/pcm_negotiate.*` (portable negotiation and the sample conversion at the boundary, tested on recorded tables of an HD-audio codec and a USB DAC), `audio/linux/alsa_device.*` (raw `ioctl` on `/dev/snd`, enumeration, error mapping), `audio/linux/pipewire_device.*` (libpipewire loaded with `os::LoadLibrary`; headers optional, `ENGINE_AUDIO_HAVE_PIPEWIRE`), `audio/linux/linux_devices.*` (the `Platform` device: PipeWire, then ALSA), `DeviceKind::PipeWire/Alsa`, `DeviceStatus::PermissionDenied`, `tools/audio_probe`. On this machine `audio_probe play` ran through both backends with the configuration reported and 0 underruns (not listened to).
- Deviation: no `audio.backend` setting yet (the kinds are chosen in code; `audio_probe --backend`). Hardware tests print a warning and return without a device.
- Runs: see Day 423.

## Post-implementation code review

See Day 426.
