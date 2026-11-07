#pragma once

// Converts the JSON that `slangc -reflection-json` writes into a renderer::ShaderReflection, and cross-checks the
// result against the compiled SPIR-V so a reflection that disagrees with the module fails the build.

#include "renderer/shader_reflection.h"
#include "text.h"
#include "types.h"

namespace engine_placeholder::shader_mint {

// Parses `json_text` (the whole file) and fills `out`. Returns true on success; on failure `error` says why.
bool ReflectionFromJson(StringView json_text, renderer::ShaderReflection* out, Text* error);

// Checks the descriptor (set, binding) pairs, the compute local size and the vertex input locations in `reflection`
// against the decorations and execution modes in the SPIR-V words.
bool CrossCheckSpirv(const renderer::ShaderReflection& reflection, const u32* words, usize word_count, Text* error);

} // namespace engine_placeholder::shader_mint
