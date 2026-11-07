# Phase 19 — Audio

**Depends on:** Phase 1 (Linux platform layer) for the Linux audio device
output path, and Phases 13 and 14 (the Windows and macOS platform layers) for
their output backends; can proceed in parallel with Phases 4-5 and 15-18
once those exist.
**Goal:** a custom audio mixer with positional 3D sound, no codec/mixing
libraries.
**Definition of done:** the custom mixer plays a hand-decoded WAV file
with correct positional (3D) attenuation/panning relative to a listener,
on Linux, Windows and macOS through one platform-independent mixer.

## Key deliverables (to be expanded into day-by-day tasks when started)

- Linux audio output backend: raw ALSA (`/dev/snd` ioctl interface) or
  PipeWire's native protocol — decide which is the right "OS boundary"
  target given assumption #1 in root `PLAN.md`; needs its own small
  research spike since both are plausible interpretations of "the
  platform."
- Windows audio output backend: WASAPI (shared mode, event-driven),
  the OS-provided API, so within assumption #1.
- macOS audio output backend: CoreAudio (an AudioUnit / `AudioQueue`
  render callback), likewise OS-provided.
- One audio-device interface (the mixer pulls interleaved float frames
  from a render callback) so the mixer never names a platform API;
  backend selection is the only per-platform code, the same shape as
  the RHI and window layers.
- WAV file parsing (simple, well-documented format — genuinely hand-
  rollable, unlike MP3/OGG which are out of scope for now).
- Mixer: multiple simultaneous voices, volume/pan, sample rate handling.
- 3D positional audio: distance attenuation, stereo panning from
  listener/emitter transforms (sourced from ECS `Transform`).

## Notes / open questions

- Mobile: Phase 22 extends this phase with the Android (AAudio) and iOS
  (CoreAudio, shared with the macOS backend where the API matches)
  output backends, plus the app-lifecycle handling audio needs (interruptions,
  route changes, pausing when backgrounded). The device interface above
  must be shaped so those are additions, not a redesign (assumption #11);
  consoles (Phase 23) follow the same pattern.

- Full day-by-day breakdown deferred until started.
- Compressed audio formats (OGG/MP3) are explicitly out of scope until
  there's a real need — WAV only for now.
