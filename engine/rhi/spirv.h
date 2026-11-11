#pragma once

#include "rhi/enums.h"
#include "types.h"

//! A small SPIR-V module reader: the cheap sanity check every backend runs before a module reaches a driver.

namespace engine_placeholder::rhi
{

/// The SPIR-V magic number (the first word of every module in the host's byte order).
inline constexpr u32 kSpirvMagic = 0x07230203u;

/// The longest shader entry point name the RHI accepts, in bytes, excluding the terminator.
inline constexpr usize kMaxShaderEntryNameLength = 63;

/// Returns whether `name` is longer than [[rhi::kMaxShaderEntryNameLength]] bytes.
///
/// - Param name: a nul-terminated string; must not be null.
/// - Returns: true if it is too long.
/// - Thread-safety: thread-safe. A pure function.
inline bool ShaderEntryNameTooLong(const char* name)
{
    usize length = 0;
    while (name[length] != '\0' && length <= kMaxShaderEntryNameLength)
    {
        ++length;
    }
    return length > kMaxShaderEntryNameLength;
}

/// Checks that a SPIR-V module is well formed enough to hand to a driver and that it implements `stage`.
///
/// The check reads the header and walks every instruction once. It accepts a module only if:
/// the magic number is right (a byte-swapped module gets its own message), the version is 1.0 to 1.6, the id bound
/// is non-zero, every instruction has a non-zero length that stays inside the module and the stream ends exactly
/// at the last word, the module declares capability `Shader` and no `Kernel`, uses the `Logical` addressing model with
/// the `GLSL450` or `Vulkan` memory model, and has an `OpEntryPoint` named `entry` whose execution model matches
/// `stage`. This is a subset check of what the engine's shaders use, not a validator: `spirv-val` is the dev-time
/// validator.
///
/// - Param words: the module's 32-bit words in host byte order.
/// - Param word_count: number of words in `words`.
/// - Param stage: the single stage the module must implement (vertex, fragment or compute).
/// - Param entry: the entry point name to look for; at most [[rhi::kMaxShaderEntryNameLength]] bytes.
/// - Returns: null if the module is acceptable, else a static string naming the first problem.
/// - Fails: reported through the return value, never by aborting.
/// - Ownership: the returned string is static and is never freed by the caller; `words` and `entry` are borrowed.
/// - Thread-safety: thread-safe. A pure function of its arguments.
const char* ValidateSpirv(const u32* words, usize word_count, ShaderStage stage, const char* entry);

}  // namespace engine_placeholder::rhi
