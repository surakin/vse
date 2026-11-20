#include "asset/material.h"

#include "cstr/mem_ops.h"

namespace vse::asset
{
namespace
{

constexpr u32 kMatSectionHeader = 16;
constexpr u32 kMatSectionBlock = 17;
constexpr u32 kMatSectionParams = 18;
constexpr u32 kMatSectionTextures = 19;
constexpr u32 kMatSectionSamplers = 20;
constexpr u32 kMatSectionOptions = 21;
constexpr usize kMatHeaderBytes = 96;
constexpr usize kMatRecordBytes = 64;

void MatStore(byte* p, u64 value, u32 width)
{
    for (u32 i = 0; i < width; ++i)
    {
        p[i] = static_cast<byte>(value >> (8 * i));
    }
}

u64 MatLoad(const byte* p, u32 width)
{
    u64 v = 0;
    for (u32 i = 0; i < width; ++i)
    {
        v |= static_cast<u64>(p[i]) << (8 * i);
    }
    return v;
}

Status MatInvalid(const char* message)
{
    return Status::Make(ErrorCode::Invalid, message);
}

Status MatCorrupt(const char* message)
{
    return Status::Make(ErrorCode::Corrupt, message);
}

// A name is one to 47 printable characters followed by zeros up to the end of the field.
bool MatNameOk(const MaterialName& name)
{
    u32 length = 0;
    while (length < kMaterialNameBytes && name.text[length] != '\0')
    {
        if (name.text[length] < 0x21 || name.text[length] > 0x7e)
        {
            return false;
        }
        ++length;
    }
    if (length == 0 || length >= kMaterialNameBytes)
    {
        return false;
    }
    for (u32 i = length; i < kMaterialNameBytes; ++i)
    {
        if (name.text[i] != '\0')
        {
            return false;
        }
    }
    return true;
}

bool MatSameName(const MaterialName& a, const MaterialName& b)
{
    return MemCompare(a.text, b.text, kMaterialNameBytes) == 0;
}

u32 MatParamSize(MaterialParamType type)
{
    switch (type)
    {
    case MaterialParamType::Float:
    case MaterialParamType::Int:
    case MaterialParamType::UInt: return 4;
    case MaterialParamType::Vec2: return 8;
    case MaterialParamType::Vec3:
    case MaterialParamType::Color3: return 12;
    case MaterialParamType::Vec4:
    case MaterialParamType::Color4: return 16;
    case MaterialParamType::Mat4: return 64;
    }
    return 0;
}

// Everything the description must satisfy, shared by the builder and the loader.
Status MatValidate(const MaterialAssetData& d, bool from_blob)
{
    const auto fail = [from_blob](const char* message)
    { return from_blob ? MatCorrupt(message) : MatInvalid(message); };
    if (!MatNameOk(d.template_name))
    {
        return fail("the template name is empty, too long or not printable");
    }
    if ((d.flags & ~static_cast<u32>(kMaterialSkinnable)) != 0)
    {
        return fail("a material flag bit is set that this version does not define");
    }
    if (d.options.Size() > kMaterialMaxOptions || d.params.Size() > kMaterialMaxParams ||
        d.textures.Size() > kMaterialMaxTextures || d.samplers.Size() > kMaterialMaxSamplers ||
        d.block.Size() > kMaterialMaxParamBytes)
    {
        return fail("a material count or the parameter block is above its limit");
    }
    for (usize i = 0; i < d.options.Size(); ++i)
    {
        if (!MatNameOk(d.options[i]))
        {
            return fail("a permutation option name is empty, too long or not printable");
        }
        for (usize j = 0; j < i; ++j)
        {
            if (MatSameName(d.options[i], d.options[j]))
            {
                return fail("a permutation option is repeated");
            }
        }
    }
    for (usize i = 0; i < d.params.Size(); ++i)
    {
        const MaterialParam& p = d.params[i];
        if (!MatNameOk(p.name))
        {
            return fail("a parameter name is empty, too long or not printable");
        }
        const u32 size = MatParamSize(p.type);
        if (size == 0 || p.size != size)
        {
            return fail("a parameter's type is unknown or its size does not match it");
        }
        if (p.offset % 4 != 0 || p.offset > d.block.Size() || p.size > d.block.Size() - p.offset)
        {
            return fail("a parameter lies outside the parameter block or is not 4-byte aligned");
        }
        for (usize j = 0; j < i; ++j)
        {
            const MaterialParam& q = d.params[j];
            if (MatSameName(p.name, q.name))
            {
                return fail("a parameter is repeated");
            }
            if (p.offset < q.offset + q.size && q.offset < p.offset + p.size)
            {
                return fail("two parameters overlap in the block");
            }
        }
    }
    for (usize i = 0; i < d.textures.Size(); ++i)
    {
        const MaterialTextureBinding& t = d.textures[i];
        if (!MatNameOk(t.slot))
        {
            return fail("a texture slot name is empty, too long or not printable");
        }
        if (t.texture.IsNone())
        {
            return fail("a texture binding has no texture");
        }
        for (usize j = 0; j < i; ++j)
        {
            if (MatSameName(t.slot, d.textures[j].slot))
            {
                return fail("a texture slot is bound twice");
            }
        }
    }
    for (usize i = 0; i < d.samplers.Size(); ++i)
    {
        const MaterialSampler& s = d.samplers[i];
        if (!MatNameOk(s.slot))
        {
            return fail("a sampler slot name is empty, too long or not printable");
        }
        if (static_cast<u32>(s.min_filter) > 1 || static_cast<u32>(s.mag_filter) > 1 ||
            static_cast<u32>(s.mip_filter) > 1 || static_cast<u32>(s.address_u) > 2 ||
            static_cast<u32>(s.address_v) > 2 || s.max_anisotropy < 1 || s.max_anisotropy > 16)
        {
            return fail("a sampler description is outside its ranges");
        }
        for (usize j = 0; j < i; ++j)
        {
            if (MatSameName(s.slot, d.samplers[j].slot))
            {
                return fail("a sampler slot is described twice");
            }
        }
    }
    const MaterialStateOverride& st = d.state;
    if (st.cull > 2 || st.depth_compare > 7)
    {
        return fail("a render-state override is outside its enum");
    }
    return Status::Success();
}

// The index of the first binding that uses `id`, or textures.Size() when none does.
usize MatFirstUse(const Vector<MaterialTextureBinding>& textures, ResourceId id)
{
    for (usize i = 0; i < textures.Size(); ++i)
    {
        if (textures[i].texture == id)
        {
            return i;
        }
    }
    return textures.Size();
}

}  // namespace

bool MakeMaterialName(const char* text, usize length, MaterialName* out)
{
    *out = MaterialName();
    if (length == 0 || length >= kMaterialNameBytes)
    {
        return false;
    }
    MemCopy(out->text, text, length);
    return true;
}

Status BuildMaterialBlob(const TypeRegistry& types, ResourceId id, const MaterialAssetData& data, Vector<byte>* out,
                         u32 target)
{
    if (id.IsNone())
    {
        return MatInvalid("a material needs a non-null id");
    }
    ENGINE_TRY(MatValidate(data, false));
    Vector<byte> header;
    header.Resize(kMatHeaderBytes);
    MemSet(header.Data(), 0, kMatHeaderBytes);
    byte* h = header.Data();
    MatStore(h, data.flags, 4);
    MatStore(h + 4, data.block.Size(), 4);
    MatStore(h + 8, data.params.Size(), 4);
    MatStore(h + 12, data.textures.Size(), 4);
    MatStore(h + 16, data.samplers.Size(), 4);
    MatStore(h + 20, data.options.Size(), 4);
    const MaterialStateOverride& st = data.state;
    const u32 mask = (st.set_cull ? 1u : 0u) | (st.set_depth_test ? 2u : 0u) | (st.set_depth_write ? 4u : 0u) |
                     (st.set_depth_compare ? 8u : 0u) | (st.set_alpha_blend ? 16u : 0u);
    MatStore(h + 24, mask, 4);
    h[28] = st.cull;
    h[29] = st.depth_test ? 1 : 0;
    h[30] = st.depth_write ? 1 : 0;
    h[31] = st.depth_compare;
    h[32] = st.alpha_blend ? 1 : 0;
    MemCopy(h + 48, data.template_name.text, kMaterialNameBytes);

    Vector<byte> params;
    params.Resize(data.params.Size() * kMatRecordBytes);
    MemSet(params.Data(), 0, params.Size());
    for (usize i = 0; i < data.params.Size(); ++i)
    {
        byte* r = params.Data() + i * kMatRecordBytes;
        MemCopy(r, data.params[i].name.text, kMaterialNameBytes);
        MatStore(r + 48, data.params[i].offset, 4);
        MatStore(r + 52, data.params[i].size, 4);
        r[56] = static_cast<byte>(data.params[i].type);
    }
    Vector<byte> textures;
    textures.Resize(data.textures.Size() * kMatRecordBytes);
    MemSet(textures.Data(), 0, textures.Size());
    for (usize i = 0; i < data.textures.Size(); ++i)
    {
        byte* r = textures.Data() + i * kMatRecordBytes;
        MemCopy(r, data.textures[i].slot.text, kMaterialNameBytes);
        MatStore(r + 48, data.textures[i].texture.Value(), 8);
    }
    Vector<byte> samplers;
    samplers.Resize(data.samplers.Size() * kMatRecordBytes);
    MemSet(samplers.Data(), 0, samplers.Size());
    for (usize i = 0; i < data.samplers.Size(); ++i)
    {
        byte* r = samplers.Data() + i * kMatRecordBytes;
        const MaterialSampler& s = data.samplers[i];
        MemCopy(r, s.slot.text, kMaterialNameBytes);
        r[48] = static_cast<byte>(s.min_filter);
        r[49] = static_cast<byte>(s.mag_filter);
        r[50] = static_cast<byte>(s.mip_filter);
        r[51] = static_cast<byte>(s.address_u);
        r[52] = static_cast<byte>(s.address_v);
        r[53] = s.max_anisotropy;
    }
    Vector<byte> options;
    options.Resize(data.options.Size() * kMaterialNameBytes);
    for (usize i = 0; i < data.options.Size(); ++i)
    {
        MemCopy(options.Data() + i * kMaterialNameBytes, data.options[i].text, kMaterialNameBytes);
    }

    BlobWriter writer(types, types::kMaterial, id, target);
    TypeInfo texture_info;
    if (data.textures.Size() != 0)
    {
        if (!types.Find(types::kTexture, &texture_info))
        {
            return Status::Make(ErrorCode::TypeMismatch, "the texture type is not registered");
        }
        // One dependency per distinct texture, the role being the index of the first slot that names it.
        for (usize i = 0; i < data.textures.Size(); ++i)
        {
            if (MatFirstUse(data.textures, data.textures[i].texture) != i)
            {
                continue;
            }
            Dependency dep;
            dep.id = data.textures[i].texture;
            dep.type = types::kTexture;
            dep.type_version = texture_info.current_version;
            dep.role = static_cast<u32>(i);
            writer.AddDependency(dep);
        }
    }
    ENGINE_TRY(writer.AddSection(kMatSectionHeader, ByteSpan(header.Data(), header.Size())));
    ENGINE_TRY(writer.AddSection(kMatSectionBlock, ByteSpan(data.block.Data(), data.block.Size())));
    ENGINE_TRY(writer.AddSection(kMatSectionParams, ByteSpan(params.Data(), params.Size())));
    ENGINE_TRY(writer.AddSection(kMatSectionTextures, ByteSpan(textures.Data(), textures.Size())));
    ENGINE_TRY(writer.AddSection(kMatSectionSamplers, ByteSpan(samplers.Data(), samplers.Size())));
    ENGINE_TRY(writer.AddSection(kMatSectionOptions, ByteSpan(options.Data(), options.Size())));
    return writer.Finish(out);
}

Status LoadMaterialBlob(ByteSpan bytes, const TypeRegistry& types, MaterialAssetData* out, ResourceId* id_out)
{
    BlobReader blob;
    ENGINE_TRY(blob.Open(bytes, types));
    if (blob.Type() != types::kMaterial)
    {
        return Status::Make(ErrorCode::TypeMismatch, "the blob is not a material");
    }
    ByteSpan header;
    ByteSpan block;
    ByteSpan params;
    ByteSpan textures;
    ByteSpan samplers;
    ByteSpan options;
    if (!blob.FindSection(kMatSectionHeader, &header) || header.Size() != kMatHeaderBytes ||
        !blob.FindSection(kMatSectionBlock, &block) || !blob.FindSection(kMatSectionParams, &params) ||
        !blob.FindSection(kMatSectionTextures, &textures) || !blob.FindSection(kMatSectionSamplers, &samplers) ||
        !blob.FindSection(kMatSectionOptions, &options))
    {
        return MatCorrupt("a material section is missing or the header has the wrong size");
    }
    const byte* h = header.Data();
    const u64 block_bytes = MatLoad(h + 4, 4);
    const u64 param_count = MatLoad(h + 8, 4);
    const u64 texture_count = MatLoad(h + 12, 4);
    const u64 sampler_count = MatLoad(h + 16, 4);
    const u64 option_count = MatLoad(h + 20, 4);
    if (block_bytes != block.Size() || params.Size() != param_count * kMatRecordBytes ||
        textures.Size() != texture_count * kMatRecordBytes || samplers.Size() != sampler_count * kMatRecordBytes ||
        options.Size() != option_count * kMaterialNameBytes || block_bytes > kMaterialMaxParamBytes ||
        param_count > kMaterialMaxParams || texture_count > kMaterialMaxTextures ||
        sampler_count > kMaterialMaxSamplers || option_count > kMaterialMaxOptions)
    {
        return MatCorrupt("a material section does not have the size its header gives, or a count is above its limit");
    }
    for (usize i = 33; i < 48; ++i)
    {
        if (h[i] != 0)
        {
            return MatCorrupt("a reserved byte of the material header is not zero");
        }
    }
    MaterialAssetData d;
    d.flags = static_cast<u32>(MatLoad(h, 4));
    const u32 mask = static_cast<u32>(MatLoad(h + 24, 4));
    if ((mask & ~31u) != 0 || h[29] > 1 || h[30] > 1 || h[32] > 1)
    {
        return MatCorrupt("the render-state overrides of a material are outside their ranges");
    }
    d.state.set_cull = (mask & 1u) != 0;
    d.state.set_depth_test = (mask & 2u) != 0;
    d.state.set_depth_write = (mask & 4u) != 0;
    d.state.set_depth_compare = (mask & 8u) != 0;
    d.state.set_alpha_blend = (mask & 16u) != 0;
    d.state.cull = h[28];
    d.state.depth_test = h[29] != 0;
    d.state.depth_write = h[30] != 0;
    d.state.depth_compare = h[31];
    d.state.alpha_blend = h[32] != 0;
    MemCopy(d.template_name.text, h + 48, kMaterialNameBytes);
    if (!d.block.TryResize(block.Size()) || !d.params.TryReserve(static_cast<usize>(param_count)) ||
        !d.textures.TryReserve(static_cast<usize>(texture_count)) ||
        !d.samplers.TryReserve(static_cast<usize>(sampler_count)) ||
        !d.options.TryReserve(static_cast<usize>(option_count)))
    {
        return Status::Make(ErrorCode::LimitExceeded, "out of memory");
    }
    if (block.Size() != 0)
    {
        MemCopy(d.block.Data(), block.Data(), block.Size());
    }
    for (usize i = 0; i < param_count; ++i)
    {
        const byte* r = params.Data() + i * kMatRecordBytes;
        MaterialParam p;
        MemCopy(p.name.text, r, kMaterialNameBytes);
        p.offset = static_cast<u32>(MatLoad(r + 48, 4));
        p.size = static_cast<u32>(MatLoad(r + 52, 4));
        if (r[56] > static_cast<byte>(MaterialParamType::Color4))
        {
            return MatCorrupt("a parameter has an unknown type");
        }
        p.type = static_cast<MaterialParamType>(r[56]);
        for (usize k = 57; k < kMatRecordBytes; ++k)
        {
            if (r[k] != 0)
            {
                return MatCorrupt("a reserved byte of a parameter record is not zero");
            }
        }
        d.params.Push(p);
    }
    for (usize i = 0; i < texture_count; ++i)
    {
        const byte* r = textures.Data() + i * kMatRecordBytes;
        MaterialTextureBinding t;
        MemCopy(t.slot.text, r, kMaterialNameBytes);
        t.texture = ResourceId::FromValue(MatLoad(r + 48, 8));
        for (usize k = 56; k < kMatRecordBytes; ++k)
        {
            if (r[k] != 0)
            {
                return MatCorrupt("a reserved byte of a texture record is not zero");
            }
        }
        d.textures.Push(t);
    }
    for (usize i = 0; i < sampler_count; ++i)
    {
        const byte* r = samplers.Data() + i * kMatRecordBytes;
        MaterialSampler s;
        MemCopy(s.slot.text, r, kMaterialNameBytes);
        if (r[48] > 1 || r[49] > 1 || r[50] > 1 || r[51] > 2 || r[52] > 2)
        {
            return MatCorrupt("a sampler description is outside its ranges");
        }
        s.min_filter = static_cast<MaterialFilter>(r[48]);
        s.mag_filter = static_cast<MaterialFilter>(r[49]);
        s.mip_filter = static_cast<MaterialFilter>(r[50]);
        s.address_u = static_cast<MaterialAddress>(r[51]);
        s.address_v = static_cast<MaterialAddress>(r[52]);
        s.max_anisotropy = r[53];
        for (usize k = 54; k < kMatRecordBytes; ++k)
        {
            if (r[k] != 0)
            {
                return MatCorrupt("a reserved byte of a sampler record is not zero");
            }
        }
        d.samplers.Push(s);
    }
    for (usize i = 0; i < option_count; ++i)
    {
        MaterialName n;
        MemCopy(n.text, options.Data() + i * kMaterialNameBytes, kMaterialNameBytes);
        d.options.Push(n);
    }
    ENGINE_TRY(MatValidate(d, true));
    // The dependency list is exactly the distinct textures, each with the index of its first binding as the role.
    usize distinct = 0;
    for (usize i = 0; i < d.textures.Size(); ++i)
    {
        if (MatFirstUse(d.textures, d.textures[i].texture) == i)
        {
            ++distinct;
        }
    }
    if (blob.DependencyCount() != distinct)
    {
        return MatCorrupt("the dependency list does not match the texture bindings");
    }
    for (usize k = 0; k < blob.DependencyCount(); ++k)
    {
        const Dependency dep = blob.DependencyAt(k);
        const usize first = MatFirstUse(d.textures, dep.id);
        if (dep.type != types::kTexture || first == d.textures.Size() || dep.role != first)
        {
            return MatCorrupt("a dependency of the material is not one of its textures");
        }
    }
    if (id_out != nullptr)
    {
        *id_out = blob.Id();
    }
    *out = Move(d);
    return Status::Success();
}

}  // namespace vse::asset
