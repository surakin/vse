// shader_cook: the offline shader "cooker" (Phase 4 Day 71; folds in Phase 3's spv_embed). Given the SPIR-V that
// slangc wrote and the JSON from `slangc -reflection-json`, it writes two committed C++ include files:
//
//   <name>.spv.inc   `inline constexpr u32 <Base>Spv[]` and `<Base>SpvWordCount`: the SPIR-V words
//   <name>.refl.inc  `inline constexpr u8 <Base>Refl[]` and `<Base>ReflByteCount`: the reflection blob that
//                    renderer::ParseShaderReflection reads, plus `<Base><Block>Size` and
//                    `<Base><Block><Member>Offset` constants for static_asserts on the matching C++ structs
//
//   shader_cook --name kLitVert --source lit.vert.slang --compiler "slangc v2026.18.2" --spv in.spv
//               --reflection in.json --spv-out lit.vert.spv.inc --refl-out lit.vert.refl.inc
//
// It fails (non-zero exit, message on stderr) if the reflection cannot be built or disagrees with the SPIR-V. It is a
// dev-time tool (PLAN.md assumptions #18 and #19): the generated files are committed, so building the engine never
// needs it, slangc or the JSON library.

#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <string>
#include <vector>

#include "reflect_json.h"
#include "renderer/shader_reflection.h"
#include "types.h"

using namespace engine_placeholder;

namespace {

int Usage() {
    std::fputs("usage: shader_cook --name IDENT --source FILE --compiler TEXT --spv FILE --reflection FILE "
               "--spv-out FILE --refl-out FILE\n",
               stderr);
    return 2;
}

bool ReadAll(const char* path, std::string* out) {
    std::FILE* f = std::fopen(path, "rb");
    if (f == nullptr) {
        return false;
    }
    char buffer[4096];
    out->clear();
    for (;;) {
        const usize got = std::fread(buffer, 1, sizeof(buffer), f);
        out->append(buffer, got);
        if (got < sizeof(buffer)) {
            break;
        }
    }
    const bool ok = std::ferror(f) == 0;
    std::fclose(f);
    return ok;
}

bool IsIdentifier(const char* s) {
    if (s == nullptr || s[0] == '\0' || (s[0] >= '0' && s[0] <= '9')) {
        return false;
    }
    for (; *s != '\0'; ++s) {
        const bool ok = (*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '_';
        if (!ok) {
            return false;
        }
    }
    return true;
}

// "normal_matrix" -> "NormalMatrix", "Params" -> "Params".
std::string Pascal(const char* snake) {
    std::string out;
    bool upper = true;
    for (; *snake != '\0'; ++snake) {
        if (*snake == '_') {
            upper = true;
        } else {
            out += upper && *snake >= 'a' && *snake <= 'z' ? static_cast<char>(*snake - 'a' + 'A') : *snake;
            upper = false;
        }
    }
    return out;
}

// Writes `text` to `path + ".tmp"`. Returns false (leaving no temporary file) on failure.
bool WriteTemp(const std::string& path, const std::string& text) {
    const std::string temp = path + ".tmp";
    std::FILE* out = std::fopen(temp.c_str(), "wb");
    if (out == nullptr) {
        return false;
    }
    const bool wrote = std::fwrite(text.data(), 1, text.size(), out) == text.size() && std::ferror(out) == 0;
    const bool closed = std::fclose(out) == 0;
    if (!wrote || !closed) {
        std::remove(temp.c_str());
        return false;
    }
    return true;
}

// Writes both files to temporaries first and renames them only after both were written, so a failed write never
// leaves a truncated committed file or a new .spv.inc beside an old .refl.inc.
bool WritePair(const std::string& path_a, const std::string& text_a, const std::string& path_b, const std::string& text_b) {
    if (!WriteTemp(path_a, text_a)) {
        return false;
    }
    if (!WriteTemp(path_b, text_b)) {
        std::remove((path_a + ".tmp").c_str());
        return false;
    }
    const bool renamed_a = std::rename((path_a + ".tmp").c_str(), path_a.c_str()) == 0;
    const bool renamed_b = std::rename((path_b + ".tmp").c_str(), path_b.c_str()) == 0;
    if (!renamed_a || !renamed_b) {
        std::remove((path_a + ".tmp").c_str());
        std::remove((path_b + ".tmp").c_str());
        return false;
    }
    return true;
}

void Appendf(std::string* out, const char* format, ...) __attribute__((format(printf, 2, 3)));
void Appendf(std::string* out, const char* format, ...) {
    va_list args;
    va_start(args, format);
    va_list again;
    va_copy(again, args);
    const int needed = std::vsnprintf(nullptr, 0, format, args);
    va_end(args);
    if (needed > 0) {
        const usize old_size = out->size();
        out->resize(old_size + static_cast<usize>(needed) + 1);
        std::vsnprintf(&(*out)[old_size], static_cast<usize>(needed) + 1, format, again);
        out->resize(old_size + static_cast<usize>(needed));
    }
    va_end(again);
}

} // namespace

int main(int argc, char** argv) {
    const char* name = nullptr;
    const char* source = nullptr;
    const char* compiler = nullptr;
    const char* spv_path = nullptr;
    const char* json_path = nullptr;
    const char* spv_out = nullptr;
    const char* refl_out = nullptr;
    for (int i = 1; i < argc; ++i) {
        const bool has_value = i + 1 < argc;
        if (std::strcmp(argv[i], "--name") == 0 && has_value) {
            name = argv[++i];
        } else if (std::strcmp(argv[i], "--source") == 0 && has_value) {
            source = argv[++i];
        } else if (std::strcmp(argv[i], "--compiler") == 0 && has_value) {
            compiler = argv[++i];
        } else if (std::strcmp(argv[i], "--spv") == 0 && has_value) {
            spv_path = argv[++i];
        } else if (std::strcmp(argv[i], "--reflection") == 0 && has_value) {
            json_path = argv[++i];
        } else if (std::strcmp(argv[i], "--spv-out") == 0 && has_value) {
            spv_out = argv[++i];
        } else if (std::strcmp(argv[i], "--refl-out") == 0 && has_value) {
            refl_out = argv[++i];
        } else {
            return Usage();
        }
    }
    if (!IsIdentifier(name) || source == nullptr || compiler == nullptr || spv_path == nullptr || json_path == nullptr ||
        spv_out == nullptr || refl_out == nullptr) {
        return Usage();
    }

    std::string spv_bytes;
    std::string json_text;
    if (!ReadAll(spv_path, &spv_bytes)) {
        std::fprintf(stderr, "shader_cook: cannot read %s\n", spv_path);
        return 1;
    }
    if (!ReadAll(json_path, &json_text)) {
        std::fprintf(stderr, "shader_cook: cannot read %s\n", json_path);
        return 1;
    }
    if (spv_bytes.empty() || spv_bytes.size() % 4 != 0) {
        std::fprintf(stderr, "shader_cook: %s is not a whole number of 32-bit words (%zu bytes)\n", spv_path, spv_bytes.size());
        return 1;
    }
    const usize words = spv_bytes.size() / 4;
    const auto* raw = reinterpret_cast<const unsigned char*>(spv_bytes.data());
    auto word_at = [&](usize i) {
        return static_cast<u32>(raw[i * 4]) | (static_cast<u32>(raw[i * 4 + 1]) << 8) | (static_cast<u32>(raw[i * 4 + 2]) << 16) |
               (static_cast<u32>(raw[i * 4 + 3]) << 24);
    };
    if (word_at(0) != 0x07230203u) {
        std::fprintf(stderr, "shader_cook: %s does not start with the SPIR-V magic number\n", spv_path);
        return 1;
    }
    std::vector<u32> spv_words(words);
    for (usize i = 0; i < words; ++i) {
        spv_words[i] = word_at(i);
    }

    // Reflection: JSON -> struct, checked against the SPIR-V, serialized, and parsed back as a final self-check.
    renderer::ShaderReflection reflection;
    std::string error;
    if (!shader_cook::ReflectionFromJson(json_text, &reflection, &error)) {
        std::fprintf(stderr, "shader_cook: %s: %s\n", source, error.c_str());
        return 1;
    }
    if (!shader_cook::CrossCheckSpirv(reflection, spv_words.data(), words, &error)) {
        std::fprintf(stderr, "shader_cook: %s: %s\n", source, error.c_str());
        return 1;
    }
    const usize blob_size = renderer::SerializeShaderReflection(reflection, nullptr, 0);
    std::vector<u8> blob(blob_size);
    if (blob_size == 0 || renderer::SerializeShaderReflection(reflection, blob.data(), blob.size()) != blob_size) {
        std::fprintf(stderr, "shader_cook: %s: the reflection could not be serialized\n", source);
        return 1;
    }
    renderer::ShaderReflection round_trip;
    if (const char* problem = renderer::ParseShaderReflection(blob.data(), blob.size(), &round_trip)) {
        std::fprintf(stderr, "shader_cook: %s: the serialized reflection does not parse back: %s\n", source, problem);
        return 1;
    }

    const std::string base = name;
    std::string spv_text;
    Appendf(&spv_text, "// Generated by tools/shader_cook from %s. Do not edit; run the `shaders` target to regenerate.\n", source);
    Appendf(&spv_text, "// compiler: %s\n", compiler);
    spv_text += "#pragma once\n\n#include \"types.h\"\n\nnamespace engine_placeholder::shaders {\n\n";
    Appendf(&spv_text, "inline constexpr usize %sSpvWordCount = %zu;\n\n", name, words);
    Appendf(&spv_text, "inline constexpr u32 %sSpv[%sSpvWordCount] = {", name, name);
    for (usize i = 0; i < words; ++i) {
        Appendf(&spv_text, "%s0x%08x,", i % 6 == 0 ? "\n    " : " ", word_at(i));
    }
    spv_text += "\n};\n\n} // namespace engine_placeholder::shaders\n";

    std::string refl_text;
    Appendf(&refl_text, "// Generated by tools/shader_cook from %s. Do not edit; run the `shaders` target to regenerate.\n", source);
    Appendf(&refl_text, "// compiler: %s\n", compiler);
    refl_text += "#pragma once\n\n#include \"types.h\"\n\nnamespace engine_placeholder::shaders {\n\n";
    Appendf(&refl_text, "inline constexpr usize %sReflByteCount = %zu;\n\n", name, blob_size);
    Appendf(&refl_text, "inline constexpr u8 %sRefl[%sReflByteCount] = {", name, name);
    for (usize i = 0; i < blob_size; ++i) {
        Appendf(&refl_text, "%s%u,", i % 16 == 0 ? "\n    " : " ", static_cast<unsigned>(blob[i]));
    }
    refl_text += "\n};\n";
    if (reflection.block_count != 0) {
        refl_text += "\n// Block sizes and member offsets, for static_asserts on the C++ structs copied into these blocks.\n";
    }
    std::vector<std::string> constants; // every generated name, to reject collisions (a member called `size`, `a_b` vs `aB`)
    auto emit = [&](const std::string& constant, u32 value) {
        for (const std::string& existing : constants) {
            if (existing == constant) {
                std::fprintf(stderr, "shader_cook: %s: the generated constant name %s would be defined twice; rename a block or member\n",
                             source, constant.c_str());
                return false;
            }
        }
        constants.push_back(constant);
        Appendf(&refl_text, "inline constexpr u32 %s = %u;\n", constant.c_str(), value);
        return true;
    };
    for (u32 b = 0; b < reflection.block_count; ++b) {
        const renderer::ReflectedBlock& block = reflection.blocks[b];
        const std::string block_name = base + Pascal(block.name);
        if (!emit(block_name + "Size", block.size)) {
            return 1;
        }
        for (u32 m = 0; m < block.member_count; ++m) {
            if (!emit(block_name + Pascal(block.members[m].name) + "Offset", block.members[m].offset)) {
                return 1;
            }
        }
    }
    refl_text += "\n} // namespace engine_placeholder::shaders\n";

    if (!WritePair(spv_out, spv_text, refl_out, refl_text)) {
        std::fprintf(stderr, "shader_cook: failed writing %s or %s\n", spv_out, refl_out);
        return 1;
    }
    return 0;
}
