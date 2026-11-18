# Tutorial: Sound in Your Window

**Goal:** Give a program that owns its window (and its audio callback) sound from the engine through the C API: clips from PCM you hold, voices, a bus with an effect, streamed music, and sounds that sit on entities of a world and are placed by a listener.

**You need:** the program of [Load the Engine and Clear a Window](loading-the-engine.md) working. The programs of this page are `tests/engine_library/host_audio.c` (clips, voices, buses, the render call), `host_audio_streams.c` (streams, music, effects, snapshots) and `host_audio_world.c` (sounds on entities); `cmake --build build --target host_audio_world` builds one and `./build/tests/engine_library/host_audio_world` runs it. The guide: [Audio](../guide/audio.md) and [The Engine as a Loadable Library](../guide/engine-library.md).

## Steps

1. **Open the audio.** `audio_open` makes the engine's mixer: a sample rate, a channel count (1, 2, 4, 6 or 8) and a voice pool. The engine opens no device and starts no thread: the sound card is yours.
2. **Render from your callback.** Your audio callback calls `audio_render(engine, frames, out)` for `frames` interleaved frames; it takes no lock, allocates nothing and may be called from any one thread at a time (an offline render calls it in a loop). Every other audio call may be made from your game threads while it runs.
3. **Make clips and play voices.** `audio_clip_create` copies PCM you give it (16-bit or 32-bit float) into a clip; `audio_play` starts a voice (gain, pan, pitch, loop, a bus); `audio_voice_set` and `audio_voice_stop` change and end it (changes ramp, so nothing clicks). `audio_events` tells you when a voice finished or was stolen.
4. **Mix with buses.** `audio_bus_create` makes a bus under another; `audio_bus_set` sets its gain and mute; `audio_bus_effect_set` puts a filter, a delay or a reverb in one of its four insert slots (calling it again with the same kind retunes the effect), `audio_bus_send` feeds an aux bus and `audio_bus_duck` lowers one bus while another is loud.
5. **Stream music.** `audio_stream_open` takes a source of yours (a decoder, a file reader) or `audio_stream_open_clip` a clip; call `audio_update` once per frame to refill on the engine's job threads; `audio_music_play`, `audio_music_crossfade` and `audio_music_layers` play and blend tracks on the bar.
6. **Put sounds in the world.** Make a world, and give an entity `ENGINE_COMPONENT_TRANSFORM` and `ENGINE_COMPONENT_AUDIO_SOURCE` (a clip from step 3, a gain, a distance model and range, loop and spatial flags, an optional cone). Give another entity (the camera's, or your character's) `ENGINE_COMPONENT_AUDIO_LISTENER`. Every `frame` call then reads the transforms, starts the voices of sources the listener can hear, pans them to the speakers, shifts them by their speed (Doppler) and stops them when the component is removed or the entity destroyed. The loudest sources get real voices and the rest play silently so they are in the right place when you walk close.
7. **Save and restore.** `audio_snapshot` and `audio_restore` save the mixer at a block boundary and put it back bit for bit; they refuse while commands are queued (render a block first).
8. **Clean up in order.** Stop your audio callback, call `audio_close` (voices, buses and clip handles go stale), destroy the world, then the engine.

## Where to go next

- [A 3D Scene in Your Window](a-3d-scene-in-your-window.md) for the entities this page puts sounds on.
- The guide's sections on the mixer, spatial sound, streaming and the C API.
