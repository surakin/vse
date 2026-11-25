// The committed hash of an audio scene for this build's platform (Phase 24 Day 381, the Windows PC). The scene hashes
// of the audio tests are a same-build regression check: the C libraries' last bits differ, so each platform keeps its
// own value and machines are compared within the tolerance instead (docs/guide/audio.md). The two-value form gives
// macOS Linux's value; the three-value form (Phase 26 Day 441, the Mac) has one for macOS.
#pragma once

#include "build_defs.h"
#include "types.h"

namespace vse::audio::testing
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

/// Skips the running test on a build that has no committed hash: the values above were taken on x86-64 machines, and
/// Apple silicon computes other bits (libm and the processor's own rounding), so none is committed for it yet. The
/// determinism checks of such a test (the same bits twice) run on every platform in the tests that have them.
#define AUDIO_SKIP_WITHOUT_COMMITTED_HASH()                                                                            \
    do                                                                                                                 \
    {                                                                                                                  \
        if (ENGINE_PLATFORM_MACOS && ENGINE_ARCH_ARM64)                                                                \
        {                                                                                                              \
            SKIP("no hash is committed for macOS on arm64 (the committed ones are the x86-64 builds')");               \
        }                                                                                                              \
    } while (false)

}  // namespace vse::audio::testing
