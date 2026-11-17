# Audio: repeatable on one build, equal within a tolerance across machines

Status: in progress (Phase 26). Built: the module and the render checks (Day 415). Next: the clip and the sample formats (Day 416).

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

## Where the tests are

`tests/audio` (phase 26): `block_hash_test.cpp` (known values, byte order, zero, invalid cases, block boundaries), `render_compare_test.cpp` (equal, rounding,
perturbed, very different, shapes) and `repeatable_render_test.cpp` (a small scene rendered twice and with different pulls, and the 1.001 gain).
