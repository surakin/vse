#include "rhi/spirv.h"

namespace engine_placeholder::rhi {

namespace {

constexpr u32 kOpMemoryModel = 14;
constexpr u32 kOpEntryPoint = 15;
constexpr u32 kOpCapability = 17;

constexpr u32 kCapabilityShader = 1;
constexpr u32 kCapabilityKernel = 6;

constexpr u32 kAddressingLogical = 0;
constexpr u32 kMemoryGlsl450 = 1;
constexpr u32 kMemoryVulkan = 3;

constexpr u32 kModelVertex = 0;
constexpr u32 kModelFragment = 4;
constexpr u32 kModelGlCompute = 5;

constexpr usize kHeaderWords = 5;
constexpr usize kMaxNameWords = (kMaxShaderEntryNameLength + 1) / 4;

bool ModelMatches(u32 model, ShaderStage stage) {
    switch (stage) {
    case ShaderStage::Vertex: return model == kModelVertex;
    case ShaderStage::Fragment: return model == kModelFragment;
    case ShaderStage::Compute: return model == kModelGlCompute;
    default: return false;
    }
}

// Compares the nul-terminated string packed into `operands` (little-endian bytes within each word) with `text`.
// Returns -1 if the string is not terminated inside the operands or its padding is not zero, else 1 on equality and
// 0 otherwise.
int PackedStringEquals(const u32* operands, usize count, const char* text) {
    usize i = 0;
    bool equal = true;
    for (usize w = 0; w < count; ++w) {
        for (u32 b = 0; b < 4; ++b) {
            const char c = static_cast<char>((operands[w] >> (8 * b)) & 0xffu);
            const char t = text[i];
            if (c != t) {
                equal = false;
            }
            if (c == '\0') {
                // SPIR-V pads the last word with zero bytes; anything else there is not padding but data (an id
                // that followed an unterminated name), so the instruction is malformed.
                if ((operands[w] >> (8 * b)) != 0) {
                    return -1;
                }
                return equal ? 1 : 0;
            }
            if (t == '\0') {
                equal = false; // `text` ended first; keep scanning for the terminator
            } else {
                ++i;
            }
        }
    }
    return -1;
}

} // namespace

const char* ValidateSpirv(const u32* words, usize word_count, ShaderStage stage, const char* entry) {
    if (words == nullptr || word_count < kHeaderWords) {
        return "SPIR-V: module is smaller than the 5-word header";
    }
    if (entry == nullptr || entry[0] == '\0') {
        return "SPIR-V: entry point name is empty";
    }
    if (ShaderEntryNameTooLong(entry)) {
        return "SPIR-V: entry point name is longer than 63 bytes";
    }
    if (words[0] != kSpirvMagic) {
        if (words[0] == 0x03022307u) {
            return "SPIR-V: module is byte-swapped (wrong endianness)";
        }
        return "SPIR-V: bad magic number";
    }
    const u32 major = (words[1] >> 16) & 0xffu;
    const u32 minor = (words[1] >> 8) & 0xffu;
    if ((words[1] & 0xff0000ffu) != 0 || major != 1 || minor > 6) {
        return "SPIR-V: unsupported version (1.0 to 1.6 are accepted)";
    }
    if (words[3] == 0) {
        return "SPIR-V: id bound is zero";
    }

    bool have_shader = false;
    bool have_kernel = false;
    bool have_memory_model = false;
    bool entry_name_found = false;
    bool entry_stage_ok = false;
    bool malformed_entry = false;

    usize pos = kHeaderWords;
    while (pos < word_count) {
        const u32 head = words[pos];
        const u32 opcode = head & 0xffffu;
        const usize length = head >> 16;
        if (length == 0) {
            return "SPIR-V: instruction with zero length";
        }
        if (length > word_count - pos) {
            return "SPIR-V: instruction runs past the end of the module (truncated)";
        }
        const u32* ops = words + pos + 1;
        const usize op_count = length - 1;
        if (opcode == kOpCapability && op_count >= 1) {
            if (ops[0] == kCapabilityShader) have_shader = true;
            if (ops[0] == kCapabilityKernel) have_kernel = true;
        } else if (opcode == kOpMemoryModel) {
            if (op_count < 2) {
                return "SPIR-V: malformed OpMemoryModel";
            }
            if (ops[0] != kAddressingLogical) {
                return "SPIR-V: only the Logical addressing model is supported";
            }
            if (ops[1] != kMemoryGlsl450 && ops[1] != kMemoryVulkan) {
                return "SPIR-V: only the GLSL450 and Vulkan memory models are supported";
            }
            have_memory_model = true;
        } else if (opcode == kOpEntryPoint) {
            if (op_count < 3) {
                malformed_entry = true;
            } else {
                // The name is at most 63 bytes plus the terminator: 16 words. Words after it are interface ids, which
                // must never be read as part of an unterminated name.
                const usize name_words = op_count - 2 < kMaxNameWords ? op_count - 2 : kMaxNameWords;
                const int match = PackedStringEquals(ops + 2, name_words, entry);
                if (match < 0) {
                    malformed_entry = true;
                } else if (match == 1) {
                    entry_name_found = true;
                    if (ModelMatches(ops[0], stage)) {
                        entry_stage_ok = true;
                    }
                }
            }
        }
        pos += length;
    }
    if (pos != word_count) {
        return "SPIR-V: the instruction stream does not end at the last word";
    }
    if (malformed_entry) {
        return "SPIR-V: malformed OpEntryPoint";
    }
    if (have_kernel || !have_shader) {
        return "SPIR-V: the module must declare capability Shader and not Kernel";
    }
    if (!have_memory_model) {
        return "SPIR-V: no OpMemoryModel";
    }
    if (!entry_name_found) {
        return "SPIR-V: no entry point with the requested name";
    }
    if (!entry_stage_ok) {
        return "SPIR-V: the entry point implements a different stage than the one declared";
    }
    return nullptr;
}

} // namespace engine_placeholder::rhi
