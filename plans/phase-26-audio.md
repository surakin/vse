# Phase 26 — Audio

**Paused (2026-11-17): the Phase 24 addendum (Days 373-384) runs first; this phase resumes at Day 415 and `CURRENT_PHASE` returns to 26 then. Phases 25, 26 and 29 moved up 12 days that day.**

**Status: in progress (day files written 2026-11-17; Day 414 done 2026-11-17: the design spec and six spikes; `CURRENT_PHASE` is 26). Next: Day 415.**

**Depends on:** Phase 17 (ECS: the components and the sync system), Phase 18 (spatial: which emitters are audible), Phase 22 (asset pipeline: audio clips are minted assets), Phases 23-24 (the C API), Phase 25 (physics: occlusion rays), Phase 11 (settings), Phase 9 (file layer: streaming), Phase 2 (jobs: decode-ahead), Phase 13 (the Windows platform layer) and Phase 21 (the macOS port) for their output backends.
**Goal:** an audio system built on a platform-independent mixer: voices with resampling, buses and effects, positional 3D sound with Doppler and occlusion, WAV and Ogg Vorbis (vendored libogg and libvorbis behind a facade), streamed music, driven from the ECS and from a C host, with the mixer's output repeatable on one build and equal within a tolerance on Linux, Windows and macOS (not byte-identical across machines).
**Definition of done:** a clip minted from a WAV or an Ogg plays through the mixer with correct gain, pan, pitch and distance cues relative to a listener, on Linux (the backend the Day 414 spike chose), Windows (WASAPI) and macOS (CoreAudio); buses, filters, delay and reverb work; music streams from disk with cross-fades; occluded emitters are dulled through the physics world; the device can be switched and lost at run time without losing the mix; a C host drives it through `engine_c`, either with an engine-owned device or by pulling the mix itself; the reference scenes render the same on one build and match committed reference tables within a stated tolerance on the three machines.

## Days

414. [Design Spec, Spikes and Phase Setup](phase-26-audio/day-414-design-spec-and-spikes.md)
415. [The Audio Module, Repeatability Rules and the Render Comparison](phase-26-audio/day-415-repeatability-and-module-setup.md)
416. [Sample Formats, Channel Layouts and Clip Data](phase-26-audio/day-416-sample-formats-and-clip-data.md)
417. [The WAV Parser](phase-26-audio/day-417-wav-parser.md)
418. [Minted Audio Clips](phase-26-audio/day-418-minted-audio-clips.md)
419. [The Device Interface, the Null Device and the File Device](phase-26-audio/day-419-device-interface-null-and-file.md)
420. [The Mixer I: Voices, Resampling, Gain and Pan](phase-26-audio/day-420-mixer-voices-and-resampling.md)
421. [The Mixer II: Playback Control, Gain Ramps and the Command Queue](phase-26-audio/day-421-playback-control-and-command-queue.md)
422. [Buses: Master, Music, Effects and Sends](phase-26-audio/day-422-buses-and-the-mix-graph.md)
423. [Effects I: Filters](phase-26-audio/day-423-filters.md)
424. [Effects II: Delay and Reverb](phase-26-audio/day-424-delay-and-reverb.md)
425. [The Linux Backend I: Opening the Device](phase-26-audio/day-425-linux-backend-i.md)
426. [The Linux Backend II: The Render Thread, Underruns and Latency](phase-26-audio/day-426-linux-backend-ii.md)
427. [Device Reset, Hot-Plug and Switching at Run Time](phase-26-audio/day-427-device-reset-and-switching.md)
428. [The Listener, Emitters and the ECS Integration](phase-26-audio/day-428-listener-emitters-and-ecs.md)
429. [Spatialisation: Panning to Stereo and Surround](phase-26-audio/day-429-panning-and-output-layouts.md)
430. [Doppler and Distance Filtering](phase-26-audio/day-430-doppler-and-air-absorption.md)
431. [Occlusion Through the Physics World](phase-26-audio/day-431-occlusion.md)
432. [Compressed Audio I: Vendoring libogg and libvorbis](phase-26-audio/day-432-vendor-libogg-libvorbis.md)
433. [Compressed Audio II: Decoding, Channel Order and the Determinism Record](phase-26-audio/day-433-vorbis-decoding-and-determinism.md)
434. [Compressed Audio III: Seeking, Loops and Minted Compressed Clips](phase-26-audio/day-434-vorbis-seeking-and-minting.md)
435. [Streaming Sources: Long Clips Without Loading Them](phase-26-audio/day-435-streaming-sources.md)
436. [Music: Cross-Fades, Layers, Stingers and Playlists](phase-26-audio/day-436-music-system.md)
437. [Snapshot, Replay and the Linux Baseline](phase-26-audio/day-437-snapshot-replay-and-determinism.md)
438. [C API I: Clips, Voices, Buses and Emitters](phase-26-audio/day-438-c-api-i-clips-voices-and-emitters.md)
439. [C API II: Streams, Effects, Devices and the Audio Sample](phase-26-audio/day-439-c-api-ii-streams-effects-devices-and-sample.md)
440. [Windows: The WASAPI Backend, Build, Tests and the Cross-Machine Comparison](phase-26-audio/day-440-windows-wasapi.md)
441. [macOS: The CoreAudio Backend, Build, Tests and the Cross-Machine Comparison](phase-26-audio/day-441-macos-coreaudio.md)
442. [Benchmarks, Stress and the Limits](phase-26-audio/day-442-benchmarks-stress-and-limits.md)
443. [Docs, Closing Runs and the Phase Review](phase-26-audio/day-443-docs-closing-runs-and-phase-review.md)

Order reasoning: the spec and the spikes (414) settle the Linux backend, the resampler and the Vorbis fixtures before code; the repeatability rules, the block hash and the tolerance comparison (415) precede everything the tests compare; formats, WAV and the minted clip (416-418) give the mixer something to play; the device interface with its Null and File devices (419) lets every later day test without hardware, before the real backend (425-427); the mixer is built in three steps, voices (420), control through the command queue (421) and buses (422), then effects (423-424) which need the bus graph; the Linux backend and run-time reset (425-427) come after the mixer so a real device plays something that is already correct; the world integration (428-431: emitters, panning, Doppler, occlusion) needs the ECS, spatial and physics modules; Vorbis (432-434) is a self-contained decoder that only needs the clip and the mint; streaming and music (435-436) need the decoder and the job system; the snapshot and the Linux hash baseline (437) read the finished system; the C API (438-439) is built on a settled core; the PC and the Mac (440-441) check the hashes and add their backends; benchmarks (442) and the closing day (443) last. A phase cut after Day 431 would still have a complete engine-internal audio system with WAV playback.

## Decisions fixed by the plan (the Day 414 spec confirms or amends them)

- **One module, `engine/audio`, above `jobs`, `ecs` and `spatial`;** it reaches `physics` only through the occlusion interface and its one implementation file. No third-party audio *convenience* library: the mixer, the resampler, the filters and the reverb are ours. **The one exception is libogg and libvorbis (the user, 2026-11-17), vendored under `third_party/`, decode only, included by one facade file and fed through hostile-input limits** (`PLAN.md` assumption #30).
- **The device is the only per-platform code:** one interface where the mixer pulls interleaved `f32` frames from a render callback; backends are ALSA or PipeWire on Linux (**the Day 414 spike decides and recommends to the user before any backend code**), WASAPI on Windows, CoreAudio on macOS; Null and WAV-file devices exist on every platform and run every test.
- **Repeatability, not cross-machine bit-equality (user, 2026-11-17: "we can tolerate audio output not being byte-per-byte the same on different machines"):** one build renders the same bits run to run, whatever the thread timing or the callback size (a fixed internal block, commands stamped with block numbers, a fixed voice and bus order); across compilers and machines the output is compared within a tolerance Day 414 sets (set on Day 414: max abs error 1e-5, -100 dB RMS; per-block RMS and peak plus sample probes against committed tables). Audio code may use libm and the default floating-point flags; no `Sqrt`-only or FMA-off rules.
- **The audio thread never allocates, locks or does I/O.** Commands arrive through a lock-free ring, events leave through another, streams are decoded ahead on a job and read from a ring.
- **Everything configurable is a setting** (`audio.*`, Phase 11); the device and the rate change at run time through one reset path (like the RHI), and a lost device falls back to the null device.
- **Extras, all in scope (user, 2026-11-17):** compressed formats (Ogg Vorbis through vendored libogg and libvorbis, hostile-input rules around them, small committed fixtures), Doppler and occlusion, streaming music with cross-fades, layers and playlists, buses and effects (biquad filters, delay, reverb).
- **Hosts and ports (user, 2026-11-17):** a C API in two days with a host-driven mode (`audio_render` pulls the mix into the host's own audio callback), then a Windows day and a macOS day that add their backends and check the hashes. Android and iOS stay in Phase 29, the consoles in Phase 30.
- **Units:** metres, seconds, linear gain (decibel helpers on the control side), +Y up, the listener looks along -Z.

## Notes / open questions (settled in the Day 414 spec)

- Settled on Day 414 (spec `docs/superpowers/specs/2026-11-17-audio-design.md`): the Linux backend is *recommended* as PipeWire through `libpipewire` plus a raw-kernel ALSA fallback, **awaiting the user's confirmation before Day 425**; resampler default Hermite; 256 mixed voices; tolerance 1e-5 / -100 dB; `audio_physics` is a separate target. Originally open: the Linux backend, the default resampler quality, the Vorbis test-stream source and licence, the pinned library versions, how far libvorbis output differs between builds (compared within the tolerance).
- Voice limit and virtualisation rule from the voice-cost spike.
- Not in this phase: HRTF and ambisonics, MIDI and synthesis, voice chat and capture (input devices), MP3 and other codecs, a loudness-normalised master, occlusion by diffraction, audio middleware import (FMOD or Wwise banks), exclusive-mode WASAPI.
- This is the largest phase planned so far (30 days); 3D text moved from Days 414-429 to Days 444-459 and then, when Phase 27 was planned (2026-11-17), to Days 478-493 (on 2026-11-23 it became a future project, `plans/future/3d-text-and-color-glyphs.md`).
- The Windows and macOS days (440-441) check the scenes against the Linux tables within the tolerance; Day 437 gives the Linux reference. arm64 macOS compiles but cannot be run.
