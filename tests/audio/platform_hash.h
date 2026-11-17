// The committed hash of an audio scene for this build's platform (Phase 24 Day 381, the Windows PC). The scene hashes
// of the audio tests are a same-build regression check: the C libraries' last bits differ, so each platform keeps its
// own value and machines are compared within the tolerance instead (docs/guide/audio.md). The two-value form gives
// macOS Linux's value; the three-value form (Phase 26 Day 441, the Mac) has one for macOS.
#pragma once

#include "build_defs.h"
#include "types.h"

namespace engine_placeholder::audio::testing
{

/// The value committed for this platform: `windows_value` on Windows, `macos_value` on macOS, `linux_value` elsewhere.
constexpr u64 PlatformHash(u64 linux_value, u64 windows_value, u64 macos_value)
{
    return ENGINE_PLATFORM_WINDOWS ? windows_value : (ENGINE_PLATFORM_MACOS ? macos_value : linux_value);
}

/// The value committed for this platform: `windows_value` on Windows, `linux_value` elsewhere (macOS included).
constexpr u64 PlatformHash(u64 linux_value, u64 windows_value)
{
    return PlatformHash(linux_value, windows_value, linux_value);
}

}  // namespace engine_placeholder::audio::testing
