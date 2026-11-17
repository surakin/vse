// Direction to speaker gains (Phase 26 Day 429): constant-power panning for every output layout, with `+ - * /` and
// `Sqrt` only, so it runs on the audio thread and renders the same bits on every machine.
#pragma once

#include "audio/format.h"
#include "types.h"

namespace engine_placeholder::audio
{

/// The most channels of any layout.
inline constexpr u32 kMaxPanChannels = 8;

/// The speaker gains of a mono source in `direction` from the listener.
///
/// The direction is in listener space (x right, y up, z forward) and need not be a unit vector. Stereo uses an
/// equal-power law of the lateral position; quad, 5.1 and 7.1 use vector-based amplitude panning over the two speakers
/// on either side of the direction (a 2x2 solve), normalised so the squares of the gains add to 1. Elevation and
/// `spread` both blend toward every speaker at equal level; a direction without a horizontal part (overhead, or at the
/// listener) spreads fully. The LFE channel gets 0. The gains vary continuously with the direction.
///
/// - Param layout: the output layout.
/// - Param x: the direction's right component.
/// - Param y: the direction's up component.
/// - Param z: the direction's forward component.
/// - Param spread: 0 for a point source to 1 for one that fills every speaker.
/// - Param gains: receives `ChannelCount(layout)` gains, the rest 0.
/// - Fails: a non-finite input gives a centred source.
/// - Ownership: `gains` is borrowed for the call and must hold `kMaxPanChannels` values.
void SpeakerGains(ChannelLayout layout, f32 x, f32 y, f32 z, f32 spread, f32* gains);

}  // namespace engine_placeholder::audio
