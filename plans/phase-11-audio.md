# Phase 11 — Audio

**Depends on:** Phase 1 (Linux platform layer) for the audio device
output path; can proceed in parallel with Phases 3-4 and 7-10 once that exists.
**Goal:** a custom audio mixer with positional 3D sound, no codec/mixing
libraries.
**Definition of done:** the custom mixer plays a hand-decoded WAV file
with correct positional (3D) attenuation/panning relative to a listener.

## Key deliverables (to be expanded into day-by-day tasks when started)

- Linux audio output backend: raw ALSA (`/dev/snd` ioctl interface) or
  PipeWire's native protocol — decide which is the right "OS boundary"
  target given assumption #1 in root `PLAN.md`; needs its own small
  research spike since both are plausible interpretations of "the
  platform."
- WAV file parsing (simple, well-documented format — genuinely hand-
  rollable, unlike MP3/OGG which are out of scope for now).
- Mixer: multiple simultaneous voices, volume/pan, sample rate handling.
- 3D positional audio: distance attenuation, stereo panning from
  listener/emitter transforms (sourced from ECS `Transform`).

## Notes / open questions

- Full day-by-day breakdown deferred until started.
- Compressed audio formats (OGG/MP3) are explicitly out of scope until
  there's a real need — WAV only for now.
