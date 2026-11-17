# Phase 26 / Day 441 — macOS: The CoreAudio Backend, Build, Tests and the Cross-Machine Comparison

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Day 440; Phase 21.

On the Mac: a real backend through CoreAudio, and the scenes match within the tolerance.

## Tasks

- [ ] Pull and build `macos-clang` (x86-64; plain; no sanitizer runs for now) and `macos-clang-arm64` compile-only in both unity modes; `engine/audio/macos/` holds the backend (Apple headers and `.mm` files only there; ARC and `@autoreleasepool` as the other macOS code).
- [ ] CoreAudio: an `AudioUnit` (`kAudioUnitSubType_DefaultOutput` or `HALOutput` for a chosen device) with a render callback that calls the mixer; device enumeration through `AudioObjectGetPropertyData`, the default device change and the device list change listeners (Day 427's reset), the stream format negotiated to `f32` non-interleaved or interleaved as the unit offers (converted at the boundary), the buffer size through `kAudioDevicePropertyBufferFrameSize`, latency from the device and stream properties.
- [ ] `audio_probe` on the Mac plays the test tone; the user listens (recorded in `KNOWN-ISSUES.md` if not yet done).
- [ ] The cross-machine comparison against the committed tables, within the tolerance, and this machine's own same-build hashes; arm64 is compile-only and its output is listed as unchecked.
- [ ] The C API tests run on the Mac; the sample matches the committed table within the tolerance.
- [ ] Record the final `ran X of Y` line and the comparison table in this file; commit and push from the Mac.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** The Mac plays through CoreAudio, passes the audio tests, and the scenes match Linux's and the PC's within the tolerance.
