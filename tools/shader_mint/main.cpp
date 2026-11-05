// shader_mint: the offline shader "minter" (Phase 4 Day 71; folds in Phase 3's spv_embed). Given the SPIR-V that
// slangc wrote and the JSON from `slangc -reflection-json`, it writes two committed C++ include files:
//
//   <name>.spv.inc   `inline constexpr u32 <Base>Spv[]` and `<Base>SpvWordCount`: the SPIR-V words
//   <name>.refl.inc  `inline constexpr u8 <Base>Refl[]` and `<Base>ReflByteCount`: the reflection blob that
//                    renderer::ParseShaderReflection reads, plus `<Base><Block>Size` and
//                    `<Base><Block><Member>Offset` constants for static_asserts on the matching C++ structs
//
//   shader_mint --name kLitVert --source lit.vert.slang --compiler "slangc v2026.18.2" --spv in.spv
//               --reflection in.json --spv-out lit.vert.spv.inc --refl-out lit.vert.refl.inc
//
// Permuted shaders (Day 72) add `--permute`: the tool reads the `// permutation-options:` line from the source and the
// `.perm` manifest, runs slangc itself once per manifest key (`-DOPTION=1` per enabled option), mints each result the
// same way, dedupes identical modules and writes `<name>.perm.inc` next to the plain pair (the plain pair is the key
// with no option on):
//
//   shader_mint --permute --manifest lit.frag.perm --slangc /usr/bin/slangc --slang-arg ... --work-dir DIR ...
//               --perm-out lit.frag.perm.inc   (plus the arguments above except --spv/--reflection)
//
// It fails (non-zero exit, message on stderr) if the reflection cannot be built or disagrees with the SPIR-V. It is a
// dev-time tool (PLAN.md assumptions #18 and #19): the generated files are committed, so building the engine never
// needs it, slangc or the JSON library.

#include <spawn.h>
#include <sys/wait.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <string>
#include <vector>

#include "permutations.h"
#include "reflect_json.h"
#include "renderer/shader_reflection.h"
#include "types.h"

using namespace engine_placeholder;
using shader_mint::Appendf;

extern char** environ;

namespace {

int Usage() {
    std::fputs("usage: shader_mint --name IDENT --source FILE --compiler TEXT --spv FILE --reflection FILE "
               "--spv-out FILE --refl-out FILE\n"
               "       shader_mint --permute --name IDENT --source LABEL --source-path FILE --manifest FILE --compiler TEXT\n"
               "                   --slangc PATH [--slang-arg ARG]... [--spirv-val PATH] --work-dir DIR\n"
               "                   --spv-out FILE --refl-out FILE --perm-out FILE\n",
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

struct OutputFile {
    std::string path;
    std::string text;
};

// Writes every file to a temporary first and renames them only after all were written, so a failed write never leaves
// a truncated committed file or a new .spv.inc beside an old .refl.inc or .perm.inc.
bool WriteFiles(const std::vector<OutputFile>& files) {
    for (usize i = 0; i < files.size(); ++i) {
        if (!WriteTemp(files[i].path, files[i].text)) {
            for (usize j = 0; j < i; ++j) {
                std::remove((files[j].path + ".tmp").c_str());
            }
            return false;
        }
    }
    bool ok = true;
    for (const OutputFile& f : files) {
        ok = std::rename((f.path + ".tmp").c_str(), f.path.c_str()) == 0 && ok;
    }
    if (!ok) {
        for (const OutputFile& f : files) {
            std::remove((f.path + ".tmp").c_str());
        }
    }
    return ok;
}

// One minted module: the SPIR-V words and the serialized reflection that was cross-checked against them.
struct Minted {
    std::vector<u32> words;
    renderer::ShaderReflection reflection;
    std::vector<u8> blob;
};

// Reads slangc's SPIR-V and reflection JSON, cross-checks them and serializes the reflection (parsing it back as a
// final self-check). Prints the reason and returns false on any problem.
bool MintModule(const std::string& spv_path, const std::string& json_path, const char* source, Minted* out) {
    std::string spv_bytes;
    std::string json_text;
    if (!ReadAll(spv_path.c_str(), &spv_bytes)) {
        std::fprintf(stderr, "shader_mint: cannot read %s\n", spv_path.c_str());
        return false;
    }
    if (!ReadAll(json_path.c_str(), &json_text)) {
        std::fprintf(stderr, "shader_mint: cannot read %s\n", json_path.c_str());
        return false;
    }
    if (spv_bytes.empty() || spv_bytes.size() % 4 != 0) {
        std::fprintf(stderr, "shader_mint: %s is not a whole number of 32-bit words (%zu bytes)\n", spv_path.c_str(), spv_bytes.size());
        return false;
    }
    const usize words = spv_bytes.size() / 4;
    const auto* raw = reinterpret_cast<const unsigned char*>(spv_bytes.data());
    out->words.assign(words, 0);
    for (usize i = 0; i < words; ++i) {
        out->words[i] = static_cast<u32>(raw[i * 4]) | (static_cast<u32>(raw[i * 4 + 1]) << 8) |
                        (static_cast<u32>(raw[i * 4 + 2]) << 16) | (static_cast<u32>(raw[i * 4 + 3]) << 24);
    }
    if (out->words[0] != 0x07230203u) {
        std::fprintf(stderr, "shader_mint: %s does not start with the SPIR-V magic number\n", spv_path.c_str());
        return false;
    }

    // Reflection: JSON -> struct, checked against the SPIR-V, serialized, and parsed back as a final self-check.
    std::string error;
    if (!shader_mint::ReflectionFromJson(json_text, &out->reflection, &error)) {
        std::fprintf(stderr, "shader_mint: %s: %s\n", source, error.c_str());
        return false;
    }
    if (!shader_mint::CrossCheckSpirv(out->reflection, out->words.data(), words, &error)) {
        std::fprintf(stderr, "shader_mint: %s: %s\n", source, error.c_str());
        return false;
    }
    const usize blob_size = renderer::SerializeShaderReflection(out->reflection, nullptr, 0);
    out->blob.assign(blob_size, 0);
    if (blob_size == 0 || renderer::SerializeShaderReflection(out->reflection, out->blob.data(), blob_size) != blob_size) {
        std::fprintf(stderr, "shader_mint: %s: the reflection could not be serialized\n", source);
        return false;
    }
    renderer::ShaderReflection round_trip;
    if (const char* problem = renderer::ParseShaderReflection(out->blob.data(), blob_size, &round_trip)) {
        std::fprintf(stderr, "shader_mint: %s: the serialized reflection does not parse back: %s\n", source, problem);
        return false;
    }
    return true;
}

// Runs `args[0]` (looked up on PATH if it has no slash) with the given arguments and waits. Returns true if it exited
// with status 0; otherwise prints how it failed.
bool RunProcess(const std::vector<std::string>& args) {
    std::vector<char*> argv;
    for (const std::string& a : args) {
        argv.push_back(const_cast<char*>(a.c_str()));
    }
    argv.push_back(nullptr);
    pid_t pid = 0;
    if (posix_spawnp(&pid, argv[0], nullptr, nullptr, argv.data(), environ) != 0) {
        std::fprintf(stderr, "shader_mint: cannot run %s\n", argv[0]);
        return false;
    }
    int status = 0;
    pid_t waited;
    do {
        waited = waitpid(pid, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited < 0) {
        std::fprintf(stderr, "shader_mint: waiting for %s failed\n", argv[0]);
        return false;
    }
    if (WIFSIGNALED(status)) {
        std::fprintf(stderr, "shader_mint: %s was killed by signal %d\n", argv[0], WTERMSIG(status));
        return false;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        std::fprintf(stderr, "shader_mint: %s exited with status %d\n", argv[0], WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        return false;
    }
    return true;
}

std::string SpvText(const std::string& name, const char* source, const char* compiler, const std::vector<u32>& words) {
    std::string text;
    Appendf(&text, "// Generated by tools/shader_mint from %s. Do not edit; run the `shaders` target to regenerate.\n", source);
    Appendf(&text, "// compiler: %s\n", compiler);
    text += "#pragma once\n\n#include \"types.h\"\n\nnamespace engine_placeholder::shaders {\n\n";
    shader_mint::AppendWordArray(&text, name + "Spv", words);
    text += "\n} // namespace engine_placeholder::shaders\n";
    return text;
}

// Returns false (after printing why) if two generated constant names collide.
bool ReflText(const std::string& base, const char* source, const char* compiler, const Minted& m, std::string* text) {
    const renderer::ShaderReflection& reflection = m.reflection;
    Appendf(text, "// Generated by tools/shader_mint from %s. Do not edit; run the `shaders` target to regenerate.\n", source);
    Appendf(text, "// compiler: %s\n", compiler);
    *text += "#pragma once\n\n#include \"types.h\"\n\nnamespace engine_placeholder::shaders {\n\n";
    shader_mint::AppendByteArray(text, base + "Refl", m.blob);
    if (reflection.block_count != 0) {
        *text += "\n// Block sizes and member offsets, for static_asserts on the C++ structs copied into these blocks.\n";
    }
    std::vector<std::string> constants; // every generated name, to reject collisions (a member called `size`, `a_b` vs `aB`)
    auto emit = [&](const std::string& constant, u32 value) {
        for (const std::string& existing : constants) {
            if (existing == constant) {
                std::fprintf(stderr, "shader_mint: %s: the generated constant name %s would be defined twice; rename a block or member\n",
                             source, constant.c_str());
                return false;
            }
        }
        constants.push_back(constant);
        Appendf(text, "inline constexpr u32 %s = %u;\n", constant.c_str(), value);
        return true;
    };
    for (u32 b = 0; b < reflection.block_count; ++b) {
        const renderer::ReflectedBlock& block = reflection.blocks[b];
        const std::string block_name = base + Pascal(block.name);
        if (!emit(block_name + "Size", block.size)) {
            return false;
        }
        for (u32 m2 = 0; m2 < block.member_count; ++m2) {
            if (!emit(block_name + Pascal(block.members[m2].name) + "Offset", block.members[m2].offset)) {
                return false;
            }
        }
    }
    *text += "\n} // namespace engine_placeholder::shaders\n";
    return true;
}

} // namespace

int main(int argc, char** argv) {
    const char* name = nullptr;
    const char* source = nullptr;
    const char* source_path = nullptr;
    const char* compiler = nullptr;
    const char* spv_path = nullptr;
    const char* json_path = nullptr;
    const char* spv_out = nullptr;
    const char* refl_out = nullptr;
    const char* perm_out = nullptr;
    const char* manifest_path = nullptr;
    const char* slangc = nullptr;
    const char* spirv_val = nullptr;
    const char* work_dir = nullptr;
    std::vector<std::string> slang_args;
    bool permute = false;
    for (int i = 1; i < argc; ++i) {
        const bool has_value = i + 1 < argc;
        if (std::strcmp(argv[i], "--permute") == 0) {
            permute = true;
        } else if (std::strcmp(argv[i], "--name") == 0 && has_value) {
            name = argv[++i];
        } else if (std::strcmp(argv[i], "--source") == 0 && has_value) {
            source = argv[++i];
        } else if (std::strcmp(argv[i], "--source-path") == 0 && has_value) {
            source_path = argv[++i];
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
        } else if (std::strcmp(argv[i], "--perm-out") == 0 && has_value) {
            perm_out = argv[++i];
        } else if (std::strcmp(argv[i], "--manifest") == 0 && has_value) {
            manifest_path = argv[++i];
        } else if (std::strcmp(argv[i], "--slangc") == 0 && has_value) {
            slangc = argv[++i];
        } else if (std::strcmp(argv[i], "--slang-arg") == 0 && has_value) {
            slang_args.push_back(argv[++i]);
        } else if (std::strcmp(argv[i], "--spirv-val") == 0 && has_value) {
            spirv_val = argv[++i];
        } else if (std::strcmp(argv[i], "--work-dir") == 0 && has_value) {
            work_dir = argv[++i];
        } else {
            return Usage();
        }
    }
    if (!IsIdentifier(name) || source == nullptr || compiler == nullptr || spv_out == nullptr || refl_out == nullptr) {
        return Usage();
    }
    if (permute ? (source_path == nullptr || manifest_path == nullptr || slangc == nullptr || work_dir == nullptr ||
                   perm_out == nullptr || spv_path != nullptr || json_path != nullptr)
                : (spv_path == nullptr || json_path == nullptr || perm_out != nullptr || manifest_path != nullptr)) {
        return Usage();
    }
    const std::string base = name;

    Minted plain;
    std::vector<u32> keys;
    std::vector<std::string> options;
    std::vector<Minted> minted_keys; // permute mode: one per manifest key, in key order
    if (!permute) {
        if (!MintModule(spv_path, json_path, source, &plain)) {
            return 1;
        }
    } else {
        std::string source_text;
        std::string manifest_text;
        std::string error;
        if (!ReadAll(source_path, &source_text) || !ReadAll(manifest_path, &manifest_text)) {
            std::fprintf(stderr, "shader_mint: cannot read %s or %s\n", source_path, manifest_path);
            return 1;
        }
        if (!shader_mint::ParseOptionDeclaration(source_text, &options, &error)) {
            std::fprintf(stderr, "shader_mint: %s: %s\n", source, error.c_str());
            return 1;
        }
        if (!shader_mint::ParseManifest(manifest_text, options, &keys, &error)) {
            std::fprintf(stderr, "shader_mint: %s: %s\n", manifest_path, error.c_str());
            return 1;
        }
        for (const u32 key : keys) {
            char tag[16];
            std::snprintf(tag, sizeof(tag), "%x", key);
            const std::string stem = std::string(work_dir) + "/" + name + "." + tag;
            std::vector<std::string> command = {slangc, source_path};
            command.insert(command.end(), slang_args.begin(), slang_args.end());
            for (usize o = 0; o < options.size(); ++o) {
                if ((key >> o) & 1u) {
                    command.push_back("-D" + options[o] + "=1");
                }
            }
            command.insert(command.end(), {"-o", stem + ".spv", "-reflection-json", stem + ".json"});
            if (!RunProcess(command)) {
                std::fprintf(stderr, "shader_mint: %s: slangc failed for key %s\n", source, shader_mint::DescribeKey(options, key).c_str());
                return 1;
            }
            if (spirv_val != nullptr && !RunProcess({spirv_val, "--target-env", "vulkan1.3", stem + ".spv"})) {
                std::fprintf(stderr, "shader_mint: %s: spirv-val rejected key %s\n", source, shader_mint::DescribeKey(options, key).c_str());
                return 1;
            }
            Minted m;
            if (!MintModule(stem + ".spv", stem + ".json", source, &m)) {
                std::fprintf(stderr, "shader_mint: (while minting key %s)\n", shader_mint::DescribeKey(options, key).c_str());
                return 1;
            }
            minted_keys.push_back(static_cast<Minted&&>(m));
        }
        plain = minted_keys[0]; // keys are sorted and the manifest requires key 0, so this is the base module
    }

    std::string spv_text = SpvText(base, source, compiler, plain.words);
    std::string refl_text;
    if (!ReflText(base, source, compiler, plain, &refl_text)) {
        return 1;
    }
    if (!permute) {
        if (!WriteFiles({{spv_out, spv_text}, {refl_out, refl_text}})) {
            std::fprintf(stderr, "shader_mint: failed writing %s or %s\n", spv_out, refl_out);
            return 1;
        }
        return 0;
    }

    // Dedupe: modules with identical words and reflection share one table entry.
    std::vector<shader_mint::BuiltModule> per_key;
    for (const Minted& m : minted_keys) {
        per_key.push_back({m.words, m.blob});
    }
    std::vector<shader_mint::BuiltModule> modules;
    std::vector<u32> key_module;
    shader_mint::DedupeModules(per_key, &modules, &key_module);
    const std::string shader_file = std::string(source).substr(0, std::string(source).rfind(".slang"));
    const std::string perm_text =
        shader_mint::EmitPermutationInc(base, shader_file, source, compiler, options, keys, key_module, modules);
    if (!WriteFiles({{spv_out, spv_text}, {refl_out, refl_text}, {perm_out, perm_text}})) {
        std::fprintf(stderr, "shader_mint: failed writing %s, %s or %s\n", spv_out, refl_out, perm_out);
        return 1;
    }
    return 0;
}
