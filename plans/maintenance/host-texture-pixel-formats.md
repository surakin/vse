# Maintenance: host textures take BGRA pixels, and the engine tells which formats it supports

Not a numbered phase, no day files. Request from a host project (2026-11-24). Nothing is broken; this removes a software conversion from the host.

## Why

`texture_update` takes RGBA8 only. The host's video frames reach it as premultiplied BGRA (what its 2D canvas, the OS capture APIs and most decoders
produce), so the host swaps the channels of every frame by hand, in a per-pixel loop, with a new allocation each time, only to hand them to an engine that
copies the rows again. A video call with several participants does this for every frame of every screen. The engine copies the rows anyway, and a GPU
samples a `B8G8R8A8` texture as easily as an `R8G8B8A8` one, so the engine is the place to do it, and to say what it can do.

## Wanted

1. **A pixel format for host textures.** `engine_update_info.flags` (documented as zero today) gets bits, with the meaning "the pixels are ..." (RGBA8
   stays the default, so no host changes):
   - `ENGINE_UPDATE_BGRA`: bytes are B, G, R, A.
   - `ENGINE_UPDATE_IGNORE_ALPHA`: the alpha byte is not read and counts as 255, for either order (video is opaque; the host then passes its buffer as it is,
     with no pass forcing the alpha byte). A name like BGRX/RGBX is fine if you prefer a format enum.
   `texture_create` may take the format too, so that a texture made as BGRA stays BGRA across the resizes `texture_update` allows; you decide whether the format
   is a property of the texture or of each update (the host only needs it per update).
2. **A query that says what is supported.** The answer can depend on the device (host textures are uploaded once per device, and Vulkan, Direct3D 12 and Metal
   do not offer the same formats), so a function rather than a bit in `engine_info.caps`, for example
   `texture_format_support(engine, format_flags, &support)`, where `support` is one of:
   - `ENGINE_FORMAT_NATIVE`: the GPU samples the pixels as they are; no conversion anywhere.
   - `ENGINE_FORMAT_CONVERTED`: accepted, and the engine converts them (on the CPU while it copies the rows, or on the GPU); costs a little.
   - `ENGINE_FORMAT_UNSUPPORTED`.
   Needs `ENGINE_CAP_HOST_TEXTURES`. State in the header how it behaves before any surface exists (the format the engine would pick for the devices it knows,
   or CONVERTED), and whether it can change when a surface or device is added.
3. **A guaranteed baseline.** Every format the engine documents is accepted by `texture_update` whatever the query says: RGBA8 is always native or
   converted, BGRA and the alpha variants likewise, so a host never has to handle "unsupported" for these. The query only tells a host whether the fast path
   exists (it can then log, or choose the format its source already has). A flag the engine does not know still fails with `INVALID_ARGUMENT` as flags do now.
4. The sRGB/linear choice stays `ENGINE_TEXTURE_SRGB` and applies to every format.
5. Document it in the C API header and the guide's host-texture section, and mention in `ENGINE_CAP_HOST_TEXTURES`'s line that the formats are queried.

## Tests

A C API test (next to the host-texture tests) that, for each of RGBA8, BGRA8 and the two opaque variants, fills a texture with a known pattern (distinct R, G,
B and alpha values, alpha not 255 in the data), draws it on a quad in an off-screen surface and reads the picture back: the colours match whichever order the
host sent; the opaque variants draw opaque whatever the alpha byte holds. The same test calls the query for each format and checks it never says
UNSUPPORTED for the documented ones, and that the results are the same on a second call.

## What the host does with it

The host (`ui/shared/views/world/EngineScene.cpp`, `set_robot_screen_bgra`) drops its per-frame channel swap and its copy: it passes the premultiplied BGRA
buffer it already holds to `texture_update` with `ENGINE_UPDATE_BGRA | ENGINE_UPDATE_IGNORE_ALPHA` and `stride = width * 4`, and logs once whether the format is
native or converted. Nothing else on the host changes.

## Done

- [x] flags and the query in `engine_c.h`
- [x] the conversion (or the BGRA texture format) in the host-texture path
- [x] C API test (`host_import.c`, `TestTextures`: the stored pixels per layout, the query; the texture keeps RGBA8 so a drawn quad is the same)
- [x] header and guide text (`engine_c.h`, `engine.h`, `docs/guide/engine-library.md` "Host textures")
- [ ] checked on Windows and macOS (see `KNOWN-ISSUES.md`)

**Decided (the user asked for GPU support, 2026-11-24):** a texture keeps the bytes in the order of its newest update and its device texture is `RGBA8` or `BGRA8` (sRGB or not) to match, when the device's `FormatSupport` allows it; otherwise the channels are swapped in the staging copy. Ignoring the alpha is a pass in the update call (the RHI has no component swizzle), so the query answers NATIVE for RGBA and BGRA and CONVERTED with `ENGINE_UPDATE_IGNORE_ALPHA`. `texture_read_pixels` always returns R, G, B, A. The table gained `texture_format_support` at its end; the ABI version is unchanged (1.0, the snapshot regenerated: the minor stays as the user decided on 2026-11-24).
