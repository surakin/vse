#include "mesh_source.h"

#include "cstr/cstr.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"
#include "source_text.h"

namespace vse::asset
{
namespace
{

void MshSetError(MeshSourceError* error, u32 line, const char* message)
{
    error->line = line;
    FormatTo(error->message.Data(), error->message.Size(), "%s", message);
}

Status MshFail(MeshSourceError* error, u32 line, const char* message)
{
    MshSetError(error, line, message);
    return Status::Make(ErrorCode::Invalid, "the mesh source is invalid");
}

Status MshLimit(MeshSourceError* error, u32 line, const char* message)
{
    MshSetError(error, line, message);
    return Status::Make(ErrorCode::LimitExceeded, "the mesh source is above a limit of this version");
}

}  // namespace

Status ParseMeshSource(StringView text, MeshSource* out, MeshSourceError* error)
{
    *out = MeshSource();
    *error = MeshSourceError();
    bool have_id = false;
    bool have_quality = false;
    bool any_skin = false;
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
            return MshFail(error, line_number, "too many fields on the line");
        }
        const StringView kind = t.tokens[0];
        if (kind == StringView("id"))
        {
            if (have_id)
            {
                return MshFail(error, line_number, "the id is given twice");
            }
            if (t.count != 2 || ParseResourceId(t.tokens[1], &out->id).Failed())
            {
                return MshFail(error, line_number, "id needs one 16-digit hex resource id");
            }
            have_id = true;
            continue;
        }
        if (!have_id)
        {
            return MshFail(error, line_number, "the id line must come first (asset_mint adopt writes it)");
        }
        if (kind == StringView("quality"))
        {
            if (have_quality || out->vertices.Size() != 0)
            {
                return MshFail(error, line_number, "quality is given once, before the vertices");
            }
            if (t.count == 2 && t.tokens[1] == StringView("packed"))
            {
                out->quality = MeshQuality::Packed;
            }
            else if (t.count == 2 && t.tokens[1] == StringView("full"))
            {
                out->quality = MeshQuality::Full;
            }
            else
            {
                return MshFail(error, line_number, "quality is packed or full");
            }
            have_quality = true;
        }
        else if (kind == StringView("skeleton"))
        {
            if (!out->skeleton.IsNone())
            {
                return MshFail(error, line_number, "the skeleton is given twice");
            }
            if (t.count != 2 || ParseResourceId(t.tokens[1], &out->skeleton).Failed())
            {
                return MshFail(error, line_number, "skeleton needs one 16-digit hex resource id");
            }
            out->skeleton_line = line_number;
        }
        else if (kind == StringView("v"))
        {
            if (out->indices.Size() != 0 || any_skin)
            {
                return MshFail(error, line_number, "vertices must come before indices and skin records");
            }
            // 16 numbers: a linear RGBA colour after the uv (glTF coverage Day 4); every v of a mesh has one or none.
            if (t.count != 13 && t.count != 17)
            {
                return MshFail(error, line_number,
                               "v needs 12 numbers (position, normal, tangent xyzw, uv) or 16 (and a colour)");
            }
            const bool colored = t.count == 17;
            if (out->vertices.Size() != 0 && colored != (out->colors.Size() != 0))
            {
                return MshFail(error, line_number, "every v has a colour, or none does");
            }
            if (out->vertices.Size() >= kMaxMeshVertices)
            {
                return MshLimit(error, line_number, "more vertices than the format allows");
            }
            f32 n[16];
            for (u32 k = 0; k + 1 < t.count; ++k)
            {
                if (!srctext::ParseFloat(t.tokens[1 + k], &n[k]))
                {
                    return MshFail(error, line_number, "a number is malformed or not finite");
                }
            }
            MeshVertex v;
            for (u32 k = 0; k < 3; ++k)
            {
                v.position[k] = n[k];
                v.normal[k] = n[3 + k];
                v.tangent[k] = n[6 + k];
            }
            v.tangent[3] = n[9];
            v.uv[0] = n[10];
            v.uv[1] = n[11];
            if (colored)
            {
                out->colors.Push(EncodeMeshColor(Array<f32, 4>{{n[12], n[13], n[14], n[15]}}));
            }
            if ((n[3] == 0.0f && n[4] == 0.0f && n[5] == 0.0f) || (n[6] == 0.0f && n[7] == 0.0f && n[8] == 0.0f))
            {
                return MshFail(error, line_number, "a normal or tangent has zero length");
            }
            if (n[9] != 1.0f && n[9] != -1.0f)
            {
                return MshFail(error, line_number, "the bitangent sign (tangent w) is +1 or -1");
            }
            out->vertices.Push(v);
            out->vertex_lines.Push(line_number);
        }
        else if (kind == StringView("i"))
        {
            if (t.count != 4)
            {
                return MshFail(error, line_number, "i needs three vertex numbers");
            }
            if (out->indices.Size() + 3 > kMaxMeshIndices)
            {
                return MshLimit(error, line_number, "more indices than the format allows");
            }
            u32 idx[3];
            for (u32 k = 0; k < 3; ++k)
            {
                if (!srctext::ParseUnsigned(t.tokens[1 + k], &idx[k]))
                {
                    return MshFail(error, line_number, "a vertex number is malformed");
                }
                if (idx[k] >= out->vertices.Size())
                {
                    return MshFail(error, line_number, "an index is at or past the vertices defined so far");
                }
            }
            for (u32 k = 0; k < 3; ++k)
            {
                out->indices.Push(idx[k]);
            }
        }
        else if (kind == StringView("lod"))
        {
            MeshLod lod;
            if (t.count != 4 || !srctext::ParseUnsigned(t.tokens[1], &lod.first_index) ||
                !srctext::ParseUnsigned(t.tokens[2], &lod.index_count) ||
                !srctext::ParseFloat(t.tokens[3], &lod.screen_size))
            {
                return MshFail(error, line_number, "lod needs FIRST COUNT SCREEN");
            }
            if (out->lods.Size() >= kMaxMeshLods)
            {
                return MshLimit(error, line_number, "more LODs than the format allows");
            }
            out->lods.Push(lod);
        }
        else if (kind == StringView("slot"))
        {
            MeshSlot slot;
            if (t.count != 4 || !srctext::ParseUnsigned(t.tokens[2], &slot.first_index) ||
                !srctext::ParseUnsigned(t.tokens[3], &slot.index_count))
            {
                return MshFail(error, line_number, "slot needs NAME FIRST COUNT");
            }
            if (t.tokens[1].Length() > kMaxSlotNameBytes)
            {
                return MshFail(error, line_number, "a slot name is longer than 31 bytes");
            }
            if (out->slots.Size() >= kMaxMeshSlots)
            {
                return MshLimit(error, line_number, "more slots than the format allows");
            }
            MemCopy(slot.name.Data(), t.tokens[1].Data(), t.tokens[1].Length());
            out->slots.Push(slot);
        }
        else if (kind == StringView("morph"))
        {
            // glTF coverage Day 6: a morph target; the `d` records after it are its deltas.
            if (t.count != 2 || t.tokens[1].Length() > kMaxMorphNameBytes)
            {
                return MshFail(error, line_number, "morph needs a NAME of up to 31 bytes");
            }
            if (out->morphs.Size() >= kMaxMorphTargets)
            {
                return MshLimit(error, line_number, "more morph targets than the format allows");
            }
            MeshSourceMorph morph;
            MemCopy(morph.name.Data(), t.tokens[1].Data(), t.tokens[1].Length());
            morph.line = line_number;
            out->morphs.Push(static_cast<MeshSourceMorph&&>(morph));
        }
        else if (kind == StringView("d"))
        {
            if (out->morphs.Size() == 0)
            {
                return MshFail(error, line_number, "a d record needs a morph record before it");
            }
            if (t.count != 5 && t.count != 8 && t.count != 11)
            {
                return MshFail(error, line_number, "d needs VERTEX and 3, 6 or 9 numbers (position, normal, tangent)");
            }
            MeshMorphDelta d;
            if (!srctext::ParseUnsigned(t.tokens[1], &d.vertex) || d.vertex >= out->vertices.Size())
            {
                return MshFail(error, line_number, "a d record names a vertex that is not defined");
            }
            f32 n[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
            for (u32 k = 0; k + 2 < t.count; ++k)
            {
                if (!srctext::ParseFloat(t.tokens[2 + k], &n[k]))
                {
                    return MshFail(error, line_number, "a number is malformed or not finite");
                }
            }
            for (u32 c = 0; c < 3; ++c)
            {
                d.position[c] = n[c];
                d.normal[c] = n[3 + c];
                d.tangent[c] = n[6 + c];
            }
            MeshSourceMorph& morph = out->morphs[out->morphs.Size() - 1];
            if (morph.deltas.Size() != 0 && d.vertex <= morph.deltas[morph.deltas.Size() - 1].vertex)
            {
                return MshFail(error, line_number, "the d records of a morph go up by vertex, without repeats");
            }
            morph.deltas.Push(d);
        }
        else if (kind == StringView("skin"))
        {
            if (t.count < 4 || t.count % 2 != 0)
            {
                return MshFail(error, line_number, "skin needs VERTEX and one or more JOINT WEIGHT pairs");
            }
            if (out->skeleton.IsNone())
            {
                return MshFail(error, line_number, "a skin record needs a skeleton record before it");
            }
            u32 vertex = 0;
            if (!srctext::ParseUnsigned(t.tokens[1], &vertex) || vertex >= out->vertices.Size())
            {
                return MshFail(error, line_number, "skin names a vertex that is not defined");
            }
            if (!any_skin)
            {
                any_skin = true;
                out->skin.Resize(out->vertices.Size());
            }
            MeshSourceSkin& entry = out->skin[vertex];
            if (entry.count != 0)
            {
                return MshFail(error, line_number, "a vertex is skinned twice");
            }
            entry.line = line_number;
            for (usize k = 2; k + 1 < t.count; k += 2)
            {
                u32 joint = 0;
                f32 weight = 0;
                if (!srctext::ParseUnsigned(t.tokens[k], &joint) || joint > 255 ||
                    !srctext::ParseFloat(t.tokens[k + 1], &weight))
                {
                    return MshFail(error, line_number, "a joint number (0 to 255) or a weight is malformed");
                }
                if (weight < 0.0f)
                {
                    return MshFail(error, line_number, "a weight is negative");
                }
                // The same joint twice is one influence with the summed weight.
                bool merged = false;
                for (u32 m = 0; m < entry.count; ++m)
                {
                    if (entry.joint[m] == joint)
                    {
                        entry.weight[m] += weight;
                        merged = true;
                    }
                }
                if (!merged)
                {
                    entry.joint[entry.count] = joint;
                    entry.weight[entry.count] = weight;
                    ++entry.count;
                }
            }
        }
        else
        {
            return MshFail(error, line_number, "unknown record");
        }
    }
    if (!have_id)
    {
        return MshFail(error, 0, "the source has no id line (asset_mint adopt writes it)");
    }
    if (out->vertices.Size() == 0 || out->indices.Size() == 0)
    {
        return MshFail(error, 0, "the source has no vertices or no indices");
    }
    if (!out->skeleton.IsNone() && !any_skin)
    {
        return MshFail(error, out->skeleton_line, "the mesh names a skeleton but has no skin records");
    }
    if (any_skin)
    {
        for (usize i = 0; i < out->skin.Size(); ++i)
        {
            bool positive = false;
            for (u32 k = 0; k < out->skin[i].count; ++k)
            {
                positive = positive || out->skin[i].weight[k] > 0.0;
            }
            if (!positive)
            {
                return MshFail(error, out->vertex_lines[i],
                               "a skinned mesh vertex has no influence with a weight above 0");
            }
        }
    }
    return Status::Success();
}

Status MintMesh(StringView text, const TypeRegistry& types, const SkeletonView* skeleton, Vector<byte>* out,
                MeshSourceError* error, u32 target)
{
    MeshSource source;
    ENGINE_TRY(ParseMeshSource(text, &source, error));
    MeshBuildInput input;
    input.vertices = Span<const MeshVertex>(source.vertices.Data(), source.vertices.Size());
    input.indices = Span<const u32>(source.indices.Data(), source.indices.Size());
    input.lods = Span<const MeshLod>(source.lods.Data(), source.lods.Size());
    input.slots = Span<const MeshSlot>(source.slots.Data(), source.slots.Size());
    input.quality = source.quality;
    input.colors = Span<const Array<u8, 4>>(source.colors.Data(), source.colors.Size());
    Vector<MeshMorphTarget> morphs;
    for (const MeshSourceMorph& m : source.morphs)
    {
        MeshMorphTarget morph;
        morph.name = m.name;
        morph.deltas = Span<const MeshMorphDelta>(m.deltas.Data(), m.deltas.Size());
        morphs.Push(morph);
    }
    input.morph_targets = Span<const MeshMorphTarget>(morphs.Data(), morphs.Size());

    Vector<MeshSkinInfluence> skin;
    if (!source.skeleton.IsNone())
    {
        if (skeleton == nullptr || skeleton->Id() != source.skeleton)
        {
            return MshFail(error, source.skeleton_line, "the skeleton blob given is not the one the source names");
        }
        for (usize i = 0; i < source.skin.Size(); ++i)
        {
            const MeshSourceSkin& entry = source.skin[i];
            f64 weights[12];
            for (u32 k = 0; k < entry.count; ++k)
            {
                if (entry.joint[k] >= skeleton->JointCount())
                {
                    return MshFail(error, entry.line, "a joint number is at or past the skeleton's joint count");
                }
                weights[k] = entry.weight[k];
            }
            MeshSkinInfluence q;
            if (!QuantizeSkinInfluences(entry.joint.Data(), weights, entry.count, &q))
            {
                return MshFail(error, entry.line, "a vertex has no influence with a weight above 0");
            }
            skin.Push(q);
        }
        input.skin = Span<const MeshSkinInfluence>(skin.Data(), skin.Size());
        input.skeleton = source.skeleton;
        input.skeleton_hash = skeleton->Hash();
        input.skeleton_joint_count = skeleton->JointCount();
    }
    out->Clear();
    const Status built = BuildMeshBlob(types, source.id, input, out, target);
    if (built.Failed())
    {
        MshSetError(error, 0, built.Message());
    }
    return built;
}

}  // namespace vse::asset
