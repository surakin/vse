#include "permutations.h"

#include "cstr/varargs.h"
#include "cstr/format.h"

#include "algorithm.h"
#include "renderer/shader_permutation.h"

namespace vse::shader_mint
{

namespace
{

bool IsUpper(char c)
{
    return c >= 'A' && c <= 'Z';
}
bool IsIdentifierChar(char c)
{
    return IsUpper(c) || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

bool IsOptionName(StringView s)
{
    if (s.Empty() || !IsUpper(s[0]))
    {
        return false;
    }
    for (usize i = 0; i < s.Length(); ++i)
    {
        if (!(IsUpper(s[i]) || (s[i] >= '0' && s[i] <= '9') || s[i] == '_'))
        {
            return false;
        }
    }
    return true;
}

// Splits on blanks; the views point into `line`.
void Words(StringView line, Vector<StringView>* out)
{
    usize start = 0;
    bool in_word = false;
    for (usize i = 0; i <= line.Length(); ++i)
    {
        const bool blank = i == line.Length() || line[i] == ' ' || line[i] == '\t' || line[i] == '\r';
        if (blank && in_word)
        {
            out->Push(line.Substring(start, i - start));
            in_word = false;
        }
        else if (!blank && !in_word)
        {
            start = i;
            in_word = true;
        }
    }
}

// The source with `//` and `/* */` comments removed (replaced by blanks, so identifiers stay separated).
Text StripComments(StringView text)
{
    Text out = MakeText();
    usize i = 0;
    while (i < text.Length())
    {
        if (text.Substring(i).StartsWith("//"))
        {
            while (i < text.Length() && text[i] != '\n')
            {
                ++i;
            }
        }
        else if (text.Substring(i).StartsWith("/*"))
        {
            const usize end = text.Substring(i + 2).Find("*/");
            i = i + 2 + end >= text.Length() ? text.Length() : i + 2 + end + 2;
            out.Append(' ');
        }
        else
        {
            out.Append(text[i++]);
        }
    }
    return out;
}

// Counts whole-identifier occurrences of `name` in `text`.
usize CountIdentifier(StringView text, StringView name)
{
    usize count = 0;
    for (usize at = text.Find(name); at != text.Length();)
    {
        const bool before = at == 0 || !IsIdentifierChar(text[at - 1]);
        const usize end = at + name.Length();
        const bool after = end >= text.Length() || !IsIdentifierChar(text[end]);
        if (before && after)
        {
            ++count;
        }
        const usize next = text.Substring(at + 1).Find(name);
        at = at + 1 + next >= text.Length() ? text.Length() : at + 1 + next;
    }
    return count;
}

usize OptionIndex(const Vector<Text>& options, StringView word)
{
    for (usize i = 0; i < options.Size(); ++i)
    {
        if (options[i].View() == word)
        {
            return i;
        }
    }
    return options.Size();
}

}  // namespace

void VAppendf(Text* out, const char* format, VarArgs args)
{
    VarArgs again;
    ENGINE_VA_COPY(again, args);
    const int needed = VFormatTo(nullptr, 0, format, args);
    if (needed > 0)
    {
        Vector<char> buffer;
        buffer.Resize(static_cast<usize>(needed) + 1);
        VFormatTo(buffer.Data(), buffer.Size(), format, again);
        out->AppendRawBytes(buffer.Data(), static_cast<usize>(needed));
    }
    ENGINE_VA_END(again);
}

void Appendf(Text* out, const char* format, ...)
{
    VarArgs args;
    ENGINE_VA_START(args, format);
    VAppendf(out, format, args);
    ENGINE_VA_END(args);
}

void SetTextf(Text* out, const char* format, ...)
{
    *out = MakeText();
    VarArgs args;
    ENGINE_VA_START(args, format);
    VAppendf(out, format, args);
    ENGINE_VA_END(args);
}

bool BuiltModule::operator==(const BuiltModule& other) const
{
    if (words.Size() != other.words.Size() || reflection.Size() != other.reflection.Size())
    {
        return false;
    }
    for (usize i = 0; i < words.Size(); ++i)
    {
        if (words[i] != other.words[i])
        {
            return false;
        }
    }
    for (usize i = 0; i < reflection.Size(); ++i)
    {
        if (reflection[i] != other.reflection[i])
        {
            return false;
        }
    }
    return true;
}

BuiltModule BuiltModule::Clone() const
{
    BuiltModule copy;
    Assign(copy.words, Span<const u32>(words));
    Assign(copy.reflection, Span<const u8>(reflection));
    return copy;
}

void AppendWordArray(Text* out, StringView name, const Vector<u32>& words)
{
    const Text n = MakeText(name);
    Appendf(out, "inline constexpr usize %sWordCount = %zu;\n\n", n.CStr(), words.Size());
    Appendf(out, "inline constexpr u32 %s[%sWordCount] = {", n.CStr(), n.CStr());
    for (usize i = 0; i < words.Size(); ++i)
    {
        Appendf(out, "%s0x%08x,", i % 6 == 0 ? "\n    " : " ", words[i]);
    }
    out->Append("\n};\n");
}

void AppendByteArray(Text* out, StringView name, const Vector<u8>& bytes)
{
    const Text n = MakeText(name);
    Appendf(out, "inline constexpr usize %sByteCount = %zu;\n\n", n.CStr(), bytes.Size());
    Appendf(out, "inline constexpr u8 %s[%sByteCount] = {", n.CStr(), n.CStr());
    for (usize i = 0; i < bytes.Size(); ++i)
    {
        Appendf(out, "%s%u,", i % 16 == 0 ? "\n    " : " ", static_cast<unsigned>(bytes[i]));
    }
    out->Append("\n};\n");
}

void DedupeModules(const Vector<BuiltModule>& per_key, Vector<BuiltModule>* modules, Vector<u32>* key_module)
{
    modules->Clear();
    key_module->Clear();
    for (usize k = 0; k < per_key.Size(); ++k)
    {
        const BuiltModule& m = per_key[k];
        usize found = modules->Size();
        for (usize i = 0; i < modules->Size(); ++i)
        {
            if ((*modules)[i] == m)
            {
                found = i;
                break;
            }
        }
        if (found == modules->Size())
        {
            modules->Push(m.Clone());
        }
        key_module->Push(static_cast<u32>(found));
    }
}

bool ParseOptionDeclaration(StringView source_text, Vector<Text>* options, Text* error)
{
    options->Clear();
    const StringView keyword = "permutation-options:";
    usize line_start = 0;
    bool found = false;
    for (int line = 0; line < 60 && line_start < source_text.Length(); ++line)
    {
        usize line_end = line_start + source_text.Substring(line_start).IndexOf('\n');
        // `//`, optional blanks, then the keyword; the line may be indented.
        usize at = line_start;
        while (at < line_end && (source_text[at] == ' ' || source_text[at] == '\t'))
        {
            ++at;
        }
        if (source_text.Substring(at).StartsWith("//"))
        {
            at += 2;
            while (at < line_end && (source_text[at] == ' ' || source_text[at] == '\t'))
            {
                ++at;
            }
            if (source_text.Substring(at).StartsWith(keyword))
            {
                found = true;
                Vector<StringView> words;
                Words(source_text.Substring(at + keyword.Length(), line_end - at - keyword.Length()), &words);
                for (usize w = 0; w < words.Size(); ++w)
                {
                    const Text word = MakeText(words[w]);
                    if (!IsOptionName(words[w]))
                    {
                        SetTextf(error, "option '%s' is not an upper-case identifier", word.CStr());
                        return false;
                    }
                    if (OptionIndex(*options, words[w]) != options->Size())
                    {
                        SetTextf(error, "option '%s' is declared twice", word.CStr());
                        return false;
                    }
                    options->Push(MakeText(words[w]));
                }
                break;
            }
        }
        line_start = line_end + 1;
    }
    if (!found)
    {
        SetTextf(error, "no '// permutation-options: ...' line in the first 60 lines");
        return false;
    }
    if (options->Empty() || options->Size() > renderer::kMaxPermutationOptions)
    {
        SetTextf(error, "a permuted shader declares between 1 and %zu options",
                 static_cast<usize>(renderer::kMaxPermutationOptions));
        return false;
    }
    // Only code counts as a use: the declaration, the header comment and a note that mentions an option do not.
    const Text code = StripComments(source_text);
    for (usize i = 0; i < options->Size(); ++i)
    {
        if (CountIdentifier(code.View(), (*options)[i].View()) == 0)
        {
            SetTextf(error, "option '%s' is declared but never used in the code (comments do not count)",
                     (*options)[i].CStr());
            return false;
        }
    }
    return true;
}

bool ParseManifest(StringView manifest_text, const Vector<Text>& options, Vector<u32>* keys, Text* error)
{
    keys->Clear();
    usize line_start = 0;
    int line_number = 0;
    while (line_start <= manifest_text.Length())
    {
        const usize line_end = line_start + manifest_text.Substring(line_start).IndexOf('\n');
        ++line_number;
        StringView line = manifest_text.Substring(line_start, line_end - line_start);
        line = line.Substring(0, line.IndexOf('#'));
        Vector<StringView> words;
        Words(line, &words);
        line_start = line_end + 1;
        if (words.Empty())
        {
            continue;
        }
        if (words[0] != StringView("key") || words.Size() < 2)
        {
            SetTextf(error, "manifest line %d: expected 'key -' or 'key OPTION...'", line_number);
            return false;
        }
        u32 bits = 0;
        if (!(words.Size() == 2 && words[1] == StringView("-")))
        {
            for (usize w = 1; w < words.Size(); ++w)
            {
                const usize index = OptionIndex(options, words[w]);
                if (index == options.Size())
                {
                    const Text name = MakeText(words[w]);
                    SetTextf(error, "manifest line %d: option '%s' is not declared by the shader", line_number,
                             name.CStr());
                    return false;
                }
                bits |= 1u << index;
            }
        }
        if (IndexOf(*keys, bits) != keys->Size())
        {
            SetTextf(error, "manifest line %d: this key is listed twice", line_number);
            return false;
        }
        keys->Push(bits);
    }
    if (IndexOf(*keys, 0u) == keys->Size())
    {
        SetTextf(error, "the manifest has no base key ('key -'): the plain module is always built");
        return false;
    }
    if (keys->Size() > renderer::kMaxPermutationKeys)
    {
        SetTextf(error, "the manifest lists %zu keys; the limit is %zu", keys->Size(),
                 static_cast<usize>(renderer::kMaxPermutationKeys));
        return false;
    }
    Sort(Span<u32>(*keys));
    return true;
}

Text DescribeKey(const Vector<Text>& options, u32 bits)
{
    Text out = MakeText("{");
    bool first = true;
    for (usize i = 0; i < options.Size(); ++i)
    {
        if ((bits >> i) & 1u)
        {
            if (!first)
            {
                out.Append(", ");
            }
            out.Append(options[i].View());
            first = false;
        }
    }
    out.Append('}');
    return out;
}

Text EmitPermutationInc(StringView base_view, StringView shader_view, StringView label_view, StringView compiler_view,
                        const Vector<Text>& options, const Vector<u32>& keys, const Vector<u32>& key_module,
                        const Vector<BuiltModule>& modules)
{
    const Text base = MakeText(base_view), shader_file = MakeText(shader_view), source_label = MakeText(label_view),
               compiler = MakeText(compiler_view);
    const char* b = base.CStr();
    Text out = MakeText();
    Appendf(&out, "// Generated by tools/shader_mint from %s. Do not edit; run the `shaders` target to regenerate.\n",
            source_label.CStr());
    Appendf(&out, "// compiler: %s\n", compiler.CStr());
    Appendf(
        &out,
        "#pragma once\n\n#include \"%s.dxil.inc\"\n#include \"%s.refl.inc\"\n#include \"%s.spv.inc\"\n#include \"renderer/shader_permutation.h\"\n#include \"types.h\"\n\n",
        shader_file.CStr(), shader_file.CStr(), shader_file.CStr());
    out.Append("namespace vse::shaders {\n\n");
    // The modules other than the base one.
    for (usize m = 1; m < modules.Size(); ++m)
    {
        Text prefix = MakeText(base.View());
        Appendf(&prefix, "Perm%zu", m);
        Text spv = MakeText(prefix.View());
        spv.Append("Spv");
        Text refl = MakeText(prefix.View());
        refl.Append("Refl");
        AppendWordArray(&out, spv.View(), modules[m].words);
        AppendByteArray(&out, refl.View(), modules[m].reflection);
        out.Append("\n");
    }
    Appendf(&out, "inline constexpr const char* %sOptions[%zu] = {", b, options.Size());
    for (usize i = 0; i < options.Size(); ++i)
    {
        Appendf(&out, "\"%s\", ", options[i].CStr());
    }
    out.Append("};\n\n");
    Appendf(&out, "inline constexpr renderer::PermutationModule %sModules[%zu] = {\n", b, modules.Size());
    // Each module's Metal library (Phase 21 Day 299) comes from the `.metallib.inc`, which the `.dxil.inc` includes.
    Appendf(
        &out,
        "    {%sSpv, %sSpvWordCount, %sRefl, %sReflByteCount, %sDxil, %sDxilByteCount, %sMetallib, %sMetallibByteCount, "
        "%sMetallibEntry, &%sMetallibSlots},\n",
        b, b, b, b, b, b, b, b, b, b);
    for (usize m = 1; m < modules.Size(); ++m)
    {
        Appendf(
            &out,
            "    {%sPerm%zuSpv, %sPerm%zuSpvWordCount, %sPerm%zuRefl, %sPerm%zuReflByteCount, %sPerm%zuDxil, %sPerm%zuDxilByteCount, "
            "%sPerm%zuMetallib, %sPerm%zuMetallibByteCount, %sPerm%zuMetallibEntry, &%sPerm%zuMetallibSlots},\n",
            b, m, b, m, b, m, b, m, b, m, b, m, b, m, b, m, b, m, b, m);
    }
    out.Append("};\n\n");
    Appendf(&out, "inline constexpr renderer::PermutationEntry %sEntries[%zu] = {\n", b, keys.Size());
    for (usize i = 0; i < keys.Size(); ++i)
    {
        Appendf(&out, "    {0x%x, %u},\n", keys[i], key_module[i]);
    }
    out.Append("};\n\n");
    Appendf(
        &out,
        "inline constexpr renderer::ShaderPermutations %sPermutations = {\"%s\", %sOptions, %zu, %sModules, %zu, %sEntries, %zu};\n\n",
        b, shader_file.CStr(), b, options.Size(), b, modules.Size(), b, keys.Size());
    out.Append("} // namespace vse::shaders\n");
    return out;
}

}  // namespace vse::shader_mint
