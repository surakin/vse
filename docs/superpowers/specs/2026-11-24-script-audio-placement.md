# Scripts placing sound in the world: `audio.play` with `position` or `entity`

Status: **A and B built** (2026-11-24, the user's decision after maintenance `linux-known-issues` Day 07); C not built. Tested in `tests/script_media/script_media_test.cpp` (`[placed]`); described in `docs/guide/scripting.md`, "Audio and input". The decisions: one source per entity (a second sound replaces the first); the handle is a table entry with an emitter entity instead of a voice.

## The gap

`audio.play(clip, options)` plays a flat voice (`pan`, `gain`, `pitch`, `loop`, `bus`). A script cannot say "play this at that
place" or "play this from that entity": distance gain, panning from the listener, Doppler and occlusion belong to the
`AudioSource` system (`engine/audio/spatial_audio.*`), and `AudioSource` is not a script-visible component because it holds
shared pointers (`clip`, `stream`), which a stored component cannot (the script component catalog only reads plain fields).

## Options

**A. A transient emitter.** `audio.play(clip, { position = vec3 })` records one command; the merge makes an entity with a
`LocalTransform` at `position` and an `AudioSource` (clip from the clip registry, `loop = false`, the options' gain, pitch, bus,
priority and attenuation preset) and the spatial system plays it like any emitter. The entity is destroyed when the voice
ends (the merge of a later tick sees the `Finished` event). Deterministic: the entity is created at the sync point, in the
merge's total order. Cost: one entity per sound, a short-lived one.

**B. Attach to an entity.** `audio.play(clip, { entity = e })` records a command that adds an `AudioSource` to `e` (or, when
`e` already has one, replaces its clip and restarts it). The sound follows `e` and takes its velocity for Doppler. One
source per entity: a second `audio.play` on the same entity replaces the first (documented), so an entity that needs two
sounds has a child entity per sound (the script can spawn one).

**C. A script-visible `AudioSource`** with the clip by name (`clip = "door_open"`) resolved by the host's clip registry when
the merge sets the component. The most general, and the largest: a component with a string-resolved shared pointer needs a
"resolver" hook in the component catalog (a new kind of field) and a clip registry both sides agree on.

## Recommendation

A and B together, no C: they cover "a sound at a place" and "a sound from a thing", need no new component field kind, and keep
`audio.play`'s existing return value (a voice handle for `audio.stop` and the fades; for A and B the handle names the emitter,
which `audio.stop` removes). C waits until a host needs per-entity authored sources in scripts.

## What the user decides

1. Whether to build A and B (a day of work: bindings, merge commands, a test that a scripted sound pans and falls with
   distance, thread-count hash equal), or leave `audio.play` flat.
2. If built: one source per entity (B as above), or a child entity per sound made by the merge.
3. Whether the handle `audio.play` returns for A and B may be a different kind from the flat voice's (the script docs say so).
