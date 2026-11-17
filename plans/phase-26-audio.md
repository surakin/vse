# Phase 26 — Audio

**Status: in progress (day files written 2026-11-17; Day 402 done 2026-11-17: the design spec and six spikes; `CURRENT_PHASE` is 26). Next: Day 403.**

**Depends on:** Phase 17 (ECS: the components and the sync system), Phase 18 (spatial: which emitters are audible), Phase 22 (asset pipeline: audio clips are minted assets), Phases 23-24 (the C API), Phase 25 (physics: occlusion rays), Phase 11 (settings), Phase 9 (file layer: streaming), Phase 2 (jobs: decode-ahead), Phase 13 (the Windows platform layer) and Phase 21 (the macOS port) for their output backends.
**Goal:** an audio system built on a platform-independent mixer: voices with resampling, buses and effects, positional 3D sound with Doppler and occlusion, WAV and Ogg Vorbis (vendored libogg and libvorbis behind a facade), streamed music, driven from the ECS and from a C host, with the mixer's output repeatable on one build and equal within a tolerance on Linux, Windows and macOS (not byte-identical across machines).
**Definition of done:** a clip minted from a WAV or an Ogg plays through the mixer with correct gain, pan, pitch and distance cues relative to a listener, on Linux (the backend the Day 402 spike chose), Windows (WASAPI) and macOS (CoreAudio); buses, filters, delay and reverb work; music streams from disk with cross-fades; occluded emitters are dulled through the physics world; the device can be switched and lost at run time without losing the mix; a C host drives it through `engine_c`, either with an engine-owned device or by pulling the mix itself; the reference scenes render the same on one build and match committed reference tables within a stated tolerance on the three machines.

## Days

402. [Design Spec, Spikes and Phase Setup](phase-26-audio/day-402-design-spec-and-spikes.md)
403. [The Audio Module, Repeatability Rules and the Render Comparison](phase-26-audio/day-403-repeatability-and-module-setup.md)
404. [Sample Formats, Channel Layouts and Clip Data](phase-26-audio/day-404-sample-formats-and-clip-data.md)
405. [The WAV Parser](phase-26-audio/day-405-wav-parser.md)
406. [Minted Audio Clips](phase-26-audio/day-406-minted-audio-clips.md)
407. [The Device Interface, the Null Device and the File Device](phase-26-audio/day-407-device-interface-null-and-file.md)
408. [The Mixer I: Voices, Resampling, Gain and Pan](phase-26-audio/day-408-mixer-voices-and-resampling.md)
409. [The Mixer II: Playback Control, Gain Ramps and the Command Queue](phase-26-audio/day-409-playback-control-and-command-queue.md)
410. [Buses: Master, Music, Effects and Sends](phase-26-audio/day-410-buses-and-the-mix-graph.md)
411. [Effects I: Filters](phase-26-audio/day-411-filters.md)
412. [Effects II: Delay and Reverb](phase-26-audio/day-412-delay-and-reverb.md)
413. [The Linux Backend I: Opening the Device](phase-26-audio/day-413-linux-backend-i.md)
414. [The Linux Backend II: The Render Thread, Underruns and Latency](phase-26-audio/day-414-linux-backend-ii.md)
415. [Device Reset, Hot-Plug and Switching at Run Time](phase-26-audio/day-415-device-reset-and-switching.md)
416. [The Listener, Emitters and the ECS Integration](phase-26-audio/day-416-listener-emitters-and-ecs.md)
417. [Spatialisation: Panning to Stereo and Surround](phase-26-audio/day-417-panning-and-output-layouts.md)
418. [Doppler and Distance Filtering](phase-26-audio/day-418-doppler-and-air-absorption.md)
419. [Occlusion Through the Physics World](phase-26-audio/day-419-occlusion.md)
420. [Compressed Audio I: Vendoring libogg and libvorbis](phase-26-audio/day-420-vendor-libogg-libvorbis.md)
421. [Compressed Audio II: Decoding, Channel Order and the Determinism Record](phase-26-audio/day-421-vorbis-decoding-and-determinism.md)
422. [Compressed Audio III: Seeking, Loops and Minted Compressed Clips](phase-26-audio/day-422-vorbis-seeking-and-minting.md)
423. [Streaming Sources: Long Clips Without Loading Them](phase-26-audio/day-423-streaming-sources.md)
424. [Music: Cross-Fades, Layers, Stingers and Playlists](phase-26-audio/day-424-music-system.md)
425. [Snapshot, Replay and the Linux Baseline](phase-26-audio/day-425-snapshot-replay-and-determinism.md)
426. [C API I: Clips, Voices, Buses and Emitters](phase-26-audio/day-426-c-api-i-clips-voices-and-emitters.md)
427. [C API II: Streams, Effects, Devices and the Audio Sample](phase-26-audio/day-427-c-api-ii-streams-effects-devices-and-sample.md)
428. [Windows: The WASAPI Backend, Build, Tests and the Cross-Machine Comparison](phase-26-audio/day-428-windows-wasapi.md)
429. [macOS: The CoreAudio Backend, Build, Tests and the Cross-Machine Comparison](phase-26-audio/day-429-macos-coreaudio.md)
430. [Benchmarks, Stress and the Limits](phase-26-audio/day-430-benchmarks-stress-and-limits.md)
431. [Docs, Closing Runs and the Phase Review](phase-26-audio/day-431-docs-closing-runs-and-phase-review.md)

Order reasoning: the spec and the spikes (402) settle the Linux backend, the resampler and the Vorbis fixtures before code; the repeatability rules, the block hash and the tolerance comparison (403) precede everything the tests compare; formats, WAV and the minted clip (404-406) give the mixer something to play; the device interface with its Null and File devices (407) lets every later day test without hardware, before the real backend (413-415); the mixer is built in three steps, voices (408), control through the command queue (409) and buses (410), then effects (411-412) which need the bus graph; the Linux backend and run-time reset (413-415) come after the mixer so a real device plays something that is already correct; the world integration (416-419: emitters, panning, Doppler, occlusion) needs the ECS, spatial and physics modules; Vorbis (420-422) is a self-contained decoder that only needs the clip and the mint; streaming and music (423-424) need the decoder and the job system; the snapshot and the Linux hash baseline (425) read the finished system; the C API (426-427) is built on a settled core; the PC and the Mac (428-429) check the hashes and add their backends; benchmarks (430) and the closing day (431) last. A phase cut after Day 419 would still have a complete engine-internal audio system with WAV playback.

## Decisions fixed by the plan (the Day 402 spec confirms or amends them)

- **One module, `engine/audio`, above `jobs`, `ecs` and `spatial`;** it reaches `physics` only through the occlusion interface and its one implementation file. No third-party audio *convenience* library: the mixer, the resampler, the filters and the reverb are ours. **The one exception is libogg and libvorbis (the user, 2026-11-17), vendored under `third_party/`, decode only, included by one facade file and fed through hostile-input limits** (`PLAN.md` assumption #30).
- **The device is the only per-platform code:** one interface where the mixer pulls interleaved `f32` frames from a render callback; backends are ALSA or PipeWire on Linux (**the Day 402 spike decides and recommends to the user before any backend code**), WASAPI on Windows, CoreAudio on macOS; Null and WAV-file devices exist on every platform and run every test.
- **Repeatability, not cross-machine bit-equality (user, 2026-11-17: "we can tolerate audio output not being byte-per-byte the same on different machines"):** one build renders the same bits run to run, whatever the thread timing or the callback size (a fixed internal block, commands stamped with block numbers, a fixed voice and bus order); across compilers and machines the output is compared within a tolerance Day 402 sets (set on Day 402: max abs error 1e-5, -100 dB RMS; per-block RMS and peak plus sample probes against committed tables). Audio code may use libm and the default floating-point flags; no `Sqrt`-only or FMA-off rules.
- **The audio thread never allocates, locks or does I/O.** Commands arrive through a lock-free ring, events leave through another, streams are decoded ahead on a job and read from a ring.
- **Everything configurable is a setting** (`audio.*`, Phase 11); the device and the rate change at run time through one reset path (like the RHI), and a lost device falls back to the null device.
- **Extras, all in scope (user, 2026-11-17):** compressed formats (Ogg Vorbis through vendored libogg and libvorbis, hostile-input rules around them, small committed fixtures), Doppler and occlusion, streaming music with cross-fades, layers and playlists, buses and effects (biquad filters, delay, reverb).
- **Hosts and ports (user, 2026-11-17):** a C API in two days with a host-driven mode (`audio_render` pulls the mix into the host's own audio callback), then a Windows day and a macOS day that add their backends and check the hashes. Android and iOS stay in Phase 29, the consoles in Phase 30.
- **Units:** metres, seconds, linear gain (decibel helpers on the control side), +Y up, the listener looks along -Z.

## Notes / open questions (settled in the Day 402 spec)

- Settled on Day 402 (spec `docs/superpowers/specs/2026-11-17-audio-design.md`): the Linux backend is *recommended* as PipeWire through `libpipewire` plus a raw-kernel ALSA fallback, **awaiting the user's confirmation before Day 413**; resampler default Hermite; 256 mixed voices; tolerance 1e-5 / -100 dB; `audio_physics` is a separate target. Originally open: the Linux backend, the default resampler quality, the Vorbis test-stream source and licence, the pinned library versions, how far libvorbis output differs between builds (compared within the tolerance).
- Voice limit and virtualisation rule from the voice-cost spike.
- Not in this phase: HRTF and ambisonics, MIDI and synthesis, voice chat and capture (input devices), MP3 and other codecs, a loudness-normalised master, occlusion by diffraction, audio middleware import (FMOD or Wwise banks), exclusive-mode WASAPI.
- This is the largest phase planned so far (30 days); Phase 28 (3D text) moved from Days 402-417 to Days 432-447.
- The Windows and macOS days (428-429) check the scenes against the Linux tables within the tolerance; Day 425 gives the Linux reference. arm64 macOS compiles but cannot be run.
