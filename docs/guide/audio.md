# Audio: repeatable on one build, equal within a tolerance across machines

Status: in progress (Phase 26). Built: the module and the render checks (Day 415), the formats, conversions and the clip (Day 416). Next: the WAV parser (Day 417).

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

## Where the tests are

`tests/audio` (phase 26): `block_hash_test.cpp` (known values, byte order, zero, invalid cases, block boundaries), `render_compare_test.cpp` (equal, rounding,
perturbed, very different, shapes) and `repeatable_render_test.cpp` (a small scene rendered twice and with different pulls, and the 1.001 gain), `format_test.cpp`, `convert_test.cpp` (every conversion, the 5.1 downmix) and `clip_test.cpp` (creation, shared bytes, validation).
