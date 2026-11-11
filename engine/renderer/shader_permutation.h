#pragma once

//! Shader permutations: one Slang source built offline into several SPIR-V modules, one per option combination.
//!
//! A shader declares its options in a `// permutation-options: A B C` comment and uses them as compile-time defines
//! (`#if A`); a manifest next to it (`<name>.perm`) lists which combinations to build. The `shader_mint` tool compiles
//! each listed key, drops duplicate modules, and commits the result as `<name>.perm.inc`: a
//! [[renderer::ShaderPermutations]] table that [[renderer::FindPermutation]] searches at run time. A key that was not
//! built is an error naming the shader and the key, never a silent fallback.

#include "renderer/shader_reflection.h"
#include "rhi/desc.h"
#include "types.h"

namespace engine_placeholder::renderer
{

/// The most options one shader may declare.
inline constexpr u32 kMaxPermutationOptions = 16;
/// The most keys (option combinations) one manifest may list.
inline constexpr u32 kMaxPermutationKeys = 32;

/// A set of shader options that are switched on: bit `i` is option `i` in declaration order.
///
/// - Thread-safety: thread-compatible plain data.
struct PermutationKey
{
    /// The bit set; bits at or above the shader's option count must be 0.
    u32 bits = 0;

    /// Returns whether option `index` is on.
    ///
    /// - Param index: an option index below [[renderer::kMaxPermutationOptions]].
    /// - Returns: true if the option is on; false for an index at or past the limit.
    /// - Fails: never; an index past the limit is not an error.
    constexpr bool Has(u32 index) const { return index < kMaxPermutationOptions && ((bits >> index) & 1u) != 0; }

    /// Returns this key with option `index` switched on.
    ///
    /// - Param index: an option index below [[renderer::kMaxPermutationOptions]]; larger values change nothing.
    /// - Returns: the key with the option on, or an unchanged copy for an index at or past the limit.
    /// - Fails: never; an index past the limit is not an error.
    constexpr PermutationKey With(u32 index) const
    {
        PermutationKey key = *this;
        if (index < kMaxPermutationOptions)
        {
            key.bits |= 1u << index;
        }
        return key;
    }

    /// Returns whether two keys switch on the same options.
    ///
    /// - Param other: the key to compare with.
    /// - Returns: true if the bit sets are equal.
    /// - Fails: never.
    constexpr bool operator==(const PermutationKey& other) const { return bits == other.bits; }
};

/// One compiled shader module in every bytecode format it was minted in, and its reflection blob (the reflection is the
/// same for every format: it comes from the one Slang source).
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: the pointers refer to static data in a generated `*.perm.inc` or `*.spv.inc` / `*.dxil.inc`.
struct PermutationModule
{
    /// The SPIR-V words, or null.
    const u32* words = nullptr;
    /// Number of SPIR-V words.
    usize word_count = 0;
    /// The reflection blob; decode it with [[renderer::ParseShaderReflection]].
    const u8* reflection = nullptr;
    /// Size of the reflection blob in bytes.
    usize reflection_size = 0;
    /// The DXIL container, or null when the module was not minted for Direct3D 12.
    const u8* dxil = nullptr;
    /// Size of the DXIL container in bytes.
    usize dxil_size = 0;

    /// Returns the module's bytecode in one format.
    ///
    /// - Param format: the device's format ([[rhi::DeviceCaps::shader_format]]).
    /// - Returns: the blob, empty when the module has none in that format.
    /// - Fails: an empty blob, as described.
    constexpr rhi::ShaderBlob Blob(rhi::ShaderFormat format) const
    {
        switch (format)
        {
        case rhi::ShaderFormat::Spirv: return rhi::SpirvBlob(words, word_count);
        case rhi::ShaderFormat::Dxil: return rhi::DxilBlob(dxil, dxil_size);
        case rhi::ShaderFormat::Msl: break;
        }
        return rhi::ShaderBlob{format, nullptr, 0};
    }

    /// Returns whether the module has bytecode in at least one format.
    ///
    /// - Fails: never.
    constexpr bool HasCode() const
    {
        return (words != nullptr && word_count != 0) || (dxil != nullptr && dxil_size != 0);
    }
};

/// Which module a key uses. Keys that compile to identical SPIR-V share a module.
///
/// - Thread-safety: thread-compatible plain data.
struct PermutationEntry
{
    /// The key's option bits.
    u32 key_bits = 0;
    /// Index into [[renderer::ShaderPermutations]]'s `modules`.
    u32 module = 0;
};

/// The table the mint tool generates for one permuted shader.
///
/// Module 0 is the base module (no option on), the same words as the shader's plain `*.spv.inc`.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: all pointers refer to static data in a generated `*.perm.inc`.
struct ShaderPermutations
{
    /// The shader's file name, for error messages (for example `permute_test.frag`).
    const char* shader = "";
    /// The option names in declaration order; option `i` is bit `i` of a key.
    const char* const* options = nullptr;
    /// Number of options, at most [[renderer::kMaxPermutationOptions]].
    u32 option_count = 0;
    /// The distinct compiled modules.
    const PermutationModule* modules = nullptr;
    /// Number of modules.
    u32 module_count = 0;
    /// One entry per built key, sorted by increasing `key_bits`.
    const PermutationEntry* entries = nullptr;
    /// Number of entries, at most [[renderer::kMaxPermutationKeys]].
    u32 entry_count = 0;
};

/// Checks that a table is internally consistent.
///
/// Option names are non-empty and unique, counts are within the limits, entries are sorted and unique, keys use only
/// declared options, module indices are in range, the base key is present and uses module 0, and every module is
/// non-empty.
///
/// - Param table: the table to check.
/// - Returns: null if it is consistent, else a static string naming the first problem.
/// - Fails: reported through the return value, never by aborting.
/// - Ownership: the returned string is static and is never freed by the caller.
/// - Thread-safety: thread-safe. A pure function of its argument.
const char* ValidateShaderPermutations(const ShaderPermutations& table);

/// Builds a key from option names.
///
/// - Param table: the permuted shader.
/// - Param names: pointers to nul-terminated option names; may be null if `count` is 0.
/// - Param count: number of names.
/// - Param out: receives the key; must not be null.
/// - Param error: receives a message naming the shader and the unknown option on failure; may be null.
/// - Returns: true if every name is a declared option.
/// - Fails: reported through the return value and `error`.
/// - Ownership: all pointers are borrowed for the call.
/// - Thread-safety: thread-safe. A pure function of its arguments.
bool MakePermutationKey(const ShaderPermutations& table, const char* const* names, u32 count, PermutationKey* out,
                        ReflectionError* error);

/// Finds the module built for a key. The search is linear over at most [[renderer::kMaxPermutationKeys]] entries.
///
/// - Param table: the permuted shader.
/// - Param key: the options that are on.
/// - Param error: receives a message naming the shader and the key (for example `shader 'x.frag' has no permutation
///   {INVERT, HALF}`) on failure; may be null.
/// - Returns: a pointer into `table`, or null if the key was not built (or the table is corrupt, which the message
/// says).
/// - Fails: reported through the return value and `error`; nothing is substituted.
/// - Ownership: the pointer refers to static data and stays valid as long as `table`'s storage does.
/// - Thread-safety: thread-safe. A pure function of its arguments.
const PermutationModule* FindPermutation(const ShaderPermutations& table, PermutationKey key, ReflectionError* error);

/// Writes a key as `{A, B}` (or `{}` for the base key).
///
/// - Param table: the permuted shader, for the option names.
/// - Param key: the key to describe.
/// - Param out: receives a nul-terminated string, truncated to fit.
/// - Param capacity: size of `out` in bytes; 0 writes nothing.
/// - Ownership: `out` is borrowed for the call.
/// - Thread-safety: thread-safe. A pure function of its arguments.
void DescribePermutationKey(const ShaderPermutations& table, PermutationKey key, char* out, usize capacity);

}  // namespace engine_placeholder::renderer
