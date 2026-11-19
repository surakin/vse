#include "asset_import/gltf_nodes.h"

#include "cstr/format.h"
#include "lang/scalar.h"
#include "lang/status_try.h"

namespace engine_placeholder::asset
{

namespace
{

Status RefuseNode(GltfError* error, StringView where, StringView what)
{
    error->Set(where, what);
    return Status::Make(ErrorCode::Invalid, "a glTF node is refused");
}

// Reads `count` numbers of the array member `key` of `node` into `out`; false when the member is absent.
Status ReadNumbers(const JsonValue& node, const char* key, usize count, f64* out, bool* present, GltfError* error)
{
    const JsonValue array = node.Get(StringView(key));
    *present = array.Exists();
    if (!array.Exists())
    {
        return Status::Success();
    }
    if (!array.IsArray() || array.Size() != count)
    {
        char what[48];
        FormatTo(what, sizeof(what), "is not an array of %u numbers", static_cast<unsigned>(count));
        return RefuseNode(error, array.Path(), StringView(what, CStrLength(what)));
    }
    for (usize i = 0; i < count; ++i)
    {
        ENGINE_TRY(array.At(i).AsDouble(&out[i], error));
    }
    return Status::Success();
}

}  // namespace

GltfMatrix MultiplyMatrices(const GltfMatrix& a, const GltfMatrix& b)
{
    GltfMatrix out;
    for (usize col = 0; col < 4; ++col)
    {
        for (usize row = 0; row < 4; ++row)
        {
            f64 sum = 0.0;
            for (usize k = 0; k < 4; ++k)
            {
                sum += a.m[k * 4 + row] * b.m[col * 4 + k];
            }
            out.m[col * 4 + row] = sum;
        }
    }
    return out;
}

bool InvertAffineMatrix(const GltfMatrix& matrix, GltfMatrix* out)
{
    const f64* m = matrix.m.Data();
    if (Abs(m[3]) > 1e-6 || Abs(m[7]) > 1e-6 || Abs(m[11]) > 1e-6 || Abs(m[15] - 1.0) > 1e-6)
    {
        return false;
    }
    // The 3x3 part (column-major: column c is m[c * 4 + 0..2]) by its adjugate.
    const f64 a = m[0], b = m[4], c = m[8];
    const f64 d = m[1], e = m[5], f = m[9];
    const f64 g = m[2], h = m[6], i = m[10];
    const f64 co_a = e * i - f * h;
    const f64 co_b = -(d * i - f * g);
    const f64 co_c = d * h - e * g;
    const f64 det = a * co_a + b * co_b + c * co_c;
    if (!(Abs(det) > 1e-300))
    {
        return false;
    }
    const f64 r = 1.0 / det;
    GltfMatrix inv;
    // Row-major inverse entries placed column-major.
    inv.m[0] = co_a * r;
    inv.m[4] = -(b * i - c * h) * r;
    inv.m[8] = (b * f - c * e) * r;
    inv.m[1] = co_b * r;
    inv.m[5] = (a * i - c * g) * r;
    inv.m[9] = -(a * f - c * d) * r;
    inv.m[2] = co_c * r;
    inv.m[6] = -(a * h - b * g) * r;
    inv.m[10] = (a * e - b * d) * r;
    for (usize row = 0; row < 3; ++row)
    {
        inv.m[12 + row] = -(inv.m[row] * m[12] + inv.m[4 + row] * m[13] + inv.m[8 + row] * m[14]);
    }
    inv.m[3] = 0.0;
    inv.m[7] = 0.0;
    inv.m[11] = 0.0;
    inv.m[15] = 1.0;
    for (usize k = 0; k < 16; ++k)
    {
        if (!IsFinite(inv.m[k]))
        {
            return false;
        }
    }
    *out = inv;
    return true;
}

GltfMatrix MatrixFromTrs(const GltfTrs& trs)
{
    const f64 x = trs.rotation[0];
    const f64 y = trs.rotation[1];
    const f64 z = trs.rotation[2];
    const f64 w = trs.rotation[3];
    GltfMatrix out;
    // Columns of the rotation, each scaled.
    out.m[0] = (1 - 2 * (y * y + z * z)) * trs.scale[0];
    out.m[1] = (2 * (x * y + z * w)) * trs.scale[0];
    out.m[2] = (2 * (x * z - y * w)) * trs.scale[0];
    out.m[3] = 0;
    out.m[4] = (2 * (x * y - z * w)) * trs.scale[1];
    out.m[5] = (1 - 2 * (x * x + z * z)) * trs.scale[1];
    out.m[6] = (2 * (y * z + x * w)) * trs.scale[1];
    out.m[7] = 0;
    out.m[8] = (2 * (x * z + y * w)) * trs.scale[2];
    out.m[9] = (2 * (y * z - x * w)) * trs.scale[2];
    out.m[10] = (1 - 2 * (x * x + y * y)) * trs.scale[2];
    out.m[11] = 0;
    out.m[12] = trs.translation[0];
    out.m[13] = trs.translation[1];
    out.m[14] = trs.translation[2];
    out.m[15] = 1;
    return out;
}

bool DecomposeMatrix(const GltfMatrix& matrix, GltfTrs* out)
{
    const f64* m = matrix.m.Data();
    if (Abs(m[3]) > 1e-6 || Abs(m[7]) > 1e-6 || Abs(m[11]) > 1e-6 || Abs(m[15] - 1.0) > 1e-6)
    {
        return false;
    }
    f64 column[3][3];
    f64 length[3];
    for (usize c = 0; c < 3; ++c)
    {
        length[c] = Sqrt(m[c * 4] * m[c * 4] + m[c * 4 + 1] * m[c * 4 + 1] + m[c * 4 + 2] * m[c * 4 + 2]);
        if (!(length[c] > 1e-12))
        {
            return false;
        }
        for (usize r = 0; r < 3; ++r)
        {
            column[c][r] = m[c * 4 + r] / length[c];
        }
    }
    for (usize a = 0; a < 3; ++a)
    {
        const usize b = (a + 1) % 3;
        const f64 dot = column[a][0] * column[b][0] + column[a][1] * column[b][1] + column[a][2] * column[b][2];
        if (Abs(dot) > 1e-6)
        {
            return false;  // a shear: translation, rotation and scale cannot hold it
        }
    }
    // A reflection: the determinant of the unit columns is -1; the first scale takes the sign.
    const f64 det = column[0][0] * (column[1][1] * column[2][2] - column[1][2] * column[2][1]) -
                    column[1][0] * (column[0][1] * column[2][2] - column[0][2] * column[2][1]) +
                    column[2][0] * (column[0][1] * column[1][2] - column[0][2] * column[1][1]);
    out->scale = {{length[0], length[1], length[2]}};
    if (det < 0)
    {
        out->scale[0] = -length[0];
        for (usize r = 0; r < 3; ++r)
        {
            column[0][r] = -column[0][r];
        }
    }
    out->translation = {{m[12], m[13], m[14]}};
    // The rotation matrix has the columns as its columns: r[row][col] = column[col][row].
    const f64 r00 = column[0][0];
    const f64 r10 = column[0][1];
    const f64 r20 = column[0][2];
    const f64 r01 = column[1][0];
    const f64 r11 = column[1][1];
    const f64 r21 = column[1][2];
    const f64 r02 = column[2][0];
    const f64 r12 = column[2][1];
    const f64 r22 = column[2][2];
    const f64 trace = r00 + r11 + r22;
    f64 x;
    f64 y;
    f64 z;
    f64 w;
    if (trace > 0)
    {
        const f64 s = Sqrt(trace + 1.0) * 2.0;
        w = 0.25 * s;
        x = (r21 - r12) / s;
        y = (r02 - r20) / s;
        z = (r10 - r01) / s;
    }
    else if (r00 > r11 && r00 > r22)
    {
        const f64 s = Sqrt(1.0 + r00 - r11 - r22) * 2.0;
        w = (r21 - r12) / s;
        x = 0.25 * s;
        y = (r01 + r10) / s;
        z = (r02 + r20) / s;
    }
    else if (r11 > r22)
    {
        const f64 s = Sqrt(1.0 + r11 - r00 - r22) * 2.0;
        w = (r02 - r20) / s;
        x = (r01 + r10) / s;
        y = 0.25 * s;
        z = (r12 + r21) / s;
    }
    else
    {
        const f64 s = Sqrt(1.0 + r22 - r00 - r11) * 2.0;
        w = (r10 - r01) / s;
        x = (r02 + r20) / s;
        y = (r12 + r21) / s;
        z = 0.25 * s;
    }
    const f64 norm = Sqrt(x * x + y * y + z * z + w * w);
    out->rotation = {{x / norm, y / norm, z / norm, w / norm}};
    return true;
}

Status ReadNodeGraph(const GltfDocument& document, GltfNodeGraph* out, GltfError* error)
{
    out->nodes.Clear();
    const JsonValue nodes = document.root.Get("nodes");
    const usize count = nodes.Size();
    const u64 mesh_count = document.root.Get("meshes").Size();
    const u64 skin_count = document.root.Get("skins").Size();
    for (usize i = 0; i < count; ++i)
    {
        const JsonValue node = nodes.At(i);
        GltfNode entry;
        bool has_name = false;
        ENGINE_TRY(node.OptionalString("name", &entry.name, &has_name, error));
        f64 values[16];
        bool has_matrix = false;
        ENGINE_TRY(ReadNumbers(node, "matrix", 16, values, &has_matrix, error));
        if (has_matrix)
        {
            for (usize k = 0; k < 16; ++k)
            {
                entry.local.m[k] = values[k];
            }
        }
        GltfTrs trs;
        bool has_t = false;
        bool has_r = false;
        bool has_s = false;
        ENGINE_TRY(ReadNumbers(node, "translation", 3, values, &has_t, error));
        if (has_t)
        {
            trs.translation = {{values[0], values[1], values[2]}};
        }
        ENGINE_TRY(ReadNumbers(node, "rotation", 4, values, &has_r, error));
        if (has_r)
        {
            const f64 length = Sqrt(values[0] * values[0] + values[1] * values[1] + values[2] * values[2] +
                                    values[3] * values[3]);
            if (!(length > 1e-9))
            {
                return RefuseNode(error, node.Get("rotation").Path(), StringView("is a quaternion of length zero"));
            }
            trs.rotation = {{values[0] / length, values[1] / length, values[2] / length, values[3] / length}};
        }
        ENGINE_TRY(ReadNumbers(node, "scale", 3, values, &has_s, error));
        if (has_s)
        {
            trs.scale = {{values[0], values[1], values[2]}};
        }
        if (has_matrix && (has_t || has_r || has_s))
        {
            return RefuseNode(error, node.Path(), StringView("has both `matrix` and translation, rotation or scale"));
        }
        if (!has_matrix)
        {
            entry.local = MatrixFromTrs(trs);
        }
        const JsonValue mesh = node.Get("mesh");
        if (mesh.Exists())
        {
            ENGINE_TRY(mesh.AsIndex(&entry.mesh, mesh_count, error));
        }
        const JsonValue skin = node.Get("skin");
        if (skin.Exists())
        {
            ENGINE_TRY(skin.AsIndex(&entry.skin, skin_count, error));
        }
        out->nodes.Push(entry);
    }
    for (usize i = 0; i < count; ++i)
    {
        const JsonValue children = nodes.At(i).Get("children");
        if (!children.Exists())
        {
            continue;
        }
        if (!children.IsArray())
        {
            return RefuseNode(error, children.Path(), StringView("is not an array"));
        }
        for (usize c = 0; c < children.Size(); ++c)
        {
            u32 child = 0;
            ENGINE_TRY(children.At(c).AsIndex(&child, count, error));
            if (out->nodes[child].parent != kNoGltfIndex)
            {
                return RefuseNode(error, children.At(c).Path(), StringView("is a node that has another parent"));
            }
            out->nodes[child].parent = static_cast<u32>(i);
        }
    }
    // A cycle: walk up from each node once; a node is `visiting` while its chain is walked, `done` after (the walk of a
    // node that reaches a `done` one stops there), so every node is walked at most once.
    Vector<u8> state;  // 0 not seen, 1 on the current walk, 2 finished
    state.Resize(count);
    for (usize i = 0; i < count; ++i)
    {
        u32 at = static_cast<u32>(i);
        while (at != kNoGltfIndex && state[at] == 0)
        {
            state[at] = 1;
            at = out->nodes[at].parent;
        }
        if (at != kNoGltfIndex && state[at] == 1)
        {
            return RefuseNode(error, nodes.At(at).Path(), StringView("is part of a cycle of nodes"));
        }
        for (u32 walk = static_cast<u32>(i); walk != kNoGltfIndex && state[walk] == 1; walk = out->nodes[walk].parent)
        {
            state[walk] = 2;
        }
    }
    return Status::Success();
}

}  // namespace engine_placeholder::asset
