#pragma once

#include "containers/vector.h"
#include "rhi/enums.h"
#include "rhi/spirv.h"

//! Builds the smallest SPIR-V module `rhi::ValidateSpirv` accepts, for tests that need a shader module.

namespace spirv_test
{

// A module with capability Shader, the Logical/GLSL450 memory model and one entry point of `stage` named `entry`.
inline vse::Vector<vse::u32> MinimalModule(vse::rhi::ShaderStage stage, const char* entry = "main")
{
    using namespace vse;
    Vector<u32> w;
    w.Push(rhi::kSpirvMagic);
    w.Push(0x00010300u);
    w.Push(0);
    w.Push(8);
    w.Push(0);
    w.Push((2u << 16) | 17u);  // OpCapability Shader
    w.Push(1);
    w.Push((3u << 16) | 14u);  // OpMemoryModel Logical GLSL450
    w.Push(0);
    w.Push(1);
    u32 model = 5;
    if (stage == rhi::ShaderStage::Vertex)
    {
        model = 0;
    }
    if (stage == rhi::ShaderStage::Fragment)
    {
        model = 4;
    }
    Vector<u32> name;
    u32 packed = 0;
    u32 byte = 0;
    for (const char* c = entry;; ++c)
    {
        packed |= static_cast<u32>(static_cast<u8>(*c)) << (8 * byte);
        if (++byte == 4)
        {
            name.Push(packed);
            packed = 0;
            byte = 0;
        }
        if (*c == '\0')
        {
            break;
        }
    }
    if (byte != 0)
    {
        name.Push(packed);
    }
    w.Push(static_cast<u32>(((3 + name.Size()) << 16) | 15u));  // OpEntryPoint model %1 "name"
    w.Push(model);
    w.Push(1);
    for (usize i = 0; i < name.Size(); ++i)
    {
        w.Push(name[i]);
    }
    return w;
}

}  // namespace spirv_test
