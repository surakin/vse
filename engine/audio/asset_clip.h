// Making an audio clip from a minted blob (Phase 26 Day 418): the audio module's side of the `ACLP` resource. The
// runtime reads minted clips, never WAV files; the WAV parser of `audio/wav.h` is for the minter and the tools.
#pragma once

#include "asset/audio_clip.h"
#include "asset/cpu_loaders.h"
#include "asset/resource_manager.h"
#include "audio/clip.h"
#include "string/string.h"

namespace vse::audio
{

/// Makes a clip from an opened clip blob. An `f32` blob is shared in place (no copy: the clip names the blob's buffer
/// by offset); an `s16` blob is converted to `f32` once, here, so the mixer converts nothing at play time.
///
/// - Param view: an opened blob.
/// - Param name: a name for logs and tools; may be empty.
/// - Param out: receives the clip on success.
/// - Returns: `Ok`, or what is wrong: `Misaligned` when the buffer does not put the samples on a 4-byte boundary,
/// `NotFinite` for a NaN or infinity.
/// - Fails: reported by return value; `out` is not written then.
/// - Thread-safety: thread-safe. Allocates; not an audio-thread operation.
/// - Ownership: the clip shares the blob's buffer (`f32`) or owns its converted copy (`s16`).
ClipStatus MakeClipFromAsset(const asset::AudioClipView& view, StringView name, AudioClipPtr* out);

/// Loads a clip through a resource manager that has the CPU loaders registered (`asset::RegisterCpuLoaders`), by id. A
/// missing or corrupt clip is the manager's named error, never a silent substitute.
///
/// - Param manager: the manager.
/// - Param id: the clip's resource id.
/// - Param name: a name for the clip; may be empty.
/// - Param out: receives the clip on success.
/// - Returns: the manager's status (`NotFound`, `Corrupt`, `TypeMismatch`, ...), or `Corrupt` when the loaded blob's
/// samples are refused.
/// - Fails: reported by return value; `out` is not written then.
/// - Thread-safety: thread-safe when the manager is (see `asset::ResourceManager::Load`).
/// - Ownership: the clip shares the buffer of an `f32` blob, so it stays valid after the manager releases it.
asset::Status LoadAudioClip(asset::ResourceManager& manager, asset::ResourceId id, StringView name, AudioClipPtr* out);

}  // namespace vse::audio
