#pragma once

// The permutation half of shader_mint: parsing a shader's option declaration and its `.perm` manifest, and writing the
// `.perm.inc` table that renderer::FindPermutation searches. Process spawning and compiling live in main.cpp.

#include "text.h"

namespace engine_placeholder::shader_mint {

// Reads the `// permutation-options: A B C` line from the shader source (it must be in the first 60 lines). Options are
// upper-case identifiers, unique, at most renderer::kMaxPermutationOptions, and each must also be used somewhere else
// in the source (a typo in the declaration or the source would otherwise build identical modules silently).
bool ParseOptionDeclaration(StringView source_text, Vector<Text>* options, Text* error);

// Reads a manifest: `#` comments, blank lines, and lines `key -` (no option on) or `key A B` (those options on). Every
// option must be declared, keys must be unique as sets, the base key `key -` must be present, and there are at most
// renderer::kMaxPermutationKeys. `keys` receives the option bit sets in increasing order (the base key first).
bool ParseManifest(StringView manifest_text, const Vector<Text>& options, Vector<u32>* keys, Text* error);

// "{INVERT, HALF}" or "{}" for the base key; used in messages.
Text DescribeKey(const Vector<Text>& options, u32 bits);

// Appends `inline constexpr usize <name>WordCount = N;` and `inline constexpr u32 <name>[<name>WordCount] = {...};`
// (six words per line) and, for the byte form, `<name>ByteCount` and a `u8` array (sixteen per line).
void AppendWordArray(Text* out, StringView name, const Vector<u32>& words);
void AppendByteArray(Text* out, StringView name, const Vector<u8>& bytes);

// One compiled module of a permuted shader.
struct BuiltModule {
    Vector<u32> words;
    Vector<u8> reflection;

    bool operator==(const BuiltModule& other) const;
    BuiltModule Clone() const;
};

// Assigns each key (in the given order) a module index, creating a new module only when no earlier module has the same
// words and reflection. The first key always gets module 0.
void DedupeModules(const Vector<BuiltModule>& per_key, Vector<BuiltModule>* modules, Vector<u32>* key_module);

// Writes the `.perm.inc` text. `base` is the symbol base (`kPermuteTestFrag`); module 0 must be the base module and is
// referenced through the plain `.spv.inc`/`.refl.inc` symbols instead of being repeated. `key_module[i]` is the module
// of `keys[i]`.
Text EmitPermutationInc(StringView base, StringView shader_file, StringView source_label, StringView compiler,
                        const Vector<Text>& options, const Vector<u32>& keys, const Vector<u32>& key_module,
                        const Vector<BuiltModule>& modules);

} // namespace engine_placeholder::shader_mint
