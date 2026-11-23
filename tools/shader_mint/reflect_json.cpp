// This file is the nlohmann/json boundary of shader_mint (PLAN.md assumption #19): the library's API is built on
// std::string and std::vector, so this translation unit is the one place in the tool that uses them. The functions
// declared in reflect_json.h convert to and from the engine's StringView and Text at the edge.
#include "reflect_json.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include <nlohmann/json.hpp>

namespace vse::shader_mint
{

namespace
{

using json = nlohmann::json;
using renderer::kMaxReflectionName;

// --- Safe accessors: a missing or mistyped field gives a default, never an abort ----------------------------------

const json* Field(const json& object, const char* key)
{
    if (!object.is_object())
    {
        return nullptr;
    }
    auto it = object.find(key);
    return it == object.end() ? nullptr : &*it;
}

std::string JsonText(const json& object, const char* key)
{
    const json* value = Field(object, key);
    return value != nullptr && value->is_string() ? value->get<std::string>() : std::string();
}

// Returns the integer at `key`, or `fallback` if absent or not a non-negative integer that fits in 32 bits.
u32 Number(const json& object, const char* key, u32 fallback)
{
    const json* value = Field(object, key);
    if (value == nullptr || !value->is_number_unsigned())
    {
        return fallback;
    }
    const auto n = value->get<unsigned long long>();
    return n > 0xffffffffull ? fallback : static_cast<u32>(n);
}

// Converts a JSON value to u32 if it is a non-negative integer that fits.
bool AsU32(const json& value, u32* out)
{
    if (!value.is_number_unsigned())
    {
        return false;
    }
    const auto n = value.get<unsigned long long>();
    if (n > 0xffffffffull)
    {
        return false;
    }
    *out = static_cast<u32>(n);
    return true;
}

// A layout fact the reflection cannot do without: a missing or mistyped field is an error, never a silent zero.
bool Required(const json& object, const char* key, const char* what, u32* out, std::string* error)
{
    const json* value = Field(object, key);
    if (value == nullptr || !AsU32(*value, out))
    {
        *error = std::string(what) + ": missing or invalid '" + key +
                 "' in the reflection file (did slangc's output format change?)";
        return false;
    }
    return true;
}

bool CopyName(const std::string& name, char* out, const char* what, std::string* error)
{
    if (name.empty() || name.size() >= kMaxReflectionName)
    {
        *error = std::string(what) + " name '" + name + "' is empty or longer than " +
                 std::to_string(kMaxReflectionName - 1) + " characters";
        return false;
    }
    std::memcpy(out, name.c_str(), name.size() + 1);
    return true;
}

std::string Describe(const json& type)
{
    std::string kind = JsonText(type, "kind");
    if (kind == "resource")
    {
        kind += " (" + JsonText(type, "baseShape") + ")";
    }
    return kind;
}

// --- Types ----------------------------------------------------------------------------------------------------------

bool ScalarFrom(const json& type, renderer::ScalarType* out)
{
    const std::string name = JsonText(type, "scalarType");
    if (name == "float32")
    {
        *out = renderer::ScalarType::Float32;
    }
    else if (name == "int32")
    {
        *out = renderer::ScalarType::Int32;
    }
    else if (name == "uint32")
    {
        *out = renderer::ScalarType::UInt32;
    }
    else
    {
        return false;
    }
    return true;
}

// Fills the type fields of `member` from a non-array type.
bool FillMemberType(const json& type, renderer::ReflectedMember* member, std::string* error)
{
    const std::string kind = JsonText(type, "kind");
    const json* element = Field(type, "elementType");
    if (kind == "scalar")
    {
        member->kind = renderer::MemberKind::Scalar;
        member->rows = 1;
        member->columns = 1;
        if (!ScalarFrom(type, &member->scalar))
        {
            *error = "member '" + std::string(member->name) + "' has unsupported scalar type '" +
                     JsonText(type, "scalarType") + "'";
            return false;
        }
        return true;
    }
    if ((kind == "vector" || kind == "matrix") && element != nullptr && ScalarFrom(*element, &member->scalar))
    {
        const u32 rows = kind == "vector" ? Number(type, "elementCount", 0) : Number(type, "rowCount", 0);
        const u32 columns = kind == "vector" ? 1 : Number(type, "columnCount", 0);
        if (rows < 1 || rows > 4 || columns < 1 || columns > 4)
        {
            *error = "member '" + std::string(member->name) + "' has an unsupported vector or matrix shape";
            return false;
        }
        member->kind = kind == "vector" ? renderer::MemberKind::Vector : renderer::MemberKind::Matrix;
        member->rows = static_cast<u8>(rows);
        member->columns = static_cast<u8>(columns);
        return true;
    }
    if (kind == "struct")
    {
        member->kind = renderer::MemberKind::Struct;
        member->rows = 0;
        member->columns = 0;
        return true;
    }
    *error = "member '" + std::string(member->name) + "' has unsupported type " + Describe(type);
    return false;
}

// Fills a block from a struct type: its name, size and members with offsets.
bool FillBlock(const json& struct_type, renderer::ReflectedBlock* block, std::string* error)
{
    if (JsonText(struct_type, "kind") != "struct")
    {
        *error = "a constant buffer must hold a struct, not " + Describe(struct_type);
        return false;
    }
    if (!CopyName(JsonText(struct_type, "name"), block->name, "block", error))
    {
        return false;
    }
    const json* sizes = Field(struct_type, "sizes");
    bool have_size = false;
    if (sizes != nullptr && sizes->is_array())
    {
        for (const json& entry : *sizes)
        {
            if (JsonText(entry, "kind") == "uniform")
            {
                have_size = Required(entry, "value", (std::string("block '") + block->name + "' size").c_str(),
                                     &block->size, error);
                if (!have_size)
                {
                    return false;
                }
            }
        }
    }
    if (!have_size)
    {
        *error = std::string("block '") + block->name + "' has no uniform size in the reflection file";
        return false;
    }
    const json* fields = Field(struct_type, "fields");
    if (fields == nullptr || !fields->is_array())
    {
        *error = std::string("block '") + block->name + "' has no fields";
        return false;
    }
    if (fields->size() > renderer::kMaxBlockMembers)
    {
        *error = std::string("block '") + block->name + "' has more than " +
                 std::to_string(renderer::kMaxBlockMembers) + " members";
        return false;
    }
    for (const json& field : *fields)
    {
        renderer::ReflectedMember& member = block->members[block->member_count];
        if (!CopyName(JsonText(field, "name"), member.name, "member", error))
        {
            return false;
        }
        const json* type = Field(field, "type");
        const json* binding = Field(field, "binding");
        if (type == nullptr || binding == nullptr || JsonText(*binding, "kind") != "uniform")
        {
            *error = std::string("member '") + member.name + "' of block '" + block->name + "' has no uniform layout";
            return false;
        }
        const std::string what = std::string("member '") + member.name + "' of block '" + block->name + "'";
        if (!Required(*binding, "offset", what.c_str(), &member.offset, error) ||
            !Required(*binding, "size", what.c_str(), &member.size, error))
        {
            return false;
        }
        if (JsonText(*type, "kind") == "array")
        {
            const json* element = Field(*type, "elementType");
            if (!Required(*type, "elementCount", what.c_str(), &member.array_count, error) ||
                !Required(*type, "uniformStride", what.c_str(), &member.array_stride, error))
            {
                return false;
            }
            if (element == nullptr || member.array_count == 0 || member.array_stride == 0 ||
                !FillMemberType(*element, &member, error))
            {
                if (error->empty())
                {
                    *error = "array " + what + " is not a sized array of a supported type";
                }
                return false;
            }
        }
        else if (!FillMemberType(*type, &member, error))
        {
            return false;
        }
        ++block->member_count;
    }
    return true;
}

// --- Bindings -------------------------------------------------------------------------------------------------------

struct BindingKind
{
    rhi::BindingType type = rhi::BindingType::UniformBuffer;
    u32 count = 1;
    const json* block_struct = nullptr;  // the struct inside a constant buffer
    u32 stride = 0;                      // a structured buffer's element size
};

bool ClassifyBinding(const json& parameter_type, const std::string& name, BindingKind* out, std::string* error)
{
    const json* type = &parameter_type;
    if (JsonText(*type, "kind") == "array")
    {
        out->count = Number(*type, "elementCount", 0);
        type = Field(*type, "elementType");
        if (type == nullptr || out->count == 0)
        {
            *error = "binding '" + name + "' is an array of unknown size or element type";
            return false;
        }
    }
    const std::string kind = JsonText(*type, "kind");
    if (kind == "constantBuffer")
    {
        out->type = rhi::BindingType::UniformBuffer;
        out->block_struct = Field(*type, "elementType");
        return out->block_struct != nullptr;
    }
    if (kind == "samplerState")
    {
        out->type = rhi::BindingType::Sampler;
        return true;
    }
    if (kind == "resource")
    {
        const std::string shape = JsonText(*type, "baseShape");
        const std::string access = JsonText(*type, "access");
        if (shape == "structuredBuffer")
        {
            // StructuredBuffer (read) is a shader resource view to Direct3D 12, RWStructuredBuffer (readWrite) an
            // unordered access view; both are structured views, made with the element size (Phase 13 Day 166).
            out->type = access.empty() || access == "read" ? rhi::BindingType::ReadOnlyStorageBuffer
                                                           : rhi::BindingType::StorageBuffer;
            const json* element = Field(*type, "resultType");
            const json* sizes = element == nullptr ? nullptr : Field(*element, "sizes");
            if (sizes != nullptr && sizes->is_array())
            {
                for (const json& size : *sizes)
                {
                    if (JsonText(size, "kind") == "uniform")
                    {
                        const u32 value = Number(size, "value", 0);
                        const u32 alignment = Number(size, "alignment", 1);
                        out->stride = alignment == 0 ? 0 : (value + alignment - 1) / alignment * alignment;
                    }
                }
            }
            if (out->stride == 0 || out->stride > 2048)
            {
                *error = "binding '" + name +
                         "' is a structured buffer whose element size is unknown or above 2048 bytes";
                return false;
            }
            return true;
        }
        if (shape == "byteAddressBuffer")
        {
            *error =
                "binding '" + name +
                "' is a ByteAddressBuffer, which needs a raw view the RHI does not make; use StructuredBuffer<uint>";
            return false;
        }
        if (shape == "texture2D" || shape == "textureCube")
        {
            out->type = access.empty() || access == "read" ? rhi::BindingType::SampledTexture
                                                           : rhi::BindingType::StorageTexture;
            return true;
        }
    }
    *error = "binding '" + name + "' has unsupported type " + Describe(*type);
    return false;
}

// --- Vertex inputs --------------------------------------------------------------------------------------------------

bool VertexFormatFrom(const json& type, rhi::VertexFormat* out)
{
    u32 count = 0;
    const json* scalar = &type;
    if (JsonText(type, "kind") == "vector")
    {
        count = Number(type, "elementCount", 0);
        scalar = Field(type, "elementType");
    }
    else if (JsonText(type, "kind") == "scalar")
    {
        count = 1;
    }
    if (scalar == nullptr || count < 1 || count > 4)
    {
        return false;
    }
    const std::string name = JsonText(*scalar, "scalarType");
    if (name == "float32")
    {
        *out = static_cast<rhi::VertexFormat>(static_cast<u32>(rhi::VertexFormat::Float1) + count - 1);
    }
    else if (name == "uint32")
    {
        *out = static_cast<rhi::VertexFormat>(static_cast<u32>(rhi::VertexFormat::UInt1) + count - 1);
    }
    else
    {
        return false;
    }
    return true;
}

bool AddVertexInput(const json& item, renderer::ShaderReflection* out, std::string* error)
{
    const std::string name = JsonText(item, "name");
    const json* binding = Field(item, "binding");
    const json* type = Field(item, "type");
    if (binding == nullptr || JsonText(*binding, "kind") != "varyingInput" || type == nullptr)
    {
        return true;  // a system value (SV_*) or something that is not a location-bound input
    }
    if (out->vertex_input_count == renderer::kMaxReflectedVertexInputs)
    {
        *error = "more than " + std::to_string(renderer::kMaxReflectedVertexInputs) + " vertex inputs";
        return false;
    }
    renderer::ReflectedVertexInput& in = out->vertex_inputs[out->vertex_input_count];
    if (!CopyName(name, in.name, "vertex input", error))
    {
        return false;
    }
    if (!Required(*binding, "index", ("vertex input '" + name + "'").c_str(), &in.location, error))
    {
        return false;
    }
    // Direct3D 12 matches input elements by semantic, the engine's D3D12 backend names location N `TEXCOORD<N>`
    // (shaders/target_common.slang), so every location-bound vertex input must be declared that way.
    const std::string semantic = JsonText(item, "semanticName");
    const u32 semantic_index = Number(item, "semanticIndex", 0);
    if (semantic != "TEXCOORD" || semantic_index != in.location)
    {
        *error = "vertex input '" + name + "' at location " + std::to_string(in.location) + " has the semantic " +
                 (semantic.empty()
                      ? std::string("(none)")
                      : semantic + (semantic_index != 0 ? std::to_string(semantic_index) : std::string())) +
                 "; it must be TEXCOORD" + std::to_string(in.location) +
                 " (the location's number; shaders/target_common.slang)";
        return false;
    }
    if (!VertexFormatFrom(*type, &in.format))
    {
        *error = "vertex input '" + name + "' has an unsupported type " + Describe(*type) +
                 " (use float or uint scalars and vectors)";
        return false;
    }
    ++out->vertex_input_count;
    return true;
}

}  // namespace

static bool ReflectionFromJsonImpl(const std::string& json_text, renderer::ShaderReflection* out, std::string* error)
{
    *out = renderer::ShaderReflection{};
    error->clear();
    const json root = json::parse(json_text, nullptr, /*allow_exceptions=*/false);
    if (root.is_discarded() || !root.is_object())
    {
        *error = "the reflection file is not valid JSON";
        return false;
    }
    const json* entries = Field(root, "entryPoints");
    const json* parameters = Field(root, "parameters");
    if (entries == nullptr || !entries->is_array() || entries->size() != 1 || parameters == nullptr ||
        !parameters->is_array())
    {
        *error = "expected exactly one entry point and a parameter list";
        return false;
    }
    const json& entry = (*entries)[0];
    if (!CopyName(JsonText(entry, "name"), out->entry, "entry point", error))
    {
        return false;
    }
    const std::string stage = JsonText(entry, "stage");
    if (stage == "vertex")
    {
        out->stage = rhi::ShaderStage::Vertex;
    }
    else if (stage == "fragment")
    {
        out->stage = rhi::ShaderStage::Fragment;
    }
    else if (stage == "compute")
    {
        out->stage = rhi::ShaderStage::Compute;
    }
    else
    {
        *error = "unsupported stage '" + stage + "'";
        return false;
    }
    if (out->stage == rhi::ShaderStage::Compute)
    {
        const json* size = Field(entry, "threadGroupSize");
        if (size == nullptr || !size->is_array() || size->size() != 3)
        {
            *error = "a compute shader without threadGroupSize";
            return false;
        }
        for (u32 i = 0; i < 3; ++i)
        {
            if (!AsU32((*size)[i], &out->local_size[i]))
            {
                *error = "threadGroupSize holds something that is not a 32-bit unsigned integer";
                return false;
            }
        }
    }

    // The bindings this entry point uses.
    const json* used = Field(entry, "bindings");
    if (used != nullptr && used->is_array())
    {
        for (const json& use : *used)
        {
            const std::string name = JsonText(use, "name");
            const json* use_binding = Field(use, "binding");
            bool use_flag = use_binding != nullptr && Number(*use_binding, "used", 1) != 0;
            // A push constant block that also names its D3D12 register (`register(b0, space15)`,
            // shaders/target_common.slang) is reported with two bindings, the push constant range and that register;
            // the SPIR-V is the push constant range alone.
            const json* several = Field(use, "bindings");
            if (use_binding == nullptr && several != nullptr && several->is_array())
            {
                for (const json& candidate : *several)
                {
                    use_flag = use_flag || Number(candidate, "used", 1) != 0;
                    if (JsonText(candidate, "kind") == "pushConstantBuffer")
                    {
                        use_binding = &candidate;
                    }
                }
            }
            if (use_binding == nullptr || !use_flag)
            {
                continue;
            }
            const json* parameter = nullptr;
            for (const json& p : *parameters)
            {
                if (JsonText(p, "name") == name)
                {
                    parameter = &p;
                }
            }
            const json* type = parameter == nullptr ? nullptr : Field(*parameter, "type");
            if (type == nullptr)
            {
                *error = "binding '" + name + "' has no matching parameter";
                return false;
            }
            BindingKind kind;
            if (!ClassifyBinding(*type, name, &kind, error))
            {
                return false;
            }
            const std::string slot_kind = JsonText(*use_binding, "kind");
            if (slot_kind == "pushConstantBuffer")
            {
                if (out->has_push_constants || out->block_count == renderer::kMaxReflectedBlocks ||
                    kind.block_struct == nullptr)
                {
                    *error = "push constant block '" + name +
                             "' is a duplicate, too many blocks, or not a constant buffer";
                    return false;
                }
                if (!FillBlock(*kind.block_struct, &out->blocks[out->block_count], error))
                {
                    return false;
                }
                out->has_push_constants = true;
                out->push_constant_block = out->block_count++;
                continue;
            }
            if (slot_kind != "descriptorTableSlot")
            {
                *error = "binding '" + name + "' has unsupported slot kind '" + slot_kind + "'";
                return false;
            }
            if (out->binding_count == renderer::kMaxReflectedBindings)
            {
                *error = "more than " + std::to_string(renderer::kMaxReflectedBindings) + " bindings";
                return false;
            }
            renderer::ReflectedBinding& b = out->bindings[out->binding_count];
            if (!CopyName(name, b.name, "binding", error))
            {
                return false;
            }
            // "space" is omitted for set 0 but must be an integer when present.
            if (Field(*use_binding, "space") != nullptr &&
                !Required(*use_binding, "space", ("binding '" + name + "'").c_str(), &b.set, error))
            {
                return false;
            }
            if (!Required(*use_binding, "index", ("binding '" + name + "'").c_str(), &b.binding, error))
            {
                return false;
            }
            b.type = kind.type;
            b.count = kind.count;
            b.stride = kind.stride;
            b.stages = out->stage;
            if (kind.block_struct != nullptr)
            {
                if (out->block_count == renderer::kMaxReflectedBlocks)
                {
                    *error = "more than " + std::to_string(renderer::kMaxReflectedBlocks) + " blocks";
                    return false;
                }
                if (!FillBlock(*kind.block_struct, &out->blocks[out->block_count], error))
                {
                    return false;
                }
                b.block = out->block_count++;
            }
            ++out->binding_count;
        }
    }

    if (out->stage == rhi::ShaderStage::Vertex)
    {
        const json* entry_parameters = Field(entry, "parameters");
        if (entry_parameters != nullptr && entry_parameters->is_array())
        {
            for (const json& p : *entry_parameters)
            {
                const json* type = Field(p, "type");
                const json* fields = type == nullptr ? nullptr : Field(*type, "fields");
                if (fields != nullptr && fields->is_array())
                {
                    for (const json& field : *fields)
                    {
                        if (!AddVertexInput(field, out, error))
                        {
                            return false;
                        }
                    }
                }
                else if (!AddVertexInput(p, out, error))
                {
                    return false;
                }
            }
        }
        std::sort(out->vertex_inputs, out->vertex_inputs + out->vertex_input_count,
                  [](const renderer::ReflectedVertexInput& a, const renderer::ReflectedVertexInput& b)
                  { return a.location < b.location; });
    }
    // Bindings in (set, binding) order so the blob does not depend on declaration order.
    std::sort(out->bindings, out->bindings + out->binding_count,
              [](const renderer::ReflectedBinding& a, const renderer::ReflectedBinding& b)
              { return a.set != b.set ? a.set < b.set : a.binding < b.binding; });
    // Sorting moved the bindings, so block indices still point at the right blocks (blocks did not move).
    if (const char* problem = renderer::ValidateShaderReflection(*out))
    {
        *error = problem;
        return false;
    }
    return true;
}

static bool CrossCheckSpirvImpl(const renderer::ShaderReflection& reflection, const u32* words, usize word_count,
                                std::string* error)
{
    constexpr u32 kOpExecutionMode = 16;
    constexpr u32 kOpVariable = 59;
    constexpr u32 kStorageInput = 1;
    constexpr u32 kOpDecorate = 71;
    constexpr u32 kLocalSize = 17;
    constexpr u32 kLocation = 30;
    constexpr u32 kBinding = 33;
    constexpr u32 kDescriptorSet = 34;

    std::vector<std::pair<u32, u32>> sets;
    std::vector<std::pair<u32, u32>> binds;
    std::vector<std::pair<u32, u32>> located;  // (id, Location)
    std::vector<u32> input_ids;                // ids of variables in the Input storage class
    u32 local_size[3] = {0, 0, 0};
    for (usize i = 5; i < word_count;)
    {
        const u32 op = words[i] & 0xffffu;
        const u32 length = words[i] >> 16;
        if (length == 0 || i + length > word_count)
        {
            *error = "the SPIR-V instruction stream is malformed";
            return false;
        }
        if (op == kOpExecutionMode && length >= 6 && words[i + 2] == kLocalSize)
        {
            local_size[0] = words[i + 3];
            local_size[1] = words[i + 4];
            local_size[2] = words[i + 5];
        }
        else if (op == kOpVariable && length >= 4)
        {
            if (words[i + 3] == kStorageInput)
            {
                input_ids.push_back(words[i + 2]);
            }
        }
        else if (op == kOpDecorate && length >= 4)
        {
            if (words[i + 2] == kLocation)
            {
                located.emplace_back(words[i + 1], words[i + 3]);
            }
            else if (words[i + 2] == kDescriptorSet)
            {
                sets.emplace_back(words[i + 1], words[i + 3]);
            }
            else if (words[i + 2] == kBinding)
            {
                binds.emplace_back(words[i + 1], words[i + 3]);
            }
        }
        i += length;
    }
    std::vector<std::pair<u32, u32>> found;
    for (const auto& b : binds)
    {
        for (const auto& s : sets)
        {
            if (s.first == b.first)
            {
                found.emplace_back(s.second, b.second);
            }
        }
    }
    std::vector<std::pair<u32, u32>> expected;
    for (u32 i = 0; i < reflection.binding_count; ++i)
    {
        expected.emplace_back(reflection.bindings[i].set, reflection.bindings[i].binding);
    }
    std::sort(found.begin(), found.end());
    std::sort(expected.begin(), expected.end());
    if (found != expected)
    {
        *error = "the reflected (set, binding) pairs differ from the SPIR-V descriptor decorations";
        return false;
    }
    for (u32 i = 0; i < 3; ++i)
    {
        if (local_size[i] != reflection.local_size[i])
        {
            *error = "the reflected compute local size differs from the SPIR-V LocalSize";
            return false;
        }
    }
    if (reflection.stage == rhi::ShaderStage::Vertex)
    {
        // The vertex shader's input locations are the Location decorations of Input variables; they must be exactly the
        // reflected ones (outputs and other stages' inputs do not count).
        std::vector<u32> actual;
        for (const auto& l : located)
        {
            if (std::find(input_ids.begin(), input_ids.end(), l.first) != input_ids.end())
            {
                actual.push_back(l.second);
            }
        }
        std::vector<u32> reflected;
        for (u32 i = 0; i < reflection.vertex_input_count; ++i)
        {
            reflected.push_back(reflection.vertex_inputs[i].location);
        }
        std::sort(actual.begin(), actual.end());
        std::sort(reflected.begin(), reflected.end());
        if (actual != reflected)
        {
            *error =
                "the reflected vertex input locations differ from the SPIR-V Input variables' Location decorations";
            return false;
        }
    }
    return true;
}

static const char* ExpectedD3D12Kind(rhi::BindingType type)
{
    switch (type)
    {
    case rhi::BindingType::UniformBuffer: return "constantBuffer";
    case rhi::BindingType::SampledTexture:
    case rhi::BindingType::ReadOnlyStorageBuffer: return "shaderResource";
    case rhi::BindingType::StorageBuffer:
    case rhi::BindingType::StorageTexture: return "unorderedAccess";
    case rhi::BindingType::Sampler: return "samplerState";
    case rhi::BindingType::Count: break;
    }
    return "";
}

static bool CheckD3D12RegistersImpl(const renderer::ShaderReflection& reflection, const std::string& dxil_json,
                                    std::string* error)
{
    const json root = json::parse(dxil_json, nullptr, /*allow_exceptions=*/false);
    const json* parameters = root.is_object() ? Field(root, "parameters") : nullptr;
    if (parameters == nullptr || !parameters->is_array())
    {
        *error = "the DXIL reflection file is not valid JSON or has no parameter list";
        return false;
    }
    bool push_seen = false;
    for (u32 i = 0; i < reflection.binding_count; ++i)
    {
        const renderer::ReflectedBinding& b = reflection.bindings[i];
        const json* found = nullptr;
        for (const json& p : *parameters)
        {
            if (JsonText(p, "name") == b.name)
            {
                found = &p;
            }
        }
        const json* binding = found == nullptr ? nullptr : Field(*found, "binding");
        if (binding == nullptr)
        {
            *error = std::string("binding '") + b.name + "' is missing from the DXIL build's reflection";
            return false;
        }
        const std::string kind = JsonText(*binding, "kind");
        const u32 index = Number(*binding, "index", 0xffffffffu);
        const u32 space = Number(*binding, "space", 0);
        const std::string want = ExpectedD3D12Kind(b.type);
        if (kind != want || index != b.binding || space != b.set)
        {
            *error = std::string("binding '") + b.name + "' (set " + std::to_string(b.set) + ", binding " +
                     std::to_string(b.binding) + ") is " + kind + " register " + std::to_string(index) + " in space " +
                     std::to_string(space) + " in the DXIL build; it must be " + want + " register " +
                     std::to_string(b.binding) + " in space " + std::to_string(b.set) +
                     " (declare it with ENGINE_REGISTER(...); shaders/target_common.slang)";
            return false;
        }
    }
    for (const json& p : *parameters)
    {
        const json* binding = Field(p, "binding");
        if (binding != nullptr && JsonText(*binding, "kind") == "constantBuffer" && Number(*binding, "space", 0) == 15)
        {
            if (Number(*binding, "index", 0xffffffffu) != 0)
            {
                *error = "the push constant block '" + JsonText(p, "name") + "' must be register b0 in space 15";
                return false;
            }
            push_seen = true;
        }
    }
    if (reflection.has_push_constants && !push_seen)
    {
        *error =
            "the push constant block is not register b0 in space 15 in the DXIL build (declare it with ENGINE_REGISTER(b0, space15))";
        return false;
    }
    return true;
}

// The uniform offsets of every field under a parameter's type, by path (`block.field.inner`): a constant buffer's
// element, a structured buffer's result, nested structs and arrays of structs (Phase 21 Day 309).
static void CollectUniformOffsets(const json& type, const std::string& prefix,
                                  std::vector<std::pair<std::string, u32>>* out)
{
    const json* element = &type;
    const std::string kind = JsonText(type, "kind");
    if (kind == "constantBuffer" || kind == "parameterBlock" || kind == "array")
    {
        element = Field(type, "elementType");
    }
    else if (kind == "resource")
    {
        element = Field(type, "resultType");
    }
    if (element == nullptr || element == &type)
    {
        element = &type;
    }
    if (JsonText(*element, "kind") == "array")
    {
        CollectUniformOffsets(*element, prefix + "[]", out);
        return;
    }
    const json* fields = JsonText(*element, "kind") == "struct" ? Field(*element, "fields") : nullptr;
    if (fields == nullptr || !fields->is_array())
    {
        return;
    }
    for (const json& f : *fields)
    {
        const std::string path = prefix + "." + JsonText(f, "name");
        const json* binding = Field(f, "binding");
        if (binding != nullptr && JsonText(*binding, "kind") == "uniform")
        {
            out->push_back({path, Number(*binding, "offset", ~0u)});
        }
        if (const json* inner = Field(f, "type"))
        {
            CollectUniformOffsets(*inner, path, out);
        }
    }
}

// The Metal build's slots (Phase 21 Day 299): each binding of the SPIR-V reflection, matched by name, gets the index
// slangc gave it in the Metal build, in the argument table its type implies (a buffer is a "constantBuffer", a texture
// a "shaderResource" or an "unorderedAccess", a sampler a "samplerState"); the push-constant block (named in the SPIR-V
// build's reflection, kind "pushConstantBuffer") gets its buffer slot. Every Metal parameter must be one the SPIR-V
// build declares, and every block field must sit at the same offset in both builds (Day 309).
static bool ApplyMetalSlotsImpl(renderer::ShaderReflection* out, const std::string& spirv_json,
                                const std::string& metal_json, std::string* error)
{
    // Filled in a copy, which replaces `out` only when every slot was found and the result validates.
    renderer::ShaderReflection copy = *out;
    renderer::ShaderReflection* reflection = &copy;
    const json spirv = json::parse(spirv_json, nullptr, /*allow_exceptions=*/false);
    const json metal = json::parse(metal_json, nullptr, /*allow_exceptions=*/false);
    const json* spirv_parameters = spirv.is_object() ? Field(spirv, "parameters") : nullptr;
    const json* parameters = metal.is_object() ? Field(metal, "parameters") : nullptr;
    if (spirv_parameters == nullptr || !spirv_parameters->is_array() || parameters == nullptr ||
        !parameters->is_array())
    {
        *error = "the SPIR-V or Metal reflection file is not valid JSON or has no parameter list";
        return false;
    }
    std::string push_name;
    for (const json& p : *spirv_parameters)
    {
        const json* binding = Field(p, "binding");
        if (binding != nullptr && JsonText(*binding, "kind") == "pushConstantBuffer")
        {
            push_name = JsonText(p, "name");
        }
        // A block that also names its D3D12 register is reported with both bindings (ReflectionFromJson reads it so).
        const json* several = Field(p, "bindings");
        if (binding == nullptr && several != nullptr && several->is_array())
        {
            for (const json& candidate : *several)
            {
                if (JsonText(candidate, "kind") == "pushConstantBuffer")
                {
                    push_name = JsonText(p, "name");
                }
            }
        }
    }
    for (u32 i = 0; i < reflection->binding_count; ++i)
    {
        renderer::ReflectedBinding& b = reflection->bindings[i];
        const json* found = nullptr;
        for (const json& p : *parameters)
        {
            if (JsonText(p, "name") == b.name)
            {
                found = &p;
            }
        }
        const json* binding = found == nullptr ? nullptr : Field(*found, "binding");
        if (binding == nullptr)
        {
            *error = std::string("binding '") + b.name + "' is missing from the Metal build's reflection";
            return false;
        }
        const std::string kind = JsonText(*binding, "kind");
        const bool texture = b.type == rhi::BindingType::SampledTexture || b.type == rhi::BindingType::StorageTexture;
        const bool sampler = b.type == rhi::BindingType::Sampler;
        const bool kind_ok = texture   ? (kind == "shaderResource" || kind == "unorderedAccess")
                             : sampler ? kind == "samplerState"
                                       : kind == "constantBuffer";
        const u32 index = Number(*binding, "index", renderer::kNoMetalSlot);
        if (!kind_ok || index == renderer::kNoMetalSlot)
        {
            *error = std::string("binding '") + b.name + "' is a '" + kind +
                     "' without a usable index in the Metal build, " + "not the " +
                     (texture   ? "texture"
                      : sampler ? "sampler"
                                : "buffer") +
                     " its type needs";
            return false;
        }
        b.metal_slot = index;
    }
    if (reflection->has_push_constants)
    {
        const json* found = nullptr;
        for (const json& p : *parameters)
        {
            if (!push_name.empty() && JsonText(p, "name") == push_name)
            {
                found = &p;
            }
        }
        const json* binding = found == nullptr ? nullptr : Field(*found, "binding");
        if (binding == nullptr || JsonText(*binding, "kind") != "constantBuffer" ||
            Number(*binding, "index", renderer::kNoMetalSlot) == renderer::kNoMetalSlot)
        {
            *error = "the push constant block '" + push_name + "' has no buffer slot in the Metal build's reflection";
            return false;
        }
        reflection->push_constant_metal_slot = Number(*binding, "index", renderer::kNoMetalSlot);
    }
    // A Metal parameter not in the reflection must be one the SPIR-V build declares too (a resource the module does
    // not use, which the reflection leaves out: gbuffer.frag's normal map in the key without NORMAL_MAP); anything
    // else is a resource only the Metal build has.
    for (const json& p : *parameters)
    {
        const std::string name = JsonText(p, "name");
        bool declared = false;
        for (const json& q : *spirv_parameters)
        {
            declared = declared || JsonText(q, "name") == name;
        }
        if (!declared)
        {
            *error = "the Metal build has a parameter '" + name + "' the SPIR-V build does not declare";
            return false;
        }
    }
    // The CPU writes a block at the SPIR-V build's (std140) offsets, so the Metal build must lay it out the same:
    // Metal's float3 is 16 bytes, so a scalar right after one moves (material_test.frag's roughness was at 28 against
    // 32).
    for (const json& q : *spirv_parameters)
    {
        const json* spirv_type = Field(q, "type");
        const json* metal_type = nullptr;
        for (const json& p : *parameters)
        {
            if (JsonText(p, "name") == JsonText(q, "name"))
            {
                metal_type = Field(p, "type");
            }
        }
        if (spirv_type == nullptr || metal_type == nullptr)
        {
            continue;
        }
        std::vector<std::pair<std::string, u32>> spirv_offsets;
        std::vector<std::pair<std::string, u32>> metal_offsets;
        CollectUniformOffsets(*spirv_type, JsonText(q, "name"), &spirv_offsets);
        CollectUniformOffsets(*metal_type, JsonText(q, "name"), &metal_offsets);
        for (const auto& a : spirv_offsets)
        {
            for (const auto& b : metal_offsets)
            {
                if (a.first == b.first && a.second != b.second)
                {
                    *error = "'" + a.first + "' is at offset " + std::to_string(a.second) +
                             " in the SPIR-V build but " + std::to_string(b.second) +
                             " in the Metal build (a float3 takes 16 bytes in Metal: put nothing after one in its 16)";
                    return false;
                }
            }
        }
    }
    reflection->has_metal_slots = true;
    if (const char* problem = renderer::ValidateShaderReflection(*reflection))
    {
        *error = std::string("the Metal slots: ") + problem;
        return false;
    }
    *out = copy;
    return true;
}

bool ApplyMetalSlots(renderer::ShaderReflection* reflection, StringView spirv_json, StringView metal_json, Text* error)
{
    std::string message;
    const bool ok = ApplyMetalSlotsImpl(reflection, std::string(spirv_json.Data(), spirv_json.Length()),
                                        std::string(metal_json.Data(), metal_json.Length()), &message);
    *error = MakeText(StringView(message.data(), message.size()));
    return ok;
}

bool CheckD3D12Registers(const renderer::ShaderReflection& reflection, StringView dxil_json, Text* error)
{
    std::string message;
    const bool ok = CheckD3D12RegistersImpl(reflection, std::string(dxil_json.Data(), dxil_json.Length()), &message);
    *error = MakeText(StringView(message.data(), message.size()));
    return ok;
}

bool ReflectionFromJson(StringView json_text, renderer::ShaderReflection* out, Text* error)
{
    std::string message;
    const bool ok = ReflectionFromJsonImpl(std::string(json_text.Data(), json_text.Length()), out, &message);
    *error = MakeText(StringView(message.data(), message.size()));
    return ok;
}

bool CrossCheckSpirv(const renderer::ShaderReflection& reflection, const u32* words, usize word_count, Text* error)
{
    std::string message;
    const bool ok = CrossCheckSpirvImpl(reflection, words, word_count, &message);
    *error = MakeText(StringView(message.data(), message.size()));
    return ok;
}

}  // namespace vse::shader_mint
