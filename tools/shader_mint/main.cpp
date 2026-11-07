// shader_mint: the offline shader "minter" (Phase 5 Day 71; folds in Phase 4's spv_embed). Given the SPIR-V that
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

#include "containers/span.h"
#include "io/file.h"
#include "io/file_system.h"
#include "permutations.h"
#include "reflect_json.h"
#include "renderer/shader_reflection.h"
#include "types.h"

using namespace engine_placeholder;
using shader_mint::Appendf;
using shader_mint::MakeText;
using shader_mint::Text;

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

// Reads a whole file into `out` (the bytes, not validated).
bool ReadAll(const char* path, Vector<byte>* out) {
    io::Path file;
    return io::Path::Parse(StringView(path), io::NativePathStyle(), &file) && io::ReadFile(file, out).Ok();
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
Text Pascal(const char* snake) {
    Text out = MakeText();
    bool upper = true;
    for (; *snake != '\0'; ++snake) {
        if (*snake == '_') {
            upper = true;
        } else {
            out.Append(upper && *snake >= 'a' && *snake <= 'z' ? static_cast<char>(*snake - 'a' + 'A') : *snake);
            upper = false;
        }
    }
    return out;
}

// `prefix` followed by each part, as one text.
Text Join(StringView a, StringView b = StringView(), StringView c = StringView(), StringView d = StringView()) {
    Text out = MakeText(a);
    out.Append(b);
    out.Append(c);
    out.Append(d);
    return out;
}

struct OutputFile {
    const char* path;
    const Text* text;
};

// Writes every file to a temporary first and renames them only after all were written, so a failed write never leaves
// a truncated committed file or a new .spv.inc beside an old .refl.inc or .perm.inc.
bool WriteFiles(const OutputFile* files, usize count) {
    Vector<io::Path> finals;
    Vector<io::Path> temps;
    finals.Resize(count);
    temps.Resize(count);
    auto remove_temps = [&](usize n) {
        for (usize j = 0; j < n; ++j) {
            if (!temps[j].IsEmpty()) io::Remove(temps[j]); // only temporaries that were named
        }
    };
    for (usize i = 0; i < count; ++i) {
        const Text temp_name = Join(files[i].path, ".tmp");
        if (!io::Path::Parse(StringView(files[i].path), io::NativePathStyle(), &finals[i]) ||
            !io::Path::Parse(temp_name.View(), io::NativePathStyle(), &temps[i]) ||
            io::WriteFile(temps[i], AsByteSpan(files[i].text->View())).Failed()) {
            remove_temps(i + 1);
            return false;
        }
    }
    bool ok = true;
    for (usize i = 0; i < count; ++i) {
        ok = io::Rename(temps[i], finals[i]).Ok() && ok;
    }
    if (!ok) {
        remove_temps(count);
    }
    return ok;
}

// One minted module: the SPIR-V words and the serialized reflection that was cross-checked against them.
struct Minted {
    Vector<u32> words;
    renderer::ShaderReflection reflection;
    Vector<u8> blob;
};

// Reads slangc's SPIR-V and reflection JSON, cross-checks them and serializes the reflection (parsing it back as a
// final self-check). Prints the reason and returns false on any problem.
bool MintModule(const char* spv_path, const char* json_path, const char* source, Minted* out) {
    Vector<byte> spv_bytes;
    Vector<byte> json_bytes;
    if (!ReadAll(spv_path, &spv_bytes)) {
        std::fprintf(stderr, "shader_mint: cannot read %s\n", spv_path);
        return false;
    }
    if (!ReadAll(json_path, &json_bytes)) {
        std::fprintf(stderr, "shader_mint: cannot read %s\n", json_path);
        return false;
    }
    if (spv_bytes.Empty() || spv_bytes.Size() % 4 != 0) {
        std::fprintf(stderr, "shader_mint: %s is not a whole number of 32-bit words (%zu bytes)\n", spv_path, spv_bytes.Size());
        return false;
    }
    const usize words = spv_bytes.Size() / 4;
    const byte* raw = spv_bytes.Data();
    out->words.Resize(words);
    for (usize i = 0; i < words; ++i) {
        out->words[i] = static_cast<u32>(raw[i * 4]) | (static_cast<u32>(raw[i * 4 + 1]) << 8) |
                        (static_cast<u32>(raw[i * 4 + 2]) << 16) | (static_cast<u32>(raw[i * 4 + 3]) << 24);
    }
    if (out->words[0] != 0x07230203u) {
        std::fprintf(stderr, "shader_mint: %s does not start with the SPIR-V magic number\n", spv_path);
        return false;
    }

    // Reflection: JSON -> struct, checked against the SPIR-V, serialized, and parsed back as a final self-check.
    Text error = MakeText();
    if (!shader_mint::ReflectionFromJson(ToStringView(json_bytes), &out->reflection, &error)) {
        std::fprintf(stderr, "shader_mint: %s: %s\n", source, error.CStr());
        return false;
    }
    if (!shader_mint::CrossCheckSpirv(out->reflection, out->words.Data(), words, &error)) {
        std::fprintf(stderr, "shader_mint: %s: %s\n", source, error.CStr());
        return false;
    }
    const usize blob_size = renderer::SerializeShaderReflection(out->reflection, nullptr, 0);
    out->blob.Resize(blob_size);
    if (blob_size == 0 || renderer::SerializeShaderReflection(out->reflection, out->blob.Data(), blob_size) != blob_size) {
        std::fprintf(stderr, "shader_mint: %s: the reflection could not be serialized\n", source);
        return false;
    }
    renderer::ShaderReflection round_trip;
    if (const char* problem = renderer::ParseShaderReflection(out->blob.Data(), blob_size, &round_trip)) {
        std::fprintf(stderr, "shader_mint: %s: the serialized reflection does not parse back: %s\n", source, problem);
        return false;
    }
    return true;
}

// Runs `args[0]` (looked up on PATH if it has no slash) with the given arguments and waits. Returns true if it exited
// with status 0; otherwise prints how it failed.
bool RunProcess(const Vector<Text>& args) {
    Vector<char*> argv;
    for (usize i = 0; i < args.Size(); ++i) {
        argv.Push(const_cast<char*>(args[i].CStr()));
    }
    argv.Push(nullptr);
    pid_t pid = 0;
    if (posix_spawnp(&pid, argv[0], nullptr, nullptr, argv.Data(), environ) != 0) {
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

Text SpvText(StringView name, const char* source, const char* compiler, const Vector<u32>& words) {
    Text text = MakeText();
    Appendf(&text, "// Generated by tools/shader_mint from %s. Do not edit; run the `shaders` target to regenerate.\n", source);
    Appendf(&text, "// compiler: %s\n", compiler);
    text.Append("#pragma once\n\n#include \"types.h\"\n\nnamespace engine_placeholder::shaders {\n\n");
    shader_mint::AppendWordArray(&text, Join(name, "Spv").View(), words);
    text.Append("\n} // namespace engine_placeholder::shaders\n");
    return text;
}

// Returns false (after printing why) if two generated constant names collide.
bool ReflText(StringView base, const char* source, const char* compiler, const Minted& m, Text* text) {
    const renderer::ShaderReflection& reflection = m.reflection;
    Appendf(text, "// Generated by tools/shader_mint from %s. Do not edit; run the `shaders` target to regenerate.\n", source);
    Appendf(text, "// compiler: %s\n", compiler);
    text->Append("#pragma once\n\n#include \"types.h\"\n\nnamespace engine_placeholder::shaders {\n\n");
    shader_mint::AppendByteArray(text, Join(base, "Refl").View(), m.blob);
    if (reflection.block_count != 0) {
        text->Append("\n// Block sizes and member offsets, for static_asserts on the C++ structs copied into these blocks.\n");
    }
    Vector<Text> constants; // every generated name, to reject collisions (a member called `size`, `a_b` vs `aB`)
    auto emit = [&](const Text& constant, u32 value) {
        for (usize i = 0; i < constants.Size(); ++i) {
            if (constants[i].View() == constant.View()) {
                std::fprintf(stderr, "shader_mint: %s: the generated constant name %s would be defined twice; rename a block or member\n",
                             source, constant.CStr());
                return false;
            }
        }
        constants.Push(MakeText(constant.View()));
        Appendf(text, "inline constexpr u32 %s = %u;\n", constant.CStr(), value);
        return true;
    };
    for (u32 b = 0; b < reflection.block_count; ++b) {
        const renderer::ReflectedBlock& block = reflection.blocks[b];
        const Text block_name = Join(base, Pascal(block.name).View());
        if (!emit(Join(block_name.View(), "Size"), block.size)) {
            return false;
        }
        for (u32 m2 = 0; m2 < block.member_count; ++m2) {
            if (!emit(Join(block_name.View(), Pascal(block.members[m2].name).View(), "Offset"), block.members[m2].offset)) {
                return false;
            }
        }
    }
    text->Append("\n} // namespace engine_placeholder::shaders\n");
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
    Vector<Text> slang_args;
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
            slang_args.Push(MakeText(StringView(argv[++i])));
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
    const Text base = MakeText(StringView(name));

    Minted plain;
    Vector<u32> keys;
    Vector<Text> options;
    Vector<Minted> minted_keys; // permute mode: one per manifest key, in key order
    if (!permute) {
        if (!MintModule(spv_path, json_path, source, &plain)) {
            return 1;
        }
    } else {
        Vector<byte> source_bytes;
        Vector<byte> manifest_bytes;
        Text error = MakeText();
        if (!ReadAll(source_path, &source_bytes) || !ReadAll(manifest_path, &manifest_bytes)) {
            std::fprintf(stderr, "shader_mint: cannot read %s or %s\n", source_path, manifest_path);
            return 1;
        }
        if (!shader_mint::ParseOptionDeclaration(ToStringView(source_bytes), &options, &error)) {
            std::fprintf(stderr, "shader_mint: %s: %s\n", source, error.CStr());
            return 1;
        }
        if (!shader_mint::ParseManifest(ToStringView(manifest_bytes), options, &keys, &error)) {
            std::fprintf(stderr, "shader_mint: %s: %s\n", manifest_path, error.CStr());
            return 1;
        }
        for (usize k = 0; k < keys.Size(); ++k) {
            const u32 key = keys[k];
            Text tag = MakeText();
            Appendf(&tag, "%x", key);
            const Text stem = Join(StringView(work_dir), "/", StringView(name), Join(".", tag.View()).View());
            Vector<Text> command;
            command.Push(MakeText(StringView(slangc)));
            command.Push(MakeText(StringView(source_path)));
            for (usize s = 0; s < slang_args.Size(); ++s) {
                command.Push(MakeText(slang_args[s].View()));
            }
            for (usize o = 0; o < options.Size(); ++o) {
                if ((key >> o) & 1u) {
                    command.Push(Join("-D", options[o].View(), "=1"));
                }
            }
            const Text spv_file = Join(stem.View(), ".spv");
            const Text json_file = Join(stem.View(), ".json");
            command.Push(MakeText("-o"));
            command.Push(MakeText(spv_file.View()));
            command.Push(MakeText("-reflection-json"));
            command.Push(MakeText(json_file.View()));
            if (!RunProcess(command)) {
                std::fprintf(stderr, "shader_mint: %s: slangc failed for key %s\n", source, shader_mint::DescribeKey(options, key).CStr());
                return 1;
            }
            if (spirv_val != nullptr) {
                Vector<Text> validate;
                validate.Push(MakeText(StringView(spirv_val)));
                validate.Push(MakeText("--target-env"));
                validate.Push(MakeText("vulkan1.3"));
                validate.Push(MakeText(spv_file.View()));
                if (!RunProcess(validate)) {
                    std::fprintf(stderr, "shader_mint: %s: spirv-val rejected key %s\n", source, shader_mint::DescribeKey(options, key).CStr());
                    return 1;
                }
            }
            Minted m;
            if (!MintModule(spv_file.CStr(), json_file.CStr(), source, &m)) {
                std::fprintf(stderr, "shader_mint: (while minting key %s)\n", shader_mint::DescribeKey(options, key).CStr());
                return 1;
            }
            minted_keys.Push(static_cast<Minted&&>(m));
        }
        // keys are sorted and the manifest requires key 0, so this is the base module
        Assign(plain.words, Span<const u32>(minted_keys[0].words));
        Assign(plain.blob, Span<const u8>(minted_keys[0].blob));
        plain.reflection = minted_keys[0].reflection;
    }

    const Text spv_text = SpvText(base.View(), source, compiler, plain.words);
    Text refl_text = MakeText();
    if (!ReflText(base.View(), source, compiler, plain, &refl_text)) {
        return 1;
    }
    if (!permute) {
        const OutputFile files[] = {{spv_out, &spv_text}, {refl_out, &refl_text}};
        if (!WriteFiles(files, 2)) {
            std::fprintf(stderr, "shader_mint: failed writing %s or %s\n", spv_out, refl_out);
            return 1;
        }
        return 0;
    }

    // Dedupe: modules with identical words and reflection share one table entry.
    Vector<shader_mint::BuiltModule> per_key;
    for (usize i = 0; i < minted_keys.Size(); ++i) {
        shader_mint::BuiltModule built;
        built.words = static_cast<Vector<u32>&&>(minted_keys[i].words); // minted_keys is not used again, except [0], copied into `plain` above
        built.reflection = static_cast<Vector<u8>&&>(minted_keys[i].blob);
        per_key.Push(static_cast<shader_mint::BuiltModule&&>(built));
    }
    Vector<shader_mint::BuiltModule> modules;
    Vector<u32> key_module;
    shader_mint::DedupeModules(per_key, &modules, &key_module);
    const StringView label(source);
    const usize slang_at = label.EndsWith(".slang") ? label.Length() - 6 : label.Length(); // drop only a trailing ".slang"
    const Text shader_file = MakeText(label.Substring(0, slang_at));
    const Text perm_text = shader_mint::EmitPermutationInc(base.View(), shader_file.View(), label, StringView(compiler), options, keys, key_module, modules);
    const OutputFile files[] = {{spv_out, &spv_text}, {refl_out, &refl_text}, {perm_out, &perm_text}};
    if (!WriteFiles(files, 3)) {
        std::fprintf(stderr, "shader_mint: failed writing %s, %s or %s\n", spv_out, refl_out, perm_out);
        return 1;
    }
    return 0;
}
