#include "material_source.h"

#include "asset/texture.h"
#include "cstr/cstr.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"
#include "renderer/gbuffer.h"
#include "renderer/shader_permutation.h"
#include "renderer/shader_reflection.h"
#include "source_text.h"

namespace engine_placeholder::asset
{
namespace
{

void MsSetError(MaterialSourceError* error, u32 line, const char* message)
{
    error->line = line;
    FormatTo(error->message.Data(), error->message.Size(), "%s", message);
}

Status MsFail(MaterialSourceError* error, u32 line, const char* message)
{
    MsSetError(error, line, message);
    return Status::Make(ErrorCode::Invalid, "the material source is invalid");
}

// One `param` line.
struct MsParam
{
    MaterialName name;
    MaterialParamType type = MaterialParamType::Float;
    u32 words[16] = {};  // the value as the block holds it
    u32 line = 0;
};

struct MsTexture
{
    MaterialName slot;
    Array<char, 256> path = {};
    u32 line = 0;
};

struct MsSampler
{
    MaterialSampler sampler;
    u32 line = 0;
};

struct MsSource
{
    ResourceId id;
    MaterialName template_name;
    u32 template_line = 0;
    Vector<MaterialName> options;
    Vector<MsParam> params;
    Vector<MsTexture> textures;
    Vector<MsSampler> samplers;
    MaterialStateOverride state;
};

bool MsParseSigned(StringView token, i32* out)
{
    if (token.Length() == 0)
    {
        return false;
    }
    const bool negative = token[0] == '-';
    u32 magnitude = 0;
    if (!srctext::ParseUnsigned(negative ? token.Substring(1, token.Length() - 1) : token, &magnitude))
    {
        return false;
    }
    if (negative ? magnitude > 0x80000000u : magnitude > 0x7fffffffu)
    {
        return false;
    }
    *out = negative ? static_cast<i32>(0u - magnitude) : static_cast<i32>(magnitude);
    return true;
}

bool MsOnOff(StringView token, bool* out)
{
    if (token == StringView("on"))
    {
        *out = true;
        return true;
    }
    if (token == StringView("off"))
    {
        *out = false;
        return true;
    }
    return false;
}

bool MsName(StringView token, MaterialName* out)
{
    return MakeMaterialName(token.Data(), token.Length(), out);
}

// The number of values a `param` of `type` takes (for `color`, 3 or 4).
u32 MsValueCount(MaterialParamType type)
{
    switch (type)
    {
    case MaterialParamType::Float:
    case MaterialParamType::Int:
    case MaterialParamType::UInt: return 1;
    case MaterialParamType::Vec2: return 2;
    case MaterialParamType::Vec3:
    case MaterialParamType::Color3: return 3;
    case MaterialParamType::Vec4:
    case MaterialParamType::Color4: return 4;
    case MaterialParamType::Mat4: return 16;
    }
    return 0;
}

Status MsParse(StringView text, MsSource* out, MaterialSourceError* error)
{
    bool have_id = false;
    bool have_template = false;
    bool seen_state[5] = {false, false, false, false, false};
    u32 line_number = 0;
    usize pos = 0;
    while (pos < text.Length())
    {
        usize end = pos;
        while (end < text.Length() && text[end] != '\n')
        {
            ++end;
        }
        ++line_number;
        const srctext::LineTokens t = srctext::Tokenize(text.Substring(pos, end - pos));
        pos = end + 1;
        if (t.count == 0 && !t.overflow)
        {
            continue;
        }
        if (t.overflow)
        {
            return MsFail(error, line_number, "too many fields on the line");
        }
        const StringView kind = t.tokens[0];
        if (kind == StringView("id"))
        {
            if (have_id)
            {
                return MsFail(error, line_number, "the id is given twice");
            }
            if (t.count != 2 || ParseResourceId(t.tokens[1], &out->id).Failed())
            {
                return MsFail(error, line_number, "id needs one 16-digit hex resource id");
            }
            have_id = true;
            continue;
        }
        if (!have_id)
        {
            return MsFail(error, line_number, "the id line must come first (asset_mint adopt writes it)");
        }
        if (kind == StringView("template"))
        {
            if (have_template)
            {
                return MsFail(error, line_number, "the template is given twice");
            }
            if (t.count != 2 || !MsName(t.tokens[1], &out->template_name))
            {
                return MsFail(error, line_number, "template needs one name of at most 47 characters");
            }
            out->template_line = line_number;
            have_template = true;
            continue;
        }
        if (!have_template)
        {
            return MsFail(error, line_number, "the template line must come second");
        }
        if (kind == StringView("option"))
        {
            MaterialName name;
            if (t.count != 2 || !MsName(t.tokens[1], &name))
            {
                return MsFail(error, line_number, "option needs one name");
            }
            out->options.Push(name);
        }
        else if (kind == StringView("param"))
        {
            MsParam p;
            p.line = line_number;
            if (t.count < 4 || !MsName(t.tokens[1], &p.name))
            {
                return MsFail(error, line_number, "param needs a name, a type and values");
            }
            const StringView type = t.tokens[2];
            u32 expected = 0;
            if (type == StringView("float"))
            {
                p.type = MaterialParamType::Float;
            }
            else if (type == StringView("int"))
            {
                p.type = MaterialParamType::Int;
            }
            else if (type == StringView("uint"))
            {
                p.type = MaterialParamType::UInt;
            }
            else if (type == StringView("vec2"))
            {
                p.type = MaterialParamType::Vec2;
            }
            else if (type == StringView("vec3"))
            {
                p.type = MaterialParamType::Vec3;
            }
            else if (type == StringView("vec4"))
            {
                p.type = MaterialParamType::Vec4;
            }
            else if (type == StringView("mat4"))
            {
                p.type = MaterialParamType::Mat4;
            }
            else if (type == StringView("color"))
            {
                p.type = t.count == 6 ? MaterialParamType::Color3 : MaterialParamType::Color4;
                if (t.count != 6 && t.count != 7)
                {
                    return MsFail(error, line_number, "a color has three or four values");
                }
            }
            else
            {
                return MsFail(error, line_number, "param type is float, int, uint, vec2, vec3, vec4, mat4 or color");
            }
            expected = MsValueCount(p.type);
            if (t.count != 3 + expected)
            {
                return MsFail(error, line_number, "the number of values does not match the type");
            }
            for (u32 i = 0; i < expected; ++i)
            {
                const StringView v = t.tokens[3 + i];
                if (p.type == MaterialParamType::Int)
                {
                    i32 value = 0;
                    if (!MsParseSigned(v, &value))
                    {
                        return MsFail(error, line_number, "an int value is a decimal integer that fits 32 bits");
                    }
                    p.words[i] = static_cast<u32>(value);
                }
                else if (p.type == MaterialParamType::UInt)
                {
                    if (!srctext::ParseUnsigned(v, &p.words[i]))
                    {
                        return MsFail(error, line_number, "a uint value is a decimal integer below 2^32");
                    }
                }
                else
                {
                    f32 value = 0;
                    if (!srctext::ParseFloat(v, &value))
                    {
                        return MsFail(error, line_number, "a float value is a finite number");
                    }
                    MemCopy(&p.words[i], &value, 4);
                }
            }
            out->params.Push(p);
        }
        else if (kind == StringView("texture"))
        {
            MsTexture tex;
            tex.line = line_number;
            if (t.count != 3 || !MsName(t.tokens[1], &tex.slot) || t.tokens[2].Length() > 255)
            {
                return MsFail(error, line_number, "texture needs a slot name and a path of at most 255 bytes");
            }
            MemCopy(tex.path.Data(), t.tokens[2].Data(), t.tokens[2].Length());
            out->textures.Push(tex);
        }
        else if (kind == StringView("sampler"))
        {
            MsSampler s;
            s.line = line_number;
            if (t.count < 2 || t.count % 2 != 0 || !MsName(t.tokens[1], &s.sampler.slot))
            {
                return MsFail(error, line_number, "sampler needs a slot name and key value pairs");
            }
            for (usize i = 2; i + 1 < t.count; i += 2)
            {
                const StringView key = t.tokens[i];
                const StringView value = t.tokens[i + 1];
                if (key == StringView("filter"))
                {
                    MaterialFilter f;
                    if (value == StringView("nearest"))
                    {
                        f = MaterialFilter::Nearest;
                    }
                    else if (value == StringView("linear"))
                    {
                        f = MaterialFilter::Linear;
                    }
                    else
                    {
                        return MsFail(error, line_number, "filter is nearest or linear");
                    }
                    s.sampler.min_filter = s.sampler.mag_filter = s.sampler.mip_filter = f;
                }
                else if (key == StringView("address"))
                {
                    MaterialAddress a;
                    if (value == StringView("repeat"))
                    {
                        a = MaterialAddress::Repeat;
                    }
                    else if (value == StringView("clamp"))
                    {
                        a = MaterialAddress::ClampToEdge;
                    }
                    else if (value == StringView("mirror"))
                    {
                        a = MaterialAddress::MirrorRepeat;
                    }
                    else
                    {
                        return MsFail(error, line_number, "address is repeat, clamp or mirror");
                    }
                    s.sampler.address_u = s.sampler.address_v = a;
                }
                else if (key == StringView("anisotropy"))
                {
                    u32 n = 0;
                    if (!srctext::ParseUnsigned(value, &n) || n < 1 || n > 16)
                    {
                        return MsFail(error, line_number, "anisotropy is a number from 1 to 16");
                    }
                    s.sampler.max_anisotropy = static_cast<u8>(n);
                }
                else
                {
                    return MsFail(error, line_number, "a sampler key is filter, address or anisotropy");
                }
            }
            out->samplers.Push(s);
        }
        else if (kind == StringView("state"))
        {
            if (t.count != 3)
            {
                return MsFail(error, line_number, "state needs a field and a value");
            }
            const StringView field = t.tokens[1];
            const StringView value = t.tokens[2];
            u32 index = 99;
            if (field == StringView("cull"))
            {
                index = 0;
                if (value == StringView("none"))
                {
                    out->state.cull = 0;
                }
                else if (value == StringView("front"))
                {
                    out->state.cull = 1;
                }
                else if (value == StringView("back"))
                {
                    out->state.cull = 2;
                }
                else
                {
                    return MsFail(error, line_number, "cull is none, front or back");
                }
                out->state.set_cull = true;
            }
            else if (field == StringView("depth_test"))
            {
                index = 1;
                if (!MsOnOff(value, &out->state.depth_test))
                {
                    return MsFail(error, line_number, "depth_test is on or off");
                }
                out->state.set_depth_test = true;
            }
            else if (field == StringView("depth_write"))
            {
                index = 2;
                if (!MsOnOff(value, &out->state.depth_write))
                {
                    return MsFail(error, line_number, "depth_write is on or off");
                }
                out->state.set_depth_write = true;
            }
            else if (field == StringView("depth_compare"))
            {
                index = 3;
                static const char* const kOps[8] = {"never",   "less",     "equal",        "lessequal",
                                                    "greater", "notequal", "greaterequal", "always"};
                bool found = false;
                for (u32 i = 0; i < 8; ++i)
                {
                    if (value == StringView(kOps[i]))
                    {
                        out->state.depth_compare = static_cast<u8>(i);
                        found = true;
                    }
                }
                if (!found)
                {
                    return MsFail(error, line_number,
                                  "depth_compare is never, less, equal, lessequal, greater, notequal, greaterequal or "
                                  "always");
                }
                out->state.set_depth_compare = true;
            }
            else if (field == StringView("alpha_blend"))
            {
                index = 4;
                if (!MsOnOff(value, &out->state.alpha_blend))
                {
                    return MsFail(error, line_number, "alpha_blend is on or off");
                }
                out->state.set_alpha_blend = true;
            }
            else
            {
                return MsFail(error, line_number,
                              "a state field is cull, depth_test, depth_write, depth_compare or alpha_blend");
            }
            if (seen_state[index])
            {
                return MsFail(error, line_number, "the state field is given twice");
            }
            seen_state[index] = true;
        }
        else
        {
            return MsFail(error, line_number, "unknown record (id, template, option, param, texture, sampler, state)");
        }
    }
    if (!have_id)
    {
        return MsFail(error, 0, "the material source has no id line (asset_mint adopt writes it)");
    }
    if (!have_template)
    {
        return MsFail(error, 0, "the material source has no template line");
    }
    return Status::Success();
}

// "float", "int3", "float4x4" for a block member.
void MsMemberType(const renderer::ReflectedMember& m, char* out, usize capacity)
{
    const char* scalar = m.scalar == renderer::ScalarType::Float32
                             ? "float"
                             : (m.scalar == renderer::ScalarType::Int32 ? "int" : "uint");
    if (m.kind == renderer::MemberKind::Scalar)
    {
        FormatTo(out, capacity, "%s", scalar);
    }
    else if (m.kind == renderer::MemberKind::Vector)
    {
        FormatTo(out, capacity, "%s%u", scalar, static_cast<unsigned>(m.rows));
    }
    else if (m.kind == renderer::MemberKind::Matrix)
    {
        FormatTo(out, capacity, "%s%ux%u", scalar, static_cast<unsigned>(m.rows), static_cast<unsigned>(m.columns));
    }
    else
    {
        FormatTo(out, capacity, "struct");
    }
}

// Whether a source parameter of `type` can fill `m`.
bool MsFits(MaterialParamType type, const renderer::ReflectedMember& m)
{
    if (m.array_count != 0)
    {
        return false;
    }
    using renderer::MemberKind;
    using renderer::ScalarType;
    switch (type)
    {
    case MaterialParamType::Float: return m.kind == MemberKind::Scalar && m.scalar == ScalarType::Float32;
    case MaterialParamType::Int: return m.kind == MemberKind::Scalar && m.scalar == ScalarType::Int32;
    case MaterialParamType::UInt: return m.kind == MemberKind::Scalar && m.scalar == ScalarType::UInt32;
    case MaterialParamType::Vec2: return m.kind == MemberKind::Vector && m.scalar == ScalarType::Float32 && m.rows == 2;
    case MaterialParamType::Vec3:
    case MaterialParamType::Color3:
        return m.kind == MemberKind::Vector && m.scalar == ScalarType::Float32 && m.rows == 3;
    case MaterialParamType::Vec4:
    case MaterialParamType::Color4:
        return m.kind == MemberKind::Vector && m.scalar == ScalarType::Float32 && m.rows == 4;
    case MaterialParamType::Mat4:
        return m.kind == MemberKind::Matrix && m.scalar == ScalarType::Float32 && m.rows == 4 && m.columns == 4;
    }
    return false;
}

// The failure for a parameter the block does not hold or holds with another type: it lists what is expected.
Status MsParamError(MaterialSourceError* error, u32 line, const char* what, const char* name,
                    const renderer::ReflectedBlock* block)
{
    char list[160] = {};
    usize used = 0;
    if (block != nullptr)
    {
        for (u32 i = 0; i < block->member_count && used + 24 < sizeof(list); ++i)
        {
            char type[24];
            MsMemberType(block->members[i], type, sizeof(type));
            used += static_cast<usize>(FormatTo(list + used, sizeof(list) - used, "%s%s (%s)", i == 0 ? "" : ", ",
                                                block->members[i].name, type));
        }
    }
    char message[256];
    FormatTo(message, sizeof(message), "%s '%s'; the template's parameters are: %s", what, name,
             used == 0 ? "none" : list);
    return MsFail(error, line, message);
}

const char* MsShapeName(renderer::SlotShape shape)
{
    return shape == renderer::SlotShape::Texture2D ? "2D" : (shape == renderer::SlotShape::Cube ? "cube" : "array");
}

}  // namespace

Status MintMaterial(StringView text, const MaterialReferenceResolver& resolver, const TypeRegistry& types,
                    Vector<byte>* out, MaterialSourceError* error, u32 target)
{
    *error = MaterialSourceError();
    MsSource source;
    ENGINE_TRY(MsParse(text, &source, error));
    const u32 template_line = source.template_line;
    const renderer::GBufferTemplateInfo* info = renderer::FindGBufferTemplateInfo(source.template_name.text);
    if (info == nullptr)
    {
        char message[160];
        FormatTo(message, sizeof(message),
                 "unknown template '%s' (gbuffer, gbuffer_color, gbuffer_skinned, gbuffer_skinned_color)",
                 source.template_name.text);
        return MsFail(error, template_line, message);
    }
    // The fragment module of the chosen options, and its reflection.
    const char* option_names[kMaterialMaxOptions];
    if (source.options.Size() > kMaterialMaxOptions)
    {
        return MsFail(error, 0, "too many options");
    }
    for (usize i = 0; i < source.options.Size(); ++i)
    {
        option_names[i] = source.options[i].text;
    }
    renderer::PermutationKey key;
    renderer::ReflectionError reflection_error;
    if (!renderer::MakePermutationKey(*info->fragment_permutations, option_names,
                                      static_cast<u32>(source.options.Size()), &key, &reflection_error))
    {
        return MsFail(error, 0, reflection_error.message);
    }
    const renderer::PermutationModule* module = renderer::FindPermutation(*info->fragment_permutations, key,
                                                                          &reflection_error);
    if (module == nullptr)
    {
        return MsFail(error, 0, reflection_error.message);
    }
    renderer::ShaderReflection reflection;
    if (const char* bad = renderer::ParseShaderReflection(module->reflection, module->reflection_size, &reflection))
    {
        return MsFail(error, 0, bad);
    }
    const renderer::ReflectedBlock* block = nullptr;
    for (u32 i = 0; i < reflection.binding_count; ++i)
    {
        const renderer::ReflectedBinding& b = reflection.bindings[i];
        if (b.set == 1 && b.type == rhi::BindingType::UniformBuffer && b.block != renderer::kNoBlock)
        {
            block = &reflection.blocks[b.block];
        }
    }

    MaterialAssetData data;
    data.template_name = source.template_name;
    data.flags = info->skinned ? static_cast<u32>(kMaterialSkinnable) : 0u;
    data.state = source.state;
    for (usize i = 0; i < source.options.Size(); ++i)
    {
        data.options.Push(source.options[i]);
    }
    // The parameter block: zeros, then the template's defaults, then the source's values.
    const u32 block_bytes = block != nullptr ? block->size : 0;
    if (block_bytes > kMaterialMaxParamBytes)
    {
        return MsFail(error, 0, "the template's parameter block is larger than a material may hold");
    }
    if (!data.block.TryResize(block_bytes))
    {
        return Status::Make(ErrorCode::LimitExceeded, "out of memory");
    }
    MemSet(data.block.Data(), 0, block_bytes);
    if (block != nullptr)
    {
        for (u32 d = 0; d < info->default_count; ++d)
        {
            for (u32 i = 0; i < block->member_count; ++i)
            {
                const renderer::ReflectedMember& m = block->members[i];
                if (CStrCompare(m.name, info->defaults[d].name) == 0 && m.size <= 16 && m.array_count == 0)
                {
                    const u32 count = m.kind == renderer::MemberKind::Vector ? m.rows : 1u;
                    for (u32 k = 0; k < count && k < 4; ++k)
                    {
                        MemCopy(data.block.Data() + m.offset + 4 * k, &info->defaults[d].value[k], 4);
                    }
                }
            }
        }
    }
    for (usize i = 0; i < source.params.Size(); ++i)
    {
        const MsParam& p = source.params[i];
        for (usize j = 0; j < i; ++j)
        {
            if (CStrCompare(source.params[j].name.text, p.name.text) == 0)
            {
                return MsFail(error, p.line, "the parameter is set twice");
            }
        }
        const renderer::ReflectedMember* member = nullptr;
        if (block != nullptr)
        {
            for (u32 k = 0; k < block->member_count; ++k)
            {
                if (CStrCompare(block->members[k].name, p.name.text) == 0)
                {
                    member = &block->members[k];
                }
            }
        }
        if (member == nullptr)
        {
            return MsParamError(error, p.line, "unknown parameter", p.name.text, block);
        }
        if (!MsFits(p.type, *member))
        {
            char type[24];
            MsMemberType(*member, type, sizeof(type));
            char what[96];
            FormatTo(what, sizeof(what), "parameter has the wrong type for a %s member", type);
            return MsParamError(error, p.line, what, p.name.text, block);
        }
        MaterialParam record;
        record.name = p.name;
        record.type = p.type;
        record.offset = member->offset;
        record.size = MsValueCount(p.type) * 4;
        for (u32 k = 0; k < MsValueCount(p.type); ++k)
        {
            MemCopy(data.block.Data() + member->offset + 4 * k, &p.words[k], 4);
        }
        data.params.Push(record);
    }
    // The textures: the slot exists in this permutation, accepts the texture's shape and color space.
    for (usize i = 0; i < source.textures.Size(); ++i)
    {
        const MsTexture& tex = source.textures[i];
        for (usize j = 0; j < i; ++j)
        {
            if (CStrCompare(source.textures[j].slot.text, tex.slot.text) == 0)
            {
                return MsFail(error, tex.line, "the texture slot is bound twice");
            }
        }
        bool slot_exists = false;
        for (u32 k = 0; k < reflection.binding_count; ++k)
        {
            const renderer::ReflectedBinding& b = reflection.bindings[k];
            slot_exists = slot_exists || (b.set == 1 && b.type == rhi::BindingType::SampledTexture &&
                                          CStrCompare(b.name, tex.slot.text) == 0);
        }
        const renderer::TemplateTextureSlot* rule = nullptr;
        for (u32 k = 0; k < info->slot_count; ++k)
        {
            if (CStrCompare(info->slots[k].name, tex.slot.text) == 0)
            {
                rule = &info->slots[k];
            }
        }
        if (!slot_exists || rule == nullptr)
        {
            char message[200];
            FormatTo(message, sizeof(message), "the template has no texture slot '%s' with these options",
                     tex.slot.text);
            return MsFail(error, tex.line, message);
        }
        ResourceId id;
        ByteSpan blob;
        const Status resolved = resolver.Resolve(StringView(tex.path.Data(), CStrLength(tex.path.Data())), &id, &blob);
        if (resolved.Failed())
        {
            char message[256];
            FormatTo(message, sizeof(message), "the texture '%s' does not exist in the project", tex.path.Data());
            if (resolved.Code() != ErrorCode::NotFound)
            {
                FormatTo(message, sizeof(message), "the texture '%s' could not be read: %s", tex.path.Data(),
                         resolved.Message());
            }
            return MsFail(error, tex.line, message);
        }
        TextureView view;
        if (view.Open(blob, types).Failed() || view.Id() != id)
        {
            char message[256];
            FormatTo(message, sizeof(message), "'%s' is not a texture blob of the current version", tex.path.Data());
            return MsFail(error, tex.line, message);
        }
        const bool shape_ok = (rule->shape == renderer::SlotShape::Texture2D && view.Kind() == TextureKind::Tex2D) ||
                              (rule->shape == renderer::SlotShape::Cube && view.Kind() == TextureKind::Cube) ||
                              (rule->shape == renderer::SlotShape::Array2D && view.Kind() == TextureKind::Array2D);
        if (!shape_ok)
        {
            char message[256];
            FormatTo(message, sizeof(message), "slot '%s' takes a %s texture; '%s' is %s", tex.slot.text,
                     MsShapeName(rule->shape), tex.path.Data(),
                     view.Kind() == TextureKind::Tex2D ? "2D"
                                                       : (view.Kind() == TextureKind::Cube ? "a cube" : "an array"));
            return MsFail(error, tex.line, message);
        }
        if ((rule->color_space == renderer::SlotColorSpace::Srgb && !view.Srgb()) ||
            (rule->color_space == renderer::SlotColorSpace::Linear && view.Srgb()))
        {
            char message[256];
            FormatTo(message, sizeof(message), "slot '%s' takes %s data; '%s' is stored %s", tex.slot.text,
                     rule->color_space == renderer::SlotColorSpace::Srgb ? "sRGB" : "linear", tex.path.Data(),
                     view.Srgb() ? "sRGB" : "linear");
            return MsFail(error, tex.line, message);
        }
        MaterialTextureBinding binding;
        binding.slot = tex.slot;
        binding.texture = id;
        data.textures.Push(binding);
    }
    for (usize i = 0; i < source.samplers.Size(); ++i)
    {
        const MsSampler& s = source.samplers[i];
        for (usize j = 0; j < i; ++j)
        {
            if (CStrCompare(source.samplers[j].sampler.slot.text, s.sampler.slot.text) == 0)
            {
                return MsFail(error, s.line, "the sampler slot is described twice");
            }
        }
        bool exists = false;
        for (u32 k = 0; k < reflection.binding_count; ++k)
        {
            const renderer::ReflectedBinding& b = reflection.bindings[k];
            exists = exists || (b.set == 1 && b.type == rhi::BindingType::Sampler &&
                                CStrCompare(b.name, s.sampler.slot.text) == 0);
        }
        if (!exists)
        {
            char message[200];
            FormatTo(message, sizeof(message), "the template has no sampler slot '%s'", s.sampler.slot.text);
            return MsFail(error, s.line, message);
        }
        data.samplers.Push(s.sampler);
    }
    const Status built = BuildMaterialBlob(types, source.id, data, out, target);
    if (built.Failed())
    {
        MsSetError(error, 0, built.Message());
    }
    return built;
}

}  // namespace engine_placeholder::asset
