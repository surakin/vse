#pragma once

// The permutation half of shader_mint: parsing a shader's option declaration and its `.perm` manifest, and writing the
// `.perm.inc` table that renderer::FindPermutation searches. Process spawning and compiling live in main.cpp.

#include <string>
#include <vector>

#include "types.h"

namespace engine_placeholder::shader_mint {

// Reads the `// permutation-options: A B C` line from the shader source (it must be in the first 60 lines). Options are
// upper-case identifiers, unique, at most renderer::kMaxPermutationOptions, and each must also be used somewhere else
// in the source (a typo in the declaration or the source would otherwise build identical modules silently).
bool ParseOptionDeclaration(const std::string& source_text, std::vector<std::string>* options, std::string* error);

// Reads a manifest: `#` comments, blank lines, and lines `key -` (no option on) or `key A B` (those options on). Every
// option must be declared, keys must be unique as sets, the base key `key -` must be present, and there are at most
// renderer::kMaxPermutationKeys. `keys` receives the option bit sets in increasing order (the base key first).
bool ParseManifest(const std::string& manifest_text, const std::vector<std::string>& options, std::vector<u32>* keys,
                   std::string* error);

// "{INVERT, HALF}" or "{}" for the base key; used in messages.
std::string DescribeKey(const std::vector<std::string>& options, u32 bits);

// printf-style append to a string.
void Appendf(std::string* out, const char* format, ...) __attribute__((format(printf, 2, 3)));

// Appends `inline constexpr usize <name>WordCount = N;` and `inline constexpr u32 <name>[<name>WordCount] = {...};`
// (six words per line) and, for the byte form, `<name>ByteCount` and a `u8` array (sixteen per line).
void AppendWordArray(std::string* out, const std::string& name, const std::vector<u32>& words);
void AppendByteArray(std::string* out, const std::string& name, const std::vector<u8>& bytes);

// One compiled module of a permuted shader.
struct BuiltModule {
    std::vector<u32> words;
    std::vector<u8> reflection;

    bool operator==(const BuiltModule& other) const { return words == other.words && reflection == other.reflection; }
};

// Assigns each key (in the given order) a module index, creating a new module only when no earlier module has the same
// words and reflection. The first key always gets module 0.
void DedupeModules(const std::vector<BuiltModule>& per_key, std::vector<BuiltModule>* modules, std::vector<u32>* key_module);

// Writes the `.perm.inc` text. `base` is the symbol base (`kPermuteTestFrag`); module 0 must be the base module and is
// referenced through the plain `.spv.inc`/`.refl.inc` symbols instead of being repeated. `key_module[i]` is the module
// of `keys[i]`.
std::string EmitPermutationInc(const std::string& base, const std::string& shader_file, const std::string& source_label,
                               const std::string& compiler, const std::vector<std::string>& options,
                               const std::vector<u32>& keys, const std::vector<u32>& key_module,
                               const std::vector<BuiltModule>& modules);

} // namespace engine_placeholder::shader_mint
