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
// DXIL for Direct3D 12 (Phase 13 Day 163) is minted beside it into `<name>.dxil.inc`: the container bytes `<Base>Dxil` and
// `<Base>DxilByteCount`, `<Base>DxilSpirvFnv` (the FNV-1a hash of the SPIR-V it was minted beside, which a test compares so a
// shader changed on a machine without dxc is found), and the shader's blob set `<Base>Shader` (rhi::ShaderBlobSet). Plain mode
// takes the DXIL slangc wrote (`--dxil FILE --dxil-reflection FILE --dxil-out FILE --dxil-compiler TEXT`); permute mode runs
// slangc for DXIL too (`--dxil-arg ARG`... plus `--dxil-out` and `--dxil-compiler`) and adds every module's DXIL. The DXIL build's
// reflection must follow the register rule (reflect_json.h, CheckD3D12Registers) and the container must be signed by the
// validator. Without the DXIL options nothing DXIL is written and the committed `.dxil.inc` stays.
//
// It fails (non-zero exit, message on stderr) if the reflection cannot be built or disagrees with the SPIR-V. It is a
// dev-time tool (PLAN.md assumptions #18 and #19): the generated files are committed, so building the engine never
// needs it, slangc or the JSON library.

#include "cstr/cstr.h"

#include "containers/span.h"
#include "io/file.h"
#include "io/file_system.h"
#include "permutations.h"
#include "reflect_json.h"
#include "run_process.h"
#include "renderer/shader_reflection.h"
#include "types.h"
#include "debug/failure_hooks.h"
#include "os/console.h"

using namespace engine_placeholder;
using shader_mint::Appendf;
using shader_mint::MakeText;
using shader_mint::RunProcess;
using shader_mint::Text;

namespace {

int Usage() {
    engine_placeholder::os::PrintErrorText("usage: shader_mint --name IDENT --source FILE --compiler TEXT --spv FILE --reflection FILE "
               "--spv-out FILE --refl-out FILE\n"
               "                   [--dxil FILE --dxil-reflection FILE --dxil-out FILE --dxil-compiler TEXT]\n"
               "       shader_mint --permute --name IDENT --source LABEL --source-path FILE --manifest FILE --compiler TEXT\n"
               "                   --slangc PATH [--slang-arg ARG]... [--spirv-val PATH] --work-dir DIR\n"
               "                   --spv-out FILE --refl-out FILE --perm-out FILE\n"
               "                   [--dxil-arg ARG... --dxil-out FILE --dxil-compiler TEXT]\n");
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
    Vector<u8> dxil; // empty when no DXIL was minted
};

// Reads slangc's SPIR-V and reflection JSON, cross-checks them and serializes the reflection (parsing it back as a
// final self-check). Prints the reason and returns false on any problem.
bool MintModule(const char* spv_path, const char* json_path, const char* source, Minted* out) {
    Vector<byte> spv_bytes;
    Vector<byte> json_bytes;
    if (!ReadAll(spv_path, &spv_bytes)) {
        os::PrintError("shader_mint: cannot read %s\n", spv_path);
        return false;
    }
    if (!ReadAll(json_path, &json_bytes)) {
        os::PrintError("shader_mint: cannot read %s\n", json_path);
        return false;
    }
    if (spv_bytes.Empty() || spv_bytes.Size() % 4 != 0) {
        os::PrintError("shader_mint: %s is not a whole number of 32-bit words (%zu bytes)\n", spv_path, spv_bytes.Size());
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
        os::PrintError("shader_mint: %s does not start with the SPIR-V magic number\n", spv_path);
        return false;
    }

    // Reflection: JSON -> struct, checked against the SPIR-V, serialized, and parsed back as a final self-check.
    Text error = MakeText();
    if (!shader_mint::ReflectionFromJson(ToStringView(json_bytes), &out->reflection, &error)) {
        os::PrintError("shader_mint: %s: %s\n", source, error.CStr());
        return false;
    }
    if (!shader_mint::CrossCheckSpirv(out->reflection, out->words.Data(), words, &error)) {
        os::PrintError("shader_mint: %s: %s\n", source, error.CStr());
        return false;
    }
    const usize blob_size = renderer::SerializeShaderReflection(out->reflection, nullptr, 0);
    out->blob.Resize(blob_size);
    if (blob_size == 0 || renderer::SerializeShaderReflection(out->reflection, out->blob.Data(), blob_size) != blob_size) {
        os::PrintError("shader_mint: %s: the reflection could not be serialized\n", source);
        return false;
    }
    renderer::ShaderReflection round_trip;
    if (const char* problem = renderer::ParseShaderReflection(out->blob.Data(), blob_size, &round_trip)) {
        os::PrintError("shader_mint: %s: the serialized reflection does not parse back: %s\n", source, problem);
        return false;
    }
    return true;
}

// FNV-1a, 64 bits, over the SPIR-V as little-endian bytes: recorded beside the DXIL so a test can tell that the two were
// minted from the same source.
u64 SpirvFnv(const Vector<u32>& words) {
    u64 hash = 0xcbf29ce484222325ull;
    for (usize i = 0; i < words.Size(); ++i) {
        for (u32 b = 0; b < 4; ++b) {
            hash ^= static_cast<u64>((words[i] >> (8 * b)) & 0xffu);
            hash *= 0x100000001b3ull;
        }
    }
    return hash;
}

// Reads the DXIL slangc wrote, checks the container and the register rule of its reflection. Prints why and returns false
// on any problem.
bool ReadDxil(const char* dxil_path, const char* json_path, const renderer::ShaderReflection& reflection, const char* source, Vector<u8>* out) {
    Vector<byte> bytes;
    Vector<byte> json_bytes;
    if (!ReadAll(dxil_path, &bytes) || !ReadAll(json_path, &json_bytes)) {
        os::PrintError("shader_mint: cannot read %s or %s\n", dxil_path, json_path);
        return false;
    }
    // A DXBC container: "DXBC", a 16-byte digest the validator writes (all zero when unsigned), a version, the total size.
    if (bytes.Size() < 32 || bytes[0] != 'D' || bytes[1] != 'X' || bytes[2] != 'B' || bytes[3] != 'C') {
        os::PrintError("shader_mint: %s: %s is not a DXIL container\n", source, dxil_path);
        return false;
    }
    const u32 recorded = static_cast<u32>(bytes[24]) | (static_cast<u32>(bytes[25]) << 8) | (static_cast<u32>(bytes[26]) << 16) |
                         (static_cast<u32>(bytes[27]) << 24);
    if (recorded != bytes.Size()) {
        os::PrintError("shader_mint: %s: %s records %u bytes but has %zu\n", source, dxil_path, recorded, bytes.Size());
        return false;
    }
    bool signed_container = false;
    for (usize i = 4; i < 20; ++i) {
        signed_container = signed_container || bytes[i] != 0;
    }
    if (!signed_container) {
        os::PrintError("shader_mint: %s: the DXIL is not signed by the validator (slangc needs dxil.dll beside dxcompiler.dll)\n", source);
        return false;
    }
    Text error = MakeText();
    if (!shader_mint::CheckD3D12Registers(reflection, ToStringView(json_bytes), &error)) {
        os::PrintError("shader_mint: %s: %s\n", source, error.CStr());
        return false;
    }
    out->Resize(bytes.Size());
    for (usize i = 0; i < bytes.Size(); ++i) {
        (*out)[i] = bytes[i];
    }
    return true;
}

// The `.dxil.inc` text: the base module's DXIL, each extra module's DXIL (permuted shaders; `module_dxil[m]` with the SPIR-V
// `module_words[m]` it was minted beside, module 0 being the base), and the base module's blob set.
Text DxilText(StringView base, StringView shader_file, const char* source, const char* compiler, const Vector<const Vector<u32>*>& module_words,
              const Vector<const Vector<u8>*>& module_dxil) {
    const Text b = MakeText(base);
    const Text file = MakeText(shader_file);
    Text text = MakeText();
    Appendf(&text, "// Generated by tools/shader_mint from %s. Do not edit; run the `shaders` target on a machine with dxc to regenerate.\n", source);
    Appendf(&text, "// compiler: %s\n", compiler);
    Appendf(&text, "#pragma once\n\n#include \"%s.spv.inc\"\n#include \"rhi/desc.h\"\n#include \"types.h\"\n\nnamespace engine_placeholder::shaders {\n\n",
            file.CStr());
    for (usize m = 0; m < module_dxil.Size(); ++m) {
        Text name = MakeText(base);
        if (m != 0) {
            Appendf(&name, "Perm%zu", m);
        }
        name.Append("Dxil");
        shader_mint::AppendByteArray(&text, name.View(), *module_dxil[m]);
        Appendf(&text, "\n// The FNV-1a hash of the SPIR-V this DXIL was minted beside: a test compares it with the SPIR-V that is committed now.\n");
        Appendf(&text, "inline constexpr u64 %sSpirvFnv = 0x%016llxull;\n\n", name.CStr(), static_cast<unsigned long long>(SpirvFnv(*module_words[m])));
    }
    Appendf(&text, "// The shader in every format it was minted in.\n");
    Appendf(&text, "inline constexpr rhi::ShaderBlobSet %sShader = {\"%s\", rhi::SpirvBlob(%sSpv, %sSpvWordCount), rhi::DxilBlob(%sDxil, %sDxilByteCount)};\n",
            b.CStr(), file.CStr(), b.CStr(), b.CStr(), b.CStr(), b.CStr());
    text.Append("\n} // namespace engine_placeholder::shaders\n");
    return text;
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
                os::PrintError("shader_mint: %s: the generated constant name %s would be defined twice; rename a block or member\n",
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
    engine_placeholder::debug::InstallDiagnosticsHooks(); // a failed assert prints its stack trace
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
    const char* dxil_path = nullptr;
    const char* dxil_json_path = nullptr;
    const char* dxil_out = nullptr;
    const char* dxil_compiler = nullptr;
    Vector<Text> slang_args;
    Vector<Text> dxil_args;
    bool permute = false;
    for (int i = 1; i < argc; ++i) {
        const bool has_value = i + 1 < argc;
        if (CStrCompare(argv[i], "--permute") == 0) {
            permute = true;
        } else if (CStrCompare(argv[i], "--name") == 0 && has_value) {
            name = argv[++i];
        } else if (CStrCompare(argv[i], "--source") == 0 && has_value) {
            source = argv[++i];
        } else if (CStrCompare(argv[i], "--source-path") == 0 && has_value) {
            source_path = argv[++i];
        } else if (CStrCompare(argv[i], "--compiler") == 0 && has_value) {
            compiler = argv[++i];
        } else if (CStrCompare(argv[i], "--spv") == 0 && has_value) {
            spv_path = argv[++i];
        } else if (CStrCompare(argv[i], "--reflection") == 0 && has_value) {
            json_path = argv[++i];
        } else if (CStrCompare(argv[i], "--spv-out") == 0 && has_value) {
            spv_out = argv[++i];
        } else if (CStrCompare(argv[i], "--refl-out") == 0 && has_value) {
            refl_out = argv[++i];
        } else if (CStrCompare(argv[i], "--perm-out") == 0 && has_value) {
            perm_out = argv[++i];
        } else if (CStrCompare(argv[i], "--manifest") == 0 && has_value) {
            manifest_path = argv[++i];
        } else if (CStrCompare(argv[i], "--slangc") == 0 && has_value) {
            slangc = argv[++i];
        } else if (CStrCompare(argv[i], "--slang-arg") == 0 && has_value) {
            slang_args.Push(MakeText(StringView(argv[++i])));
        } else if (CStrCompare(argv[i], "--spirv-val") == 0 && has_value) {
            spirv_val = argv[++i];
        } else if (CStrCompare(argv[i], "--work-dir") == 0 && has_value) {
            work_dir = argv[++i];
        } else if (CStrCompare(argv[i], "--dxil") == 0 && has_value) {
            dxil_path = argv[++i];
        } else if (CStrCompare(argv[i], "--dxil-reflection") == 0 && has_value) {
            dxil_json_path = argv[++i];
        } else if (CStrCompare(argv[i], "--dxil-out") == 0 && has_value) {
            dxil_out = argv[++i];
        } else if (CStrCompare(argv[i], "--dxil-compiler") == 0 && has_value) {
            dxil_compiler = argv[++i];
        } else if (CStrCompare(argv[i], "--dxil-arg") == 0 && has_value) {
            dxil_args.Push(MakeText(StringView(argv[++i])));
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
    // DXIL: all of its options or none (plain mode: the files slangc wrote; permute mode: slangc's DXIL arguments).
    const bool want_dxil = permute ? !dxil_args.Empty() : dxil_path != nullptr;
    if (want_dxil ? (dxil_out == nullptr || dxil_compiler == nullptr || (!permute && dxil_json_path == nullptr) || (permute && dxil_path != nullptr))
                  : (dxil_out != nullptr || dxil_json_path != nullptr || dxil_compiler != nullptr)) {
        return Usage();
    }
    const Text base = MakeText(StringView(name));
    const StringView label(source);
    const usize slang_at = label.EndsWith(".slang") ? label.Length() - 6 : label.Length(); // drop only a trailing ".slang"
    const Text shader_file = MakeText(label.Substring(0, slang_at));

    Minted plain;
    Vector<u32> keys;
    Vector<Text> options;
    Vector<Minted> minted_keys; // permute mode: one per manifest key, in key order
    if (!permute) {
        if (!MintModule(spv_path, json_path, source, &plain)) {
            return 1;
        }
        if (want_dxil && !ReadDxil(dxil_path, dxil_json_path, plain.reflection, source, &plain.dxil)) {
            return 1;
        }
    } else {
        Vector<byte> source_bytes;
        Vector<byte> manifest_bytes;
        Text error = MakeText();
        if (!ReadAll(source_path, &source_bytes) || !ReadAll(manifest_path, &manifest_bytes)) {
            os::PrintError("shader_mint: cannot read %s or %s\n", source_path, manifest_path);
            return 1;
        }
        if (!shader_mint::ParseOptionDeclaration(ToStringView(source_bytes), &options, &error)) {
            os::PrintError("shader_mint: %s: %s\n", source, error.CStr());
            return 1;
        }
        if (!shader_mint::ParseManifest(ToStringView(manifest_bytes), options, &keys, &error)) {
            os::PrintError("shader_mint: %s: %s\n", manifest_path, error.CStr());
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
                os::PrintError("shader_mint: %s: slangc failed for key %s\n", source, shader_mint::DescribeKey(options, key).CStr());
                return 1;
            }
            if (spirv_val != nullptr) {
                Vector<Text> validate;
                validate.Push(MakeText(StringView(spirv_val)));
                validate.Push(MakeText("--target-env"));
                validate.Push(MakeText("vulkan1.3"));
                validate.Push(MakeText(spv_file.View()));
                if (!RunProcess(validate)) {
                    os::PrintError("shader_mint: %s: spirv-val rejected key %s\n", source, shader_mint::DescribeKey(options, key).CStr());
                    return 1;
                }
            }
            Minted m;
            if (!MintModule(spv_file.CStr(), json_file.CStr(), source, &m)) {
                os::PrintError("shader_mint: (while minting key %s)\n", shader_mint::DescribeKey(options, key).CStr());
                return 1;
            }
            if (want_dxil) {
                Vector<Text> dxil_command;
                dxil_command.Push(MakeText(StringView(slangc)));
                dxil_command.Push(MakeText(StringView(source_path)));
                for (usize s2 = 0; s2 < dxil_args.Size(); ++s2) {
                    dxil_command.Push(MakeText(dxil_args[s2].View()));
                }
                for (usize o = 0; o < options.Size(); ++o) {
                    if ((key >> o) & 1u) {
                        dxil_command.Push(Join("-D", options[o].View(), "=1"));
                    }
                }
                const Text dxil_file = Join(stem.View(), ".dxil");
                const Text dxil_json = Join(stem.View(), ".dxil.json");
                dxil_command.Push(MakeText("-o"));
                dxil_command.Push(MakeText(dxil_file.View()));
                dxil_command.Push(MakeText("-reflection-json"));
                dxil_command.Push(MakeText(dxil_json.View()));
                if (!RunProcess(dxil_command)) {
                    os::PrintError("shader_mint: %s: slangc failed to build DXIL for key %s\n", source, shader_mint::DescribeKey(options, key).CStr());
                    return 1;
                }
                if (!ReadDxil(dxil_file.CStr(), dxil_json.CStr(), m.reflection, source, &m.dxil)) {
                    os::PrintError("shader_mint: (while minting the DXIL of key %s)\n", shader_mint::DescribeKey(options, key).CStr());
                    return 1;
                }
            }
            minted_keys.Push(static_cast<Minted&&>(m));
        }
        // keys are sorted and the manifest requires key 0, so this is the base module
        Assign(plain.words, Span<const u32>(minted_keys[0].words));
        Assign(plain.blob, Span<const u8>(minted_keys[0].blob));
        Assign(plain.dxil, Span<const u8>(minted_keys[0].dxil));
        plain.reflection = minted_keys[0].reflection;
    }

    const Text spv_text = SpvText(base.View(), source, compiler, plain.words);
    Text refl_text = MakeText();
    if (!ReflText(base.View(), source, compiler, plain, &refl_text)) {
        return 1;
    }
    if (!permute) {
        Vector<const Vector<u32>*> module_words;
        Vector<const Vector<u8>*> module_dxil;
        module_words.Push(&plain.words);
        module_dxil.Push(&plain.dxil);
        const Text dxil_text = want_dxil ? DxilText(base.View(), shader_file.View(), source, dxil_compiler, module_words, module_dxil) : MakeText();
        const OutputFile files[] = {{spv_out, &spv_text}, {refl_out, &refl_text}, {dxil_out, &dxil_text}};
        if (!WriteFiles(files, want_dxil ? 3 : 2)) {
            os::PrintError("shader_mint: failed writing %s, %s or the DXIL\n", spv_out, refl_out);
            return 1;
        }
        return 0;
    }

    // Dedupe: modules with identical words and reflection share one table entry.
    // The DXIL is not part of the comparison, so a machine without dxc dedupes exactly as one with it and writes the same table: two
    // keys whose SPIR-V and reflection are identical compile from equivalent code, and the module keeps its first key's DXIL.
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
    const Text perm_text = shader_mint::EmitPermutationInc(base.View(), shader_file.View(), label, StringView(compiler), options, keys, key_module, modules);
    Text dxil_text = MakeText();
    if (want_dxil) {
        Vector<const Vector<u32>*> module_words;
        Vector<const Vector<u8>*> module_dxil;
        for (usize m = 0; m < modules.Size(); ++m) {
            usize first = 0;
            while (key_module[first] != m) ++first; // every module has a key: DedupeModules made it from one
            module_words.Push(&modules[m].words);
            module_dxil.Push(&minted_keys[first].dxil);
        }
        dxil_text = DxilText(base.View(), shader_file.View(), source, dxil_compiler, module_words, module_dxil);
    }
    const OutputFile files[] = {{spv_out, &spv_text}, {refl_out, &refl_text}, {perm_out, &perm_text}, {dxil_out, &dxil_text}};
    if (!WriteFiles(files, want_dxil ? 4 : 3)) {
        os::PrintError("shader_mint: failed writing %s, %s, %s or the DXIL\n", spv_out, refl_out, perm_out);
        return 1;
    }
    return 0;
}
