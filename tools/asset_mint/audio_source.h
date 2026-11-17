#pragma once

//! The text source form of an audio clip (`.audio.src`) and the audio minter. Phase 26 Day 418. An offline tool's code:
//! never linked into the engine; its tests compile it directly. It is the one place a WAV file is read for the engine
//! (the runtime reads the minted blob only).
//!
//! Line-oriented text; `#` starts a comment; fields are separated by spaces or tabs:
//!
//!     id 0123456789abcdef
//!     wav relative/path/to/file.wav
//!     loop 1000 48000        # optional: loop start and end in frames, overriding the file's smpl loop
//!     format f32             # optional: f32 (default) or s16
//!     normalize on           # optional: scale so the peak is 1.0 (default off)
//!
//! `id` comes first (written by `asset_mint adopt`); `wav` names the file, resolved by the caller (the command line
//! tool resolves it against the source's directory).

#include "asset/audio_clip.h"
#include "asset/resource_id.h"
#include "asset/resource_type.h"
#include "asset/status.h"
#include "containers/array.h"
#include "containers/vector.h"
#include "string/string.h"
#include "string_view.h"
#include "types.h"

namespace engine_placeholder::asset
{

/// A parsed audio source.
///
/// - Thread-safety: thread-compatible.
struct AudioSource
{
    ResourceId id;                                     ///< The resource id from the `id` line.
    String<HeapAllocator> wav;                         ///< The WAV path as written.
    bool has_loop = false;                             ///< Whether a `loop` line overrides the file's loop.
    u32 loop_start = 0;                                ///< The override's start in frames.
    u32 loop_end = 0;                                  ///< The override's end in frames.
    AudioClipStorage storage = AudioClipStorage::F32;  ///< The stored sample format.
    bool normalize = false;                            ///< Whether to scale the peak to 1.0.

    /// Makes an empty source.
    AudioSource() : wav(HeapAllocator::Default()) {}
};

/// Where and why an audio source was refused.
///
/// - Thread-safety: immutable value.
struct AudioSourceError
{
    u32 line = 0;                  ///< 1-based line number; 0 when the error is about the file as a whole.
    Array<char, 96> message = {};  ///< A sentence.
};

/// Parses an audio source.
///
/// - Fails: `Invalid` with `error` filled in: a missing or repeated id, wav, loop, format or normalize line, an unknown
/// record, a wrong field count, a malformed value.
/// - Ownership: borrows `text` for the call; `*out` and `*error` are written.
Status ParseAudioSource(StringView text, AudioSource* out, AudioSourceError* error);

/// The audio minter: a parsed source and the bytes of the WAV file it names, to blob bytes. Deterministic: the same
/// inputs give the same bytes.
///
/// - Param wav_bytes: the whole WAV file.
/// - Fails: `Invalid` (with `error`) for a WAV the parser refuses, a loop outside the data, or what
/// [[asset::BuildAudioClipBlob]] refuses.
/// - Ownership: borrows its arguments for the call; `*out` is replaced.
Status MintAudio(const AudioSource& source, ByteSpan wav_bytes, const TypeRegistry& types, Vector<byte>* out,
                 AudioSourceError* error, u32 target = 0);

}  // namespace engine_placeholder::asset
