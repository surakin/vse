// The audio snapshot (Phase 26 Day 437): what a mixer needs to carry on a render bit for bit from the block it was
// saved at.
#pragma once

#include "audio/clip.h"
#include "audio/stream.h"
#include "string_view.h"
#include "types.h"

namespace engine_placeholder::audio
{

/// How a snapshot is read back: a voice names its clip (and, for a stream, its stream) by the clip's name, and the
/// host gives the same objects back.
///
/// - Thread-safety: thread-compatible; called on the restoring thread only.
/// - Ownership: the returned pointers are shared with the caller's own tables.
class SnapshotResolver
{
public:
    virtual ~SnapshotResolver() = default;

    /// Finds the clip a voice played.
    ///
    /// - Param name: the clip's name as `AudioClip::Name` gave it.
    /// - Param frames: the frame count the saved clip had (a different count is a different clip).
    /// - Returns: the clip, or null when it is unknown or has another length.
    virtual AudioClipConstPtr FindClip(StringView name, u32 frames) = 0;

    /// Finds the stream buffer of a stream voice.
    ///
    /// - Param name: the name of the stream's clip.
    /// - Returns: the buffer, or null when it is unknown.
    virtual StreamBufferPtr FindStream(StringView name) = 0;
};

/// How a snapshot call ended.
enum class SnapshotStatus : u8
{
    /// Done.
    Ok,
    /// Commands are queued: the mixer must have applied everything it was sent (render a block first).
    Busy,
    /// The bytes are too short, not a snapshot, or a value is out of range.
    Corrupt,
    /// The snapshot is of another version.
    Version,
    /// The mixer is set up differently from the one saved (rate, layout, voices, buses, inserts, effects).
    Mismatch,
    /// A clip or stream the snapshot names is unknown.
    MissingClip,
    /// A voice plays a live push stream, which a snapshot cannot hold (`SaveState` refuses; stop that voice first).
    LiveStream,
};

}  // namespace engine_placeholder::audio
