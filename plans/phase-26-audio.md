# Phase 26 — Audio

**Status: planned (day files written 2026-11-17). `CURRENT_PHASE` stays 25 until Day 402. Next: Day 402.**

**Depends on:** Phase 17 (ECS: the components and the sync system), Phase 18 (spatial: which emitters are audible), Phase 22 (asset pipeline: audio clips are minted assets), Phases 23-24 (the C API), Phase 25 (physics: occlusion rays), Phase 11 (settings), Phase 9 (file layer: streaming), Phase 2 (jobs: decode-ahead), Phase 13 (the Windows platform layer) and Phase 21 (the macOS port) for their output backends.
**Goal:** an audio system built on a platform-independent mixer: voices with resampling, buses and effects, positional 3D sound with Doppler and occlusion, WAV and Ogg Vorbis (a hand-written decoder), streamed music, driven from the ECS and from a C host, with the mixer's output bit-identical on Linux, Windows and macOS (the device excluded).
**Definition of done:** a clip minted from a WAV or an Ogg plays through the mixer with correct gain, pan, pitch and distance cues relative to a listener, on Linux (the backend the Day 402 spike chose), Windows (WASAPI) and macOS (CoreAudio); buses, filters, delay and reverb work; music streams from disk with cross-fades; occluded emitters are dulled through the physics world; the device can be switched and lost at run time without losing the mix; a C host drives it through `engine_c`, either with an engine-owned device or by pulling the mix itself; the reference scenes' render hashes are equal on the three machines (or the difference is explained in `KNOWN-ISSUES.md`).

## Days

402. [Design Spec, Spikes and Phase Setup](phase-26-audio/day-402-design-spec-and-spikes.md)
403. [The Audio Module, Determinism Rules and the Block Hash](phase-26-audio/day-403-determinism-and-module-setup.md)
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
420. [Compressed Audio I: Ogg and the Vorbis Headers](phase-26-audio/day-420-ogg-vorbis-i.md)
421. [Compressed Audio II: Packet Decoding and the Inverse Transform](phase-26-audio/day-421-ogg-vorbis-ii.md)
422. [Compressed Audio III: Seeking, Loops and Minted Compressed Clips](phase-26-audio/day-422-vorbis-seeking-and-minting.md)
423. [Streaming Sources: Long Clips Without Loading Them](phase-26-audio/day-423-streaming-sources.md)
424. [Music: Cross-Fades, Layers, Stingers and Playlists](phase-26-audio/day-424-music-system.md)
425. [Snapshot, Replay and the Linux Hash Baseline](phase-26-audio/day-425-snapshot-replay-and-determinism.md)
426. [C API I: Clips, Voices, Buses and Emitters](phase-26-audio/day-426-c-api-i-clips-voices-and-emitters.md)
427. [C API II: Streams, Effects, Devices and the Audio Sample](phase-26-audio/day-427-c-api-ii-streams-effects-devices-and-sample.md)
428. [Windows: The WASAPI Backend, Build, Tests and Hashes](phase-26-audio/day-428-windows-wasapi.md)
429. [macOS: The CoreAudio Backend, Build, Tests and Hashes](phase-26-audio/day-429-macos-coreaudio.md)
430. [Benchmarks, Stress and the Limits](phase-26-audio/day-430-benchmarks-stress-and-limits.md)
431. [Docs, Closing Runs and the Phase Review](phase-26-audio/day-431-docs-closing-runs-and-phase-review.md)

Order reasoning: the spec and the spikes (402) settle the Linux backend, the resampler and the Vorbis fixtures before code; the determinism rules and the block hash (403) precede everything the tests compare; formats, WAV and the minted clip (404-406) give the mixer something to play; the device interface with its Null and File devices (407) lets every later day test without hardware, before the real backend (413-415); the mixer is built in three steps, voices (408), control through the command queue (409) and buses (410), then effects (411-412) which need the bus graph; the Linux backend and run-time reset (413-415) come after the mixer so a real device plays something that is already correct; the world integration (416-419: emitters, panning, Doppler, occlusion) needs the ECS, spatial and physics modules; Vorbis (420-422) is a self-contained decoder that only needs the clip and the mint; streaming and music (423-424) need the decoder and the job system; the snapshot and the Linux hash baseline (425) read the finished system; the C API (426-427) is built on a settled core; the PC and the Mac (428-429) check the hashes and add their backends; benchmarks (430) and the closing day (431) last. A phase cut after Day 419 would still have a complete engine-internal audio system with WAV playback.

## Decisions fixed by the plan (the Day 402 spec confirms or amends them)

- **One module, `engine/audio`, above `jobs`, `ecs` and `spatial`;** it reaches `physics` only through the occlusion interface and its one implementation file. No third-party audio library (the zero-convenience-dependency rule): the mixer, the resampler, the filters, the reverb and the Vorbis decoder are ours (`PLAN.md` assumption #30).
- **The device is the only per-platform code:** one interface where the mixer pulls interleaved `f32` frames from a render callback; backends are ALSA or PipeWire on Linux (**the Day 402 spike decides and recommends to the user before any backend code**), WASAPI on Windows, CoreAudio on macOS; Null and WAV-file devices exist on every platform and run every test.
- **Determinism (user, 2026-11-17): the mixer's output is a pure function of its inputs,** not of the device or the thread timing: a fixed internal block, commands stamped with block numbers, a fixed voice and bus order, `+ - * / Sqrt` on the audio thread, 32.32 fixed-point positions in the resampler. libm is used as is through `lang/scalar.h`, on the control side only (filter and reverb coefficients, table building); a libm-only difference between machines is recorded in `KNOWN-ISSUES.md`, not worked around (the user's rule of 2026-11-16).
- **The audio thread never allocates, locks or does I/O.** Commands arrive through a lock-free ring, events leave through another, streams are decoded ahead on a job and read from a ring.
- **Everything configurable is a setting** (`audio.*`, Phase 11); the device and the rate change at run time through one reset path (like the RHI), and a lost device falls back to the null device.
- **Extras, all in scope (user, 2026-11-17):** compressed formats (a hand-written Ogg Vorbis decoder, hostile-input rules, small committed fixtures), Doppler and occlusion, streaming music with cross-fades, layers and playlists, buses and effects (biquad filters, delay, reverb).
- **Hosts and ports (user, 2026-11-17):** a C API in two days with a host-driven mode (`audio_render` pulls the mix into the host's own audio callback), then a Windows day and a macOS day that add their backends and check the hashes. Android and iOS stay in Phase 29, the consoles in Phase 30.
- **Units:** metres, seconds, linear gain (decibel helpers on the control side), +Y up, the listener looks along -Z.

## Notes / open questions (settled in the Day 402 spec)

- The Linux backend (raw ALSA through `libasound` or the kernel interface, or PipeWire's native protocol), the default resampler quality, the Vorbis test-stream source and licence, whether Vorbis decoding is bit-exact across machines (its tables use `sin`/`cos`) or compared with a tolerance.
- Voice limit and virtualisation rule from the voice-cost spike.
- Not in this phase: HRTF and ambisonics, MIDI and synthesis, voice chat and capture (input devices), MP3 and other codecs, a loudness-normalised master, occlusion by diffraction, audio middleware import (FMOD or Wwise banks), exclusive-mode WASAPI.
- This is the largest phase planned so far (30 days); Phase 28 (3D text) moved from Days 402-417 to Days 432-447.
- The Windows and macOS days (428-429) are the real test of the determinism claim; Day 425 only gives the Linux reference. arm64 macOS compiles but cannot be run.
