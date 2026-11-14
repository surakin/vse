#include "asset_import/gltf_mesh.h"

#include "algorithm.h"
#include "asset_import/gltf_accessor.h"
#include "asset_import/gltf_names.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"
#include "lang/scalar.h"
#include "lang/status_try.h"

namespace engine_placeholder::asset
{

namespace
{

constexpr u32 kComponentUByte = 5121;
constexpr u32 kComponentUShort = 5123;
constexpr u32 kComponentFloat = 5126;
constexpr u32 kMaxSkinSets = 8;

Status RefuseMesh(GltfError* error, StringView where, StringView what, ErrorCode code = ErrorCode::Invalid)
{
    error->Set(where, what);
    return Status::Make(code, "a glTF mesh is refused");
}

// What one primitive becomes before the mesh's pools are assembled.
struct PrimData
{
    Vector<MeshVertex> vertices;
    Vector<MeshSkinInfluence> skin;  // empty, or one per vertex
    Vector<u32> indices;             // a triangle list over `vertices`
};

struct Vec3
{
    f64 x, y, z;
};

Vec3 Sub(const Vec3& a, const Vec3& b)
{
    return Vec3{a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 Cross(const Vec3& a, const Vec3& b)
{
    return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

f64 Dot(const Vec3& a, const Vec3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 Position(const MeshVertex& v)
{
    return Vec3{static_cast<f64>(v.position[0]), static_cast<f64>(v.position[1]), static_cast<f64>(v.position[2])};
}

// True and `v` scaled to unit length when it is long enough to have a direction.
bool Normalize(Vec3* v)
{
    const f64 length = Sqrt(Dot(*v, *v));
    if (!(length > 1e-30))
    {
        return false;
    }
    v->x /= length;
    v->y /= length;
    v->z /= length;
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Attributes

// Reads the accessor `value` names as floats of the given element shape.
Status ReadFloatAttribute(const GltfDocument& document, const JsonValue& value, GltfAccessorType type,
                          bool normalized_integers, u64 expected_count, Vector<f32>* out, u64* count, GltfError* error)
{
    u32 index = 0;
    ENGINE_TRY(value.AsIndex(&index, document.accessors.Size(), error));
    const GltfAccessor& accessor = document.accessors[index];
    if (accessor.type != type)
    {
        return RefuseMesh(error, value.Path(), StringView("is an accessor of the wrong element type"));
    }
    const bool integer_ok = normalized_integers && accessor.normalized &&
                            (accessor.component_type == kComponentUByte || accessor.component_type == kComponentUShort);
    if (accessor.component_type != kComponentFloat && !integer_ok)
    {
        return RefuseMesh(error, value.Path(), StringView("is an accessor of the wrong component type"));
    }
    if (expected_count != 0 && accessor.count != expected_count)
    {
        error->SetNumber(value.Path(), StringView("has a different number of elements than POSITION: "),
                         accessor.count);
        return Status::Make(ErrorCode::Invalid, "a glTF mesh is refused");
    }
    *count = accessor.count;
    return ReadAccessorFloats(document, index, out, error);
}

Status ReadJointAttribute(const GltfDocument& document, const JsonValue& value, u64 expected_count, Vector<u32>* out,
                          u64* count, GltfError* error)
{
    u32 index = 0;
    ENGINE_TRY(value.AsIndex(&index, document.accessors.Size(), error));
    const GltfAccessor& accessor = document.accessors[index];
    if (accessor.type != GltfAccessorType::Vec4 ||
        (accessor.component_type != kComponentUByte && accessor.component_type != kComponentUShort))
    {
        return RefuseMesh(error, value.Path(), StringView("is not a VEC4 of unsigned bytes or shorts"));
    }
    if (accessor.count != expected_count)
    {
        error->SetNumber(value.Path(), StringView("has a different number of elements than POSITION: "),
                         accessor.count);
        return Status::Make(ErrorCode::Invalid, "a glTF mesh is refused");
    }
    *count = accessor.count;
    return ReadAccessorUnsigned(document, index, out, error);
}

bool IsHandledAttribute(StringView key)
{
    return key == StringView("POSITION") || key == StringView("NORMAL") || key == StringView("TANGENT") ||
           key == StringView("TEXCOORD_0");
}

// `JOINTS_n` or `WEIGHTS_n` (n < kMaxSkinSets): true when `key` is one.
bool IsSkinAttribute(StringView key)
{
    for (u32 n = 0; n < kMaxSkinSets; ++n)
    {
        char name[16];
        FormatTo(name, sizeof(name), "JOINTS_%u", static_cast<unsigned>(n));
        if (key == StringView(name))
        {
            return true;
        }
        FormatTo(name, sizeof(name), "WEIGHTS_%u", static_cast<unsigned>(n));
        if (key == StringView(name))
        {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------------------------------------------------
// Triangles

// Appends the triangles of `source` read in `mode` (4 list, 5 strip, 6 fan) to `out`.
Status MakeTriangles(const Vector<u32>& source, u64 mode, StringView where, Vector<u32>* out, GltfError* error)
{
    const usize n = source.Size();
    if (mode == 4)
    {
        if (n < 3 || n % 3 != 0)
        {
            return RefuseMesh(error, where, StringView("TRIANGLES needs a multiple of three indices, at least three"));
        }
        for (usize i = 0; i < n; ++i)
        {
            out->Push(source[i]);
        }
        return Status::Success();
    }
    if (n < 3)
    {
        return RefuseMesh(error, where, StringView("a strip or fan needs at least three indices"));
    }
    for (usize i = 0; i + 2 < n; ++i)
    {
        u32 a;
        u32 b;
        u32 c;
        if (mode == 5)
        {
            const bool odd = (i & 1) != 0;  // alternate triangles are wound the other way
            a = odd ? source[i + 1] : source[i];
            b = odd ? source[i] : source[i + 1];
            c = source[i + 2];
        }
        else
        {
            a = source[0];
            b = source[i + 1];
            c = source[i + 2];
        }
        if (a == b || b == c || a == c)
        {
            continue;  // a degenerate triangle: a restart in a strip, not geometry
        }
        out->Push(a);
        out->Push(b);
        out->Push(c);
    }
    return Status::Success();
}

// ---------------------------------------------------------------------------------------------------------------------
// Missing attributes

// Gives every triangle its own three vertices (so a normal made from the face is the face's).
void Unshare(PrimData* prim)
{
    Vector<MeshVertex> vertices;
    Vector<MeshSkinInfluence> skin;
    const bool skinned = prim->skin.Size() != 0;
    for (usize i = 0; i < prim->indices.Size(); ++i)
    {
        vertices.Push(prim->vertices[prim->indices[i]]);
        if (skinned)
        {
            skin.Push(prim->skin[prim->indices[i]]);
        }
        prim->indices[i] = static_cast<u32>(i);
    }
    prim->vertices = static_cast<Vector<MeshVertex>&&>(vertices);
    prim->skin = static_cast<Vector<MeshSkinInfluence>&&>(skin);
}

// Smooth normals: the sum of the (area-weighted) face normals around each vertex.
void MakeNormals(PrimData* prim)
{
    Vector<f64> sum;
    for (usize i = 0; i < prim->vertices.Size() * 3; ++i)
    {
        sum.Push(0.0);
    }
    for (usize t = 0; t + 2 < prim->indices.Size(); t += 3)
    {
        const Vec3 p0 = Position(prim->vertices[prim->indices[t]]);
        const Vec3 face = Cross(Sub(Position(prim->vertices[prim->indices[t + 1]]), p0),
                                Sub(Position(prim->vertices[prim->indices[t + 2]]), p0));
        for (usize k = 0; k < 3; ++k)
        {
            const usize v = prim->indices[t + k];
            sum[v * 3] += face.x;
            sum[v * 3 + 1] += face.y;
            sum[v * 3 + 2] += face.z;
        }
    }
    for (usize v = 0; v < prim->vertices.Size(); ++v)
    {
        Vec3 n{sum[v * 3], sum[v * 3 + 1], sum[v * 3 + 2]};
        if (!Normalize(&n))
        {
            n = Vec3{0, 0, 1};
        }
        prim->vertices[v].normal = {{static_cast<f32>(n.x), static_cast<f32>(n.y), static_cast<f32>(n.z)}};
    }
}

// Tangents from the texture-coordinate derivatives, summed per vertex and made perpendicular to the normal; the
// bitangent sign is the handedness of the texture mapping. Not MikkTSpace (KNOWN-ISSUES).
void MakeTangents(PrimData* prim, bool has_uv)
{
    Vector<f64> sum;  // per vertex: tangent xyz, bitangent xyz
    for (usize i = 0; i < prim->vertices.Size() * 6; ++i)
    {
        sum.Push(0.0);
    }
    for (usize t = 0; has_uv && t + 2 < prim->indices.Size(); t += 3)
    {
        const MeshVertex& v0 = prim->vertices[prim->indices[t]];
        const MeshVertex& v1 = prim->vertices[prim->indices[t + 1]];
        const MeshVertex& v2 = prim->vertices[prim->indices[t + 2]];
        const Vec3 e1 = Sub(Position(v1), Position(v0));
        const Vec3 e2 = Sub(Position(v2), Position(v0));
        const f64 du1 = static_cast<f64>(v1.uv[0]) - static_cast<f64>(v0.uv[0]);
        const f64 dv1 = static_cast<f64>(v1.uv[1]) - static_cast<f64>(v0.uv[1]);
        const f64 du2 = static_cast<f64>(v2.uv[0]) - static_cast<f64>(v0.uv[0]);
        const f64 dv2 = static_cast<f64>(v2.uv[1]) - static_cast<f64>(v0.uv[1]);
        const f64 r = du1 * dv2 - du2 * dv1;
        if (!(Abs(r) > 1e-20))
        {
            continue;  // a triangle whose texture coordinates collapse has no tangent
        }
        const f64 inv = 1.0 / r;
        const Vec3 tangent{(e1.x * dv2 - e2.x * dv1) * inv, (e1.y * dv2 - e2.y * dv1) * inv,
                           (e1.z * dv2 - e2.z * dv1) * inv};
        const Vec3 bitangent{(e2.x * du1 - e1.x * du2) * inv, (e2.y * du1 - e1.y * du2) * inv,
                             (e2.z * du1 - e1.z * du2) * inv};
        for (usize k = 0; k < 3; ++k)
        {
            const usize v = prim->indices[t + k];
            sum[v * 6] += tangent.x;
            sum[v * 6 + 1] += tangent.y;
            sum[v * 6 + 2] += tangent.z;
            sum[v * 6 + 3] += bitangent.x;
            sum[v * 6 + 4] += bitangent.y;
            sum[v * 6 + 5] += bitangent.z;
        }
    }
    for (usize v = 0; v < prim->vertices.Size(); ++v)
    {
        MeshVertex& vertex = prim->vertices[v];
        Vec3 n{static_cast<f64>(vertex.normal[0]), static_cast<f64>(vertex.normal[1]),
               static_cast<f64>(vertex.normal[2])};
        if (!Normalize(&n))
        {
            n = Vec3{0, 0, 1};
        }
        Vec3 t{sum[v * 6], sum[v * 6 + 1], sum[v * 6 + 2]};
        t = Vec3{t.x - n.x * Dot(n, t), t.y - n.y * Dot(n, t), t.z - n.z * Dot(n, t)};
        bool from_uv = Normalize(&t);
        if (!from_uv)
        {
            const Vec3 axis = Abs(n.x) < 0.9 ? Vec3{1, 0, 0} : Vec3{0, 1, 0};  // any direction across the normal
            t = Vec3{axis.x - n.x * Dot(n, axis), axis.y - n.y * Dot(n, axis), axis.z - n.z * Dot(n, axis)};
            Normalize(&t);
        }
        const Vec3 b{sum[v * 6 + 3], sum[v * 6 + 4], sum[v * 6 + 5]};
        const f32 sign = from_uv && Dot(Cross(n, t), b) < 0.0 ? -1.0f : 1.0f;
        vertex.tangent = {{static_cast<f32>(t.x), static_cast<f32>(t.y), static_cast<f32>(t.z), sign}};
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Names

// A slot name: the material's name, else `primitive<N>`, cut to the format's length at a character boundary.
void SlotName(StringView material_name, u32 primitive, Array<char, kMaxSlotNameBytes + 1>* out)
{
    String<HeapAllocator> clean(HeapAllocator::Default());
    CleanPathSegment(material_name, &clean);
    if (clean.Length() == 0)
    {
        char text[24];
        FormatTo(text, sizeof(text), "primitive%u", static_cast<unsigned>(primitive));
        clean = String<HeapAllocator>(HeapAllocator::Default(), StringView(text, CStrLength(text)));
    }
    usize length = clean.Length() < kMaxSlotNameBytes ? clean.Length() : kMaxSlotNameBytes;
    while (length > 0 && length < clean.Length() && (static_cast<u8>(clean.View()[length]) & 0xC0) == 0x80)
    {
        --length;
    }
    *out = {};
    MemCopy(out->Data(), clean.View().Data(), length);
}

bool SameSlotName(const Array<char, kMaxSlotNameBytes + 1>& a, const Array<char, kMaxSlotNameBytes + 1>& b)
{
    return MemCompare(a.Data(), b.Data(), a.Size()) == 0;
}

bool SlotNameTaken(const Vector<MeshSlot>& slots, const Array<char, kMaxSlotNameBytes + 1>& name)
{
    for (usize k = 0; k < slots.Size(); ++k)
    {
        if (SameSlotName(slots[k].name, name))
        {
            return true;
        }
    }
    return false;
}

// The slot name of primitive `primitive`: its material's name; when that is empty or taken, `primitive<N>`; when that
// is taken too (a material may be called anything), `p<N>_<k>` for the first k that is free.
void ChooseSlotName(StringView material_name, u32 primitive, const Vector<MeshSlot>& slots,
                    Array<char, kMaxSlotNameBytes + 1>* out)
{
    SlotName(material_name, primitive, out);
    if (!SlotNameTaken(slots, *out))
    {
        return;
    }
    SlotName(StringView(), primitive, out);
    for (u32 k = 0; SlotNameTaken(slots, *out); ++k)
    {
        char text[40];
        FormatTo(text, sizeof(text), "p%u_%u", static_cast<unsigned>(primitive), static_cast<unsigned>(k));
        *out = {};
        MemCopy(out->Data(), text, CStrLength(text));
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// One primitive

Status ImportPrimitive(const GltfDocument& document, const ImportOptions& options, const JsonValue& primitive,
                       const MeshSkeletonBinding* binding, ImportReport* report, PrimData* out, GltfError* error)
{
    const StringView where = primitive.Path();
    u64 mode = 4;
    bool present = false;
    ENGINE_TRY(primitive.OptionalUnsigned("mode", &mode, 6, &present, error));
    if (mode < 4)
    {
        return RefuseMesh(error, where, StringView("is made of points or lines; only triangles are imported"),
                          ErrorCode::Unsupported);
    }
    const JsonValue attributes = primitive.Get("attributes");
    if (!attributes.IsObject())
    {
        return RefuseMesh(error, where, StringView("has no `attributes` object"));
    }
    const JsonValue targets = primitive.Get("targets");
    if (targets.Exists() && targets.Size() != 0)
    {
        if (!options.drop_morph_targets)
        {
            return RefuseMesh(error, targets.Path(),
                              StringView("has morph targets, which the mesh format does not hold (drop them with "
                                         "drop-morph-targets)"),
                              ErrorCode::Unsupported);
        }
        AddImportWarning(report, where, StringView("morph targets dropped; the base shape is imported"));
    }
    // What is read and what is dropped.
    JsonMembers members;
    attributes.Members(&members);
    StringView key;
    JsonValue value;
    while (members.Next(&key, &value))
    {
        if (!IsHandledAttribute(key) && !IsSkinAttribute(key))
        {
            String<HeapAllocator> what(HeapAllocator::Default(), StringView("attribute "));
            what.Append(key);
            what.Append(StringView(" dropped: the mesh format has no such stream"));
            AddImportWarning(report, where, what.View());
        }
    }
    const JsonValue position_value = attributes.Get("POSITION");
    if (!position_value.Exists())
    {
        return RefuseMesh(error, attributes.Path(), StringView("has no POSITION"));
    }
    Vector<f32> positions;
    u64 count = 0;
    ENGINE_TRY(
        ReadFloatAttribute(document, position_value, GltfAccessorType::Vec3, false, 0, &positions, &count, error));
    if (count > kMaxMeshVertices)
    {
        return RefuseMesh(error, position_value.Path(), StringView("has more vertices than a mesh may hold"),
                          ErrorCode::TooLarge);
    }
    const usize vertex_count = static_cast<usize>(count);
    Vector<f32> normals;
    Vector<f32> tangents;
    Vector<f32> uvs;
    struct Optional
    {
        const char* name;
        GltfAccessorType type;
        bool integers;
        Vector<f32>* out;
        usize components;
    };
    const Optional optionals[] = {{"NORMAL", GltfAccessorType::Vec3, false, &normals, 3},
                                  {"TANGENT", GltfAccessorType::Vec4, false, &tangents, 4},
                                  {"TEXCOORD_0", GltfAccessorType::Vec2, true, &uvs, 2}};
    for (const Optional& optional : optionals)
    {
        const JsonValue attribute = attributes.Get(StringView(optional.name));
        if (!attribute.Exists())
        {
            continue;
        }
        u64 n = 0;
        ENGINE_TRY(
            ReadFloatAttribute(document, attribute, optional.type, optional.integers, count, optional.out, &n, error));
    }
    // The skin: every JOINTS_k with its WEIGHTS_k, k from 0.
    Vector<Vector<u32>> joint_sets;
    Vector<Vector<f32>> weight_sets;
    for (u32 k = 0; k < kMaxSkinSets; ++k)
    {
        char joints_name[16];
        char weights_name[16];
        FormatTo(joints_name, sizeof(joints_name), "JOINTS_%u", static_cast<unsigned>(k));
        FormatTo(weights_name, sizeof(weights_name), "WEIGHTS_%u", static_cast<unsigned>(k));
        const JsonValue joints = attributes.Get(StringView(joints_name));
        const JsonValue weights = attributes.Get(StringView(weights_name));
        if (!joints.Exists() && !weights.Exists())
        {
            break;
        }
        if (!joints.Exists() || !weights.Exists())
        {
            return RefuseMesh(error, attributes.Path(), StringView("has JOINTS_n without WEIGHTS_n or the reverse"));
        }
        Vector<u32> joint_values;
        Vector<f32> weight_values;
        u64 n = 0;
        ENGINE_TRY(ReadJointAttribute(document, joints, count, &joint_values, &n, error));
        ENGINE_TRY(
            ReadFloatAttribute(document, weights, GltfAccessorType::Vec4, true, count, &weight_values, &n, error));
        joint_sets.Push(static_cast<Vector<u32>&&>(joint_values));
        weight_sets.Push(static_cast<Vector<f32>&&>(weight_values));
    }
    for (u32 k = static_cast<u32>(joint_sets.Size()); k < kMaxSkinSets; ++k)
    {
        char joints_name[16];
        char weights_name[16];
        FormatTo(joints_name, sizeof(joints_name), "JOINTS_%u", static_cast<unsigned>(k));
        FormatTo(weights_name, sizeof(weights_name), "WEIGHTS_%u", static_cast<unsigned>(k));
        if (attributes.Get(StringView(joints_name)).Exists() || attributes.Get(StringView(weights_name)).Exists())
        {
            return RefuseMesh(
                error, attributes.Path(),
                StringView("has a JOINTS_n or WEIGHTS_n after a missing set: the sets are numbered from 0 "
                           "without a gap"));
        }
    }
    const bool has_skin = joint_sets.Size() != 0;
    const bool keep_skin = has_skin && binding != nullptr && binding->present;
    if (has_skin && !keep_skin)
    {
        AddImportWarning(report, where, StringView("skin attributes dropped: the mesh has no skeleton of this import"));
    }
    // Indices.
    Vector<u32> source;
    const JsonValue index_value = primitive.Get("indices");
    if (index_value.Exists())
    {
        u32 accessor_index = 0;
        ENGINE_TRY(index_value.AsIndex(&accessor_index, document.accessors.Size(), error));
        if (document.accessors[accessor_index].type != GltfAccessorType::Scalar)
        {
            return RefuseMesh(error, index_value.Path(), StringView("is not a SCALAR accessor"));
        }
        ENGINE_TRY(ReadAccessorUnsigned(document, accessor_index, &source, error));
    }
    else
    {
        if (count > kMaxMeshIndices)
        {
            return RefuseMesh(error, where, StringView("has more vertices than a mesh may hold indices"),
                              ErrorCode::TooLarge);
        }
        for (u32 i = 0; i < vertex_count; ++i)
        {
            source.Push(i);
        }
    }
    if (source.Size() > kMaxMeshIndices)
    {
        return RefuseMesh(error, where, StringView("has more indices than a mesh may hold"), ErrorCode::TooLarge);
    }
    for (usize i = 0; i < source.Size(); ++i)
    {
        if (source[i] >= vertex_count)
        {
            error->SetNumber(index_value.Exists() ? index_value.Path() : where,
                             StringView("has an index past the vertices: "), source[i]);
            return Status::Make(ErrorCode::Invalid, "a glTF mesh is refused");
        }
    }
    ENGINE_TRY(MakeTriangles(source, mode, where, &out->indices, error));
    if (out->indices.Size() == 0)
    {
        return RefuseMesh(error, where, StringView("has no triangles"));
    }
    // The vertices.
    const f64 scale = static_cast<f64>(options.scale);
    for (usize v = 0; v < vertex_count; ++v)
    {
        MeshVertex vertex;
        for (usize k = 0; k < 3; ++k)
        {
            vertex.position[k] = static_cast<f32>(static_cast<f64>(positions[v * 3 + k]) * scale);
            if (!IsFinite(vertex.position[k]))
            {
                return RefuseMesh(error, position_value.Path(),
                                  StringView("is past the range of a float after scaling"));
            }
        }
        if (normals.Size() != 0)
        {
            for (usize k = 0; k < 3; ++k)
            {
                vertex.normal[k] = normals[v * 3 + k];
            }
        }
        if (uvs.Size() != 0)
        {
            vertex.uv = {{uvs[v * 2], uvs[v * 2 + 1]}};
        }
        if (tangents.Size() != 0)
        {
            const f32 w = tangents[v * 4 + 3];
            vertex.tangent = {{tangents[v * 4], tangents[v * 4 + 1], tangents[v * 4 + 2], w < 0 ? -1.0f : 1.0f}};
        }
        out->vertices.Push(vertex);
    }
    if (keep_skin)
    {
        usize unweighted = 0;
        bool more_than_four = false;
        for (usize v = 0; v < vertex_count; ++v)
        {
            u32 joints[kMaxSkinSets * 4];
            f64 weights[kMaxSkinSets * 4];
            usize pairs = 0;
            for (usize s = 0; s < joint_sets.Size(); ++s)
            {
                for (usize k = 0; k < 4; ++k)
                {
                    u32 joint = joint_sets[s][v * 4 + k];
                    const f32 weight = weight_sets[s][v * 4 + k];
                    if (joint >= binding->joint_count)
                    {
                        error->SetNumber(attributes.Path(), StringView("has a joint past the skeleton's joints: "),
                                         joint);
                        return Status::Make(ErrorCode::Invalid, "a glTF mesh is refused");
                    }
                    if (binding->joint_remap.Size() != 0)
                    {
                        joint = binding->joint_remap[joint];  // the skin's joint slot, as the skeleton orders it
                    }
                    if (!(weight > 0.0f))
                    {
                        continue;  // zero (or negative: nothing a weight can be) influences nothing
                    }
                    usize slot = 0;
                    while (slot < pairs && joints[slot] != joint)
                    {
                        ++slot;
                    }
                    if (slot == pairs)
                    {
                        joints[pairs] = joint;
                        weights[pairs] = 0.0;
                        ++pairs;
                    }
                    weights[slot] += static_cast<f64>(weight);
                }
            }
            more_than_four = more_than_four || pairs > 4;
            MeshSkinInfluence influence;
            if (pairs == 0 || !QuantizeSkinInfluences(joints, weights, pairs, &influence))
            {
                influence.joint = {{0, 0, 0, 0}};
                influence.weight = {{255, 0, 0, 0}};
                ++unweighted;
            }
            out->skin.Push(influence);
        }
        if (unweighted != 0)
        {
            AddImportWarning(report, where, StringView("vertices without a weight are bound to joint 0"));
        }
        if (more_than_four)
        {
            AddImportWarning(report, where, StringView("more than four influences: the four largest are kept"));
        }
    }
    if (uvs.Size() == 0)
    {
        AddImportWarning(report, where, StringView("no TEXCOORD_0: the texture coordinates are zero"));
    }
    // What the file lacks, made deterministically.
    if (normals.Size() == 0)
    {
        if (options.flat_normals)
        {
            Unshare(out);
        }
        MakeNormals(out);
    }
    if (tangents.Size() == 0)
    {
        MakeTangents(out, uvs.Size() != 0);
    }
    return Status::Success();
}

}  // namespace

Status ImportMeshes(const GltfDocument& document, const ImportOptions& options, const TypeRegistry& types,
                    Span<const MeshSkeletonBinding> bindings, ImportResult* result, GltfError* error)
{
    const JsonValue meshes = document.root.Get("meshes");
    const usize mesh_count = meshes.Size();
    if (mesh_count == 0)
    {
        return Status::Success();
    }
    if (!(options.scale > 0.0f) || options.scale > 1e12f)
    {
        error->Set(StringView(), StringView("the import scale must be a positive, finite number"));
        return Status::Make(ErrorCode::Invalid, "a bad import scale");
    }
    // Ids: the manifest's where it has the mesh, new ones for the rest.
    Vector<StringView> names;
    Vector<ManifestObject> objects;
    for (usize m = 0; m < mesh_count; ++m)
    {
        StringView name;
        bool has_name = false;
        ENGINE_TRY(meshes.At(m).OptionalString("name", &name, &has_name, error));
        names.Push(has_name ? name : StringView());
        objects.Push(ManifestObject{static_cast<u32>(m), has_name ? name : StringView()});
    }
    Vector<ResourceId> ids;
    AssignIds(options, ImportKind::Mesh, Span<const ManifestObject>(objects.Data(), objects.Size()), &ids);
    // Paths: `<prefix>/meshes/<name>`, distinct.
    const String<HeapAllocator> prefix = ImportPathPrefix(options);
    Vector<String<HeapAllocator>> segments;
    for (usize m = 0; m < mesh_count; ++m)
    {
        String<HeapAllocator> segment(HeapAllocator::Default());
        CleanPathSegment(names[m], &segment);
        if (segment.Length() == 0)
        {
            char text[24];
            FormatTo(text, sizeof(text), "mesh%u", static_cast<unsigned>(m));
            segment = String<HeapAllocator>(HeapAllocator::Default(), StringView(text, CStrLength(text)));
        }
        segments.Push(static_cast<String<HeapAllocator>&&>(segment));
    }
    MakeNamesUnique(&segments);
    const JsonValue materials = document.root.Get("materials");
    for (usize m = 0; m < mesh_count; ++m)
    {
        const JsonValue mesh = meshes.At(m);
        const JsonValue primitives = mesh.Get("primitives");
        if (!primitives.IsArray() || primitives.Size() == 0)
        {
            return RefuseMesh(error, mesh.Path(), StringView("has no primitives"));
        }
        if (primitives.Size() > options.limits.max_primitives_per_mesh || primitives.Size() > kMaxMeshSlots)
        {
            error->SetNumber(primitives.Path(), StringView("has more primitives than a mesh has slots: "),
                             primitives.Size());
            return Status::Make(ErrorCode::TooLarge, "too many primitives");
        }
        const MeshSkeletonBinding* binding = m < bindings.Size() ? &bindings[m] : nullptr;
        Vector<PrimData> prims;
        Vector<MeshSlot> slots;
        bool skinned = false;
        for (usize p = 0; p < primitives.Size(); ++p)
        {
            const JsonValue primitive = primitives.At(p);
            PrimData data;
            ENGINE_TRY(ImportPrimitive(document, options, primitive, binding, &result->report, &data, error));
            skinned = skinned || data.skin.Size() != 0;
            prims.Push(static_cast<PrimData&&>(data));
            StringView material_name;
            const JsonValue material_value = primitive.Get("material");
            if (material_value.Exists())
            {
                u32 material_index = 0;
                ENGINE_TRY(material_value.AsIndex(&material_index, materials.Size(), error));
                bool has_name = false;
                ENGINE_TRY(materials.At(material_index).OptionalString("name", &material_name, &has_name, error));
            }
            MeshSlot slot;
            ChooseSlotName(material_name, static_cast<u32>(p), slots, &slot.name);
            slots.Push(slot);
        }
        Vector<MeshVertex> vertices;
        Vector<MeshSkinInfluence> skin;
        Vector<u32> indices;
        for (usize p = 0; p < prims.Size(); ++p)
        {
            PrimData& data = prims[p];
            if (vertices.Size() + data.vertices.Size() > kMaxMeshVertices ||
                indices.Size() + data.indices.Size() > kMaxMeshIndices)
            {
                return RefuseMesh(error, mesh.Path(), StringView("has more vertices or indices than a mesh may hold"),
                                  ErrorCode::TooLarge);
            }
            const u32 base = static_cast<u32>(vertices.Size());
            slots[p].first_index = static_cast<u32>(indices.Size());
            slots[p].index_count = static_cast<u32>(data.indices.Size());
            for (usize i = 0; i < data.indices.Size(); ++i)
            {
                indices.Push(base + data.indices[i]);
            }
            for (usize v = 0; v < data.vertices.Size(); ++v)
            {
                vertices.Push(data.vertices[v]);
                if (skinned)
                {
                    if (data.skin.Size() != 0)
                    {
                        skin.Push(data.skin[v]);
                    }
                    else
                    {
                        MeshSkinInfluence rigid;  // a primitive that is not skinned follows the root joint
                        rigid.weight = {{255, 0, 0, 0}};
                        skin.Push(rigid);
                    }
                }
            }
        }
        MeshBuildInput input;
        input.vertices = Span<const MeshVertex>(vertices.Data(), vertices.Size());
        input.indices = Span<const u32>(indices.Data(), indices.Size());
        input.slots = Span<const MeshSlot>(slots.Data(), slots.Size());
        if (skinned)
        {
            input.skin = Span<const MeshSkinInfluence>(skin.Data(), skin.Size());
            input.skeleton = binding->skeleton;
            input.skeleton_hash = binding->skeleton_hash;
            input.skeleton_joint_count = binding->joint_count;
        }
        ImportedResource resource;
        resource.id = ids[m];
        resource.type = types::kMesh;
        resource.path = String<HeapAllocator>(HeapAllocator::Default(), prefix.View());
        resource.path.Append(StringView("/meshes/"));
        resource.path.Append(segments[m].View());
        const Status built = BuildMeshBlob(types, resource.id, input, &resource.blob);
        if (built.Failed())
        {
            error->Set(mesh.Path(), StringView(built.Message()));
            return built;
        }
        result->report.output_bytes += resource.blob.Size();
        if (result->report.output_bytes > options.limits.max_output_bytes)
        {
            error->Set(mesh.Path(), StringView("takes the import past the limit on the bytes it may make"));
            return Status::Make(ErrorCode::TooLarge, "the import is too large");
        }
        result->manifest.Add(ImportKind::Mesh, static_cast<u32>(m), names[m], resource.id);
        result->resources.Push(static_cast<ImportedResource&&>(resource));
    }
    return Status::Success();
}

}  // namespace engine_placeholder::asset
