# Maintenance: double precision, Day 8 — Audio and script bindings

**Series:** [double precision](../double-precision.md)

## Tasks

- [ ] `engine/audio` listener and emitter positions in `f64`, the distance, the Doppler and the occlusion queries computed from `f64` differences narrowed to `f32` (the mixer itself stays `f32`); the audio-physics occlusion rays use the new query types.
- [ ] Scripts: Lua numbers are already `f64`; `script::Value`'s vector kinds gain an `f64` form where a position travels (or the position bindings take and return plain numbers), `entity.position`, `physics.*`, `terrain.*` and `audio.*` bindings stop narrowing positions to `f32`; the `ThreadRule` marks stay.
- [ ] The script determinism tests (state hashes at 1 to 16 threads) are re-baselined if a position now hashes differently, recorded in the day file; the hostile-script suite still passes.
- [ ] Docs: `docs/guide/scripting.md` and `docs/reference/script-api.md` regenerated.
- [ ] **Runs and review (the series' rules):** scoped tests plain, asan and tsan as the day's code needs; docs gates; `/code-review high`; `STATUS.md` and `KNOWN-ISSUES.md`; commit and push.

**Done:** A script reads and writes positions without narrowing, and audio is exact far from the origin.

## Notes

- Written 2026-11-23.

## Post-implementation code review

(To be filled in when the day is done.)
