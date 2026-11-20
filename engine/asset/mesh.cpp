#include "asset/mesh.h"

#include "cstr/mem_ops.h"
#include "lang/scalar.h"

namespace vse::asset
{
namespace
{

constexpr usize kMeshHeaderBytes = 96;
constexpr usize kMeshAttributeBytes = 16;
constexpr usize kMeshLodBytes = 16;
constexpr usize kMeshSlotBytes = 40;
constexpr usize kMeshJointBoundsBytes = 32;
constexpr u32 kMeshFlagSkinned = 1;
constexpr u32 kMeshFlagPacked = 2;
constexpr u32 kMeshPackedStride = 24;
constexpr u32 kMeshFullStride = 48;
constexpr u32 kMeshSkinStride = 8;
constexpr u32 kMeshMorphTargetBytes = 48;
constexpr u32 kMeshMorphDeltaBytes = 40;

void MeshStore(byte* p, u64 value, u32 width)
{
    for (u32 i = 0; i < width; ++i)
    {
        p[i] = static_cast<byte>(value >> (8 * i));
    }
}

u64 MeshLoad(const byte* p, u32 width)
{
    u64 v = 0;
    for (u32 i = 0; i < width; ++i)
    {
        v |= static_cast<u64>(p[i]) << (8 * i);
    }
    return v;
}

void MeshStoreF32(byte* p, f32 v)
{
    MemCopy(p, &v, 4);
}

f32 MeshLoadF32(const byte* p)
{
    f32 v;
    MemCopy(&v, p, 4);
    return v;
}

bool MeshFinite(const f32* values, usize count)
{
    for (usize i = 0; i < count; ++i)
    {
        if (!IsFinite(values[i]))
        {
            return false;
        }
    }
    return true;
}

Status MeshInvalid(const char* message)
{
    return Status::Make(ErrorCode::Invalid, message);
}

Status MeshCorrupt(const char* message)
{
    return Status::Make(ErrorCode::Corrupt, message);
}

f64 MeshRound(f64 v)
{
    return Floor(v + 0.5);
}

byte MeshSnorm8(f64 v)
{
    const f64 clamped = v > 1.0 ? 1.0 : (v < -1.0 ? -1.0 : v);
    const int q = static_cast<int>(MeshRound(clamped * 127.0));
    return static_cast<byte>(q & 0xff);
}

f64 MeshSnorm8Decode(byte b)
{
    const int q = b >= 128 ? static_cast<int>(b) - 256 : static_cast<int>(b);
    return static_cast<f64>(q) / 127.0;
}

void MeshPutAttribute(Vector<byte>* out, u32 location, rhi::VertexFormat format, u32 offset, u32 binding)
{
    const usize at = out->Size();
    out->Resize(at + kMeshAttributeBytes);
    MeshStore(out->Data() + at, location, 4);
    MeshStore(out->Data() + at + 4, static_cast<u32>(format), 4);
    MeshStore(out->Data() + at + 8, offset, 4);
    MeshStore(out->Data() + at + 12, binding, 4);
}

bool MeshRange(u32 first, u32 count, u32 index_count)
{
    return count != 0 && count % 3 == 0 && first % 3 == 0 && first <= index_count && count <= index_count - first;
}

}  // namespace

bool QuantizeSkinInfluences(const u32* joints, const f64* weights, usize count, MeshSkinInfluence* out)
{
    // Selection of the four largest: weight descending, joint ascending.
    Vector<usize> order;
    for (usize i = 0; i < count; ++i)
    {
        if (weights[i] > 0.0)
        {
            order.Push(i);
        }
    }
    for (usize i = 1; i < order.Size(); ++i)
    {
        const usize v = order[i];
        usize k = i;
        while (k > 0 && (weights[order[k - 1]] < weights[v] ||
                         (weights[order[k - 1]] == weights[v] && joints[order[k - 1]] > joints[v])))
        {
            order[k] = order[k - 1];
            --k;
        }
        order[k] = v;
    }
    if (order.Size() > 4)
    {
        order.Resize(4);
    }
    if (order.Size() == 0)
    {
        return false;
    }
    f64 sum = 0;
    for (usize i = 0; i < order.Size(); ++i)
    {
        sum += weights[order[i]];
    }
    u32 q[4] = {0, 0, 0, 0};
    f64 fraction[4] = {0, 0, 0, 0};
    u32 total = 0;
    for (usize i = 0; i < order.Size(); ++i)
    {
        const f64 exact = weights[order[i]] / sum * 255.0;
        q[i] = static_cast<u32>(Floor(exact));
        fraction[i] = exact - Floor(exact);
        total += q[i];
    }
    while (total < 255)
    {
        usize best = 0;
        for (usize i = 1; i < order.Size(); ++i)
        {
            if (fraction[i] > fraction[best])
            {
                best = i;
            }
        }
        ++q[best];
        fraction[best] = -1.0;
        ++total;
    }
    // Sort the quantized pairs by weight, then joint.
    u32 j[4] = {0, 0, 0, 0};
    for (usize i = 0; i < order.Size(); ++i)
    {
        j[i] = joints[order[i]];
    }
    for (usize i = 1; i < order.Size(); ++i)
    {
        const u32 vq = q[i];
        const u32 vj = j[i];
        usize k = i;
        while (k > 0 && (q[k - 1] < vq || (q[k - 1] == vq && j[k - 1] > vj)))
        {
            q[k] = q[k - 1];
            j[k] = j[k - 1];
            --k;
        }
        q[k] = vq;
        j[k] = vj;
    }
    for (usize i = 0; i < 4; ++i)
    {
        out->joint[i] = static_cast<u8>(j[i]);
        out->weight[i] = static_cast<u8>(q[i]);
    }
    return true;
}

Array<u8, 4> EncodeMeshColor(const Array<f32, 4>& rgba)
{
    Array<u8, 4> out = {{0, 0, 0, 0}};
    for (u32 c = 0; c < 4; ++c)
    {
        f64 v = rgba[c];
        v = v > 0.0 ? (v < 1.0 ? v : 1.0) : 0.0;  // a NaN compares false and becomes 0
        if (c < 3)
        {
            v = v <= 0.0031308 ? v * 12.92 : 1.055 * Pow(v, 1.0 / 2.4) - 0.055;
        }
        out[c] = static_cast<u8>(Floor(v * 255.0 + 0.5));
    }
    return out;
}

Status BuildMeshBlob(const TypeRegistry& types, ResourceId id, const MeshBuildInput& input, Vector<byte>* out,
                     u32 target)
{
    const usize vcount = input.vertices.Size();
    const usize icount = input.indices.Size();
    if (vcount == 0 || icount == 0)
    {
        return MeshInvalid("a mesh needs vertices and indices");
    }
    if (vcount > kMaxMeshVertices || icount > kMaxMeshIndices || input.lods.Size() > kMaxMeshLods ||
        input.slots.Size() > kMaxMeshSlots)
    {
        return Status::Make(ErrorCode::LimitExceeded, "a mesh count is above the limit of this version");
    }
    if (icount % 3 != 0)
    {
        return MeshInvalid("the index count is not a multiple of 3");
    }
    for (usize i = 0; i < icount; ++i)
    {
        if (input.indices[i] >= vcount)
        {
            return MeshInvalid("an index is at or past the vertex count");
        }
    }
    const bool skinned = input.skin.Size() != 0;
    if (skinned && input.skin.Size() != vcount)
    {
        return MeshInvalid("the skin array does not have one entry per vertex");
    }
    const bool colored = input.colors.Size() != 0;
    if (colored && input.colors.Size() != vcount)
    {
        return MeshInvalid("the colour array does not have one entry per vertex");
    }
    if (input.morph_targets.Size() > kMaxMorphTargets)
    {
        return Status::Make(ErrorCode::LimitExceeded, "more morph targets than a mesh holds");
    }
    if (input.morph_targets.Size() != 0 && input.quality != MeshQuality::Full)
    {
        return MeshInvalid("morph targets need the full layout (the GPU morph pass writes f32 attributes)");
    }
    for (usize t = 0; t < input.morph_targets.Size(); ++t)
    {
        const MeshMorphTarget& morph = input.morph_targets[t];
        usize length = 0;
        while (length <= kMaxMorphNameBytes && morph.name[length] != '\0')
        {
            ++length;
        }
        if (length == 0 || length > kMaxMorphNameBytes)
        {
            return MeshInvalid("a morph target name is empty or longer than 31 bytes");
        }
        for (usize k = 0; k < morph.deltas.Size(); ++k)
        {
            const MeshMorphDelta& d = morph.deltas[k];
            if (d.vertex >= vcount || (k > 0 && d.vertex <= morph.deltas[k - 1].vertex) ||
                !MeshFinite(d.position.Data(), 3) || !MeshFinite(d.normal.Data(), 3) ||
                !MeshFinite(d.tangent.Data(), 3))
            {
                return MeshInvalid("a morph delta is past the vertices, out of order or not finite");
            }
        }
    }
    if (skinned && (input.skeleton.IsNone() || input.skeleton_joint_count == 0 || input.skeleton_joint_count > 256))
    {
        return MeshInvalid("a skinned mesh needs its skeleton id and a joint count of 1 to 256");
    }
    for (usize i = 0; i < vcount; ++i)
    {
        const MeshVertex& v = input.vertices[i];
        if (!MeshFinite(v.position.Data(), 3) || !MeshFinite(v.normal.Data(), 3) || !MeshFinite(v.tangent.Data(), 4) ||
            !MeshFinite(v.uv.Data(), 2))
        {
            return MeshInvalid("a vertex holds a value that is not finite");
        }
        const f64 n2 = static_cast<f64>(v.normal[0]) * v.normal[0] + static_cast<f64>(v.normal[1]) * v.normal[1] +
                       static_cast<f64>(v.normal[2]) * v.normal[2];
        const f64 t2 = static_cast<f64>(v.tangent[0]) * v.tangent[0] + static_cast<f64>(v.tangent[1]) * v.tangent[1] +
                       static_cast<f64>(v.tangent[2]) * v.tangent[2];
        if (n2 == 0.0 || t2 == 0.0)
        {
            return MeshInvalid("a normal or tangent has zero length");
        }
        if (skinned)
        {
            u32 sum = 0;
            for (u32 k = 0; k < 4; ++k)
            {
                sum += input.skin[i].weight[k];
                if (input.skin[i].weight[k] != 0 && input.skin[i].joint[k] >= input.skeleton_joint_count)
                {
                    return MeshInvalid("a skin joint is at or past the skeleton's joint count");
                }
                for (u32 m = 0; m < k && input.skin[i].weight[k] != 0; ++m)
                {
                    if (input.skin[i].weight[m] != 0 && input.skin[i].joint[m] == input.skin[i].joint[k])
                    {
                        return MeshInvalid("a vertex lists the same skin joint twice");
                    }
                }
            }
            if (sum != 255)
            {
                return MeshInvalid("the skin weights of a vertex do not sum to 255");
            }
        }
    }

    // LODs and slots.
    Vector<MeshLod> lods;
    if (input.lods.Size() == 0)
    {
        lods.Push(MeshLod{0, static_cast<u32>(icount), 0.0f});
    }
    else
    {
        for (usize i = 0; i < input.lods.Size(); ++i)
        {
            lods.Push(input.lods[i]);
        }
    }
    for (usize i = 0; i < lods.Size(); ++i)
    {
        if (!MeshRange(lods[i].first_index, lods[i].index_count, static_cast<u32>(icount)))
        {
            return MeshInvalid("a LOD range is outside the indices or not whole triangles");
        }
        if (!IsFinite(lods[i].screen_size) || lods[i].screen_size < 0.0f ||
            (i > 0 && lods[i].screen_size > lods[i - 1].screen_size))
        {
            return MeshInvalid("LOD screen sizes must be finite and not grow from one LOD to the next");
        }
    }
    for (usize i = 0; i < input.slots.Size(); ++i)
    {
        const MeshSlot& s = input.slots[i];
        usize length = 0;
        while (length < s.name.Size() && s.name[length] != '\0')
        {
            ++length;
        }
        if (length == 0 || length > kMaxSlotNameBytes || length == s.name.Size())
        {
            return MeshInvalid("a slot name is empty or longer than 31 bytes");
        }
        if (!MeshRange(s.first_index, s.index_count, static_cast<u32>(icount)))
        {
            return MeshInvalid("a slot range is outside the indices or not whole triangles");
        }
        for (usize k = 0; k < i; ++k)
        {
            if (MemCompare(input.slots[k].name.Data(), s.name.Data(), s.name.Size()) == 0)
            {
                return MeshInvalid("two slots have the same name");
            }
        }
    }

    // Bounds and the texture-coordinate range.
    f32 bmin[3] = {input.vertices[0].position[0], input.vertices[0].position[1], input.vertices[0].position[2]};
    f32 bmax[3] = {bmin[0], bmin[1], bmin[2]};
    f32 uvmin[2] = {input.vertices[0].uv[0], input.vertices[0].uv[1]};
    f32 uvmax[2] = {uvmin[0], uvmin[1]};
    for (usize i = 0; i < vcount; ++i)
    {
        for (u32 c = 0; c < 3; ++c)
        {
            const f32 p = input.vertices[i].position[c];
            bmin[c] = p < bmin[c] ? p : bmin[c];
            bmax[c] = p > bmax[c] ? p : bmax[c];
        }
        for (u32 c = 0; c < 2; ++c)
        {
            const f32 u = input.vertices[i].uv[c];
            uvmin[c] = u < uvmin[c] ? u : uvmin[c];
            uvmax[c] = u > uvmax[c] ? u : uvmax[c];
        }
    }
    // Morph targets (glTF coverage Day 6): the box grows by the largest position delta of each target, on each side, so
    // it holds every target at any weight from -1 to 1 together.
    for (usize t = 0; t < input.morph_targets.Size(); ++t)
    {
        f32 extent[3] = {0, 0, 0};
        for (const MeshMorphDelta& d : input.morph_targets[t].deltas)
        {
            for (u32 c = 0; c < 3; ++c)
            {
                const f32 a = Abs(d.position[c]);
                extent[c] = a > extent[c] ? a : extent[c];
            }
        }
        for (u32 c = 0; c < 3; ++c)
        {
            bmin[c] -= extent[c];
            bmax[c] += extent[c];
        }
    }
    const bool packed = input.quality == MeshQuality::Packed;
    f32 uv_offset[2] = {0, 0};
    f32 uv_scale[2] = {1, 1};
    if (packed)
    {
        for (u32 c = 0; c < 2; ++c)
        {
            uv_offset[c] = uvmin[c];
            uv_scale[c] = uvmax[c] - uvmin[c];
            if (!IsFinite(uv_scale[c]))
            {
                return MeshInvalid("the texture coordinate range is too large to pack");
            }
        }
    }

    // The vertex stream.
    // A colour (glTF coverage Day 4) is four bytes after the other attributes.
    const u32 base_stride = packed ? kMeshPackedStride : kMeshFullStride;
    const u32 stride = base_stride + (colored ? 4u : 0u);
    Vector<byte> vertices;
    vertices.Resize(vcount * stride);
    f64 uv_error = 0;
    f64 frame_error = 0;
    for (usize i = 0; i < vcount; ++i)
    {
        const MeshVertex& v = input.vertices[i];
        byte* p = vertices.Data() + i * stride;
        const f64 nl = Sqrt(static_cast<f64>(v.normal[0]) * v.normal[0] + static_cast<f64>(v.normal[1]) * v.normal[1] +
                            static_cast<f64>(v.normal[2]) * v.normal[2]);
        const f64 tl = Sqrt(static_cast<f64>(v.tangent[0]) * v.tangent[0] +
                            static_cast<f64>(v.tangent[1]) * v.tangent[1] +
                            static_cast<f64>(v.tangent[2]) * v.tangent[2]);
        f64 n[3];
        f64 t[3];
        for (u32 c = 0; c < 3; ++c)
        {
            n[c] = v.normal[c] / nl;
            t[c] = v.tangent[c] / tl;
        }
        const f64 sign = v.tangent[3] < 0.0f ? -1.0 : 1.0;
        if (colored)
        {
            for (u32 c = 0; c < 4; ++c)
            {
                p[base_stride + c] = static_cast<byte>(input.colors[i][c]);
            }
        }
        MeshStoreF32(p, v.position[0]);
        MeshStoreF32(p + 4, v.position[1]);
        MeshStoreF32(p + 8, v.position[2]);
        if (packed)
        {
            for (u32 c = 0; c < 3; ++c)
            {
                p[12 + c] = MeshSnorm8(n[c]);
                p[16 + c] = MeshSnorm8(t[c]);
                const f64 en = Abs(MeshSnorm8Decode(p[12 + c]) - n[c]);
                const f64 et = Abs(MeshSnorm8Decode(p[16 + c]) - t[c]);
                frame_error = en > frame_error ? en : frame_error;
                frame_error = et > frame_error ? et : frame_error;
            }
            p[15] = 0;
            p[19] = MeshSnorm8(sign);
            for (u32 c = 0; c < 2; ++c)
            {
                const f64 range = uv_scale[c];
                f64 q = 0;
                if (range > 0.0)
                {
                    q = MeshRound((static_cast<f64>(v.uv[c]) - uv_offset[c]) / range * 65535.0);
                    q = q < 0.0 ? 0.0 : (q > 65535.0 ? 65535.0 : q);
                }
                const u32 qi = static_cast<u32>(q);
                MeshStore(p + 20 + c * 2, qi, 2);
                const f32 decoded = static_cast<f32>(uv_offset[c] + range * (static_cast<f64>(qi) / 65535.0));
                const f64 e = Abs(static_cast<f64>(decoded) - v.uv[c]);
                uv_error = e > uv_error ? e : uv_error;
            }
        }
        else
        {
            for (u32 c = 0; c < 3; ++c)
            {
                MeshStoreF32(p + 12 + c * 4, static_cast<f32>(n[c]));
            }
            for (u32 c = 0; c < 3; ++c)
            {
                MeshStoreF32(p + 24 + c * 4, static_cast<f32>(t[c]));
            }
            MeshStoreF32(p + 36, static_cast<f32>(sign));
            MeshStoreF32(p + 40, v.uv[0]);
            MeshStoreF32(p + 44, v.uv[1]);
        }
    }

    // The indices: 16-bit when the vertex count allows.
    const u32 index_width = vcount <= 65536 ? 2 : 4;
    Vector<byte> indices;
    indices.Resize(icount * index_width);
    for (usize i = 0; i < icount; ++i)
    {
        MeshStore(indices.Data() + i * index_width, input.indices[i], index_width);
    }

    // Attributes.
    Vector<byte> attributes;
    Vector<byte> skin_bytes;
    Vector<byte> joint_bounds;
    u32 attribute_count = 4;
    if (packed)
    {
        MeshPutAttribute(&attributes, kMeshLocationPosition, rhi::VertexFormat::Float3, 0, 0);
        MeshPutAttribute(&attributes, kMeshLocationNormal, rhi::VertexFormat::SNorm8x4, 12, 0);
        MeshPutAttribute(&attributes, kMeshLocationTangent, rhi::VertexFormat::SNorm8x4, 16, 0);
        MeshPutAttribute(&attributes, kMeshLocationUv, rhi::VertexFormat::UNorm16x2, 20, 0);
    }
    else
    {
        MeshPutAttribute(&attributes, kMeshLocationPosition, rhi::VertexFormat::Float3, 0, 0);
        MeshPutAttribute(&attributes, kMeshLocationNormal, rhi::VertexFormat::Float3, 12, 0);
        MeshPutAttribute(&attributes, kMeshLocationTangent, rhi::VertexFormat::Float4, 24, 0);
        MeshPutAttribute(&attributes, kMeshLocationUv, rhi::VertexFormat::Float2, 40, 0);
    }
    if (colored)
    {
        MeshPutAttribute(&attributes, kMeshLocationColor, rhi::VertexFormat::UNorm8x4, base_stride, 0);
        attribute_count += 1;
    }
    if (skinned)
    {
        MeshPutAttribute(&attributes, kMeshLocationJoints, rhi::VertexFormat::UInt8x4, 0, 1);
        MeshPutAttribute(&attributes, kMeshLocationWeights, rhi::VertexFormat::UNorm8x4, 4, 1);
        attribute_count += 2;
        skin_bytes.Resize(vcount * kMeshSkinStride);
        for (usize i = 0; i < vcount; ++i)
        {
            for (u32 k = 0; k < 4; ++k)
            {
                skin_bytes[i * kMeshSkinStride + k] = static_cast<byte>(input.skin[i].joint[k]);
                skin_bytes[i * kMeshSkinStride + 4 + k] = static_cast<byte>(input.skin[i].weight[k]);
            }
        }
        const u32 jc = input.skeleton_joint_count;
        joint_bounds.Resize(static_cast<usize>(jc) * kMeshJointBoundsBytes);
        MemSet(joint_bounds.Data(), 0, joint_bounds.Size());
        for (usize i = 0; i < vcount; ++i)
        {
            for (u32 k = 0; k < 4; ++k)
            {
                if (input.skin[i].weight[k] == 0)
                {
                    continue;
                }
                byte* box = joint_bounds.Data() + static_cast<usize>(input.skin[i].joint[k]) * kMeshJointBoundsBytes;
                const bool used = MeshLoad(box + 24, 4) != 0;
                for (u32 c = 0; c < 3; ++c)
                {
                    const f32 p = input.vertices[i].position[c];
                    if (!used || p < MeshLoadF32(box + c * 4))
                    {
                        MeshStoreF32(box + c * 4, p);
                    }
                    if (!used || p > MeshLoadF32(box + 12 + c * 4))
                    {
                        MeshStoreF32(box + 12 + c * 4, p);
                    }
                }
                MeshStore(box + 24, 1, 4);
            }
        }
    }

    Vector<byte> lod_bytes;
    lod_bytes.Resize(lods.Size() * kMeshLodBytes);
    MemSet(lod_bytes.Data(), 0, lod_bytes.Size());
    for (usize i = 0; i < lods.Size(); ++i)
    {
        MeshStore(lod_bytes.Data() + i * kMeshLodBytes, lods[i].first_index, 4);
        MeshStore(lod_bytes.Data() + i * kMeshLodBytes + 4, lods[i].index_count, 4);
        MeshStoreF32(lod_bytes.Data() + i * kMeshLodBytes + 8, lods[i].screen_size);
    }
    Vector<byte> slot_bytes;
    slot_bytes.Resize(input.slots.Size() * kMeshSlotBytes);
    MemSet(slot_bytes.Data(), 0, slot_bytes.Size());
    for (usize i = 0; i < input.slots.Size(); ++i)
    {
        byte* p = slot_bytes.Data() + i * kMeshSlotBytes;
        MemCopy(p, input.slots[i].name.Data(), kMaxSlotNameBytes);
        MeshStore(p + 32, input.slots[i].first_index, 4);
        MeshStore(p + 36, input.slots[i].index_count, 4);
    }

    Vector<byte> header;
    header.Resize(kMeshHeaderBytes);
    MemSet(header.Data(), 0, header.Size());
    byte* h = header.Data();
    MeshStore(h, vcount, 4);
    MeshStore(h + 4, icount, 4);
    MeshStore(h + 8, index_width, 4);
    MeshStore(h + 12, (skinned ? kMeshFlagSkinned : 0u) | (packed ? kMeshFlagPacked : 0u), 4);
    MeshStore(h + 16, attribute_count, 4);
    MeshStore(h + 20, lods.Size(), 4);
    MeshStore(h + 24, input.slots.Size(), 4);
    MeshStore(h + 28, skinned ? input.skeleton_joint_count : 0, 4);
    for (u32 c = 0; c < 3; ++c)
    {
        MeshStoreF32(h + 32 + c * 4, bmin[c]);
        MeshStoreF32(h + 44 + c * 4, bmax[c]);
    }
    MeshStoreF32(h + 56, uv_offset[0]);
    MeshStoreF32(h + 60, uv_offset[1]);
    MeshStoreF32(h + 64, uv_scale[0]);
    MeshStoreF32(h + 68, uv_scale[1]);
    MeshStoreF32(h + 72, static_cast<f32>(uv_error));
    MeshStoreF32(h + 76, static_cast<f32>(frame_error));
    if (skinned)
    {
        MemCopy(h + 80, input.skeleton_hash.bytes, 16);
    }

    BlobWriter writer(types, types::kMesh, id, target);
    if (skinned)
    {
        TypeInfo info;
        if (!types.Find(types::kSkeleton, &info))
        {
            return Status::Make(ErrorCode::TypeMismatch, "the skeleton type is not registered");
        }
        Dependency dependency;
        dependency.id = input.skeleton;
        dependency.type = types::kSkeleton;
        dependency.type_version = info.current_version;
        dependency.role = kMeshRoleSkinSkeleton;
        writer.AddDependency(dependency);
    }
    ENGINE_TRY(writer.AddSection(kMeshSectionHeader, ByteSpan(header.Data(), header.Size())));
    ENGINE_TRY(writer.AddSection(kMeshSectionAttributes, ByteSpan(attributes.Data(), attributes.Size())));
    ENGINE_TRY(writer.AddSection(kMeshSectionVertices, ByteSpan(vertices.Data(), vertices.Size())));
    ENGINE_TRY(writer.AddSection(kMeshSectionIndices, ByteSpan(indices.Data(), indices.Size())));
    ENGINE_TRY(writer.AddSection(kMeshSectionLods, ByteSpan(lod_bytes.Data(), lod_bytes.Size())));
    if (input.slots.Size() != 0)
    {
        ENGINE_TRY(writer.AddSection(kMeshSectionSlots, ByteSpan(slot_bytes.Data(), slot_bytes.Size())));
    }
    if (skinned)
    {
        ENGINE_TRY(writer.AddSection(kMeshSectionSkin, ByteSpan(skin_bytes.Data(), skin_bytes.Size())));
        ENGINE_TRY(writer.AddSection(kMeshSectionJointBounds, ByteSpan(joint_bounds.Data(), joint_bounds.Size())));
    }
    if (input.morph_targets.Size() != 0)
    {
        Vector<byte> target_bytes;
        Vector<byte> delta_bytes;
        target_bytes.Resize(input.morph_targets.Size() * kMeshMorphTargetBytes);
        MemSet(target_bytes.Data(), 0, target_bytes.Size());
        u32 written = 0;
        for (usize t = 0; t < input.morph_targets.Size(); ++t)
        {
            const MeshMorphTarget& morph = input.morph_targets[t];
            const u32 first = written;
            for (const MeshMorphDelta& d : morph.deltas)
            {
                bool zero = true;
                for (u32 c = 0; c < 3; ++c)
                {
                    zero = zero && d.position[c] == 0.0f && d.normal[c] == 0.0f && d.tangent[c] == 0.0f;
                }
                if (zero)
                {
                    continue;  // an all-zero delta moves nothing
                }
                const usize at = delta_bytes.Size();
                delta_bytes.Resize(at + kMeshMorphDeltaBytes);
                byte* p = delta_bytes.Data() + at;
                MeshStore(p, d.vertex, 4);
                for (u32 c = 0; c < 3; ++c)
                {
                    MeshStoreF32(p + 4 + c * 4, d.position[c]);
                    MeshStoreF32(p + 16 + c * 4, d.normal[c]);
                    MeshStoreF32(p + 28 + c * 4, d.tangent[c]);
                }
                ++written;
            }
            byte* r = target_bytes.Data() + t * kMeshMorphTargetBytes;
            MemCopy(r, morph.name.Data(), kMaxMorphNameBytes);
            MeshStore(r + 32, first, 4);
            MeshStore(r + 36, written - first, 4);
        }
        ENGINE_TRY(writer.AddSection(kMeshSectionMorphTargets, ByteSpan(target_bytes.Data(), target_bytes.Size())));
        ENGINE_TRY(writer.AddSection(kMeshSectionMorphDeltas, ByteSpan(delta_bytes.Data(), delta_bytes.Size())));
    }
    return writer.Finish(out);
}

Status MeshView::Open(ByteSpan data, const TypeRegistry& types)
{
    *this = MeshView();
    BlobReader blob;
    ENGINE_TRY(blob.Open(data, types));
    if (blob.Type() != types::kMesh)
    {
        return Status::Make(ErrorCode::TypeMismatch, "the blob is not a mesh");
    }
    ByteSpan unused;
    ByteSpan header;
    if (!blob.FindSection(kMeshSectionHeader, &header) || header.Size() != kMeshHeaderBytes)
    {
        return MeshCorrupt("the mesh header section is missing or has the wrong size");
    }
    const byte* h = header.Data();
    const u64 vcount = MeshLoad(h, 4);
    const u64 icount = MeshLoad(h + 4, 4);
    const u64 width = MeshLoad(h + 8, 4);
    const u64 flags = MeshLoad(h + 12, 4);
    const u64 acount = MeshLoad(h + 16, 4);
    const u64 lcount = MeshLoad(h + 20, 4);
    const u64 scount = MeshLoad(h + 24, 4);
    const u64 jcount = MeshLoad(h + 28, 4);
    if (vcount == 0 || vcount > kMaxMeshVertices || icount == 0 || icount > kMaxMeshIndices || icount % 3 != 0 ||
        lcount == 0 || lcount > kMaxMeshLods || scount > kMaxMeshSlots || acount == 0 || acount > kMaxMeshAttributes)
    {
        return MeshCorrupt("a mesh count is zero, not whole triangles or above the limit");
    }
    if ((width != 2 && width != 4) || (width == 2 && vcount > 65536))
    {
        return MeshCorrupt("the index width is not 2 or 4, or too small for the vertex count");
    }
    if ((flags & ~static_cast<u64>(kMeshFlagSkinned | kMeshFlagPacked)) != 0)
    {
        return MeshCorrupt("a mesh header flag is not defined by this version");
    }
    const bool skinned = (flags & kMeshFlagSkinned) != 0;
    if (skinned != (jcount != 0) || jcount > 256)
    {
        return MeshCorrupt("the skin joint count does not match the skinned flag, or is above 256");
    }
    Array<f32, 3> bmin, bmax;
    for (u32 c = 0; c < 3; ++c)
    {
        bmin[c] = MeshLoadF32(h + 32 + c * 4);
        bmax[c] = MeshLoadF32(h + 44 + c * 4);
    }
    Array<f32, 2> uv_offset, uv_scale;
    for (u32 c = 0; c < 2; ++c)
    {
        uv_offset[c] = MeshLoadF32(h + 56 + c * 4);
        uv_scale[c] = MeshLoadF32(h + 64 + c * 4);
    }
    const f32 uv_error = MeshLoadF32(h + 72);
    const f32 normal_error = MeshLoadF32(h + 76);
    if (!MeshFinite(bmin.Data(), 3) || !MeshFinite(bmax.Data(), 3) || !MeshFinite(uv_offset.Data(), 2) ||
        !MeshFinite(uv_scale.Data(), 2) || !IsFinite(uv_error) || !IsFinite(normal_error) || bmin[0] > bmax[0] ||
        bmin[1] > bmax[1] || bmin[2] > bmax[2] || uv_scale[0] < 0.0f || uv_scale[1] < 0.0f)
    {
        return MeshCorrupt("a mesh bound or texture coordinate decode is not finite or is inverted");
    }

    // Attributes.
    ByteSpan attributes;
    if (!blob.FindSection(kMeshSectionAttributes, &attributes) || attributes.Size() != acount * kMeshAttributeBytes)
    {
        return MeshCorrupt("the mesh attribute section is missing or has the wrong size");
    }
    u32 stride[2] = {0, 0};
    u32 locations = 0;
    bool has_colors = false;
    for (u32 i = 0; i < acount; ++i)
    {
        const byte* a = attributes.Data() + i * kMeshAttributeBytes;
        const u64 location = MeshLoad(a, 4);
        const u64 format = MeshLoad(a + 4, 4);
        const u64 offset = MeshLoad(a + 8, 4);
        const u64 binding = MeshLoad(a + 12, 4);
        if (location >= 16 || format >= static_cast<u64>(rhi::VertexFormat::Count) || binding > 1 || offset % 4 != 0 ||
            (locations & (1u << location)) != 0)
        {
            return MeshCorrupt("a mesh attribute is out of range, misaligned or repeats a location");
        }
        locations |= 1u << location;
        has_colors = has_colors || (location == kMeshLocationColor &&
                                    format == static_cast<u64>(rhi::VertexFormat::UNorm8x4) && binding == 0);
        const u64 end = offset + rhi::VertexFormatSize(static_cast<rhi::VertexFormat>(format));
        if (end > 256)
        {
            return MeshCorrupt("a mesh attribute ends past 256 bytes of its vertex");
        }
        stride[binding] = end > stride[binding] ? static_cast<u32>(end) : stride[binding];
    }
    stride[0] = (stride[0] + 3) & ~3u;
    stride[1] = (stride[1] + 3) & ~3u;
    if (stride[0] == 0 || (stride[1] != 0) != skinned || (skinned && stride[1] != kMeshSkinStride))
    {
        return MeshCorrupt("the attributes do not match the streams the mesh declares");
    }

    ByteSpan vertices, indices, lods, slots, skin, joint_bounds;
    if (!blob.FindSection(kMeshSectionVertices, &vertices) || vertices.Size() != vcount * stride[0])
    {
        return MeshCorrupt("the vertex section is missing or its size is not the stride times the count");
    }
    if (!blob.FindSection(kMeshSectionIndices, &indices) || indices.Size() != icount * width)
    {
        return MeshCorrupt("the index section is missing or has the wrong size");
    }
    if (!blob.FindSection(kMeshSectionLods, &lods) || lods.Size() != lcount * kMeshLodBytes)
    {
        return MeshCorrupt("the LOD section is missing or has the wrong size");
    }
    if (scount != 0 && (!blob.FindSection(kMeshSectionSlots, &slots) || slots.Size() != scount * kMeshSlotBytes))
    {
        return MeshCorrupt("the slot section is missing or has the wrong size");
    }
    if (scount == 0 && blob.FindSection(kMeshSectionSlots, &slots))
    {
        return MeshCorrupt("a slot section is present but the header counts none");
    }
    if (skinned)
    {
        if (!blob.FindSection(kMeshSectionSkin, &skin) || skin.Size() != vcount * stride[1] ||
            !blob.FindSection(kMeshSectionJointBounds, &joint_bounds) ||
            joint_bounds.Size() != jcount * kMeshJointBoundsBytes)
        {
            return MeshCorrupt("a skin section is missing or has the wrong size");
        }
        if (blob.DependencyCount() != 1)
        {
            return MeshCorrupt("a skinned mesh declares exactly one dependency, its skeleton");
        }
        const Dependency d = blob.DependencyAt(0);
        if (d.type != types::kSkeleton || d.role != kMeshRoleSkinSkeleton)
        {
            return MeshCorrupt("the skin dependency is not a skeleton with the skin role");
        }
        skeleton_ = d.id;
        MemCopy(skeleton_hash_.bytes, h + 80, 16);
        for (u32 i = 0; i < jcount; ++i)
        {
            const byte* box = joint_bounds.Data() + i * kMeshJointBoundsBytes;
            const u64 used = MeshLoad(box + 24, 4);
            f32 values[6];
            MemCopy(values, box, 24);
            if (used > 1 || !MeshFinite(values, 6) ||
                (used != 0 && (values[0] > values[3] || values[1] > values[4] || values[2] > values[5])))
            {
                return MeshCorrupt("a joint bound is not finite or is inverted");
            }
        }
    }
    else if (blob.DependencyCount() != 0 || blob.FindSection(kMeshSectionSkin, &unused) ||
             blob.FindSection(kMeshSectionJointBounds, &unused))
    {
        return MeshCorrupt("a mesh that is not skinned holds a skin section or a dependency");
    }

    // Morph targets: every delta checked, since the GPU indexes vertices with them.
    ByteSpan morph_targets;
    ByteSpan morph_deltas;
    const bool has_targets = blob.FindSection(kMeshSectionMorphTargets, &morph_targets);
    const bool has_deltas = blob.FindSection(kMeshSectionMorphDeltas, &morph_deltas);
    u64 target_count = 0;
    u64 delta_count = 0;
    if (has_targets != has_deltas)
    {
        return MeshCorrupt("a morph section is missing its other half");
    }
    if (has_targets)
    {
        if (morph_targets.Size() == 0 || morph_targets.Size() % kMeshMorphTargetBytes != 0 ||
            morph_targets.Size() / kMeshMorphTargetBytes > kMaxMorphTargets ||
            morph_deltas.Size() % kMeshMorphDeltaBytes != 0)
        {
            return MeshCorrupt("a morph section is mis-sized or holds more targets than the limit");
        }
        target_count = morph_targets.Size() / kMeshMorphTargetBytes;
        delta_count = morph_deltas.Size() / kMeshMorphDeltaBytes;
        u64 next = 0;
        for (u64 t = 0; t < target_count; ++t)
        {
            const byte* r = morph_targets.Data() + t * kMeshMorphTargetBytes;
            const u64 first = MeshLoad(r + 32, 4);
            const u64 count = MeshLoad(r + 36, 4);
            if (r[0] == 0 || r[kMaxMorphNameBytes] != 0 || first != next || count > delta_count - next ||
                MeshLoad(r + 40, 4) != 0 || MeshLoad(r + 44, 4) != 0)
            {
                return MeshCorrupt("a morph target has no name or its deltas do not follow the one before");
            }
            for (u64 k = first; k < first + count; ++k)
            {
                const byte* d = morph_deltas.Data() + k * kMeshMorphDeltaBytes;
                const u64 vertex = MeshLoad(d, 4);
                f32 values[9];
                MemCopy(values, d + 4, 36);
                if (vertex >= vcount || (k > first && vertex <= MeshLoad(d - kMeshMorphDeltaBytes, 4)) ||
                    !MeshFinite(values, 9))
                {
                    return MeshCorrupt("a morph delta is past the vertices, out of order or not finite");
                }
            }
            next = first + count;
        }
        if (next != delta_count)
        {
            return MeshCorrupt("the morph deltas are not covered exactly by the targets");
        }
    }

    // LODs and slots.
    f32 previous_screen = 0;
    for (u32 i = 0; i < lcount; ++i)
    {
        const byte* l = lods.Data() + i * kMeshLodBytes;
        const f32 screen = MeshLoadF32(l + 8);
        if (!MeshRange(static_cast<u32>(MeshLoad(l, 4)), static_cast<u32>(MeshLoad(l + 4, 4)),
                       static_cast<u32>(icount)) ||
            !IsFinite(screen) || screen < 0.0f || (i > 0 && screen > previous_screen) || MeshLoad(l + 12, 4) != 0)
        {
            return MeshCorrupt("a LOD range is outside the indices, or the screen sizes grow");
        }
        previous_screen = screen;
    }
    for (u32 i = 0; i < scount; ++i)
    {
        const byte* s = slots.Data() + i * kMeshSlotBytes;
        usize length = 0;
        while (length < 32 && s[length] != 0)
        {
            ++length;
        }
        for (usize k = length; k < 32; ++k)
        {
            if (s[k] != 0)
            {
                return MeshCorrupt("a slot name has bytes after its terminator");
            }
        }
        if (length == 0 || length > kMaxSlotNameBytes ||
            !MeshRange(static_cast<u32>(MeshLoad(s + 32, 4)), static_cast<u32>(MeshLoad(s + 36, 4)),
                       static_cast<u32>(icount)))
        {
            return MeshCorrupt("a slot name is empty or too long, or its range is outside the indices");
        }
    }

    vertex_count_ = static_cast<u32>(vcount);
    index_count_ = static_cast<u32>(icount);
    index_width_ = static_cast<u32>(width);
    skinned_ = skinned;
    has_colors_ = has_colors;
    morph_target_count_ = static_cast<u32>(target_count);
    morph_delta_count_ = static_cast<u32>(delta_count);
    morph_targets_ = morph_targets;
    morph_deltas_ = morph_deltas;
    quality_ = (flags & kMeshFlagPacked) != 0 ? MeshQuality::Packed : MeshQuality::Full;
    attribute_count_ = static_cast<u32>(acount);
    stride_ = stride[0];
    lod_count_ = static_cast<u32>(lcount);
    slot_count_ = static_cast<u32>(scount);
    skin_joint_count_ = static_cast<u32>(jcount);
    bounds_min_ = bmin;
    bounds_max_ = bmax;
    uv_offset_ = uv_offset;
    uv_scale_ = uv_scale;
    uv_error_ = uv_error;
    normal_error_ = normal_error;
    attributes_ = attributes;
    vertices_ = vertices;
    indices_ = indices;
    lods_ = lods;
    slots_ = slots;
    skin_ = skin;
    joint_bounds_ = joint_bounds;
    return Status::Success();
}

MeshAttribute MeshView::Attribute(u32 i) const
{
    ENGINE_ASSERT(i < attribute_count_, "MeshView::Attribute: index out of range");
    const byte* a = attributes_.Data() + i * kMeshAttributeBytes;
    MeshAttribute out;
    out.location = static_cast<u32>(MeshLoad(a, 4));
    out.format = static_cast<rhi::VertexFormat>(MeshLoad(a + 4, 4));
    out.offset = static_cast<u32>(MeshLoad(a + 8, 4));
    out.binding = static_cast<u32>(MeshLoad(a + 12, 4));
    return out;
}

MeshLod MeshView::Lod(u32 i) const
{
    ENGINE_ASSERT(i < lod_count_, "MeshView::Lod: index out of range");
    const byte* l = lods_.Data() + i * kMeshLodBytes;
    MeshLod out;
    out.first_index = static_cast<u32>(MeshLoad(l, 4));
    out.index_count = static_cast<u32>(MeshLoad(l + 4, 4));
    out.screen_size = MeshLoadF32(l + 8);
    return out;
}

MeshSlot MeshView::Slot(u32 i) const
{
    ENGINE_ASSERT(i < slot_count_, "MeshView::Slot: index out of range");
    const byte* s = slots_.Data() + i * kMeshSlotBytes;
    MeshSlot out;
    MemCopy(out.name.Data(), s, 32);
    out.first_index = static_cast<u32>(MeshLoad(s + 32, 4));
    out.index_count = static_cast<u32>(MeshLoad(s + 36, 4));
    return out;
}

MeshJointBounds MeshView::JointBounds(u32 i) const
{
    ENGINE_ASSERT(i < skin_joint_count_, "MeshView::JointBounds: index out of range");
    const byte* box = joint_bounds_.Data() + i * kMeshJointBoundsBytes;
    MeshJointBounds out;
    out.used = MeshLoad(box + 24, 4) != 0;
    MemCopy(out.min.Data(), box, 12);
    MemCopy(out.max.Data(), box + 12, 12);
    return out;
}

Status MeshView::CheckSkeleton(const SkeletonView& skeleton) const
{
    if (!skinned_)
    {
        return Status::Success();
    }
    if (skeleton.Id() != skeleton_ || skeleton.Hash() != skeleton_hash_ || skeleton.JointCount() != skin_joint_count_)
    {
        return Status::Make(ErrorCode::DependencyTypeMismatch,
                            "the skeleton is not the one this mesh was skinned against");
    }
    return Status::Success();
}

Status MeshView::CheckIndices() const
{
    for (u32 i = 0; i < index_count_; ++i)
    {
        const u64 index = MeshLoad(indices_.Data() + static_cast<usize>(i) * index_width_, index_width_);
        if (index >= vertex_count_)
        {
            return MeshCorrupt("an index is at or past the vertex count");
        }
    }
    return Status::Success();
}

Status MeshView::CheckSkin() const
{
    for (u32 v = 0; v < (skinned_ ? vertex_count_ : 0u); ++v)
    {
        const byte* p = skin_.Data() + static_cast<usize>(v) * 8;
        u32 sum = 0;
        for (u32 k = 0; k < 4; ++k)
        {
            sum += p[4 + k];
            if (p[4 + k] != 0 && p[k] >= skin_joint_count_)
            {
                return MeshCorrupt("a skin joint is at or past the joint count");
            }
        }
        if (sum != 255)
        {
            return MeshCorrupt("the skin weights of a vertex do not sum to 255");
        }
    }
    return Status::Success();
}

}  // namespace vse::asset

namespace vse::asset
{

MeshMorphTargetInfo MeshView::MorphTarget(u32 i) const
{
    ENGINE_ASSERT(i < morph_target_count_, "MeshView::MorphTarget: index out of range");
    const byte* r = morph_targets_.Data() + static_cast<usize>(i) * kMeshMorphTargetBytes;
    MeshMorphTargetInfo out;
    MemCopy(out.name.Data(), r, kMaxMorphNameBytes);
    out.name[kMaxMorphNameBytes] = '\0';
    out.first_delta = static_cast<u32>(MeshLoad(r + 32, 4));
    out.delta_count = static_cast<u32>(MeshLoad(r + 36, 4));
    return out;
}

MeshMorphDelta MeshView::MorphDelta(u32 k) const
{
    ENGINE_ASSERT(k < morph_delta_count_, "MeshView::MorphDelta: index out of range");
    const byte* d = morph_deltas_.Data() + static_cast<usize>(k) * kMeshMorphDeltaBytes;
    MeshMorphDelta out;
    out.vertex = static_cast<u32>(MeshLoad(d, 4));
    for (u32 c = 0; c < 3; ++c)
    {
        out.position[c] = MeshLoadF32(d + 4 + c * 4);
        out.normal[c] = MeshLoadF32(d + 16 + c * 4);
        out.tangent[c] = MeshLoadF32(d + 28 + c * 4);
    }
    return out;
}

}  // namespace vse::asset
