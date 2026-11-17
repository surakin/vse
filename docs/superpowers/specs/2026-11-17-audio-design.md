# Audio design (Phase 26, Day 414, 2026-11-17)

A platform-independent mixer with voices, buses and effects, positional 3D sound, streamed music, and Ogg Vorbis through vendored libogg and libvorbis,
driven from the ECS and from a C host. This spec fixes the open questions of `plans/phase-26-audio.md` with numbers from six spikes (appendix; sources in
`2026-11-17-audio-spikes/`). Where a spike moved a plan default, it says so.

## 1. Scope and layout

- **One module, `engine/audio`**, layer 13 (beside `physics`, `asset` and `scene`); depends on `foundation` (including `settings`, `io`, `lang`, `sync`, `cstr`,
  `os`), `jobs`, `ecs` (the world transform in `ecs/transform.h`), `spatial` (audible-emitter lookup) and `asset` (the minted clip; the one same-layer edge,
  allowed in `modules.txt` when first used on Day 418); later `storage` for snapshots (Day 437). **It never includes `physics`:** occlusion is an interface
  (`OcclusionQuery`, Day 431) and its one implementation lives in a separate small target **`audio_physics`**, layer 14 beside `engine_c`, which depends on
  `audio` and `physics` (this keeps the layering rule: a module links only modules below it). Added on Day 415 (`ARCHITECTURE.md`, `modules.txt`,
  `tests/module_map.txt`, `docs/reference/modules.txt`, `BUILD.md`).
- **Not built:** HRTF and ambisonics, MIDI and synthesis, voice chat and capture, MP3 and other codecs, a loudness-normalised master, occlusion by
  diffraction, audio middleware banks, exclusive-mode WASAPI.
- **The mixer**: a `Mixer` owns the voice pool, the bus tree, the effect states, the command queue (game thread to audio thread), the event ring (back) and
  the audio clock (frames rendered). It renders in **fixed blocks of 256 frames** (`kBlockFrames`) at the device's rate. A device callback of any size pulls from
  the block renderer with a carry-over, so the output does not depend on the callback size (spike 2: callback sizes 64, 100, 256 and 1,024 give the same hash).
- **The audio thread never allocates, locks or does I/O.** Commands come through a single-producer single-consumer ring, events leave through another, streams
  are decoded ahead on a job and read from a ring (Day 435), clips are immutable shared data.
- **Frame and units:** metres, seconds, **linear gain** (dB helpers on the control side), +Y up, the listener looks along **-Z** like the camera, right-handed;
  sample rate per device (8,000 to 384,000), at most 8 channels; internal sample type `f32`.

## 2. Decisions

| Question | Decision | Evidence |
|---|---|---|
| Mixer block | **256 frames**, fixed; callback size independent | Spike 2 |
| Resampler | Position is a **32.32 fixed-point `u64`**; three qualities: `0` linear, **`1` 4-point cubic Hermite (default)**, `2` 16-tap windowed sinc (32 phases, music and offline renders). | Spike 3 |
| Voice limit | `audio.max_voices` **256 mixed voices** (default); up to **4,096 tracked** (the rest are virtual: position advances, nothing is mixed); a voice over the limit is virtualised by lowest loudness, then priority, then slot. | Spike 6 |
| Command timing | Every command carries the block it takes effect at; a post stamps **current block + 1** (`lead` of one block, about 5.3 ms at 256/48k); offline renders post before the render of that block. | Spike 4 |
| Cross-machine rule | **Repeatable on one build, equal within a tolerance across machines** (the user, 2026-11-17): max absolute sample error **1e-5** (-100 dBFS) and RMS error **-100 dB** against the committed reference table. | Spike 2 |
| Floating point | The project's flags apply (`-ffp-contract=off`, no fast-math: they cover every target of ours); audio code may use libm. No `Sqrt`-only rule. | Spike 2 |
| Pan law | Equal power: `left = Sqrt((1 - p) / 2)`, `right = Sqrt((1 + p) / 2)`; -3 dB at the centre. | Plan |
| Gain ramp | Every gain, pan and pitch change ramps linearly over `audio.ramp_frames` (64); the target is exact on the last frame. | Plan |
| Linux backend | **Recommended, pending the user's confirmation before Day 425:** PipeWire through `libpipewire` (the reference client library of the OS audio server, the same boundary as `libwayland-client`), **plus a raw-kernel ALSA fallback** (about 50 lines of ioctls, no library) for systems without PipeWire. See 3. | Spike 1 |
| Vorbis | **Vendored libogg 1.3.6 and libvorbis 1.3.7** (decode only, 21 C files), facade `audio/vorbis.cpp`; BSD-3-Clause. | Spike 5 |
| Test streams | Four small `.ogg` fixtures made once with `oggenc` (libvorbis 1.3.7), committed with the recipe. | Spike 5 |
| Device reset | The mixer owns the render state; `AudioSystem::ApplySettings` changes device, rate and layout at run time; a lost device falls back to the null device. | Plan |
| Occlusion | `OcclusionQuery` interface in `audio`; physics ray casts in `audio_physics`. | 1 |
| Snapshot | Through `engine/storage`'s byte encoding (Day 437). | Plan |
| State hash | FNV-1a 64 over the bit patterns of the output samples in order (-0.0 as +0.0; NaN or infinity invalidate); a same-build regression check, not a cross-machine one. | Spike 2 |

## 3. The Linux backend (the recommendation)

What each route costs and does, measured on this machine (PipeWire 1.6.11 serving a USB DAC as the default sink; card 3 is an analogue HD-audio codec):

| Route | Code to play | Periods (256 / 1,024 frames at 48 kHz) | Notes |
|---|---|---|---|
| libasound `default` | 34 lines | **fails here**: `default` routes to dmix on card 0, which cannot open (this is the distro's ALSA configuration, not ours) | the system's `pipewire` PCM works: 5.29 ms / 21.35 ms, delay 29 ms at 256 |
| libasound `hw:3,0` | same | 5.333 ms / 21.333 ms (exact), delay 16.6 ms / 64.6 ms; F32 only through `plughw` (the hardware takes S16) | opened while PipeWire was running; two programs opened the same device concurrently and both worked (observed, not understood) |
| **raw kernel ioctls** on `/dev/snd/pcmC3D0p` | **49 lines** (`PVERSION`, `HW_PARAMS` with masks and intervals, `SW_PARAMS`, `PREPARE`, `WRITEI_FRAMES`, `DELAY`) | 5.333 ms / 21.333 ms, delay 16.6 ms / 64.6 ms, 0 xruns in 200 periods | S16 on this codec; we would convert at the boundary (Day 416 helpers); device list needs `/proc/asound` or the control ioctls; no server routing |
| PipeWire native protocol, hand-written | the handshake is 23 lines (the server answered `Core::Info`, 2,384 bytes); a playback stream still needs the registry, a client-node, node and port parameters, a memfd buffer pool with eventfd wakeups: **estimated 1,500+ lines** | not built | rejected: a large protocol to maintain with no gain over the library |
| **`libpipewire` `pw_stream`** | 60 lines | **5.292 ms / 21.351 ms** (the requested `node.latency`), F32 stereo accepted, no conversion | the server does routing (the user's default sink, Bluetooth, network and per-application volume), hot-plug and rate conversion; headers are installed here (`libpipewire-0.3` 1.6.11) |

**Why PipeWire first:** on a desktop Linux of this decade the sound server owns routing, and the user's chosen output (a USB DAC, a Bluetooth speaker) is often not
reachable as a raw `hw:` device at all; here the ALSA `default` does not work. **Why still the raw ALSA ioctl backend:** it needs no library and no server (a
headless machine, an embedded board, a container with a sound device, a system without PipeWire), 49 lines prove it works, and it is the fallback when
`libpipewire` is absent or the connection fails. **Assumption #1:** `libpipewire` is the OS audio server's own client library, the same category as
`libwayland-client`; the build uses it when its headers exist (`ENGINE_AUDIO_HAVE_PIPEWIRE`, the way `ENGINE_RHI_HAVE_VULKAN` guards Vulkan) and links it
dynamically, so a binary built with it still runs where the library is missing (the backend then falls through to ALSA). **The decision is the user's:** if
they prefer a single backend, the plan's Days 425-427 shrink (ALSA only) or keep one backend (PipeWire only) without any other change.

The device interface (`audio/device.h`, Day 419) is the only per-platform seam; the mixer pulls interleaved `f32` frames from a render callback.

## 4. Repeatability and tolerance (spike 2)

The reference scene (24 looped voices at three interpolation qualities, equal-power pan with ramps, a biquad low-pass per voice, an 8-line Hadamard reverb fed
by a send, two commands applied at fixed block stamps, 300 blocks) was rendered by every combination of GCC 16 and Clang 23, `-O0`, `-O2`, `-O3`, with
`-ffp-contract=off` and `=fast -march=native`:

- all `-ffp-contract=off` builds are **bit-identical** (hash `88cc6e64680ee4f8`), at any optimisation level, under both compilers;
- the callback sizes 64, 100, 256 and 1,024 are bit-identical to the 256 case;
- `-ffp-contract=fast -march=native` (FMA) changes the output by **at most 3.3e-7** absolute (**-134 dB** RMS), the same under GCC and Clang.

The project builds every target with contraction off, so audio is bit-identical across builds on one machine; across machines the differences come from
libm (the `sin`/`cos` of filter and reverb coefficient tables), which the FMA experiment bounds from above for this scene: the tolerance is set at **1e-5**
absolute and **-100 dB** RMS, about thirty times the FMA difference. The cross-machine check compares per-block RMS and peak and 64 sample probes per
scene with the committed table (`audio/render_compare.h`, Day 415); the same-build hash is committed as a regression check and re-committed when a compiler
change moves the bits.

## 5. The resampler and the voice cost (spikes 3 and 6)

Interpolation error against the ideal tone (dB below the tone, lower is better):

| Tone | linear | Hermite | sinc16 |
|---|---|---|---|
| 1 kHz | -56 | -92 to -106 | -87 |
| 5 kHz | -28 | -48 to -51 | -84 to -92 |
| 10 kHz | -16 | -27 | -71 to -76 |
| 15 kHz | -10 | -15 | -65 to -71 |

Cost per output sample, one channel, looping clip, not yet tuned: linear 2.8 ns, Hermite 5.4 ns, sinc16 21.6 ns. Cost of a 256-frame block (5.33 ms budget at
48 kHz), one core, stereo mixing without effects:

| Voices | linear | Hermite | sinc16 |
|---|---|---|---|
| 1,000 | 1.36 ms (25%) | 1.93 ms (36%) | 6.24 ms (117%) |
| 5,000 | 6.32 ms (119%) | 9.67 ms (181%) | 31.2 ms (586%) |

So **256 mixed voices** cost about 9 percent of a core at Hermite, which sets the default and leaves room for effects and the job-system budget; 1,000 voices at
linear is affordable on request (`audio.max_voices` up to 1,024; the setting's upper bound). Sinc16 is for music, a few voices and offline renders; its loop
is naive (a bounds-checked fetch per tap) and Day 420 blocks it by block-level wrap handling, which the numbers above do not include. **Fixed point:** the 32.32
position drifts by 0.12 frames after 10^9 frames at a ratio of 1.0000001 (about six hours of audio), while an `f32` accumulator stalls at 33,554,432 frames
(about 12 minutes at 48 kHz); the fixed-point position is a requirement, not an optimisation.

## 6. The command queue (spike 4)

A power-of-two single-producer single-consumer ring of 24-byte-class commands (`stamp`, `id`, payload); the consumer applies, before rendering block `b`,
every command with `stamp <= b` in post order. Offline (the file device, tests): the render of block `b` waits until the producer published a watermark past
`b`; six random producer schedules (sleeps, yields) gave one hash (`a67879bdcd4f45c3`). Live (paced at 256 frames at 48 kHz, 1,500 blocks, commands every
0.5-3.5 ms): with `lead` 0 (stamp = the block being rendered) **3,838 of 3,839 commands were late** (applied at the current block, not at their stamp);
with **`lead` 1: none late**, post-to-apply latency median 2.7 ms, maximum 8.4 ms; with lead 2: median 8.0 ms. **Default lead: one block**; a command that
arrives late is applied at the current block and counted (`MixerStats::late_commands`); a full ring returns a status and counts.

## 7. Buses, voices and effects

- **Bus tree:** `master` always exists; `music`, `sfx`, `voice` and user buses are made by name; each has gain, mute, solo, parent and up to four inserts;
  buses are summed child before parent, in creation order within a parent. Sends (a gain, pre or post fader) go to auxiliary buses (reverb); a cycle is refused.
- **Master:** gain, a soft-knee peak limiter with fixed look-ahead, then the hard clip to [-1, 1] at the device boundary only.
- **Voices:** a clip (`AudioClipPtr`), a position (32.32), pitch, gain, pan, bus, loop, priority, a `VoiceId` (generation-checked), up to four inserts.
- **Effects:** biquad (low-pass, high-pass, band-pass, notch, peaking, shelves), one-pole, feedback delay, 8-line feedback delay network reverb with
  Hadamard feedback and per-line damping; `TailFrames()` keeps a tail after the input stops. Coefficient maths on the control side (libm), per-sample
  arithmetic plain `+ - * /`.
- **Ducking and metering:** one bus ducks another by a level with a release; RMS and peak per bus per block are readable from the game thread.

## 8. ECS, spatial and the world

- **Components** (`audio/components.h`, registered like the spatial ones): `AudioSource` {clip (resource id), bus, gain, pitch, loop, play-on-start,
  spatial flag, min and max distance, rolloff model (inverse, linear, exponential, curve), cone inner and outer angle and outer gain, priority, doppler and
  occlusion switches}; `AudioListener` {gain; the one active listener is the entity with the lowest id carrying it}.
- **Sync system:** reads the world transforms (`ecs/transform.h`), moves only what changed (the cost is what moved); emitters live in a `spatial::SpatialSet` by
  their audible radius (where the rolloff falls below the audibility threshold, -60 dB by default); a listener query returns the emitters in range; those
  past `max_voices` are ranked by loudness, then priority, then entity order (deterministic).
- **Doppler and air absorption:** velocities from transform deltas (a jump over `audio.teleport_distance`, 10 m per frame by default, is not a velocity); the
  ratio `(c - v_listener) / (c + v_emitter)` with `c = 343 m/s` (`audio.speed_of_sound`) and the factor `audio.doppler_factor`, clamped to [0.5, 2].
- **Occlusion:** `OcclusionQuery::Occlusion(from, to)` returns 0 to 1; the physics implementation casts a ray with `RayCastAll` and sums the thickness of the
  colliders crossed with a per-material absorption; at most `audio.occlusion_rays_per_frame` (16) rays per frame, round-robin in entity order, results smoothed over
  0.15 s into a one-pole low-pass cutoff and a gain.

## 9. Devices, the reset and settings

- **Interface:** `Device::Open(DeviceConfig)`, `Start`, `Stop`, `Close`, `Info()` (granted rate, layout, buffer frames, latency frames), a render callback
  `void(f32* interleaved, u32 frames)` and an error callback; `DeviceStatus` values for every failure; `EnumerateDevices`. **Null** (the test clock advances by
  `Pump`), **WAV file** (offline, renders as fast as asked) and **host** (the C API's `audio_render` pulls) devices exist on every platform.
- **Run-time reset:** `AudioSystem::ApplySettings` is classified like `Renderer::ApplySettings` (volume: free; rate or layout: re-plans resamplers and effect
  lengths; device: reopens), the voices go on from where they were; a lost device switches to the null device (the audio clock keeps advancing), retries on a
  timer and on a device-added event.
- **Settings** (`settings::Registry`, each with `env` `ENGINE_AUDIO_*` and `--audio-*` flag names): `audio.device` (`default`; restart of the device),
  `audio.sample_rate` (0 = the device's), `audio.channels` (0 = the device's), `audio.buffer_frames` (0 = 256), `audio.master_volume` (0 to 2, live),
  `audio.max_voices` (256, live), `audio.resampler_quality` (1, live for new voices), `audio.ramp_frames` (64, live), `audio.doppler_factor`,
  `audio.speed_of_sound`, `audio.occlusion` (on), `audio.occlusion_rays_per_frame`, `audio.stream_ring_blocks` (4), `audio.backend` (`auto`, `pipewire`, `alsa`).

## 10. Ogg Vorbis (spike 5)

- **Libraries:** libogg 1.3.6 (sha256 `5c82534...a1061`) and libvorbis 1.3.7 (sha256 `b33cc49...72954b`), BSD-3-Clause, vendored as source under
  `third_party/libogg/` and `third_party/libvorbis/` on Day 432, with `VENDORED.md`. Decode-only: 21 C files (libogg `bitwise.c`, `framing.c`; libvorbis
  `analysis bitrate block codebook envelope floor0 floor1 info lookup lpc lsp mapping0 mdct psy registry res0 sharedbook smallft synthesis window`); not built:
  `vorbisenc.c`, `vorbisfile.c`, `barkmel.c`, `psytune.c`. `analysis`, `bitrate`, `envelope` and `psy` are needed only because `block.c` references them. All
  21 compile with Clang 23 and GCC 16 at `-O2` without our warning flags; **the vendored files do not get `-Werror`** (they sit after `third_party/`).
- **Decoding:** four fixtures (6.9, 31.8, 28.8 and 20.2 KB) decode through the plain `ogg_sync` / `ogg_stream` / `vorbis_synthesis` loop with the right
  channels, rate and frame counts (88,200; 88,200; 132,300; 48,000); 2 s of mono audio decodes in 0.8 ms (a 100x real-time margin).
- **Decoder repeatability:** with contraction off, GCC and Clang, `-O2`, give the same output hash for every fixture; with contraction on and
  `-march=native` the hash differs (different again between GCC and Clang): the same rule as the mixer.
- **Hostile input:** 8,000 mutations (bit flips, truncation, random overwrite, block swaps) of the four fixtures under AddressSanitizer (with leak detection):
  **no crash, hang or leak**; 4,880 still decoded, 711 were refused at the headers; the slowest took 5.6 ms. **UBSan reports on valid streams too:** left shifts
  of negative or overflowing signed values in `sharedbook.c:425` (benign on two's-complement machines). So **the
  vendored files are built without UBSan** (the sanitizer flavors instrument ours only, `UBSAN_OPTIONS` or per-source flags), recorded in `KNOWN-ISSUES.md`;
  the fuzz stays under ASan. The facade still adds input limits, a step budget, a frame cap and an allocation-aware setup limit (Day 432); the library's
  `malloc` calls cannot be intercepted without editing `os_types.h` (its `_ogg_malloc` is an unconditional macro), so the limit is on the stream (setup size,
  channels, rate) and a process-wide allocation cap in tests.
- **Fixtures:** `tests/audio/fixtures/*.ogg`, made by `oggenc -q 3` (libvorbis 1.3.7) from generated WAVs (a sine, noise, a stereo tone mix, 6 channels at
  48 kHz) with the recipe in `README.md` there; the 5.1 stream uses ffmpeg's libvorbis binding. Generated test content, no third-party recordings.

## 11. The C API (Days 438-439)

Appended to the end of `engine_api` with sized structs: `audio_clip_create` and `_destroy`, `audio_play`, `audio_voice_set` and `_stop`, `audio_bus_set`,
`audio_set_listener`, `audio_master_set`; the components `ENGINE_COMPONENT_AUDIO_SOURCE` and `_AUDIO_LISTENER`; **host-driven mode** (`audio.device = host`):
`audio_render(engine, frames, out_f32)` from the host's own audio thread (lock-free, allocation-free, no engine thread); `audio_stream_open`, `audio_music_*`,
`audio_bus_effect_set`, sends and ducking, the occlusion world binding, device list and selection and state, polled `audio_events`, snapshot and restore
through `engine_buffer`. Every function callable from any thread except `audio_render`; no callbacks cross the boundary except a host PCM source
(`audio_stream_open` with a host function, called on the decode job and documented as such). Struct-size growth rule of `engine_c.h` honoured.

## 12. Where the plan changed

- The Linux plan grew from one backend to **PipeWire plus the raw ALSA fallback** (a recommendation awaiting the user): Days 425-427 stay three days, the
  second backend is a day-413 task; the device list code differs per backend.
- The cross-machine tolerance is **1e-5 / -100 dB**, tighter than the plan's 1e-4, because the FMA experiment shows the numbers can bear it.
- **256 mixed voices** by default (the plan had no number); sinc16 is not for mass voices.
- `audio_physics` is a separate small target above `audio` and `physics` (layering), not a file inside `audio`.
- UBSan is off for the vendored Vorbis files.

## Appendix: the six spikes

Sources are in `2026-11-17-audio-spikes/` (`.txt`). Machine: Linux 7.3, 32 cores, GCC 16.2, Clang 23.1, PipeWire 1.6.11, ALSA protocol 2.0.18.

1. **Linux backend:** section 3 (`spike1-*.txt`).
2. **Repeatability:** section 4 (`spikes-2-3-6-mixer-resampler-voices.cpp.txt`, `spike-2-compare-builds.py.txt`).
3. **Resampler:** section 5.
4. **Command queue:** section 6 (`spike-4-command-ring.cpp.txt`).
5. **Vorbis:** section 10 (`spike-5-notes.txt`, `spike-5-vorbis-harness.cpp.txt`).
6. **Voices at scale:** section 5.
