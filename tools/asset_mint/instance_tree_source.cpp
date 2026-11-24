#include "instance_tree_source.h"

#include "cstr/cstr.h"
#include "cstr/format.h"
#include "cstr/number.h"
#include "lang/scalar.h"
#include "source_text.h"

namespace vse::asset
{
namespace
{

Status ItFail(InstanceTreeSourceError* error, u32 line, const char* message)
{
    error->line = line;
    FormatTo(error->message.Data(), error->message.Size(), "%s", message);
    return Status::Make(ErrorCode::Invalid, "the instance tree source is invalid");
}

bool ItParseDouble(StringView token, f64* out)
{
    const ParseDoubleResult r = ParseDoubleExact(token.Data(), token.Data() + token.Length());
    if (!r.ok || r.end != token.Data() + token.Length() || !IsFinite(r.value))
    {
        return false;
    }
    *out = r.value;
    return true;
}

}  // namespace

Status ParseInstanceTreeSource(StringView text, InstanceTreeSource* out, InstanceTreeSourceError* error)
{
    *out = InstanceTreeSource();
    *error = InstanceTreeSourceError();
    bool have_id = false;
    bool have_node = false;
    bool with_data = false;
    Vector<u32> depth;
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
            return ItFail(error, line_number, "too many fields on the line");
        }
        const StringView kind = t.tokens[0];
        if (kind == StringView("id"))
        {
            if (have_id)
            {
                return ItFail(error, line_number, "the id is given twice");
            }
            if (t.count != 2 || ParseResourceId(t.tokens[1], &out->id).Failed())
            {
                return ItFail(error, line_number, "id needs one 16-digit hex resource id");
            }
            have_id = true;
            continue;
        }
        if (!have_id)
        {
            return ItFail(error, line_number, "the id line must come first (asset_mint adopt writes it)");
        }
        if (kind == StringView("mesh"))
        {
            if (have_node)
            {
                return ItFail(error, line_number, "mesh lines come before the node lines");
            }
            if (t.count != 7 || out->meshes.Size() >= renderer::kMaxTreeMeshes)
            {
                return ItFail(error, line_number, "mesh needs six numbers (at most 256 meshes)");
            }
            f32 v[6];
            for (u32 k = 0; k < 6; ++k)
            {
                if (!srctext::ParseFloat(t.tokens[1 + k], &v[k]))
                {
                    return ItFail(error, line_number, "a mesh box value is not a finite number");
                }
            }
            if (v[0] > v[3] || v[1] > v[4] || v[2] > v[5])
            {
                return ItFail(error, line_number, "a mesh box has its minimum above its maximum");
            }
            renderer::TreeMeshInfo m;
            m.bounds = Aabb::FromMinMax(Vec3(v[0], v[1], v[2]), Vec3(v[3], v[4], v[5]));
            out->meshes.Push(m);
            continue;
        }
        if (kind != StringView("node"))
        {
            return ItFail(error, line_number, "an unknown record");
        }
        if (t.count != 14 && t.count != 19)
        {
            return ItFail(error, line_number, "node needs 14 fields, or 19 with a tint and a custom value");
        }
        const bool data_here = t.count == 19;
        if (have_node && data_here != with_data)
        {
            return ItFail(error, line_number, "every node must have the tint and custom value, or none");
        }
        with_data = data_here;
        have_node = true;
        InstanceTreeSourceNode node;
        u32 node_depth = 0;
        if (t.tokens[1] != StringView("-"))
        {
            u32 parent = 0;
            if (!srctext::ParseUnsigned(t.tokens[1], &parent) || parent >= out->nodes.Size())
            {
                return ItFail(error, line_number, "the parent is not an earlier node");
            }
            node.parent = parent;
            node_depth = depth[parent] + 1;
            if (node_depth > renderer::kMaxTreeDepth)
            {
                return ItFail(error, line_number, "the tree is deeper than the limit");
            }
        }
        f64 position[3];
        f32 rotation[4];
        f32 scale[3];
        for (u32 k = 0; k < 3; ++k)
        {
            if (!ItParseDouble(t.tokens[2 + k], &position[k]) || Abs(position[k]) > renderer::kMaxInstanceCoordinate)
            {
                return ItFail(error, line_number, "a position is not a finite number within 1e12 metres");
            }
        }
        for (u32 k = 0; k < 4; ++k)
        {
            if (!srctext::ParseFloat(t.tokens[5 + k], &rotation[k]))
            {
                return ItFail(error, line_number, "a rotation value is not a finite number");
            }
        }
        for (u32 k = 0; k < 3; ++k)
        {
            if (!srctext::ParseFloat(t.tokens[9 + k], &scale[k]) || !(scale[k] > 0.0f))
            {
                return ItFail(error, line_number, "a scale is not a finite number above 0");
            }
        }
        const f32 length_squared = rotation[0] * rotation[0] + rotation[1] * rotation[1] + rotation[2] * rotation[2] +
                                   rotation[3] * rotation[3];
        if (Abs(length_squared - 1.0f) > 2.0e-3f)
        {
            return ItFail(error, line_number, "the rotation is not a unit quaternion");
        }
        if (t.tokens[12] == StringView("-"))
        {
            node.desc.mesh = renderer::kTreeNoMesh;
        }
        else if (!srctext::ParseUnsigned(t.tokens[12], &node.desc.mesh) || node.desc.mesh >= out->meshes.Size())
        {
            return ItFail(error, line_number, "the mesh is not a listed mesh index or -");
        }
        if (!srctext::ParseUnsigned(t.tokens[13], &node.desc.material_slot) ||
            node.desc.material_slot >= renderer::kMaxTreeMaterialSlots)
        {
            return ItFail(error, line_number, "the material slot is not below 256");
        }
        if (data_here)
        {
            f32 tint[4];
            for (u32 k = 0; k < 4; ++k)
            {
                if (!srctext::ParseFloat(t.tokens[14 + k], &tint[k]))
                {
                    return ItFail(error, line_number, "a tint value is not a finite number");
                }
            }
            if (!srctext::ParseUnsigned(t.tokens[18], &node.desc.extra.custom))
            {
                return ItFail(error, line_number, "the custom value is not an unsigned number");
            }
            node.desc.extra.tint = Vec4(tint[0], tint[1], tint[2], tint[3]);
        }
        node.desc.position = Vec3d(position[0], position[1], position[2]);
        node.desc.rotation = Quaternion(rotation[0], rotation[1], rotation[2], rotation[3]);
        node.desc.scale = Vec3(scale[0], scale[1], scale[2]);
        if (out->nodes.Size() >= renderer::kMaxTreeNodes)
        {
            return ItFail(error, line_number, "more nodes than a tree holds");
        }
        out->nodes.Push(node);
        depth.Push(node_depth);
    }
    if (!have_id)
    {
        return ItFail(error, 0, "the id line is missing (asset_mint adopt writes it)");
    }
    if (!have_node)
    {
        return ItFail(error, 0, "there is no node line");
    }
    return Status::Success();
}

Status MintInstanceTree(StringView text, const TypeRegistry& types, Vector<byte>* out, InstanceTreeSourceError* error,
                        u32 target)
{
    InstanceTreeSource source;
    ENGINE_TRY(ParseInstanceTreeSource(text, &source, error));
    const Status built = BuildInstanceTreeBlob(
        types, source.id, Span<const renderer::TreeMeshInfo>(source.meshes.Data(), source.meshes.Size()),
        Span<const InstanceTreeSourceNode>(source.nodes.Data(), source.nodes.Size()), out, target);
    if (built.Failed())
    {
        return ItFail(error, 0, "the instance tree could not be built");
    }
    return Status::Success();
}

}  // namespace vse::asset
