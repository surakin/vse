# Phase 26 / Day 440 — Windows: The WASAPI Backend, Build, Tests and the Cross-Machine Comparison

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Day 439; Phase 13.

On the Windows PC: a real backend through the OS API (WASAPI), and the rendered scenes match Linux's within the tolerance.

## Tasks

- [x] Pull and build with `windows-msvc` (plain and asan) and `windows-clang-cl` in both unity modes; `engine/audio/windows/` holds the backend (the Win32 and COM headers only there, through the project's wrappers: `windows.h` only through `engine/foundation/os/windows/win32.h`, the `platform_neutral_check` and `module_deps_check` gates enforce it).
- [x] WASAPI in shared mode with event-driven buffering (`IAudioClient`, `IAudioRenderClient`, `IMMDeviceEnumerator`): enumerate the render endpoints, open the default or the named one, take the mix format (usually `f32` at 48 kHz) and convert at the boundary where the endpoint wants another, run the render thread with MMCSS class `Pro Audio` (best effort) and the event handle, recover from `AUDCLNT_E_DEVICE_INVALIDATED` (unplug, default device change) through Day 427's path, report latency from `GetStreamLatency`; exclusive mode is not built.
- [x] `audio_probe` on Windows lists the endpoints and plays the test tone; the user listens (a human check recorded in `KNOWN-ISSUES.md` if nobody has).
- [x] The cross-machine comparison: every reference scene of Day 437 and the audio sample are rendered and compared with the committed tables within the tolerance; the same-build hash is committed for this machine as its own baseline; a scene outside the tolerance is a bug to find (a wrong platform conversion, a rate or layout difference), not accepted bits.
- [x] The C API tests of Days 438-439 run on Windows (the DLL load path of Phase 23); the C++ layer builds.
- [x] Run `scripts/test-phase.sh --affected` (plain and asan), quote the final `ran X of Y` lines and the comparison table (scene, max abs error, RMS error in dB against Linux) in this file; commit and push from the PC.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** The Windows PC plays through WASAPI, passes the audio tests, and every reference scene matches Linux's within the tolerance.

## Outcome

Done 2026-11-17 on the Windows PC (AMD Ryzen with a Realtek output), at `44b95e83` (Days 437-439).

- **The backend:** `engine/audio/windows/wasapi_device.{h,cpp}`, WASAPI in shared mode, event-driven (`IMMDeviceEnumerator`,
  `IAudioClient`, `IAudioRenderClient`), registered as `DeviceKind::Platform` on Windows (`CreateDevice`, `EnumerateDevices`). Names
  are endpoint ids, the default (console role) is marked; `Open` grants the endpoint's mix rate and channel count and writes `f32`
  as is or through `ConvertForPcm` for 16- and 32-bit integer endpoints; the render thread joins MMCSS `Pro Audio` (best effort);
  `AUDCLNT_E_DEVICE_INVALIDATED`, the endpoint leaving the active state, or a change of the default output when the default was
  opened report `Lost` once (an `IMMNotificationClient` sets a flag and wakes the thread), which Day 427's `AudioSystem` reopens;
  latency is the buffer plus `GetStreamLatency`. Exclusive mode is not built. `LayoutOf` moved from the ALSA backend to
  `pcm_negotiate.h` for both. The gates learned the directory: `engine/audio/windows/` in `platform_neutral_check` and
  `module_deps_check` (`ARCHITECTURE.md`), and `avrt` joined the SDK header list. Links `ole32` and `avrt`.
- **`audio_probe`** builds on Windows now (it was in the Linux list of the root `CMakeLists.txt` only; `--backend=wasapi` is the
  platform's). `list`: one endpoint, `{0.0.0.00000000}.{69388c2f-1269-4f13-980f-c8ffe95b8aae} (default)  up to 2 channels`. `play
  --seconds=3`: `granted: 48000 Hz, 2 channels, 2048 frames per call, latency 2048 frames`, `callbacks 301, underruns 0, slow 0,
  longest 21 us, frames 144480`. **The user listened on 2026-11-17: the tone sounds clean.**
- **Tests:** `backend_test.cpp`'s three hardware tests (own pace, a stalled callback counted, destruction waits for the callback)
  run on Windows too, and two WASAPI tests (the endpoint list, refusals by value) were added: 8 of 8 `[backend]` cases pass, plain
  and asan, against the real endpoint. The C API tests of Days 438-439 pass statically linked and through the DLL
  (`engine_c_audio`, `engine_c_audio_sample`, `engine_c_audio_streams`, plain and asan).
- **The cross-machine comparison.** Day 437 committed reference tables for two scenes (the other scenes of the plan carry
  same-build hashes only, and the sample's level table is not committed on Linux). Against the committed rows, under cl and
  clang-cl:

  | Scene | Row | Linux (committed) | Windows | Difference |
  |---|---|---|---|---|
  | buses | block 10 RMS | 0.152793 | 0.152793 | 0 at six digits |
  | buses | block 30 RMS | 0.112413 | 0.112413 | 0 at six digits |
  | buses | block 50 RMS | 0.220639 | 0.220639 | 0 at six digits |
  | thousand voices | block 19 RMS | 0.002474 | 0.00247437 | 0.00000037 (about -136 dB of full scale) |

  Max abs error and RMS error in dB against Linux's samples cannot be computed: Linux committed no samples or summaries. The
  sample's table on Windows, for Linux to compare: checksum `0x314e20b320eaf20a`; RMS of blocks 0, 50, ..., 400: 0.0000, 0.1270,
  0.1241, 0.1267, 0.1467, 0.1102, 0.0948, 0.1114, 0.1214. **Same-build hashes:** the buses and thousand-voice scenes give
  `381981972835177576` and `12564515357694794300` here (Linux `10403398739231254235`, `7677309547114575656`), committed as Windows'
  slot of `PlatformHash`.
- **Runs.** The `--affected` selection is every phase (the phase's commits touched `cmake/` and `third_party/`), which holds the phase
  8 reset-loop test that froze the PC on Day 410, so it ran through ctest with that test left out (the user's rule of Day 381):
  `build-msvc` `ran 3639 of 7033 tests (the `--affected` selection, every phase because the phase touched `cmake/` and `third_party/`, but the phase 8 reset-loop test; flavor: plain; display included; crash tests excluded) in 236 s`; `build-msvc` `ran 3109 of 7033 tests (the same selection, flavor asan) in 265 s`; `build-clang-cl` `ran 3669 of 3826 tests (the same selection, flavor plain) in 321 s`. Failures: the three gates on the
  new directory (fixed: below; then 85 of 85 in both trees) and `Snapshot: the cost of a voice at each quality, with and without
  effects` (plain, both compilers; below). After the fixes `ctest -L "^phase26$"` 438 of 439 (`build-msvc`) and 220 of 221
  (`build-clang-cl`), the cost test failing. All six trees build clean. `ctest -L docs` passes.

## Post-implementation code review

`/code-review high` over the day's diff, done inline: 4 findings. **Fixed (1):** the GUID tail table was a raw `unsigned char`
array (`std_usage_check`): now `Array<u8, 8>`. **Recorded (3):** the loss and default-change paths have no test (nothing on the PC
can unplug the endpoint or change the default output without changing system settings; `KNOWN-ISSUES.md`); the notification
client reads the event handle that `Close` closes after unregistering, safe only if `UnregisterEndpointNotificationCallback` does
not return while a callback runs (not changed: the documented contract); the cost bound fails in the PC's Debug builds
(`KNOWN-ISSUES.md`).
