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

## Effects: filters, delay and reverb

An `Effect` (`audio/effect.h`) processes interleaved frames in place. It is made on the control thread with all its
memory (`MakeFilterEffect`, `MakeOnePoleEffect`, `MakeDelayEffect`, `MakeReverbEffect`), handed to a bus with
`Mixer::SetInsert(bus, slot, effect)` (four slots per bus, run in slot order on the bus's sum, before its pre-fader
sends and its fader), and changed with `SetInsertParams(bus, slot, params)`. `ClearInsert` bypasses a slot; the effect
is kept until the mixer is destroyed (at most `kMaxEffects` = 64 over the mixer's life). A parameter block is 16 floats
built on the control side by the effect's own function (`DesignFilter`, `OnePoleParams`, `MakeDelayParams`,
`MakeReverbParams`), so every logarithm, power and trigonometric call is control-side libm and the audio thread uses
`+ - * /` and `Sqrt` only. An effect glides to new parameters over `ramp_frames` (filters and the delay; the reverb
changes at once). Inserts keep running on a silent bus, so a reverb tail is not cut when the last voice ends;
`Effect::TailFrames(params)` says how long a tail lasts.

- **Filters:** `DesignFilter(type, rate, cutoff, q, gain_db)` gives a biquad (low-pass, high-pass, band-pass, notch,
  peaking, low and high shelf, the RBJ cookbook forms) in direct form II transposed; inputs out of range are clamped, and a
  design with poles outside the unit circle comes back as the identity, both reported in `FilterDesign::clamped`. The
  identity is bit-transparent (the effect skips it). Both states of a channel are flushed to zero together once both are
  below 1e-20 (-400 dB): without it a float biquad near the unit circle stalls at a rounding floor and never reaches
  silence. `FilterMagnitude` gives the analytic gain at a frequency. The one-pole low-pass is `OnePoleParams` and
  costs one multiply per sample.
- **Delay:** a feedback delay with a buffer sized for `max_delay_seconds` when it is made, linear interpolation for a
  fractional time (`delay = 10.5` frames gives two half taps), feedback up to 0.995, wet and dry, and a one-pole
  `damping` in the loop.
- **Reverb:** eight prime-length lines (`ReverbLineLengths`), a Hadamard feedback matrix by butterflies (adds and
  subtracts), a damping one-pole per line, a pre-delay and a stereo output from decorrelated taps. The decay time is the
  time to fall 60 dB (`MakeReverbParams` computes the per-line gains `10^(-3 length / (decay rate))`); with no damping a
  test measures it within 5 percent. The rate and the size are fixed when the effect is made. Presets: `SmallRoom`,
  `Hall`, `Cave` (`ReverbPresetDesc`).
- **Voice inserts are not built:** inserts are on buses; a voice that needs a filter (occlusion, air absorption, Days
  430-431) uses a bus of its own.

## The Linux backend

`CreateDevice(DeviceKind::Platform)` on Linux makes a device whose `Open` tries **PipeWire** (the sound server's own client
library, `libpipewire-0.3.so.0`, loaded with `os::LoadLibrary` when the device is opened, so a binary built with the
headers still runs where the library is missing) and then **ALSA** through raw `ioctl`s on `/dev/snd/pcmC<card>D<device>p`
(no `libasound`, no server). `DeviceKind::PipeWire` and `DeviceKind::Alsa` pick one; names are `default`, `pipewire`, or
`hw:<card>,<device>`. `EnumerateDevices` lists them, the first being the default.

- **Negotiation** (`NegotiatePcm`, portable and tested against recorded tables of an HD-audio codec and a USB DAC): the
  rate and channel count clamp into the card's range, the format is float, else 32-bit, else 16-bit (`ConvertForPcm` is
  the conversion at the boundary), the period is the one asked for (as far as the card's limits allow) and the buffer is
  three periods. What was granted is in `DeviceInfo`; `latency_frames` is the whole buffer (ALSA) or two periods (PipeWire).
- **Threads:** the ALSA device runs one thread that renders a period, converts it and writes it with a blocking
  `WRITEI_FRAMES`; it asks for `SCHED_FIFO` at priority 20 and carries on without it if refused. PipeWire renders on the
  server's real-time thread inside the stream's `process` callback. Neither callback allocates, locks or does I/O.
  `Stop` returns after the last render call; destroying a running device stops it first.
- **Underruns:** a write that finds the buffer empty (`EPIPE`) re-prepares the stream and counts in
  `DeviceCounters::underruns`; a render call longer than the time the buffer lasts counts in `slow_callbacks`
  (`longest_callback_micros` is the longest). PipeWire reports no underruns of its own.
- **Errors:** `EBUSY` is `Busy`, `EACCES`/`EPERM` `PermissionDenied`, `ENODEV` `Lost` (also sent to the error callback),
  an unsupported configuration `Unsupported`, a missing device `NotFound`.
- `tools/audio_probe` (`list`, `play --backend=... --seconds=N`) plays a quiet tone through the real backend and prints the
  granted configuration and the counters.

## Changing the device at run time

`AudioSystem` owns a `Mixer` and the device that plays it (`audio/audio_system.h`). `Init` opens the wanted device with the
audio settings, starts the mixer at the rate and layout the device **granted**, and falls back to the null device when the
wanted one cannot be opened. The mixer outlives every device: switching keeps the voices, their positions, the buses and
the queued commands.

- `Mixer::Reconfigure(rate, layout)` re-plans the mixer for a new rate or layout: every voice recomputes its step and its
  routing, so a clip that was half played keeps its place and its pitch. A change of channel *count* re-allocates the bus
  buffers and the limiter and **bypasses the bus inserts** (their state is per channel; the application re-creates them).
  Effect parameter blocks are in frames, so after a rate change they are rebuilt by the application (`Make...Params`).
- `ApplySettings` is classified by cost (`ApplyCost`): the master volume is a bus gain and free; a changed rate or channel
  count re-plans and reopens the device; a changed device name or buffer size reopens it. The voice limit and the resampler
  quality belong to a mixer and are not applied live. A reopen that fails leaves the system on the null device.
- A device that reports `Lost` (from its own thread) only sets a flag; `Update(now)` on the control thread then swaps to the
  null device, pumps it for the time that passed (so the clock and the voices go on) and retries the wanted device every
  `retry_nanos`, logging each transition once. `Switches()` counts the devices opened (a leak check: 100 resets leave one
  device alive).
- The device factory is a seam (`AudioSystemConfig::factory`) so the tests use a fake card that grants another rate or
  vanishes.

## Sound in the world

`AudioSource` and `AudioListener` (`audio/components.h`) are ECS components read together with `ecs::WorldTransform`; the
entity's local -Z is its forward axis. `SpatialAudio::Update(world, dt)` (`audio/spatial_audio.h`) is the sync:

1. The active listener with the lowest entity id is the ears. Its velocity is the change of position over `dt` (a jump
   larger than `teleport_distance` is a teleport, not a speed).
2. Every source is an item of a `spatial::SpatialSet`, a dynamic box of its **audible radius** (the distance at which its
   gain falls to `audibility_threshold`). An entity that did not change its position or radius does not touch the set or
   the mixer; only a change posts a command.
3. The listener query (a zero-radius sphere) returns the emitters in range; their loudness is `gain * listener gain *
   distance gain * cone gain`. They are ranked by loudness, then `priority`, then entity id, and the loudest
   `max_real_voices` get real voices, the rest virtual ones (looping sources keep a virtual voice so their position goes
   on; a one-shot starts when it is first audible). Sources without `spatial` are always candidates and are not placed.
4. A destroyed entity (or one that lost its source) fades its voice out over `stop_fade_frames` and is forgotten.

`DistanceGain` (`audio/distance.h`) has four models, all clamped to `min_distance` and silent beyond `max_distance`:
inverse `min / (min + rolloff (d - min))`, linear `1 - rolloff (d - min) / (max - min)`, exponential `(d / min)^-rolloff`
and a custom table of up to 16 points. `ConeGain` blends from 1 inside the inner angle to `outer_gain` outside the outer
one (by the cosine of the angle).

## Panning

A voice made `directional` (or moved by `Mixer::SetDirection`) is placed by the direction of its source in listener space
(x right, y up, z forward). `SpeakerGains` (`audio/panning.h`, `+ - * /` and `Sqrt` only, so it runs on the audio thread)
gives: stereo an equal-power law of the lateral position, quad, 5.1 and 7.1 vector-based amplitude panning over the two
speakers either side of the direction (a 2x2 solve), normalised so the squares of the gains add to 1. **Spread** (0 to 1)
and elevation blend toward every speaker at equal level, and a source at the listener's own position (no direction) is
fully spread, so a source passing through the listener does not jump. The LFE channel gets nothing from position:
`lfe_send` (a voice setting, `SetLfeSend`) feeds it a level, and the channel's own crossover takes the highs out. A stereo
clip balances by the direction's lateral position. Gains ramp over `ramp_frames` like every other change.

## Doppler and air absorption

`DopplerRatio` is `(c + v_listener . u) / (c + v_emitter . u)` with `u` from the listener to the emitter, the speeds scaled
by the world's Doppler factor and limited to 0.9 c, the ratio to 0.25 to 4; the system multiplies it into the voice's pitch
(the mixer ramps the change). The emitter's velocity is the change of its position over `dt`, or an `AudioVelocity`
component when the entity has one (a physics body's); `AudioSource::doppler` turns it off per source and a factor of 0
for the world. Air absorption is a one-pole low-pass per voice (`SetAirAbsorption`, `y += k (x - y)`) whose cutoff falls
with distance by a table from 20 kHz at the listener to 1.2 kHz at 400 m (`AirCutoffHz`, scaled by the world factor);
`AirCoefficient` turns a cutoff into `k` on the control side (libm, listed), and the audio side only multiplies and adds.

## Occlusion

`audio::OcclusionQuery` (`audio/occlusion.h`) is the whole of what the audio module knows about walls: `Occlusion(from, to,
&crossed)` returns 0 (open) to 1 (blocked). `SpatialAudio::SetOcclusion` hands it a query; a source is tested when its
`occlusion` flag is set. The cost is bounded: an emitter is tested at most `tests_per_second` times, at most
`max_tests_per_update` tests run per update over all emitters, in a round-robin by entity id that continues where the last
update stopped (so every emitter is visited within `ceil(emitters / budget)` updates), and an emitter beyond the budget keeps
its last value. `SpatialAudioReport::occlusion_tests` is the cost. The value is smoothed every update with the time constant
`smooth_seconds`, so a door that opens fades instead of clicking, and is heard as a gain factor down to `occluded_gain` and a
low-pass (the voice's one-pole of Day 430) falling toward `occluded_cutoff_hz` (the air absorption's cutoff is used when it is
already lower).

`audio_physics::PhysicsOcclusion` (`engine/audio_physics`, layer 14: the one target that names both `audio` and `physics`)
implements the query over a `physics::PhysicsWorld`, read between steps. It casts `RayCastAll` from the emitter to the
listener and another from the listener back; the second ray's entry into a collider is the first ray's exit, so each collider's
**thickness** is known, and the occlusion is `1 - exp(-sum(absorption[layer] * thickness))` with an absorption per collision
layer (3 per metre by default: a 0.1 m panel blocks 26 percent, a metre of wall 95 percent; `layer_mask` selects what blocks
and a layer of 0 is glass). A collider whose far side the second ray did not find counts `default_thickness`. Diffraction
(sound bending around the obstacle) is not modelled; the ray test scans the world's colliders, so a thousand emitters are
fine and a hundred thousand are not (rate limiting is what keeps it cheap).

## Ogg Vorbis

`audio/vorbis.h` is the only door to the vendored libogg and libvorbis (decode only; no header of either reaches another
file). `VorbisDecoder::Open` takes the bytes of an Ogg Vorbis stream and `VorbisLimits` (input size, pages, setup size,
frames, steps) and returns a `VorbisStatus`: `Ok`, `InvalidInput`, `NotVorbis`, `Truncated`, `Unsupported` (1, 2, 4, 6 and 8
channels at 8 kHz to 192 kHz only) or `LimitExceeded`. `Info()` gives channels, rate, total frames and bitrate; `Tags()` the
comment lines as the stream spells them. `Decode(out, frames, &got)` writes interleaved `f32` frames in the engine's channel
order (6 and 8 channel streams are reordered from Vorbis order) and ends with `EndOfStream`. A hostile stream is refused or
stops early; it never crashes or hangs (`[fuzz]` tests). Fixtures and the script that makes them are in
`tests/audio/fixtures/`.

### Decoded output, cost and determinism

libvorbis computes in floating point with libm, so decoded PCM may differ in the last bits between builds and machines; that is
accepted. `tests/audio/vorbis_decode_test.cpp` commits a decode hash per fixture as a **same-build** baseline (Clang, Linux) and a
table of per-block RMS levels that every other build and machine must match within 2e-3. **Cost** (Clang, plain build, this
machine): about 1.1 ms of CPU per second of 48 kHz stereo (`[cost]` test, a loose bound of one twentieth of real time); the resident
size per stream was not measured (KNOWN-ISSUES.md).

### Seeking and loops

`VorbisDecoder::SeekFrame(frame)` is exact: the samples after a seek equal those of a decode from the start (bit for bit; the tests
seek to thirteen frames, forwards and backwards, and compare). The first seek reads the page index of the whole input once
(bounded by the page limit): every page that starts a packet, with the frame its restart emits first (from the packets' block
sizes); a seek bisects the index, restarts the library there (one packet only primes the overlap) and discards the frames up to
the target. `LOOPSTART` and `LOOPEND` comment tags (decimal frames, names in any case) become `Info().loop_start` and
`loop_end` when they satisfy `start < end <= total_frames`; `SetLooping(true)` makes `Decode` wrap by seeking at `loop_end`, with
no change to a sample (tested over two and a half passes). A wrap costs one seek (a page of decode), so a streaming source decodes
ahead (Day 435).

### Compressed clips

An `ACLP` blob has a codec field. `codec = Vorbis` holds the Ogg bytes verbatim (section 17) with the decoded length, rate,
channels and loop points in the header; `AudioClipView::Open` accepts either, and `MakeClipFromAsset` decodes a Vorbis blob to the
clip's `f32` samples once (the way for short effects; Day 435 streams music from the same blob). `asset_mint mint-audio` takes an
`ogg` line in the source: `compress on` keeps the bytes, `compress off` decodes at mint time to PCM (and records the libvorbis
version and the hash of the decoded samples in section 18, so a decoder change shows when the clip is minted again), `compress
auto` (default) keeps the bytes when the decoded PCM would be over 1 MiB. Both are byte-stable for one decoder version. The
runtime never reads a loose `.ogg` or `.wav`; the `asset_boundary_check` gate lets only `engine/audio/vorbis.cpp` include the
codec's headers and only `engine/audio/CMakeLists.txt` link it.

## Streaming

`audio/stream.h` plays music that is too long to keep decoded. A `StreamSource` (`MakeMemoryStream` over a clip,
`MakeVorbisStream` over the Ogg bytes of a clip, `MakeWavFileStream` over an `io::File`, 16-bit PCM or `f32`) is read by a
`StreamBuffer`, a ring of `StreamConfig::capacity_frames` frames (default 8192; chunks of 1024; refill below 4096). A
`ResampledStream` converts a source of another rate to the mixer's (Catmull-Rom, on the refill job, never in the mixer). The
**audio thread only pops**: `StreamBuffer::Pop` takes frames from the ring and allocates, locks and reads nothing; a ring that is
short counts one underrun (`Underruns()`) and the voice mixes silence for the missing frames, then recovers on its own.
`StreamPump` spawns one `RefillJob` per buffer that is below its low-water mark on the job scheduler (`Update`), and `Run()` is
the offline mode (`Update` then wait). A seek or a loop change *flushes* the ring (an epoch in the write counter makes a refill
that raced the flush drop its chunk), so there is a short silence until the next refill. Play a stream with `VoiceDesc.stream`
(`clip` must be `stream->Clip()`, `start_frame` 0); streams play at pitch 1 only. `VoiceDesc.delay_frames` (under one block)
starts a voice that many frames into its first block, which is how a stream joins another without a gap.

## Music

`audio/music.h` `MusicPlayer(mixer, bus)` plays streams with musical timing. `Tempo{bpm, beats_per_bar}` is given by the caller;
`NextBeatFrame()` and `NextBarFrame()` return the exact frame of the next beat or bar of the playing track, so a transition or a
stinger lands on that frame. `Play`, `CrossFadeTo(next, fade_frames, at)` and `CrossFadeAtBar` fade the old track out along a
cosine and the new one in along a sine (equal power: the squares add to one; the fade is a run of one-block linear segments, so
the error is under 1e-3 for fades of a tenth of a second or more). `PlayLayers` starts several streams of one length on the
same frame with per-layer gains (`SetLayerGain`, `SeekLayers` keep them in step). `PlayStinger(clip, gain, on_beat, &frame)`
sounds a short clip on the next beat, `DuckForDialogue` ducks the music bus under a dialogue bus, and `PlayPlaylist(tracks, seed,
shuffle, ...)` plays tracks in a seeded order (`PlaylistOrder`: the same seed gives the same order on every machine), each next
track starting at the exact end frame of the last (gapless, tested on a ramp). Call `Update()` once per block, before the render:
actions are posted when their block is within the mixer's lead. Fades and transitions start on the block of the boundary frame
(block granularity); a stinger and a bar-aligned start are exact because the voice itself is delayed.

## Snapshots and the Linux baseline

`Mixer::SaveState(writer)` writes the whole mixer at a block boundary: every voice (clip by name and length, position, step, ramps,
gains, pan, direction, routing matrices, flags), the buses (gains, sends, ducking, which insert is in which slot), the limiter,
the history of every effect (`Effect::SaveState`: filter memories, delay and reverb lines, parameter ramps), the carry of the
pull buffer and the control side's slots, serial and counters. It refuses with `SnapshotStatus::Busy` while commands are queued
(render a block first), and does not save unpolled events. `LoadState(bytes, resolver)` restores into a mixer set up the same
way (rate, layout, voice pool, buses and inserts; a difference is `Mismatch`); a `SnapshotResolver` gives back the clips by name
and length, and the stream buffers of stream voices, which are flushed and refilled from the saved position. The restore is
atomic: it parses and checks everything first, and the effects and the limiter (the one thing loaded in place) are put back from
a backup on any failure. Bad input is a value (`Corrupt`, `Version`, `Mismatch`, `MissingClip`), never an abort
(`tests/audio/snapshot_test.cpp`: every truncation, 300 byte flips). **Replay:** the mixer is deterministic, so a snapshot at
block N plus the commands posted from N on reproduces the render bit for bit (tested on a scene of eight voices on two buses
with a delay, a reverb, a filter, a send, a ramp in flight and the limiter). The `MusicPlayer` and the `AudioSystem` are not
saved: the host rebuilds them and posts their commands again.

**Reference scenes** committed with a same-build hash and per-block levels: the buses scene and the thousand-voice scene
(`snapshot_test.cpp`), the mixer scene (`mixer_test.cpp`), the echo and reverb scenes, the flyby, the 5.1 panning. A scene pulled
in callbacks of 64, 256 and 1,024 frames renders the same samples as whole blocks.

**Cost** (Release build, Clang, Linux, one thread, 256 looping voices on a stereo mixer at 48 kHz, `[cost]` test): linear 13 percent of
one core (2.8 microseconds per voice-block), Hermite 16 percent (3.3), sinc 31 percent (6.5); a reverb and a delay on the master
add under 1 percent.

## Where the tests are

`tests/audio` (phase 26): `block_hash_test.cpp` (known values, byte order, zero, invalid cases, block boundaries), `render_compare_test.cpp` (equal, rounding,
perturbed, very different, shapes) and `repeatable_render_test.cpp` (a small scene rendered twice and with different pulls, and the 1.001 gain), `format_test.cpp`, `convert_test.cpp` (every conversion, the 5.1 downmix) `clip_test.cpp` (creation, shared bytes, validation) and `wav_test.cpp` (every encoding, extensible, loops, truncation, the fuzz tests) and `aclip_test.cpp` (mint then view, loops, determinism, source errors, corruption at every byte, the resource manager). `device_test.cpp` (the null and file devices, refused configurations, the list, the settings). `mixer_test.cpp` (exact reproduction at pitch 1 and 2, the octave, the loop wrap, the pan law, voice stealing, virtual voices, pull sizes, the committed scene hashes). `control_test.cpp` (ramps, stop, pause, seek, events, the queue, late commands, a random stream against a model, the producer thread). `bus_test.cpp` (the bus tree, mute and solo, sends, cycles, ducking, meters, the limiter, the decibel helpers). `effect_test.cpp` (the filter responses, the clamps, the ring-out, the delay taps, the reverb decay and stereo field, the inserts, the committed echo and reverb scene hashes). `backend_test.cpp` (the negotiation tables, the conversion, the lists, a real device rendered at its pace, a stalled callback, destroying a running device; the hardware tests print a warning and skip without a sound device). `system_test.cpp` (a fake card: another granted rate mid-clip, a lost device and its recovery, every setting in turn, 100 resets). `spatial_test.cpp` (the distance models at ten distances, the cone, the sync: ranking, determinism, 10,000 emitters with one moved, a destroyed entity, the listener's velocity). `panning_test.cpp` (the documented gains per layout, constant power at 360 azimuths, smoothness, the 5.1 mix with the LFE send, a ramp without a step). `doppler_test.cpp` (the formula, teleports, a flyby, the cutoff table, the one-pole low-pass, the committed flyby hash). `occlusion_test.cpp` (a test double: open, behind a wall, the level, a door fading, the rate limit, the round-robin and the budget); `snapshot_test.cpp` (save, restore and replay bit for bit, refusals, stream voices, the committed scene hashes, the callback sizes, the cost). `tests/audio_physics` (the real physics world: thin and thick walls, two walls, layers and absorptions, the level through the system).
