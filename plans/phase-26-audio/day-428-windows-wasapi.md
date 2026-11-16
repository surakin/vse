# Phase 26 / Day 428 — Windows: The WASAPI Backend, Build, Tests and Hashes

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Day 427; Phase 13.

On the Windows PC: a real backend through the OS API (WASAPI), and the rendered hashes equal Linux's.

## Tasks

- [ ] Pull and build with `windows-msvc` (plain and asan) and `windows-clang-cl` in both unity modes; `engine/audio/windows/` holds the backend (the Win32 and COM headers only there, through the project's wrappers: `windows.h` only through `engine/foundation/os/windows/win32.h`, the `platform_neutral_check` and `module_deps_check` gates enforce it).
- [ ] WASAPI in shared mode with event-driven buffering (`IAudioClient`, `IAudioRenderClient`, `IMMDeviceEnumerator`): enumerate the render endpoints, open the default or the named one, take the mix format (usually `f32` at 48 kHz) and convert at the boundary where the endpoint wants another, run the render thread with MMCSS class `Pro Audio` (best effort) and the event handle, recover from `AUDCLNT_E_DEVICE_INVALIDATED` (unplug, default device change) through Day 415's path, report latency from `GetStreamLatency`; exclusive mode is not built.
- [ ] `audio_probe` on Windows lists the endpoints and plays the test tone; the user listens (a human check recorded in `KNOWN-ISSUES.md` if nobody has).
- [ ] The cross-machine hashes: every reference scene of Day 425 and the audio sample's checksum are equal to Linux's, or the difference is a libm-only difference explained with the scene and block where it first differs (filters, reverb coefficients, the Vorbis tables) in `KNOWN-ISSUES.md`.
- [ ] The C API tests of Days 426-427 run on Windows (the DLL load path of Phase 23); the C++ layer builds.
- [ ] Run `scripts/test-phase.sh --affected` (plain and asan), quote the final `ran X of Y` lines and the hash table (scene, Linux, Windows) in this file; commit and push from the PC.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** The Windows PC plays through WASAPI, passes the audio tests, and every reference hash equals Linux's or is explained.
