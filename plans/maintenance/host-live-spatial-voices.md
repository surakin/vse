# Maintenance: placed (spatial) voices from a live host stream, for a call's participants

Not a numbered phase. Request from the the host project (2026-11-18), written against the engine at `ed6d8327` (Phase 26 audio, Days 415-440 built). Nothing here is started.

## Why

In the host's 3D call world each participant is an entity (a robot or a custom avatar) that moves. the host wants the engine to mix their voices in the world: louder when
close, quieter far away, from the right direction, with the engine's rolloff, cone and optional Doppler, heard from the camera entity.

The voices are live. the host receives each participant's microphone as 10 ms blocks of 48 kHz mono 16-bit PCM from the call (WebRTC), on a worker thread, and has no clip
to give the engine. the host already drives the engine's output itself: it would call `audio_render` from its own playback path and push the stereo result to its audio
player (the engine opens no device, which suits this).

## What the engine has now (checked in `engine_c.h`)

- `ENGINE_COMPONENT_AUDIO_SOURCE` (`engine_audio_source`): a sound on an entity, placed by its world matrix, with `min_distance`, `max_distance`, `rolloff`, `model`, cone, and
  `ENGINE_AUDIO_SOURCE_SPATIAL`/`DOPPLER`/`OCCLUSION`. **But it plays a `clip`** (from `audio_clip_create`), a finished buffer of samples.
- `ENGINE_COMPONENT_AUDIO_LISTENER`: the ears, placed by the entity's world matrix.
- `audio_stream_open(engine_audio_stream_source)`: a stream fed by a host `read(user, out, frames)` callback on an engine job thread, which is the right shape for live PCM.
  **But `audio_stream_play` takes an `engine_audio_play` (clip, bus, gain, pitch, pan, loop, priority): no position.** A stream voice can only be panned by hand.
- `audio_render(frames, out)`: the host's audio callback, any frame count, stereo (or the opened layout). Fine as it is.

So a stream can be heard but not placed, and a placed source can be placed but not live. Nothing joins them.

## Missing

1. **A stream as the sound of an entity's audio source (required).** Either
   - a field `uint64_t stream` in `engine_audio_source` (used when `clip` is 0; `clip` and `stream` both set is INVALID_ARGUMENT), or
   - a call `entity_audio_source_set_stream(world, key, stream)`.
   Everything else about the source applies unchanged (spatial flag, distances, rolloff, cone, Doppler, bus, gain, priority). The voice follows the entity. Destroying the entity
   or clearing the source stops the voice; the stream stays open until `audio_stream_close`, so one stream can be reattached (a participant who leaves and rejoins). One
   stream per source; a stream on two sources at once is INVALID_ARGUMENT (a live read cannot be shared). Same threading rule as the rest of the world calls.

2. **A live stream: running dry is silence, not the end (required).** `engine_audio_stream_source.read` returns "how many (0 at the end)", and a stream that ends finishes its
   voice. A live source often has less than asked. Wanted: a flag on `engine_audio_stream_source` (`live`, and `frames` meaning "unbounded" for it) under which
   a short or zero return means "no data yet": the engine fills the rest of that block with silence, counts it (`engine_audio_state.stream_underruns`), asks again at the next refill, and never
   finishes or loops the voice. `seek` is never called on a live stream. Without it a host can still return exactly `frames` by padding silence itself; the flag exists so the engine
   can also say how often it happened and does not treat zero as the end.

3. **The live stream's latency, stated and bounded (required, a measurement and a number).** The refill job reads ahead of the audio thread. the host needs the delay between the
   host having a 10 ms block and it reaching `audio_render`: the guide should state the read-ahead in frames (and let the host set it, for instance `engine_audio_stream_source.
   read_ahead_frames`, default the current value, lowest allowed one mixer block) so a call keeps the voice delay under about 60 ms in total. Report the measured value on the
   three platforms. This is the main risk of the whole request; if the read-ahead cannot go below roughly 40 ms, say so before building the rest.

4. **A frame-accurate "feed" alternative (optional, instead of 2 and 3 if easier).** `audio_stream_push(engine, stream, const float* data, uint32_t frames)` for a stream opened
   with no `read` callback: the host pushes blocks when it has them, the engine keeps a bounded ring (oldest dropped past a limit, counted), the voice reads from the ring
   on the audio thread with no job in between. For live audio this removes the refill job's latency and the callback's "must not block" rules. The host calls it from its worker
   thread, any thread. If this exists, 2 and 3 reduce to its ring rules. Pick one; the host prefers this one.

5. **Samples as 16-bit (small).** The call gives `int16`; the read callback and `audio_stream_push` take `float`. A variant taking `int16` saves the host a conversion per block
   of every participant. Not required.

## Not needed

- Mono sources placed in stereo or surround: the existing spatial mixer does it.
- Occlusion by the world's geometry: later, if the occlusion flag works with the imported level.
- Recording or encoding the mix: the host does not send the rendered mix to anyone; each person hears their own copy.
- Speech processing in the engine (noise gate, gain control): the call already does it before the block reaches the host.

## What the host does with it

On each remote participant who joins the world view: open a stream (live), create or reuse that participant's entity, set its audio source with `SPATIAL` (and `DOPPLER` off at first),
`min_distance` about 1 m, `max_distance` about 25 m, inverse model, and the stream; put an audio listener on the camera entity; feed the stream from the call's per-participant
blocks. Its playback path calls `audio_render` and pushes the stereo result to its player. When the world view is hidden the engine's audio is closed and the old single mixed
stream plays again. The engine and its audio exist only while the world view does.

## Echo cancellation

Not an engine matter, noted for the record: the call's echo canceller takes its speaker reference from libwebrtc's own mix of the remote audio, not from what the host plays. A
placed mix differs in level and balance from that reference. The canceller adapts to level changes, so no change is expected, but the host will test it with real speakers.

## Tests this needs

- A live stream on a spatial source: a 1 kHz tone fed in 10 ms blocks from another thread, the source at 1 m, 5 m and 25 m to the front, left and behind: the rendered levels and
  channel balance equal those of the same tone as a clip (same source settings), within the mixer's tolerance.
- Moving the entity between frames moves the voice smoothly (no zipper noise), and Doppler on a moving entity matches the clip case.
- Underrun: the host stops feeding for 200 ms: silence for that time, the voice stays alive (no FINISHED event), the underrun counter counts, and the voice resumes when blocks return.
- Overrun (for `audio_stream_push`): a host that pushes faster than the audio thread reads drops the oldest and counts it.
- Latency: from a marker sample pushed to the marker appearing in `audio_render`'s output, on the plain flavor, the number the guide states.
- Destroying the entity, clearing the source, and `audio_stream_close` in any order stop the voice with no crash or leak under asan; a stream attached to two sources is refused.
- The snapshot (`audio_snapshot`/`audio_restore`) refuses or skips live streams with a clear status (a host stream cannot be saved).

## Result

Done on Linux, 2026-11-18. Option 4 (the push ring) was built, as the host prefers; items 2 and 3 (a `live` flag and a read-ahead on the callback stream) are not, since the ring has no refill job to measure.

- `audio_stream_open_push` / `audio_stream_push` / `audio_stream_push_i16` / `audio_stream_stats`, `engine_audio_source.stream` (first-version structs without it still work), `ENGINE_CAP_AUDIO_PUSH_STREAMS`, C++ wrappers; `StreamBuffer::CreatePush` in `engine/audio/stream.*`: a ring of the newest `capacity_frames` plus room for a push in flight; the mixer announces where its read starts and the pusher never writes over it, so neither side takes a lock, allocates or waits; the oldest frames are dropped and counted; `prefill_frames` is the jitter buffer; a closed stream ends its voice.
- The spatial driver (`SpatialAudio::Drive`) starts the voice from the source's stream (virtual too, so the stream keeps being read) and restarts it when another stream is set.
- Tests: `Push stream` unit tests (order, underrun, prefill, overrun, int16, a pusher thread against the reader) and `host_audio_live` (argument checks; live tone vs the same tone as a clip at 1, 5 and 25 m in front, left and behind: equal to 5 digits; 200 ms of silence: silent output, 37 underruns, the voice alive and resumed; overrun counted; delay; the six orders of destroying the entity, removing the source and closing the stream; one reader per stream; a participant's stream moved to a new entity; the first-version struct; the snapshot refusing a live voice; a pusher thread in real time).
- Delay (Linux, 48 kHz, 480-frame pushes and calls, `prefill_frames` 0): at most 256 frames, 5.33 ms; Windows and macOS not measured. Well under the 40 ms the request set as the threshold.
- Code review (high): 5 findings; the doc claim that a push never waits was corrected (it takes the control lock for the lookup); stale frames before a voice starts, the newest-dropped window, the per-world one-reader rule and the missing Doppler/movement test are recorded in `KNOWN-ISSUES.md`.
- Not done or not measured: Doppler and smooth movement of a live voice against a clip's, Windows and Mac runs; recorded in `KNOWN-ISSUES.md`.

