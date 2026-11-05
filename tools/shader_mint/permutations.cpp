#include "permutations.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>

#include "renderer/shader_permutation.h"

namespace engine_placeholder::shader_mint {

namespace {

bool IsOptionName(const std::string& s) {
    if (s.empty() || !(s[0] >= 'A' && s[0] <= 'Z')) {
        return false;
    }
    for (char c : s) {
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) {
            return false;
        }
    }
    return true;
}

std::vector<std::string> Words(const std::string& line) {
    std::vector<std::string> out;
    std::string current;
    for (char c : line) {
        if (c == ' ' || c == '\t' || c == '\r') {
            if (!current.empty()) {
                out.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        out.push_back(current);
    }
    return out;
}

// The source with `//` and `/* */` comments removed (replaced by blanks, so identifiers stay separated).
std::string StripComments(const std::string& text) {
    std::string out;
    usize i = 0;
    while (i < text.size()) {
        if (text.compare(i, 2, "//") == 0) {
            while (i < text.size() && text[i] != '\n') {
                ++i;
            }
        } else if (text.compare(i, 2, "/*") == 0) {
            const usize end = text.find("*/", i + 2);
            i = end == std::string::npos ? text.size() : end + 2;
            out += ' ';
        } else {
            out += text[i++];
        }
    }
    return out;
}

// Counts whole-identifier occurrences of `name` in `text`.
usize CountIdentifier(const std::string& text, const std::string& name) {
    usize count = 0;
    for (usize at = text.find(name); at != std::string::npos; at = text.find(name, at + 1)) {
        const bool before = at == 0 || !((text[at - 1] >= 'A' && text[at - 1] <= 'Z') || (text[at - 1] >= 'a' && text[at - 1] <= 'z') ||
                                         (text[at - 1] >= '0' && text[at - 1] <= '9') || text[at - 1] == '_');
        const usize end = at + name.size();
        const bool after = end >= text.size() || !((text[end] >= 'A' && text[end] <= 'Z') || (text[end] >= 'a' && text[end] <= 'z') ||
                                                    (text[end] >= '0' && text[end] <= '9') || text[end] == '_');
        if (before && after) {
            ++count;
        }
    }
    return count;
}

} // namespace

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

void AppendWordArray(std::string* out, const std::string& name, const std::vector<u32>& words) {
    Appendf(out, "inline constexpr usize %sWordCount = %zu;\n\n", name.c_str(), words.size());
    Appendf(out, "inline constexpr u32 %s[%sWordCount] = {", name.c_str(), name.c_str());
    for (usize i = 0; i < words.size(); ++i) {
        Appendf(out, "%s0x%08x,", i % 6 == 0 ? "\n    " : " ", words[i]);
    }
    *out += "\n};\n";
}

void AppendByteArray(std::string* out, const std::string& name, const std::vector<u8>& bytes) {
    Appendf(out, "inline constexpr usize %sByteCount = %zu;\n\n", name.c_str(), bytes.size());
    Appendf(out, "inline constexpr u8 %s[%sByteCount] = {", name.c_str(), name.c_str());
    for (usize i = 0; i < bytes.size(); ++i) {
        Appendf(out, "%s%u,", i % 16 == 0 ? "\n    " : " ", static_cast<unsigned>(bytes[i]));
    }
    *out += "\n};\n";
}

void DedupeModules(const std::vector<BuiltModule>& per_key, std::vector<BuiltModule>* modules, std::vector<u32>* key_module) {
    modules->clear();
    key_module->clear();
    for (const BuiltModule& m : per_key) {
        usize found = modules->size();
        for (usize i = 0; i < modules->size(); ++i) {
            if ((*modules)[i] == m) {
                found = i;
                break;
            }
        }
        if (found == modules->size()) {
            modules->push_back(m);
        }
        key_module->push_back(static_cast<u32>(found));
    }
}

bool ParseOptionDeclaration(const std::string& source_text, std::vector<std::string>* options, std::string* error) {
    options->clear();
    const std::string keyword = "permutation-options:";
    usize line_start = 0;
    bool found = false;
    for (int line = 0; line < 60 && line_start < source_text.size(); ++line) {
        usize line_end = source_text.find('\n', line_start);
        if (line_end == std::string::npos) {
            line_end = source_text.size();
        }
        // `//`, optional blanks, then the keyword; the line may be indented.
        usize at = line_start;
        while (at < line_end && (source_text[at] == ' ' || source_text[at] == '\t')) {
            ++at;
        }
        if (source_text.compare(at, 2, "//") == 0) {
            at += 2;
            while (at < line_end && (source_text[at] == ' ' || source_text[at] == '\t')) {
                ++at;
            }
            if (source_text.compare(at, keyword.size(), keyword) == 0) {
                found = true;
                for (const std::string& word : Words(source_text.substr(at + keyword.size(), line_end - at - keyword.size()))) {
                    if (!IsOptionName(word)) {
                        *error = "option '" + word + "' is not an upper-case identifier";
                        return false;
                    }
                    if (std::find(options->begin(), options->end(), word) != options->end()) {
                        *error = "option '" + word + "' is declared twice";
                        return false;
                    }
                    options->push_back(word);
                }
                break;
            }
        }
        line_start = line_end + 1;
    }
    if (!found) {
        *error = "no '// permutation-options: ...' line in the first 60 lines";
        return false;
    }
    if (options->empty() || options->size() > renderer::kMaxPermutationOptions) {
        *error = "a permuted shader declares between 1 and " + std::to_string(renderer::kMaxPermutationOptions) + " options";
        return false;
    }
    // Only code counts as a use: the declaration, the header comment and a note that mentions an option do not.
    const std::string code = StripComments(source_text);
    for (const std::string& option : *options) {
        if (CountIdentifier(code, option) == 0) {
            *error = "option '" + option + "' is declared but never used in the code (comments do not count)";
            return false;
        }
    }
    return true;
}

bool ParseManifest(const std::string& manifest_text, const std::vector<std::string>& options, std::vector<u32>* keys,
                   std::string* error) {
    keys->clear();
    usize line_start = 0;
    int line_number = 0;
    while (line_start <= manifest_text.size()) {
        usize line_end = manifest_text.find('\n', line_start);
        if (line_end == std::string::npos) {
            line_end = manifest_text.size();
        }
        ++line_number;
        std::string line = manifest_text.substr(line_start, line_end - line_start);
        const usize hash = line.find('#');
        if (hash != std::string::npos) {
            line.resize(hash);
        }
        const std::vector<std::string> words = Words(line);
        line_start = line_end + 1;
        if (words.empty()) {
            continue;
        }
        const std::string where = "manifest line " + std::to_string(line_number) + ": ";
        if (words[0] != "key" || words.size() < 2) {
            *error = where + "expected 'key -' or 'key OPTION...'";
            return false;
        }
        u32 bits = 0;
        if (!(words.size() == 2 && words[1] == "-")) {
            for (usize w = 1; w < words.size(); ++w) {
                const auto it = std::find(options.begin(), options.end(), words[w]);
                if (it == options.end()) {
                    *error = where + "option '" + words[w] + "' is not declared by the shader";
                    return false;
                }
                bits |= 1u << (it - options.begin());
            }
        }
        if (std::find(keys->begin(), keys->end(), bits) != keys->end()) {
            *error = where + "this key is listed twice";
            return false;
        }
        keys->push_back(bits);
    }
    if (std::find(keys->begin(), keys->end(), 0u) == keys->end()) {
        *error = "the manifest has no base key ('key -'): the plain module is always built";
        return false;
    }
    if (keys->size() > renderer::kMaxPermutationKeys) {
        *error = "the manifest lists " + std::to_string(keys->size()) + " keys; the limit is " + std::to_string(renderer::kMaxPermutationKeys);
        return false;
    }
    std::sort(keys->begin(), keys->end());
    return true;
}

std::string DescribeKey(const std::vector<std::string>& options, u32 bits) {
    std::string out = "{";
    bool first = true;
    for (usize i = 0; i < options.size(); ++i) {
        if ((bits >> i) & 1u) {
            out += (first ? "" : ", ") + options[i];
            first = false;
        }
    }
    return out + "}";
}

std::string EmitPermutationInc(const std::string& base, const std::string& shader_file, const std::string& source_label,
                               const std::string& compiler, const std::vector<std::string>& options,
                               const std::vector<u32>& keys, const std::vector<u32>& key_module,
                               const std::vector<BuiltModule>& modules) {
    std::string out;
    Appendf(&out, "// Generated by tools/shader_mint from %s. Do not edit; run the `shaders` target to regenerate.\n", source_label.c_str());
    Appendf(&out, "// compiler: %s\n", compiler.c_str());
    Appendf(&out, "#pragma once\n\n#include \"%s.refl.inc\"\n#include \"%s.spv.inc\"\n#include \"renderer/shader_permutation.h\"\n#include \"types.h\"\n\n",
            shader_file.c_str(), shader_file.c_str());
    out += "namespace engine_placeholder::shaders {\n\n";
    // The modules other than the base one.
    for (usize m = 1; m < modules.size(); ++m) {
        const std::string prefix = base + "Perm" + std::to_string(m);
        AppendWordArray(&out, prefix + "Spv", modules[m].words);
        AppendByteArray(&out, prefix + "Refl", modules[m].reflection);
        out += "\n";
    }
    Appendf(&out, "inline constexpr const char* %sOptions[%zu] = {", base.c_str(), options.size());
    for (const std::string& option : options) {
        Appendf(&out, "\"%s\", ", option.c_str());
    }
    out += "};\n\n";
    Appendf(&out, "inline constexpr renderer::PermutationModule %sModules[%zu] = {\n", base.c_str(), modules.size());
    Appendf(&out, "    {%sSpv, %sSpvWordCount, %sRefl, %sReflByteCount},\n", base.c_str(), base.c_str(), base.c_str(), base.c_str());
    for (usize m = 1; m < modules.size(); ++m) {
        Appendf(&out, "    {%sPerm%zuSpv, %sPerm%zuSpvWordCount, %sPerm%zuRefl, %sPerm%zuReflByteCount},\n", base.c_str(), m, base.c_str(), m,
                base.c_str(), m, base.c_str(), m);
    }
    out += "};\n\n";
    Appendf(&out, "inline constexpr renderer::PermutationEntry %sEntries[%zu] = {\n", base.c_str(), keys.size());
    for (usize i = 0; i < keys.size(); ++i) {
        Appendf(&out, "    {0x%x, %u},\n", keys[i], key_module[i]);
    }
    out += "};\n\n";
    Appendf(&out, "inline constexpr renderer::ShaderPermutations %sPermutations = {\"%s\", %sOptions, %zu, %sModules, %zu, %sEntries, %zu};\n\n",
            base.c_str(), shader_file.c_str(), base.c_str(), options.size(), base.c_str(), modules.size(), base.c_str(), keys.size());
    out += "} // namespace engine_placeholder::shaders\n";
    return out;
}

} // namespace engine_placeholder::shader_mint
