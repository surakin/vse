#pragma once

#include <vector>

#include "rhi/enums.h"
#include "rhi/spirv.h"

//! Builds the smallest SPIR-V module `rhi::ValidateSpirv` accepts, for tests that need a shader module.

namespace spirv_test {

// A module with capability Shader, the Logical/GLSL450 memory model and one entry point of `stage` named `entry`.
inline std::vector<engine_placeholder::u32> MinimalModule(engine_placeholder::rhi::ShaderStage stage, const char* entry = "main") {
    using namespace engine_placeholder;
    std::vector<u32> w = {rhi::kSpirvMagic, 0x00010300u, 0, 8, 0};
    w.push_back((2u << 16) | 17u); // OpCapability Shader
    w.push_back(1);
    w.push_back((3u << 16) | 14u); // OpMemoryModel Logical GLSL450
    w.push_back(0);
    w.push_back(1);
    u32 model = 5;
    if (stage == rhi::ShaderStage::Vertex) model = 0;
    if (stage == rhi::ShaderStage::Fragment) model = 4;
    std::vector<u32> name;
    u32 packed = 0;
    u32 byte = 0;
    for (const char* c = entry;; ++c) {
        packed |= static_cast<u32>(static_cast<unsigned char>(*c)) << (8 * byte);
        if (++byte == 4) {
            name.push_back(packed);
            packed = 0;
            byte = 0;
        }
        if (*c == '\0') break;
    }
    if (byte != 0) name.push_back(packed);
    w.push_back(static_cast<u32>(((3 + name.size()) << 16) | 15u)); // OpEntryPoint model %1 "name"
    w.push_back(model);
    w.push_back(1);
    for (u32 n : name) w.push_back(n);
    return w;
}

} // namespace spirv_test
