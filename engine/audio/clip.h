// The audio clip's shared handle (Phase 26 Day 415): the class is defined on Day 416; its aliases live here, beside it,
// as the shared-unit rule asks (tools/shared_unit_check/units.txt).
#pragma once

#include "containers/shared_ptr.h"

namespace engine_placeholder::audio
{

class AudioClip;

/// A shared, mutable-by-its-owner handle to a decoded or minted clip; every voice that plays the clip holds one.
using AudioClipPtr = SharedPtr<AudioClip>;
/// A shared read-only handle to a clip.
using AudioClipConstPtr = SharedPtr<const AudioClip>;

}  // namespace engine_placeholder::audio
