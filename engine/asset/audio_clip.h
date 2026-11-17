#pragma once
//! The audio clip resource (Phase 26 Day 418): decoded interleaved samples with a rate, a channel count and loop
//! points, minted offline from a WAV file. [[asset::BuildAudioClipBlob]] writes an `ACLP` blob,
//! [[asset::AudioClipView]] reads one in place (the audio module makes its clip from the view without copying `f32`
//! samples).
//!
//! Layout of the blob's sections (little-endian):
//!
//! | Section | Content |
//! |---|---|
//! | 16 header | 32 bytes: sample rate `u32`, channels `u32` (1, 2, 4, 6 or 8), stored format `u32` (0 = `f32`, 1 =
//! `s16`), frames `u32`, loop start `u32`, loop end `u32` (0 = no loop), flags `u32` (zero), reserved `u32` (zero) | |
//! 17 samples | `frames * channels` samples of the stored format, interleaved; 64-byte aligned like every section |
//!
//! The asset module knows nothing about conversions: the minter encodes the samples, the audio module converts a
//! `s16` clip to `f32` once, when it makes its clip.

#include "asset/blob.h"
#include "asset/resource_id.h"
#include "asset/resource_type.h"
#include "asset/status.h"
#include "containers/shared_bytes.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "types.h"

namespace engine_placeholder::asset
{

/// The size of the header section.
inline constexpr usize kAudioClipHeaderBytes = 32;
/// The most frames a clip blob holds (the audio module's clip limit).
inline constexpr u32 kMaxAudioClipFrames = 1u << 29;

/// The section ids of an audio clip blob.
enum AudioClipSection : u32
{
    kAudioClipSectionHeader = 16,   ///< The description (32 bytes).
    kAudioClipSectionSamples = 17,  ///< The interleaved samples.
};

/// How the samples of a clip blob are stored.
enum class AudioClipStorage : u32
{
    F32 = 0,  ///< 32-bit float, little-endian.
    S16 = 1,  ///< Signed 16-bit, little-endian.
};

/// What a clip blob says about its samples, apart from the samples.
///
/// - Thread-safety: thread-compatible plain data.
struct AudioClipHeader
{
    u32 sample_rate = 0;                               ///< Frames per second, 8,000 to 384,000.
    u32 channels = 0;                                  ///< 1, 2, 4, 6 or 8.
    AudioClipStorage storage = AudioClipStorage::F32;  ///< How the samples are stored.
    u32 frames = 0;                                    ///< At least 1, at most `kMaxAudioClipFrames`.
    u32 loop_start = 0;                                ///< The loop start in frames.
    u32 loop_end = 0;  ///< The loop end in frames; 0 means no loop, else `loop_start < loop_end <= frames`.
};

/// Writes the blob of an audio clip, deterministically.
///
/// - Param types: the registry; it must hold `kAudioClip`.
/// - Param id: the clip's resource id.
/// - Param header: the description; every field is checked.
/// - Param samples: the encoded samples, exactly `frames * channels` samples of `header.storage`.
/// - Param out: receives the blob bytes.
/// - Param target: the target platform code of the blob header.
/// - Fails: `Invalid` naming the first broken rule; the errors of [[asset::BlobWriter::Finish]].
/// - Ownership: `samples` is read during the call; `out` receives a copy.
Status BuildAudioClipBlob(const TypeRegistry& types, ResourceId id, const AudioClipHeader& header, ByteSpan samples,
                          Vector<byte>* out, u32 target = 0);

/// An audio clip blob validated and read in place.
///
/// - Thread-safety: thread-compatible; immutable after `Open`.
/// - Ownership: shares the buffer it was opened on.
class AudioClipView
{
public:
    /// Creates an empty view.
    AudioClipView() = default;

    /// Validates `bytes` as an audio clip blob of the current version and keeps a share of them. The samples are not
    /// scanned (the audio module checks that `f32` samples are finite when it makes its clip).
    ///
    /// - Fails: the errors of [[asset::BlobReader::Open]]; `TypeMismatch` when the blob is not an `ACLP`; `Corrupt` for
    /// a missing or mis-sized section, a field outside its limits, a header field this version does not define or a bad
    /// loop.
    /// - Ownership: shares `bytes`.
    Status Open(const SharedBytesPtr& bytes, const TypeRegistry& types);

    /// The description.
    /// - Fails: never.
    const AudioClipHeader& Header() const { return header_; }

    /// The resource id recorded in the blob header.
    /// - Fails: never.
    ResourceId Id() const { return id_; }

    /// The sample bytes (`frames * channels` samples of the stored format).
    /// - Fails: never.
    /// - Ownership: borrowed from the shared buffer.
    ByteSpan Samples() const;

    /// The buffer the view shares (the audio module shares it too, to read `f32` samples in place).
    /// - Fails: never.
    /// - Ownership: borrowed.
    const SharedBytesPtr& Bytes() const { return bytes_; }

    /// Where the samples start in `Bytes()`.
    /// - Fails: never.
    usize SamplesOffset() const { return samples_; }

private:
    AudioClipHeader header_;
    ResourceId id_;
    SharedBytesPtr bytes_;
    usize samples_ = 0;
    usize sample_bytes_ = 0;
};

}  // namespace engine_placeholder::asset
