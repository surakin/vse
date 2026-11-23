# Maintenance: double precision, Day 8 — Audio and script bindings

**Series:** [double precision](../double-precision.md)

## Tasks

- [x] `engine/audio` listener and emitter positions in `f64`, the distance, the Doppler and the occlusion queries computed from `f64` differences narrowed to `f32` (the mixer itself stays `f32`); the audio-physics occlusion rays use the new query types.
- [x] Scripts: Lua numbers are already `f64`; `script::Value`'s vector kinds gain an `f64` form where a position travels (or the position bindings take and return plain numbers), `entity.position`, `physics.*`, `terrain.*` and `audio.*` bindings stop narrowing positions to `f32`; the `ThreadRule` marks stay.
- [x] The script determinism tests (state hashes at 1 to 16 threads) are re-baselined if a position now hashes differently, recorded in the day file; the hostile-script suite still passes.
- [x] Docs: `docs/guide/scripting.md` and `docs/reference/script-api.md` regenerated.
- [x] **Runs and review (the series' rules):** scoped tests plain, asan and tsan as the day's code needs; docs gates; `/code-review high`; `STATUS.md` and `KNOWN-ISSUES.md`; commit and push.

**Done:** A script reads and writes positions without narrowing, and audio is exact far from the origin.

## Notes

- Written 2026-11-23.

## Post-implementation code review

`/code-review high` over the day's diff: 2 findings, both nits. (1) The script value record is now version 2, so records written by an earlier build are refused: accepted (current version only), listed in KNOWN-ISSUES. (2) `transform.set_position` bounds a position at 1e30, not at physics' 1e12: declined, unchanged behaviour (physics rejects it later).

Hashes: the script demo's cross-machine hash moved from 0x04d98e8a59bb0c83 to 0x0c091fc4195f2f75 (script vectors are now f64); equal at 1 to 16 threads. Hostile-script suite passes. New tests: `spatial audio: a listener and an emitter far from the origin...` and `Entity bindings: a position far from the origin...`.

Runs: plain `ran 795 of 12035 tests (phases: 3 26 27 28; flavors: plain; display: excluded; crash tests: excluded; soak: excluded) in 52 s`, asan `ran 771 of 12035 tests (... asan ...) in 121 s`, tsan `ran 576 of 12035 tests (... tsan ...) in 129 s`. Failures: the Debug voice-cost snapshot (known) and, in tsan, the LuaJIT sandbox flake (known). Reduced runs. GCC unity-off builds clean; `docgen lint` 14191 of 14191; `ctest -L docs` 13 of 13.
