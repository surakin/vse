#include "renderer/shader_reflection.h"

#include "cstr/varargs.h"
#include "cstr/format.h"
#include "cstr/cstr.h"

#include "debug/compiler.h"
#include "cstr/mem_ops.h"

namespace engine_placeholder::renderer
{

namespace
{

constexpr u32 kBlobMagic = 0x4c465253u;  // "SRFL" read as little-endian bytes

// --- Names ---------------------------------------------------------------------------------------------------------

// A name is non-empty, nul-terminated inside its array, and made of identifier characters only.
bool IsTerminated(const char* name)
{
    for (u32 i = 0; i < kMaxReflectionName; ++i)
    {
        const char c = name[i];
        if (c == '\0')
        {
            return i > 0;
        }
        const bool identifier = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        if (!identifier)
        {
            return false;
        }
    }
    return false;
}

const char* BindingTypeName(rhi::BindingType type)
{
    switch (type)
    {
    case rhi::BindingType::UniformBuffer: return "uniform buffer";
    case rhi::BindingType::StorageBuffer: return "storage buffer";
    case rhi::BindingType::SampledTexture: return "sampled texture";
    case rhi::BindingType::Sampler: return "sampler";
    case rhi::BindingType::StorageTexture: return "storage texture";
    case rhi::BindingType::ReadOnlyStorageBuffer: return "read-only storage buffer";
    default: return "unknown";
    }
}

const char* StageName(rhi::ShaderStage stage)
{
    switch (stage)
    {
    case rhi::ShaderStage::Vertex: return "vertex";
    case rhi::ShaderStage::Fragment: return "fragment";
    case rhi::ShaderStage::Compute: return "compute";
    default: return "unknown";
    }
}

bool IsSingleStage(rhi::ShaderStage stage)
{
    return stage == rhi::ShaderStage::Vertex || stage == rhi::ShaderStage::Fragment ||
           stage == rhi::ShaderStage::Compute;
}

void SetError(ReflectionError* error, const char* format, ...) ENGINE_PRINTF_FORMAT(2, 3);

void SetError(ReflectionError* error, const char* format, ...)
{
    if (error == nullptr)
    {
        return;
    }
    VarArgs args;
    ENGINE_VA_START(args, format);
    VFormatTo(error->message, kReflectionErrorLength, format, args);
    ENGINE_VA_END(args);
}

// --- Blob writer and reader ----------------------------------------------------------------------------------------

// Counts every byte; stores them only if there is an output buffer and it is large enough.
class Writer
{
public:
    Writer(u8* out, usize capacity) : out_(out), capacity_(capacity) {}

    void U8(u32 value) { Put(value & 0xffu); }
    void U32(u32 value)
    {
        for (u32 shift = 0; shift < 32; shift += 8)
        {
            Put((value >> shift) & 0xffu);
        }
    }
    void String(const char* text)
    {
        const usize length = CStrLength(text);
        U8(static_cast<u32>(length));
        for (usize i = 0; i < length; ++i)
        {
            Put(static_cast<u8>(text[i]));
        }
    }
    usize Size() const { return size_; }
    bool Fits() const { return out_ == nullptr || size_ <= capacity_; }

private:
    void Put(u32 byte)
    {
        if (out_ != nullptr && size_ < capacity_)
        {
            out_[size_] = static_cast<u8>(byte);
        }
        ++size_;
    }

    u8* out_;
    usize capacity_;
    usize size_ = 0;
};

// Bounds-checked: once a read would pass the end, every later read returns 0 and Failed() stays true.
class Reader
{
public:
    Reader(const u8* data, usize size) : data_(data), size_(size) {}

    u32 U8()
    {
        if (!Has(1))
        {
            return 0;
        }
        return data_[pos_++];
    }
    u32 U32()
    {
        if (!Has(4))
        {
            return 0;
        }
        const u32 value = static_cast<u32>(data_[pos_]) | (static_cast<u32>(data_[pos_ + 1]) << 8) |
                          (static_cast<u32>(data_[pos_ + 2]) << 16) | (static_cast<u32>(data_[pos_ + 3]) << 24);
        pos_ += 4;
        return value;
    }
    // Reads a length-prefixed name into `out` (capacity kMaxReflectionName). Fails if it does not fit with a
    // terminator, is empty, or contains anything but identifier characters (so an embedded NUL cannot make two blobs
    // mean the same).
    bool String(char* out)
    {
        const u32 length = U8();
        if (failed_ || length == 0 || length >= kMaxReflectionName || !Has(length))
        {
            failed_ = true;
            return false;
        }
        for (u32 i = 0; i < length; ++i)
        {
            const u8 c = data_[pos_ + i];
            const bool identifier = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                                    c == '_';
            if (!identifier)
            {
                failed_ = true;
                return false;
            }
        }
        MemCopy(out, data_ + pos_, length);
        out[length] = '\0';
        pos_ += length;
        return true;
    }
    bool Failed() const { return failed_; }
    bool AtEnd() const { return pos_ == size_; }

private:
    bool Has(usize count)
    {
        if (failed_ || count > size_ - pos_)
        {
            failed_ = true;
            return false;
        }
        return true;
    }

    const u8* data_;
    usize size_;
    usize pos_ = 0;
    bool failed_ = false;
};

void WriteReflection(Writer& w, const ShaderReflection& r)
{
    w.U32(kBlobMagic);
    w.U32(kReflectionVersion);
    w.U32(static_cast<u32>(r.stage));
    w.String(r.entry);
    for (u32 i = 0; i < 3; ++i)
    {
        w.U32(r.local_size[i]);
    }
    w.U32(r.binding_count);
    for (u32 i = 0; i < r.binding_count; ++i)
    {
        const ReflectedBinding& b = r.bindings[i];
        w.String(b.name);
        w.U32(b.set);
        w.U32(b.binding);
        w.U8(static_cast<u32>(b.type));
        w.U32(b.count);
        w.U32(static_cast<u32>(b.stages));
        w.U32(b.block);
        w.U32(b.stride);
        w.U32(b.metal_slot);
    }
    w.U32(r.block_count);
    for (u32 i = 0; i < r.block_count; ++i)
    {
        const ReflectedBlock& block = r.blocks[i];
        w.String(block.name);
        w.U32(block.size);
        w.U32(block.member_count);
        for (u32 m = 0; m < block.member_count; ++m)
        {
            const ReflectedMember& member = block.members[m];
            w.String(member.name);
            w.U32(member.offset);
            w.U32(member.size);
            w.U8(static_cast<u32>(member.kind));
            w.U8(static_cast<u32>(member.scalar));
            w.U8(member.rows);
            w.U8(member.columns);
            w.U32(member.array_count);
            w.U32(member.array_stride);
        }
    }
    w.U8(r.has_push_constants ? 1 : 0);
    w.U32(r.push_constant_block);
    w.U8(r.has_metal_slots ? 1 : 0);
    w.U32(r.push_constant_metal_slot);
    w.U32(r.vertex_input_count);
    for (u32 i = 0; i < r.vertex_input_count; ++i)
    {
        const ReflectedVertexInput& in = r.vertex_inputs[i];
        w.String(in.name);
        w.U32(in.location);
        w.U8(static_cast<u32>(in.format));
    }
}

}  // namespace

// --- Validation ----------------------------------------------------------------------------------------------------

namespace
{

// The Metal argument table a binding type uses: 0 buffers, 1 textures, 2 samplers.
u32 MetalClass(rhi::BindingType type)
{
    switch (type)
    {
    case rhi::BindingType::SampledTexture:
    case rhi::BindingType::StorageTexture: return 1;
    case rhi::BindingType::Sampler: return 2;
    default: return 0;
    }
}

// Every binding (and the push-constant block) has a slot when the Metal slots were minted, none otherwise; two
// resources of one argument table never share a slot (the Phase 26 Day 298 failure); a slot is below Metal's 31
// buffers, 128 textures and 16 samplers.
const char* ValidateMetalSlots(const ShaderReflection& r)
{
    if (!r.has_metal_slots)
    {
        for (u32 i = 0; i < r.binding_count; ++i)
        {
            if (r.bindings[i].metal_slot != kNoMetalSlot)
            {
                return "reflection: a binding has a Metal slot, but the module has no Metal slots";
            }
        }
        return r.push_constant_metal_slot == kNoMetalSlot
                   ? nullptr
                   : "reflection: the push-constant block has a Metal slot, but the module has no Metal slots";
    }
    constexpr u32 kLimits[3] = {31, 128, 16};
    for (u32 i = 0; i < r.binding_count; ++i)
    {
        const ReflectedBinding& b = r.bindings[i];
        const u32 cls = MetalClass(b.type);
        // An array takes `count` consecutive slots (the end-of-phase review: ranges, not only the first slot, must not
        // meet, or the pipeline is refused at run time instead of at mint time).
        const u32 count = b.count == 0 ? 1 : b.count;
        if (b.metal_slot == kNoMetalSlot || b.metal_slot >= kLimits[cls] || count > kLimits[cls] - b.metal_slot)
        {
            return "reflection: a binding has no Metal slot, or one past Metal's argument table";
        }
        for (u32 j = 0; j < i; ++j)
        {
            const ReflectedBinding& o = r.bindings[j];
            const u32 other_count = o.count == 0 ? 1 : o.count;
            if (MetalClass(o.type) == cls && o.metal_slot < b.metal_slot + count &&
                b.metal_slot < o.metal_slot + other_count)
            {
                return "reflection: two bindings share a Metal slot";
            }
        }
        if (r.has_push_constants && cls == 0 && r.push_constant_metal_slot >= b.metal_slot &&
            r.push_constant_metal_slot < b.metal_slot + count)
        {
            return "reflection: a buffer binding shares the push-constant block's Metal slot";
        }
    }
    if (r.has_push_constants ? r.push_constant_metal_slot >= kLimits[0] : r.push_constant_metal_slot != kNoMetalSlot)
    {
        return "reflection: the push-constant block's Metal slot is missing or out of range, or set without one";
    }
    return nullptr;
}

}  // namespace

const char* ValidateShaderReflection(const ShaderReflection& r)
{
    if (!IsSingleStage(r.stage))
    {
        return "reflection: the stage must be exactly one of vertex, fragment or compute";
    }
    if (!IsTerminated(r.entry))
    {
        return "reflection: the entry point name is empty or not terminated";
    }
    const bool compute = r.stage == rhi::ShaderStage::Compute;
    for (u32 i = 0; i < 3; ++i)
    {
        if ((r.local_size[i] != 0) != compute)
        {
            return compute ? "reflection: a compute shader needs a non-zero local size in every dimension"
                           : "reflection: only a compute shader has a local size";
        }
    }
    if (r.binding_count > kMaxReflectedBindings || r.block_count > kMaxReflectedBlocks ||
        r.vertex_input_count > kMaxReflectedVertexInputs)
    {
        return "reflection: a count is above the fixed capacity";
    }
    if (r.vertex_input_count != 0 && r.stage != rhi::ShaderStage::Vertex)
    {
        return "reflection: only a vertex shader has vertex inputs";
    }
    for (u32 i = 0; i < r.block_count; ++i)
    {
        const ReflectedBlock& block = r.blocks[i];
        if (!IsTerminated(block.name) || block.member_count > kMaxBlockMembers)
        {
            return "reflection: a block name is not terminated or it has too many members";
        }
        for (u32 j = 0; j < i; ++j)
        {
            if (CStrCompareN(r.blocks[j].name, block.name, kMaxReflectionName) == 0)
            {
                return "reflection: two blocks have the same name";
            }
        }
        for (u32 m = 0; m < block.member_count; ++m)
        {
            const ReflectedMember& member = block.members[m];
            if (!IsTerminated(member.name))
            {
                return "reflection: a member name is not terminated";
            }
            for (u32 j = 0; j < m; ++j)
            {
                if (CStrCompareN(block.members[j].name, member.name, kMaxReflectionName) == 0)
                {
                    return "reflection: two members of a block have the same name";
                }
            }
            if (static_cast<u32>(member.kind) >= static_cast<u32>(MemberKind::Count) ||
                static_cast<u32>(member.scalar) >= static_cast<u32>(ScalarType::Count))
            {
                return "reflection: a member has an unknown kind or scalar type";
            }
            const u32 rows = member.rows;
            const u32 columns = member.columns;
            bool shape_ok = false;
            switch (member.kind)
            {
            case MemberKind::Scalar: shape_ok = rows == 1 && columns == 1; break;
            case MemberKind::Vector: shape_ok = rows >= 1 && rows <= 4 && columns == 1; break;
            case MemberKind::Matrix: shape_ok = rows >= 1 && rows <= 4 && columns >= 1 && columns <= 4; break;
            case MemberKind::Struct:
                shape_ok = rows == 0 && columns == 0 && member.scalar == ScalarType::Float32;
                break;
            default: break;
            }
            if (!shape_ok)
            {
                return "reflection: a member's rows and columns do not fit its kind";
            }
            if (static_cast<u64>(member.offset) + member.size > block.size)
            {
                return "reflection: a member extends past the end of its block";
            }
            if (member.array_count != 0 && member.array_stride == 0)
            {
                return "reflection: an array member has a zero stride";
            }
        }
    }
    for (u32 i = 0; i < r.binding_count; ++i)
    {
        const ReflectedBinding& b = r.bindings[i];
        if (!IsTerminated(b.name))
        {
            return "reflection: a binding name is not terminated";
        }
        if (static_cast<u32>(b.type) >= static_cast<u32>(rhi::BindingType::Count))
        {
            return "reflection: a binding has an unknown type";
        }
        if (b.count == 0 || b.stages != r.stage)
        {
            return "reflection: a binding has a zero array size or a stage other than its module's";
        }
        if (b.block != kNoBlock && (b.block >= r.block_count || b.type != rhi::BindingType::UniformBuffer))
        {
            return "reflection: a binding names a block that does not exist or is not a uniform buffer";
        }
        const bool storage = b.type == rhi::BindingType::StorageBuffer ||
                             b.type == rhi::BindingType::ReadOnlyStorageBuffer;
        if (storage ? (b.stride == 0 || b.stride > 2048) : b.stride != 0)
        {
            return "reflection: a storage buffer binding has no stride (or one above 2048), or another binding has one";
        }
        for (u32 j = 0; j < i; ++j)
        {
            if (r.bindings[j].set == b.set && r.bindings[j].binding == b.binding)
            {
                return "reflection: two bindings share a set and binding index";
            }
        }
    }
    if (r.has_push_constants ? r.push_constant_block >= r.block_count : r.push_constant_block != 0)
    {
        return "reflection: the push-constant block index is out of range, or set without a push-constant block";
    }
    if (const char* metal = ValidateMetalSlots(r))
    {
        return metal;
    }
    for (u32 i = 0; i < r.vertex_input_count; ++i)
    {
        const ReflectedVertexInput& in = r.vertex_inputs[i];
        if (!IsTerminated(in.name))
        {
            return "reflection: a vertex input name is not terminated";
        }
        if (static_cast<u32>(in.format) >= static_cast<u32>(rhi::VertexFormat::Count))
        {
            return "reflection: a vertex input has an unknown format";
        }
        for (u32 j = 0; j < i; ++j)
        {
            if (r.vertex_inputs[j].location == in.location)
            {
                return "reflection: two vertex inputs share a location";
            }
        }
    }
    return nullptr;
}

// --- Serialization -------------------------------------------------------------------------------------------------

usize SerializeShaderReflection(const ShaderReflection& reflection, u8* out, usize capacity)
{
    if (ValidateShaderReflection(reflection) != nullptr)
    {
        return 0;
    }
    Writer writer(out, capacity);
    WriteReflection(writer, reflection);
    return writer.Fits() ? writer.Size() : 0;
}

const char* ParseShaderReflection(const u8* data, usize size, ShaderReflection* out)
{
    *out = ShaderReflection{};
    if (data == nullptr && size != 0)
    {
        return "reflection blob: null data";
    }
    Reader r(data, size);
    if (r.U32() != kBlobMagic)
    {
        return r.Failed() ? "reflection blob: truncated" : "reflection blob: wrong magic number";
    }
    if (r.U32() != kReflectionVersion)
    {
        return r.Failed() ? "reflection blob: truncated" : "reflection blob: unsupported version";
    }
    const u32 stage = r.U32();
    if (stage != static_cast<u32>(rhi::ShaderStage::Vertex) && stage != static_cast<u32>(rhi::ShaderStage::Fragment) &&
        stage != static_cast<u32>(rhi::ShaderStage::Compute))
    {
        return r.Failed() ? "reflection blob: truncated" : "reflection blob: unknown stage";
    }
    out->stage = static_cast<rhi::ShaderStage>(stage);
    if (!r.String(out->entry))
    {
        return "reflection blob: truncated or over-long entry name";
    }
    for (u32 i = 0; i < 3; ++i)
    {
        out->local_size[i] = r.U32();
    }
    out->binding_count = r.U32();
    if (r.Failed())
    {
        return "reflection blob: truncated";
    }
    if (out->binding_count > kMaxReflectedBindings)
    {
        return "reflection blob: too many bindings";
    }
    for (u32 i = 0; i < out->binding_count; ++i)
    {
        ReflectedBinding& b = out->bindings[i];
        if (!r.String(b.name))
        {
            return "reflection blob: truncated or over-long binding name";
        }
        b.set = r.U32();
        b.binding = r.U32();
        const u32 type = r.U8();
        if (!r.Failed() && type >= static_cast<u32>(rhi::BindingType::Count))
        {
            return "reflection blob: unknown binding type";
        }
        b.type = static_cast<rhi::BindingType>(type);
        b.count = r.U32();
        b.stages = static_cast<rhi::ShaderStage>(r.U32());
        b.block = r.U32();
        b.stride = r.U32();
        b.metal_slot = r.U32();
    }
    out->block_count = r.U32();
    if (r.Failed())
    {
        return "reflection blob: truncated";
    }
    if (out->block_count > kMaxReflectedBlocks)
    {
        return "reflection blob: too many blocks";
    }
    for (u32 i = 0; i < out->block_count; ++i)
    {
        ReflectedBlock& block = out->blocks[i];
        if (!r.String(block.name))
        {
            return "reflection blob: truncated or over-long block name";
        }
        block.size = r.U32();
        block.member_count = r.U32();
        if (r.Failed())
        {
            return "reflection blob: truncated";
        }
        if (block.member_count > kMaxBlockMembers)
        {
            return "reflection blob: too many block members";
        }
        for (u32 m = 0; m < block.member_count; ++m)
        {
            ReflectedMember& member = block.members[m];
            if (!r.String(member.name))
            {
                return "reflection blob: truncated or over-long member name";
            }
            member.offset = r.U32();
            member.size = r.U32();
            const u32 kind = r.U8();
            const u32 scalar = r.U8();
            if (!r.Failed() &&
                (kind >= static_cast<u32>(MemberKind::Count) || scalar >= static_cast<u32>(ScalarType::Count)))
            {
                return "reflection blob: unknown member type";
            }
            member.kind = static_cast<MemberKind>(kind);
            member.scalar = static_cast<ScalarType>(scalar);
            member.rows = static_cast<u8>(r.U8());
            member.columns = static_cast<u8>(r.U8());
            member.array_count = r.U32();
            member.array_stride = r.U32();
        }
    }
    const u32 has_push = r.U8();
    if (!r.Failed() && has_push > 1)
    {
        return "reflection blob: the push-constant flag is neither 0 nor 1";
    }
    out->has_push_constants = has_push == 1;
    out->push_constant_block = r.U32();
    const u32 has_metal = r.U8();
    if (!r.Failed() && has_metal > 1)
    {
        return "reflection blob: the Metal-slots flag is neither 0 nor 1";
    }
    out->has_metal_slots = has_metal == 1;
    out->push_constant_metal_slot = r.U32();
    out->vertex_input_count = r.U32();
    if (r.Failed())
    {
        return "reflection blob: truncated";
    }
    if (out->vertex_input_count > kMaxReflectedVertexInputs)
    {
        return "reflection blob: too many vertex inputs";
    }
    for (u32 i = 0; i < out->vertex_input_count; ++i)
    {
        ReflectedVertexInput& in = out->vertex_inputs[i];
        if (!r.String(in.name))
        {
            return "reflection blob: truncated or over-long vertex input name";
        }
        in.location = r.U32();
        const u32 format = r.U8();
        if (!r.Failed() && format >= static_cast<u32>(rhi::VertexFormat::Count))
        {
            return "reflection blob: unknown vertex format";
        }
        in.format = static_cast<rhi::VertexFormat>(format);
    }
    if (r.Failed())
    {
        return "reflection blob: truncated";
    }
    if (!r.AtEnd())
    {
        return "reflection blob: trailing bytes";
    }
    return ValidateShaderReflection(*out);
}

// --- Queries and builders ------------------------------------------------------------------------------------------

const ReflectedBlock* FindBlock(const ShaderReflection& reflection, const char* name)
{
    for (u32 i = 0; i < reflection.block_count && i < kMaxReflectedBlocks; ++i)
    {
        if (CStrCompareN(reflection.blocks[i].name, name, kMaxReflectionName) == 0)
        {
            return &reflection.blocks[i];
        }
    }
    return nullptr;
}

bool CheckBlockSize(const ShaderReflection& reflection, const char* block_name, usize cpp_size, ReflectionError* error)
{
    const ReflectedBlock* block = FindBlock(reflection, block_name);
    if (block == nullptr)
    {
        SetError(error, "the %s shader '%s' has no block named '%s'", StageName(reflection.stage), reflection.entry,
                 block_name);
        return false;
    }
    if (block->size != cpp_size)
    {
        SetError(error, "block '%s' is %u bytes in the shader but the C++ struct is %zu bytes", block_name, block->size,
                 static_cast<size_t>(cpp_size));
        return false;
    }
    return true;
}

rhi::BindGroupLayoutDesc BindGroupLayoutData::Desc(const char* debug_name) const
{
    rhi::BindGroupLayoutDesc desc;
    desc.entries = rhi::Span<rhi::BindingDesc>(entries, entry_count);
    desc.debug_name = debug_name;
    return desc;
}

bool BuildBindGroupLayout(const ShaderReflection* const* shaders, u32 shader_count, u32 set, BindGroupLayoutData* out,
                          ReflectionError* error)
{
    *out = BindGroupLayoutData{};
    if (shader_count == 0 || shaders == nullptr)
    {
        SetError(error, "BuildBindGroupLayout needs at least one shader");
        return false;
    }
    // The first reflected binding that produced each entry, to name both sides of a conflict.
    const ReflectedBinding* origin[rhi::kMaxBindingsPerLayout] = {};
    const ShaderReflection* origin_shader[rhi::kMaxBindingsPerLayout] = {};
    for (u32 s = 0; s < shader_count; ++s)
    {
        const ShaderReflection& shader = *shaders[s];
        if (const char* problem = ValidateShaderReflection(shader))
        {
            SetError(error, "%s", problem);
            return false;
        }
        for (u32 i = 0; i < shader.binding_count; ++i)
        {
            const ReflectedBinding& b = shader.bindings[i];
            if (b.set != set)
            {
                continue;
            }
            u32 slot = out->entry_count;
            for (u32 e = 0; e < out->entry_count; ++e)
            {
                if (out->entries[e].binding == b.binding)
                {
                    slot = e;
                    break;
                }
            }
            if (slot == out->entry_count)
            {
                if (out->entry_count == rhi::kMaxBindingsPerLayout)
                {
                    SetError(error, "set %u has more than %u bindings", set, rhi::kMaxBindingsPerLayout);
                    return false;
                }
                rhi::BindingDesc& entry = out->entries[out->entry_count];
                entry.binding = b.binding;
                entry.type = b.type;
                entry.stages = b.stages;
                entry.count = b.count;
                entry.stride = b.stride;
                origin[out->entry_count] = &b;
                origin_shader[out->entry_count] = &shader;
                ++out->entry_count;
                continue;
            }
            rhi::BindingDesc& entry = out->entries[slot];
            if (entry.type != b.type || entry.count != b.count || entry.stride != b.stride)
            {
                SetError(
                    error,
                    "set %u binding %u: '%s' (%s, count %u, stride %u) in the %s shader conflicts with '%s' (%s, count %u, stride %u) in the %s "
                    "shader",
                    set, b.binding, origin[slot]->name, BindingTypeName(entry.type), entry.count, entry.stride,
                    StageName(origin_shader[slot]->stage), b.name, BindingTypeName(b.type), b.count, b.stride,
                    StageName(shader.stage));
                return false;
            }
            entry.stages = entry.stages | b.stages;
        }
    }
    // Sort by binding index (insertion sort; at most 32 entries) so the layout does not depend on declaration order.
    for (u32 i = 1; i < out->entry_count; ++i)
    {
        const rhi::BindingDesc key = out->entries[i];
        u32 j = i;
        while (j > 0 && out->entries[j - 1].binding > key.binding)
        {
            out->entries[j] = out->entries[j - 1];
            --j;
        }
        out->entries[j] = key;
    }
    return true;
}

bool BuildBindGroupLayoutFor(const ShaderReflection& shader, u32 set, BindGroupLayoutData* out, ReflectionError* error)
{
    const ShaderReflection* one = &shader;
    return BuildBindGroupLayout(&one, 1, set, out, error);
}

bool BuildVertexLayout(const ShaderReflection& shader, u32 binding, VertexLayoutData* out, ReflectionError* error)
{
    *out = VertexLayoutData{};
    if (const char* problem = ValidateShaderReflection(shader))
    {
        SetError(error, "%s", problem);
        return false;
    }
    if (shader.stage != rhi::ShaderStage::Vertex)
    {
        SetError(error, "a %s shader has no vertex layout", StageName(shader.stage));
        return false;
    }
    if (shader.vertex_input_count > rhi::kMaxVertexAttributes)
    {
        SetError(error, "the vertex shader has %u inputs; the RHI allows %u", shader.vertex_input_count,
                 rhi::kMaxVertexAttributes);
        return false;
    }
    // Locations in increasing order.
    bool used[kMaxReflectedVertexInputs] = {};
    u32 offset = 0;
    for (u32 n = 0; n < shader.vertex_input_count; ++n)
    {
        u32 best = kMaxReflectedVertexInputs;
        for (u32 i = 0; i < shader.vertex_input_count; ++i)
        {
            if (!used[i] && (best == kMaxReflectedVertexInputs ||
                             shader.vertex_inputs[i].location < shader.vertex_inputs[best].location))
            {
                best = i;
            }
        }
        used[best] = true;
        rhi::VertexAttributeDesc& attribute = out->attributes[n];
        attribute.location = shader.vertex_inputs[best].location;
        attribute.binding = binding;
        attribute.format = shader.vertex_inputs[best].format;
        attribute.offset = offset;
        offset += rhi::VertexFormatSize(attribute.format);
    }
    out->attribute_count = shader.vertex_input_count;
    out->stride = offset;
    return true;
}

}  // namespace engine_placeholder::renderer
