# Audio: repeatable on one build, equal within a tolerance across machines

Status: in progress (Phase 26). Built: the module and the render checks (Day 415), the formats, conversions and the clip (Day 416). the WAV parser (Day 417). minted clips (Day 418). Next: the device interface (Day 419).

**Audience:** engine programmers who write or review audio code, and hosts that compare renders across machines. Read [Math](foundation-math.md) first.
Design and the experiments behind it: `docs/superpowers/specs/2026-11-17-audio-design.md`.

The audio module (`engine/audio`, namespace `audio`) mixes voices into blocks of interleaved `f32` frames that a device pulls. This page states the rules
every later day of the phase obeys.

## The rules

1. **The render is repeatable on one build.** The same scene, the same commands at the same block stamps and the same build give the same bits, run to run
   and whatever the size of the device's pulls (a callback of 64, 100, 256 or 1,024 frames hashes like one of 256). That needs a fixed order everywhere:
   voices are mixed in slot order, buses in tree order, effects in chain order; there is no reduction whose order depends on thread timing; and a command
   from the game thread takes effect at a block boundary the command carries (its block stamp), never "whenever the audio thread sees it".
2. **It is not bit-identical across compilers or machines.** libm's `sin` and `cos` (filter and reverb coefficient tables) differ in the last bits between
   glibc, the Microsoft C library and Apple's, and the vendored Vorbis decoder may differ in its last bits too. A check across machines compares within the
   tolerance (below), never by hash. The hash is for one build: a regression check that is re-committed when a compiler change moves the bits.
3. **The audio thread never allocates, locks or does I/O.** Commands arrive through a single-producer single-consumer ring, events leave through another,
   and decoding and streaming happen on job-system workers that hand blocks over through rings. Everything the audio thread touches is allocated when a
   voice, bus or effect is created.
4. **No special floating-point flags.** The project's flags apply to audio like everything else (no contraction, no fast-math, from
   `ENGINE_FP_COMPILE_OPTIONS`), and audio code may use libm through `lang/scalar.h`; the `Sqrt`-only rule of [physics](physics.md) does not apply here.

## The block hash

`BlockHasher` (`audio/block_hash.h`) is a 64-bit FNV-1a hash of `f32` samples: each sample's bit pattern in little-endian byte order, with -0.0 hashed as
+0.0, and nothing else (no lengths, no padding), so block boundaries do not matter. A NaN or an infinity is hashed as it is and makes `Valid()` false,
because a mix that produced one has failed. `RenderHash` folds blocks in order and counts them. The hash uses the same constants as the physics
`StateHasher` but is the audio module's own, since `audio` does not depend on `physics`.

## The tolerance comparison

`audio/render_compare.h` reduces a render to a `RenderSummary` (the RMS and peak of each block and a table of sample probes at evenly spaced positions; the
committed references use 64 probes) and compares two renders, or two summaries, within a `RenderTolerance`:

| Limit | Default | Meaning |
|---|---|---|
| `max_abs_error` | 1e-5 | the largest absolute difference of one sample, block statistic or probe (-100 dBFS) |
| `rms_error_db` | -100 | the RMS of the differences, in dB relative to full scale |

`CompareResult` reports both numbers. (The RMS of a difference never exceeds its largest value, so with the defaults the absolute limit decides; the RMS limit counts when a caller loosens the absolute one.) A render of another shape (block count, probe count, length) is `shape_differs`, and a NaN is never equal. The
defaults are about thirty times the difference that fused multiply-adds make in the reference scene (3.3e-7 absolute, -134 dB), so a rounding is
accepted and a gain of 1.001 (a difference of about 5e-4) is flagged.

## Formats, conversions and clips

`audio/format.h`: `SampleFormat` (`U8`, `S16`, `S24`, `S32`, `F32`), `ChannelLayout` (mono, stereo, quad, 5.1, 7.1; the channel order is the WAV mask's, and
`ChannelName` gives each channel's speaker: `FL FR FC LFE BL BR SL SR`) and `AudioFormat` (rate, layout, sample format). A valid format has a rate from 8,000 to
384,000 Hz and at most `kMaxChannels` (8) channels.

`audio/convert.h` is the one place conversions are written, with `+ - * /`, comparisons and `Round` only, and no allocation:

- Integer to `f32` divides by `2^(N-1)` (`1/32768` for 16-bit; unsigned 8-bit is `(v - 128) / 128`), exact for 8, 16 and 24 bits. `f32` to integer multiplies by
  the same scale, rounds to the nearest with halves away from zero, clamps (so +1.0 gives the largest value and a sample beyond +-1 clips), turns a NaN into
  silence and adds no dither. Every 16-bit and 8-bit value round-trips exactly.
- `Interleave` and `Deinterleave` convert between planar (channel 0's frames, then channel 1's) and interleaved.
- `BuildChannelMatrix(from, to)` makes the gain table `ApplyChannelMatrix` applies: mono to stereo copies (gain 1 each side), a layout to mono averages the stereo
  fold (gain 0.5 each), a speaker the destination has goes to it with gain 1, and a speaker it lacks is folded with `kDownmixGain` (1/sqrt(2), ITU-R BS.775):
  the center into both fronts, a back speaker into the front on its side, a side speaker into the back on its side (or the front when there is none), the LFE
  dropped. A fold adds, so a loud 5.1 frame can exceed 1.0; the master limiter handles it.

`audio/clip.h`: an `AudioClip` is immutable decoded interleaved `f32` frames in a `SharedBytes`, named by an offset (never a pointer), with the format (rate and
layout), the frame count, loop points in frames (`loop_start < loop_end <= frames`; none when `loop_end` is 0) and a name. It is held as `f32` so the mixer
converts nothing at play time: a 3-minute stereo 48 kHz clip is 69 MB as `f32` (it would be 35 MB as 16-bit; the spec chose `f32`), and long clips stream
(Day 435) instead; a clip over `kMaxClipFrames` (2^29 frames) is refused. `ClipView` is the validated zero-copy view the mixer reads (`MakeClipView` checks the
format, frame count, size, 4-byte alignment, loop points and that every sample is finite); `AudioClip::View()` rebuilds it without validation or allocation, so
a voice that starts allocates nothing for its clip. Creating a clip allocates and scans the samples once; it is not an audio-thread operation.

## WAV files

`audio/wav.h` reads WAV files (the source format of the minter and the tools; the runtime reads minted clips, never WAV). `ParseWav` handles RIFF/WAVE with
`fmt ` (PCM 8, 16, 24 and 32-bit, IEEE float 32 and 64-bit, and `WAVE_FORMAT_EXTENSIBLE` with its channel mask), `data`, `smpl` (the first loop; the file's end
point is inclusive, `WavInfo::loop_end` is exclusive) and `cue ` (markers, up to 256); chunks are padded to even sizes, unknown chunks are skipped, and a `data`
chunk that claims more bytes than the file has (a truncated recording) is read up to the bytes present with `truncated` set. A loop or cue that points outside
the data is dropped with `ignored_metadata` set, not an error. RF64, other format tags, other bit depths, channel counts without a layout (3, 5, 7) and rates
outside 8,000 to 384,000 Hz are `Unsupported`.

The input is hostile by rule: every size is compared with the bytes that remain before it is used, counts times sizes are done in 64 bits, the chunk walk always
advances by at least 8 bytes, the frame count is capped at `kMaxWavFrames`, and nothing aborts on bad bytes (`WavStatus`). `DecodeWav` makes an `AudioClip`
with the Day 416 conversions (a NaN or infinity in a float file is `BadSamples`); `WavReader` reads frames in order or from a position without allocating, for
streaming sources (Day 435; it reads from bytes, the `io::File` source arrives with the stream interface). The tests build their files in memory
(`tests/audio/wav_builder.h`), cut a file at every length and mutate it 6,000 times with a fixed seed.

## Minted clips

The runtime never reads a WAV file: `asset_mint mint-audio SOURCE.audio.src OUTPUT.blob` reads the source (an `id`, the `wav` it names, relative to the source's
directory, an optional `loop START END` in frames that overrides the file's `smpl` loop, `format f32|s16` (default `f32`) and `normalize on|off` (the peak scaled to
1.0, default off)) and writes an `ACLP` blob (`asset/audio_clip.h`): a 32-byte header (rate, channels, stored format, frames, loop points) and the interleaved
samples in a 64-byte-aligned section. The same inputs and tool version give the same bytes. `asset::AudioClipView::Open` validates a blob and refuses a bad one by
value (counts, limits, section sizes against the bytes, an undefined header field, a bad loop).

`audio::MakeClipFromAsset` makes the clip: an `f32` blob is shared in place (the clip names the blob's buffer by offset, so the mixer reads the loaded bytes
and the clip outlives the resource manager's handle); an `s16` blob is converted to `f32` once, at load. `audio::LoadAudioClip(manager, id, name, &clip)` goes
through a manager that has `asset::RegisterCpuLoaders`; a missing or corrupt clip is the manager's named error (`NotFound`, `Corrupt`), never a substitute.
`mint-project` does not pick `.audio.src` files up yet (a WAV is an input file outside the project's dependency tracking; `KNOWN-ISSUES.md`).

## Devices

`audio::AudioDevice` (`audio/device.h`) is the only per-platform seam. The mixer renders into a callback, `void(void* user, f32* interleaved, u32 frames)`, and the device
decides when it runs and where the frames go: `SetCallbacks`, `Open(DeviceConfig)` (a name, a rate with 0 for the device's own, a layout, buffer frames),
`Start`, `Stop`, `Close`, and `Info()` for what was granted (rate, layout, buffer frames, latency). A refusal is a `DeviceStatus` (`InvalidConfig`, `NotFound`,
`Unsupported`, `AlreadyOpen`, `NotOpen`, `Busy`, `Lost`, `IoError`, `OutOfMemory`); a device that stops on its own calls the error callback.

Two devices need no hardware and are the test and offline route. `CreateDevice(DeviceKind::Null)` renders on demand and throws the samples away;
`CreateWavFileDevice(path)` renders on demand and writes a 32-bit float WAV on `Close` (a test reads it back with `ParseWav`). Both are driven by
`AudioDevice::Pump(frames)` on the caller's thread, in calls of at most the buffer size, and `FramesRendered()` is their clock; a hardware device runs from its own
thread and `Pump` returns `Unsupported`. `CreateDevice(DeviceKind::Platform)` is the platform's output, and the null device on a platform with no backend yet.
`EnumerateDevices` lists the outputs of a kind (one fake device for the null and file kinds). A new backend implements `engine/audio/BACKEND.md`, kept in step
with the header by the ctest `audio_backend_lists`.

The settings (`audio/audio_settings.h`, `RegisterAudioSettings`, `AudioSettingsFromRegistry`) are `audio.device`, `audio.sample_rate`, `audio.buffer_frames` and
`audio.channels` (each needs a device restart), and `audio.master_volume`, `audio.max_voices`, `audio.resampler_quality` and `audio.ramp_frames` (live), with the environment variables
`ENGINE_AUDIO_*` and the flags `--audio-*`. `DeviceConfigFromSettings` turns them into a `DeviceConfig`.

## The mixer

`audio::Mixer` (`audio/mixer.h`) renders in fixed blocks of `kBlockFrames` (256) frames; `Render(out, frames)` pulls any number of frames through a carry-over, so
the output does not depend on the size the device calls back with. `Init(MixerConfig)` allocates everything (the voice pool, the sinc table, the carry block) and
`RenderBlock` and `Render` allocate nothing. A voice (`VoiceDesc`: a clip, gain 0 to 16, pan -1 to 1, pitch 1/16 to 16, loop, priority, virtual, start frame) is
started by `Play`, which returns a `VoiceId` (a slot and a generation, so a stale id names nothing), and changed by `Stop`, `SetGain`, `SetPan`, `SetPitch` and
`SetVirtual`; until the command queue (Day 421) these are control-thread calls that must not overlap a render.

A voice's position is a 32.32 fixed-point `u64`, so a long clip does not drift (an `f32` accumulator stalls after about twelve minutes at 48 kHz). The three
resamplers, chosen by `audio.resampler_quality`, are linear, 4-point Hermite (the default) and a 16-tap Blackman-windowed sinc with 32 phases; at a whole-frame
position all three return the sample exactly, so pitch 1 reproduces a clip bit for bit and pitch 2 plays every second sample. The sinc kernel does not low-pass for
pitches above 1. A looping voice wraps at the clip's loop end (or the whole clip): the sample after the end is the one at the loop start and the taps of the
interpolators wrap too, so there is no gap and no blend of the two ends; the intro before `loop_start` plays once. A voice that runs off the end of a non-looping
clip is released.

Routing: a mono clip pans over the front pair with the equal-power law `left = Sqrt((1 - p) / 2)`, `right = Sqrt((1 + p) / 2)` (-3 dB, gain 0.7071, at the centre); a
stereo or multichannel clip on a layout with a front pair balances it (the side away from the pan is attenuated by `Sqrt(1 - |p|)`, the centre is unity); every
other pairing goes through the conversion table of `audio/convert.h`. Voices are summed in slot order into the block, which is not clipped (the master bus limits,
Day 422). When the pool is full a new voice stops the one with the lowest (priority, gain, age) if it does not rank below it, and is refused with `Full`
otherwise; a virtual voice advances its position and costs no mixing. The scene hashes of `mixer_test.cpp` are a same-build regression check.

## Controlling voices

Two threads use a mixer and share nothing but two rings and two counters (`audio/commands.h`, `SpscRing`: single producer, single consumer, no lock, no allocation
after `Init`). The **game thread** calls `Play`, `Stop`, `Pause`, `Resume`, `Seek`, `SetGain`, `SetPan`, `SetPitch`, `SetLoop`, `Fade`, `SetVirtual`, `IsPlaying` and
`PollEvents`; each call checks its arguments, posts a command and returns at once. The **audio thread** calls `RenderBlock`, which first applies every command whose
stamp is at most the block it is about to render, in the order posted, then mixes. A stamp is the block a command takes effect at: the default is the next block plus
`MixerConfig::lead_blocks` (one, about 5.3 ms at 256 frames and 48 kHz, so the audio thread has a block of notice), and an offline render passes the block itself. A
command that arrives after its stamp is applied at the block being rendered and counted in `Stats().late_commands`. Commands apply in posting order, so a command
stamped for a later block holds the ones behind it.

`Play` reserves the voice's slot and generation on the game thread, so the returned `VoiceId` can be used before the audio thread has seen the command; a stale id
(finished, stopped, or from a reused slot) is refused with `InvalidVoice` by the game side and ignored by the audio side. A full pool is decided on the game side by
the same ranking as before (priority, gain, age), and a victim is stopped in the command stream just before the new voice. A slot is free again once the voice's
`Finished` or `Stopped` event has come back, so a slot is never reused while the audio thread still holds the old voice.

Every setter ramps: a change of gain, pan or pitch moves linearly over `audio.ramp_frames` (64 by default; 0 is instant), one step per frame, and the last frame uses
the target itself, so the ramp ends exactly on it. A pitch ramp moves the position step the same way, so the position stays continuous. `Stop` fades out and then frees
the voice, `Pause` fades to silence and holds the position, `Resume` fades back, `Fade(gain, frames)` is a ramp of any length, and a voice played with `fade_in`
starts from silence; `Seek` jumps and is not smoothed. Events (`VoiceEvent`: `Finished`, `Looped` at most once a block, `Virtualised`, `Stolen`, `Stopped`) come back through
a second ring that always keeps room for one terminal event per voice, and `PollEvents` hands them out; nothing is called back across threads. A full command ring
returns `QueueFull` and counts in `Stats().commands_dropped`; nothing blocks.

An offline render (`MixerConfig::offline`) renders block `b` only when the producer has called `SetWatermark(b)` or later (`TryRenderBlock` returns false until
then), so the output does not depend on when the producer thread posts: `control_test.cpp` posts one scripted stream with five different timings and gets one hash.
A voice's last clip reference is released on the audio thread when the voice ends, so keep another reference (the resource handle) while voices play.

## Buses, sends, ducking and the limiter

Every voice mixes into a **bus** (`VoiceDesc::bus`; the master when none is named). Buses form a tree under the master
(`Mixer::CreateBus(name, parent, &id)`, at most `kMaxBuses` = 32, names unique); each has a gain, a mute and a solo flag.
`ProcessBuses` sums them in an order fixed by the graph (a bus after everything that feeds it, ties by index), so the sum
is the same on every run. A gain change takes effect through the same per-frame ramp as a voice's (`audio.ramp_frames`).

- **Sends:** `SetBusSend(bus, slot, aux, gain, pre_fader)` copies a share of a bus into an auxiliary bus (up to
  `kMaxSends` per bus); `VoiceDesc::send_bus`/`send_gain` do the same for one voice. A send that would close a loop
  (through parents, sends or ducking) is refused with `MixerStatus::Cycle` on the control side.
- **Solo:** while any bus is soloed, only soloed buses, their ancestors and their descendants are audible. An ancestor
  stays audible with its own voices, since a bus carries both its own voices and its children.
- **Ducking:** `SetDuck(target, source, depth, threshold, release_frames)` lowers `target` to `depth` (`DbToGain(-12)` is
  about 0.25) in the same block in which `source`'s peak passes `threshold`, and lets it recover exponentially.
- **Meters:** `SetMeter(bus, true)` posts one `BusLevel` event (peak and RMS) per block.
- **Limiter:** `MixerConfig::limiter` or `SetLimiter(true)` puts a look-ahead peak limiter on the master. It delays
  the output by its look-ahead (64 frames), never exceeds its threshold, and below the threshold passes the signal bit for
  bit. `Stats().limiter_min_gain` is the lowest gain in the last block. The output of `Mixer::Render` is hard-clipped to
  [-1, 1] at the device boundary, and only there.
- **Decibels:** `DbToGain` and `GainToDb` run on the control side (libm); the mixer takes linear gains.

## Where the tests are

`tests/audio` (phase 26): `block_hash_test.cpp` (known values, byte order, zero, invalid cases, block boundaries), `render_compare_test.cpp` (equal, rounding,
perturbed, very different, shapes) and `repeatable_render_test.cpp` (a small scene rendered twice and with different pulls, and the 1.001 gain), `format_test.cpp`, `convert_test.cpp` (every conversion, the 5.1 downmix) `clip_test.cpp` (creation, shared bytes, validation) and `wav_test.cpp` (every encoding, extensible, loops, truncation, the fuzz tests) and `aclip_test.cpp` (mint then view, loops, determinism, source errors, corruption at every byte, the resource manager). `device_test.cpp` (the null and file devices, refused configurations, the list, the settings). `mixer_test.cpp` (exact reproduction at pitch 1 and 2, the octave, the loop wrap, the pan law, voice stealing, virtual voices, pull sizes, the committed scene hashes). `control_test.cpp` (ramps, stop, pause, seek, events, the queue, late commands, a random stream against a model, the producer thread). `bus_test.cpp` (the bus tree, mute and solo, sends, cycles, ducking, meters, the limiter, the decibel helpers).
