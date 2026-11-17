# Phase 26 / Day 441 — macOS: The CoreAudio Backend, Build, Tests and the Cross-Machine Comparison

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Day 440; Phase 21.

On the Mac: a real backend through CoreAudio, and the scenes match within the tolerance.

## Tasks

- [x] Pull and build `macos-clang` (x86-64; plain; no sanitizer runs for now) and `macos-clang-arm64` compile-only in both unity modes; `engine/audio/macos/` holds the backend (Apple headers and `.mm` files only there; ARC and `@autoreleasepool` as the other macOS code).
- [x] CoreAudio: an `AudioUnit` (`kAudioUnitSubType_DefaultOutput` or `HALOutput` for a chosen device) with a render callback that calls the mixer; device enumeration through `AudioObjectGetPropertyData`, the default device change and the device list change listeners (Day 427's reset), the stream format negotiated to `f32` non-interleaved or interleaved as the unit offers (converted at the boundary), the buffer size through `kAudioDevicePropertyBufferFrameSize`, latency from the device and stream properties.
- [x] `audio_probe` on the Mac plays the test tone; the user listens (recorded in `KNOWN-ISSUES.md` if not yet done).
- [x] The cross-machine comparison against the committed tables, within the tolerance, and this machine's own same-build hashes; arm64 is compile-only and its output is listed as unchecked.
- [x] The C API tests run on the Mac; the sample matches the committed table within the tolerance.
- [x] Record the final `ran X of Y` line and the comparison table in this file; commit and push from the Mac.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** The Mac plays through CoreAudio, passes the audio tests, and the scenes match Linux's and the PC's within the tolerance.

## Outcome

Done 2026-11-18 on the Mac (Intel MacBook Pro, the built-in speakers), at `b5da434b` (Day 440).

- **The backend:** `engine/audio/macos/coreaudio_device.{h,cpp}`, registered as `DeviceKind::Platform` on macOS (`CreateDevice`,
  `EnumerateDevices`). An AUHAL output unit (`kAudioUnitSubType_HALOutput`) bound to the device by
  `kAudioOutputUnitProperty_CurrentDevice`, with a render callback on CoreAudio's I/O thread; plain C APIs (AudioToolbox, CoreAudio,
  CoreFoundation, libdispatch), so a `.cpp` with no Objective-C, ARC or autorelease pool. Names are device UIDs
  (`kAudioDevicePropertyDeviceUID`, from `AudioObjectGetPropertyData` over `kAudioHardwarePropertyDevices`; input-only devices are
  left out), the default output marked. `Open` grants the device's nominal rate and its own channel count (`LayoutOf`), sets the
  wanted buffer size through `kAudioDevicePropertyBufferFrameSize` within `...BufferFrameSizeRange` and reads back the grant, gives
  the unit `f32` interleaved (one buffer per channel, de-interleaved from a scratch buffer, if the unit refuses it), and reports the
  buffer plus the device's latency, safety offset and first stream's latency. The default-output, device-list, device-alive and
  processor-overload listeners are blocks on a private serial queue: a loss (or a change of the default when the default was
  opened) reports `Lost` once for Day 427's `AudioSystem`, an overload counts as an underrun, and `Stop`/`Close` drain the queue and
  wait for a render call in flight, so nothing is called after they return. Links AudioToolbox, CoreAudio and CoreFoundation (SDK
  frameworks: nothing for `BUILD.md`). The gates already allowed Apple headers in a `macos/` folder; `platform_neutral_check`,
  `module_deps_check`, `std_usage_check` and `audio_backend_lists` pass.
- **`audio_probe`** is built on macOS now (the root `CMakeLists.txt`'s macOS list), `--backend=coreaudio` (accepted only on macOS,
  `wasapi` only on Windows: review). `list`: `BuiltInSpeakerDevice (default)  up to 2 channels`. `play --seconds=1`: `granted: 48000
  Hz, 2 channels, 1024 frames per call, latency 1394 frames`, `callbacks 47, underruns 0, slow 0, longest 47 us, frames 48128`
  (2 s: 94 calls, no underrun). **The user listened on 2026-11-18: a steady tone.** With the console locked `Start` failed
  (`AudioOutputUnitStart` status 35, and `afplay` failed too); it played once the screen was unlocked (`KNOWN-ISSUES.md`).
- **Tests:** the three hardware tests of `backend_test.cpp` run on macOS too, and three CoreAudio tests were added (the UID list,
  refusals by value, a device opened by its UID granting its own layout for a mono and a 5.1 request, reopening, and no render call
  after `Stop`): 9 of 9 `[backend]` cases pass against the speakers. The C API tests pass: `engine_c_audio` (`host_audio: ok`),
  `engine_c_audio_sample` and `engine_c_audio_streams`.
- **The cross-machine comparison** against the committed rows (the same rows as Day 440):

  | Scene | Row | Linux (committed) | Mac | Difference |
  |---|---|---|---|---|
  | buses | block 10 RMS | 0.152793 | within 0.000000104 | about -140 dB of full scale |
  | buses | block 30 RMS | 0.112413 | within 0.000000462 | about -127 dB |
  | buses | block 50 RMS | 0.220639 | within 0.000000402 | about -128 dB |
  | thousand voices | block 19 RMS | 0.002474 | within 0.000000375 | about -128 dB |
  | Vorbis `sine_mono.ogg` ch 0 block 0 | RMS | 0.355948 | 0.355948329 | 0 at six digits |
  | Vorbis `sine_mono.ogg` ch 0 block 4 | RMS | 0.355907 | 0.355906963 | 0 at six digits |
  | Vorbis `noise_mono.ogg` ch 0 block 1 | RMS | 0.213018 | 0.213017687 | 0 at six digits |
  | Vorbis `stereo_mix.ogg` ch 0 block 3 | RMS | 0.256521 | 0.256520569 | 0 at six digits |
  | Vorbis `stereo_mix.ogg` ch 1 block 3 | RMS | 0.256396 | 0.256396264 | 0 at six digits |
  | Vorbis `surround51.ogg` ch 3 block 1 | RMS | 0.256063 | 0.256063074 | 0 at six digits |

  The sample's table on the Mac: checksum `0xede7f5f84688c152`; RMS of blocks 0, 50, ..., 400: 0.0000, 0.1270, 0.1241, 0.1267,
  0.1467, 0.1102, 0.0948, 0.1114, 0.1214, the same as the PC's to four digits. **Same-build hashes**, committed as the macOS value of
  the new three-value `PlatformHash` (`tests/audio/platform_hash.h`): reference scene Linear `0xd2a3eb7663627cd5`, Hermite
  `0xa30c5966692fcf80`, Sinc `0x94d46fa909f4011e`; echo `0x8a305eea71f21699`, reverb `0xa321c9f1cedbc469`; Doppler flyby
  `0x27ca90661ecfd4f8`; buses `5105081423923813688`; thousand voices `2335128283347691732`. The Vorbis decode hashes match Linux's on
  the Mac (the two-value form). arm64 is compile-only: its output is unchecked.
- **Runs** (`build-macos`, x86-64, plain, console unlocked): `scripts/test-phase.sh --affected` (every phase: the phase's commits
  touched `cmake/` and `third_party/`): `ran 3682 of 10153 tests (phases: all; flavors: plain; display: included; crash tests:
  excluded) in 632 s`, 2 failed: `Throughput scales with participant count` (flaky, phase 2; 3 of 3 on rerun) and `Snapshot: the
  cost of a voice at each quality, with and without effects` (the Debug-build voice-cost bound that fails on the PC too: Hermite 60
  and sinc 191 percent of a core against 50). After the review fixes `scripts/test-phase.sh 26`: `ran 222 of 10153 tests (phases: 26;
  flavors: plain; display: included; crash tests: excluded) in 30 s`, the cost test the only failure. `ctest -L docs` 7 of 7; the
  gates 6 of 6. All four trees build clean (`build-macos`, `build-macos-nounity`, compile-only `build-macos-arm64` and `-arm64-nounity`), configured fresh. Not run: asan, tsan (plain only on the Mac), the shared tree.

## Post-implementation code review

`/code-review high` over the day's diff: 10 findings.

Fixed:
1. **A narrower stream than the device** (a mono request on stereo speakers) was handed to AUHAL as is and played from the left
   speaker only. `Open` now grants the device's own channel count, as WASAPI does. Regression check in `Backend: a CoreAudio device
   opened by its UID grants its own layout and reopens` (a mono and a 5.1 request both get the device's layout), shown to fail with
   the old rule (`0 == 1`).
2. **The interleaved path wrote `frames * channels` floats** into the unit's first buffer without checking the buffer count or size.
3. **The non-interleaved path** did not check each channel buffer and left extra buffers untouched. For 2 and 3, `BuffersFit` checks
   the count and every size against the format set in `Open`; a pull that does not fit is played as silence. No test: AUHAL's
   buffers cannot be planted (recorded in `KNOWN-ISSUES.md`).
4. **`ReportLost` read `error_` before `stop_`**, so a listener could race `SetCallbacks` on a stopped device. It reads `stop_`
   first now (false only between `Start` and `Stop`, when `SetCallbacks` may not run; `Stop` drains the queue). No test: the HAL's
   notifications cannot be made to fire.
5. **A failed read of the default output was taken as a change** and latched `Lost`; only a different device is a change now. No
   test, as for 4.
6. **A listener that failed to register was ignored**, leaving a device with no loss detection; `Open` fails with `IoError` now.
7. **`audio_probe` accepted `coreaudio` and `wasapi` on every platform**; each is accepted on its own platform only (checked by
   hand: `list --backend=wasapi` on the Mac prints the usage and exits 64; `--backend=coreaudio` lists the speakers).
8. **The slow-call bound used a full buffer's time** for a shorter tail chunk; each call is compared with the time of the frames
   it rendered.
10. **The two-value `PlatformHash` duplicated the selection** of the three-value one; it forwards now. (The finding's note that
    `STATUS.md` was not yet updated is met by this commit.)

Declined:
9. **The per-call counter bookkeeping is copied in four backends** (ALSA, PipeWire, WASAPI, CoreAudio). A shared helper would
   change the Linux and Windows backends, which the Mac cannot build or run; left for a day on the Linux machine
   (`KNOWN-ISSUES.md`, Day 441).

