#include "asset_import/gltf_collision.h"

#include "asset/mesh_collider.h"
#include "asset_import/gltf_accessor.h"
#include "asset_import/gltf_names.h"
#include "containers/array.h"
#include "cstr/cstr.h"
#include "cstr/format.h"
#include "lang/status_try.h"

namespace vse::asset
{

namespace
{

Status RefuseCollider(GltfError* error, StringView where, const char* what, ErrorCode code = ErrorCode::Invalid)
{
    error->Set(where, StringView(what, CStrLength(what)));
    return Status::Make(code, "a glTF collider is refused");
}

void NodePath(u32 node, char* out, usize size)
{
    FormatTo(out, size, "/nodes/%u", static_cast<unsigned>(node));
}

// The suffix of a collision mark, after a trailing `.` and digits (Blender's duplicate number) are set aside.
struct SuffixMark
{
    bool marked = false;
    bool draw = true;
    bool convex = false;
};

SuffixMark ReadSuffix(StringView name)
{
    usize end = name.Length();
    usize digits = 0;
    while (digits < end && name[end - 1 - digits] >= '0' && name[end - 1 - digits] <= '9')
    {
        ++digits;
    }
    if (digits != 0 && digits < end && name[end - 1 - digits] == '.')
    {
        end -= digits + 1;
    }
    const StringView core = name.Substring(0, end);
    SuffixMark mark;
    // The longer suffixes first: "-convcolonly" ends with "colonly", "-colonly" and "-convcol" end with "col".
    if (core.EndsWith(StringView("-convcolonly")))
    {
        mark = SuffixMark{true, false, true};
    }
    else if (core.EndsWith(StringView("-colonly")))
    {
        mark = SuffixMark{true, false, false};
    }
    else if (core.EndsWith(StringView("-convcol")))
    {
        mark = SuffixMark{true, true, true};
    }
    else if (core.EndsWith(StringView("-col")))
    {
        mark = SuffixMark{true, true, false};
    }
    return mark;
}

// A finite number of a JSON object, or its default when absent; refused when present and not finite or below `min`.
Status OmiNumber(const JsonValue& object, const char* key, f64 fallback, f64 min, f64* out, GltfError* error)
{
    *out = fallback;
    const JsonValue value = object.Get(key);
    if (!value.Exists())
    {
        return Status::Success();
    }
    ENGINE_TRY(value.AsDouble(out, error));
    if (!(*out >= min) || !(*out <= 1.0e5))
    {
        return RefuseCollider(error, value.Path(), "is not a finite number in its range (at most 100000 m)");
    }
    return Status::Success();
}

// One node's OMI_physics_body collider and the OMI_physics_shape it names (glTF colliders Day 4).
Status ReadOmiCollider(const GltfDocument& document, const JsonValue& body, const JsonValue& shapes,
                       const JsonValue& materials, ImportReport* report, StringView where, GltfNodeCollision* out,
                       bool* convex, GltfError* error)
{
    const JsonValue collider = body.Get("collider");
    if (!shapes.IsArray())
    {
        return RefuseCollider(error, collider.Path(), "names a shape, but the file has no OMI_physics_shape shapes");
    }
    u32 shape_index = 0;
    ENGINE_TRY(collider.RequiredIndex("shape", &shape_index, shapes.Size(), error));
    const JsonValue shape = shapes.At(shape_index);
    StringView type;
    ENGINE_TRY(shape.Get("type").AsString(&type, error));
    const u64 mesh_count = document.root.Get("meshes").Size();
    if (type == StringView("box"))
    {
        const JsonValue box = shape.Get("box");
        f64 size[3] = {1.0, 1.0, 1.0};
        const JsonValue size_value = box.Get("size");
        if (size_value.Exists())
        {
            if (!size_value.IsArray() || size_value.Size() != 3)
            {
                return RefuseCollider(error, size_value.Path(), "is not three numbers");
            }
            for (usize k = 0; k < 3; ++k)
            {
                ENGINE_TRY(size_value.At(k).AsDouble(&size[k], error));
                if (!(size[k] > 0.0) || !(size[k] <= 2.0e5))
                {
                    return RefuseCollider(error, size_value.Path(), "has a size that is not above 0 and finite");
                }
            }
        }
        out->kind = GltfCollisionKind::Box;
        out->half_extents = Vec3(static_cast<f32>(size[0] * 0.5), static_cast<f32>(size[1] * 0.5),
                                 static_cast<f32>(size[2] * 0.5));
    }
    else if (type == StringView("sphere"))
    {
        f64 radius = 0.5;
        ENGINE_TRY(OmiNumber(shape.Get("sphere"), "radius", 0.5, 1e-9, &radius, error));
        out->kind = GltfCollisionKind::Sphere;
        out->radius = static_cast<f32>(radius);
    }
    else if (type == StringView("capsule"))
    {
        const JsonValue capsule = shape.Get("capsule");
        f64 height = 1.0;  // the distance between the two caps' centres (the specification's "mid height")
        f64 bottom = 0.5;
        f64 top_radius = 0.5;
        ENGINE_TRY(OmiNumber(capsule, "height", 1.0, 0.0, &height, error));
        ENGINE_TRY(OmiNumber(capsule, "radiusBottom", 0.5, 1e-9, &bottom, error));
        ENGINE_TRY(OmiNumber(capsule, "radiusTop", 0.5, 1e-9, &top_radius, error));
        if (bottom != top_radius)
        {
            AddImportWarning(report, where, StringView("is a capsule with two radii: the larger is used for both"));
        }
        out->kind = GltfCollisionKind::Capsule;
        out->radius = static_cast<f32>(bottom > top_radius ? bottom : top_radius);
        out->half_height = static_cast<f32>(height * 0.5);
    }
    else if (type == StringView("trimesh") || type == StringView("convex"))
    {
        u32 mesh = 0;
        ENGINE_TRY(shape.Get(type == StringView("trimesh") ? "trimesh" : "convex")
                       .RequiredIndex("mesh", &mesh, mesh_count, error));
        out->kind = GltfCollisionKind::Mesh;
        out->mesh = mesh;
        *convex = type == StringView("convex");
    }
    else
    {
        AddImportWarning(report, where,
                         StringView("has an OMI shape this engine has no collider for (a cylinder, or an unknown "
                                    "type): it is not solid"));
        ++report->collision_dropped;
        return Status::Success();
    }
    // The physics material: dynamicFriction and restitution (the engine has one friction).
    const JsonValue material_value = collider.Get("physicsMaterial");
    if (material_value.Exists())
    {
        f64 index = -1;
        ENGINE_TRY(material_value.AsDouble(&index, error));
        if (index >= 0)
        {
            u32 material = 0;
            ENGINE_TRY(material_value.AsIndex(&material, materials.IsArray() ? materials.Size() : 0, error));
            const JsonValue m = materials.At(material);
            f64 friction = 0.6;
            f64 restitution = 0.0;
            ENGINE_TRY(m.Get("dynamicFriction").Exists() ? m.Get("dynamicFriction").AsDouble(&friction, error)
                                                         : Status::Success());
            ENGINE_TRY(m.Get("restitution").Exists() ? m.Get("restitution").AsDouble(&restitution, error)
                                                     : Status::Success());
            if (!(friction >= 0.0 && friction <= 1.0e3) || !(restitution >= 0.0 && restitution <= 1.0))
            {
                AddImportWarning(report, m.Path(),
                                 StringView("has a friction below 0 or a restitution outside 0 to 1: clamped"));
                friction = !(friction >= 0.0) ? 0.0 : (friction > 1.0e3 ? 1.0e3 : friction);
                restitution = !(restitution >= 0.0) ? 0.0 : (restitution > 1.0 ? 1.0 : restitution);
            }
            out->friction = static_cast<f32>(friction);
            out->restitution = static_cast<f32>(restitution);
        }
    }
    return Status::Success();
}

}  // namespace

Status ReadCollisionMarks(const GltfDocument& document, const GltfNodeGraph& graph, ImportReport* report,
                          Vector<GltfNodeCollision>* out, GltfError* error)
{
    out->Clear();
    const JsonValue meshes = document.root.Get("meshes");
    const JsonValue node_list = document.root.Get("nodes");
    const usize count = graph.nodes.Size();
    // Every node's root and world matrix, in one pass (each node is computed once, from its parent's, whatever the
    // depth: a hostile file of 200,000 nodes in a chain costs 200,000 steps).
    Vector<u32> top;
    Vector<GltfMatrix> world;
    Vector<u8> state;    // 0 not done, 1 done
    Vector<bool> moves;  // the node or a node above it is an OMI body with a motion other than static (Day 4)
    moves.Resize(count);
    top.Resize(count);
    world.Resize(count);
    state.Resize(count);
    Vector<u32> path;
    for (usize n = 0; n < count; ++n)
    {
        path.Clear();
        u32 at = static_cast<u32>(n);
        while (state[at] == 0)
        {
            path.Push(at);
            const u32 parent = graph.nodes[at].parent;
            if (parent == kNoGltfIndex ||
                path.Size() > count)  // a root (a cycle cannot reach here: the graph has none)
            {
                break;
            }
            at = parent;
        }
        for (usize k = path.Size(); k > 0; --k)
        {
            const u32 node = path[k - 1];
            const u32 parent = graph.nodes[node].parent;
            const JsonValue motion = node_list.At(node).Get("extensions").Get("OMI_physics_body").Get("motion");
            StringView type;
            bool has_type = false;
            GltfError ignored;
            const bool own_motion = motion.Exists() && motion.OptionalString("type", &type, &has_type, &ignored).Ok() &&
                                    has_type && type != StringView("static");
            if (parent == kNoGltfIndex || state[parent] == 0)
            {
                top[node] = node;
                world[node] = graph.nodes[node].local;
                moves[node] = own_motion;
            }
            else
            {
                top[node] = top[parent];
                world[node] = MultiplyMatrices(world[parent], graph.nodes[node].local);
                moves[node] = own_motion || moves[parent];
            }
            state[node] = 1;
        }
    }
    // Only the nodes of the level (under the default scene's roots) are read: the others make no entity. A file the
    // level import will refuse (a bad scene index) marks nothing here; the level import reports it.
    Vector<bool> root;
    root.Resize(count);
    {
        const JsonValue scenes = document.root.Get("scenes");
        u64 scene_index = 0;
        bool present = false;
        GltfError ignored;
        const bool scene_ok = document.root.OptionalUnsigned("scene", &scene_index, 0xFFFFFFFFull, &present, &ignored)
                                  .Ok() &&
                              scene_index < scenes.Size();
        const JsonValue roots = scene_ok ? scenes.At(static_cast<usize>(scene_index)).Get("nodes") : JsonValue();
        for (usize i = 0; roots.Exists() && i < roots.Size(); ++i)
        {
            u64 index = 0;
            if (roots.At(i).AsUnsigned(&index, 0xFFFFFFFFull, &ignored).Ok() && index < count)
            {
                root[static_cast<usize>(index)] = true;
            }
        }
    }
    // OMI_physics_* (Day 4): the document's shapes and physics materials.
    const JsonValue shapes = document.root.Get("extensions").Get("OMI_physics_shape").Get("shapes");
    const JsonValue materials = document.root.Get("extensions").Get("OMI_physics_body").Get("physicsMaterials");
    for (usize n = 0; n < count; ++n)
    {
        const GltfNode& node = graph.nodes[n];
        GltfNodeCollision collision;
        if (!root[top[n]])
        {
            out->Push(collision);
            continue;
        }
        char where[32];
        NodePath(static_cast<u32>(n), where, sizeof(where));
        const StringView path_text(where, CStrLength(where));
        const SuffixMark mark = ReadSuffix(node.name);
        const JsonValue body = node_list.At(n).Get("extensions").Get("OMI_physics_body");
        bool convex = mark.convex;
        bool marked = false;
        if (body.Exists() && body.Get("collider").Exists())
        {
            ENGINE_TRY(
                ReadOmiCollider(document, body, shapes, materials, report, path_text, &collision, &convex, error));
            marked = collision.kind != GltfCollisionKind::None;
            if (mark.marked)
            {
                AddImportWarning(report, path_text,
                                 StringView("has an OMI_physics_body collider and a collider name: the OMI one is "
                                            "used"));
            }
            // A body with motion, on itself or above it, is not static: it is made static, with a warning.
            if (moves[n])
            {
                AddImportWarning(report, path_text,
                                 StringView("is a kinematic or dynamic body: it is imported as a static collider "
                                            "(moving bodies from a file are not imported)"));
            }
        }
        else if (mark.marked)
        {
            if (node.mesh == kNoGltfIndex)
            {
                AddImportWarning(report, path_text,
                                 StringView("is named as a collider but has no mesh: it is not solid"));
                ++report->collision_dropped;
            }
            else
            {
                collision.kind = GltfCollisionKind::Mesh;
                collision.draw = mark.draw;
                collision.mesh = node.mesh;
                marked = true;
            }
        }
        if (body.Exists() && body.Get("trigger").Exists())
        {
            AddImportWarning(report, path_text,
                             StringView("has an OMI_physics_body trigger: triggers are not imported"));
        }
        if (marked)
        {
            if (convex && collision.kind == GltfCollisionKind::Mesh)
            {
                AddImportWarning(report, path_text,
                                 StringView("asks for convex collision: it is a triangle mesh (convex hulls are not "
                                            "imported)"));
            }
            if (collision.kind == GltfCollisionKind::Mesh && node.skin != kNoGltfIndex && collision.mesh == node.mesh)
            {
                AddImportWarning(report, path_text,
                                 StringView("is skinned: its collider is the mesh's bind-pose shape, it does not "
                                            "follow the joints"));
            }
            if (collision.kind == GltfCollisionKind::Mesh && collision.mesh < meshes.Size())
            {
                const JsonValue primitives = meshes.At(collision.mesh).Get("primitives");
                bool morphed = false;
                for (usize p = 0; p < primitives.Size(); ++p)
                {
                    morphed = morphed || primitives.At(p).Get("targets").Exists();
                }
                if (morphed)
                {
                    AddImportWarning(report, path_text,
                                     StringView("has morph targets: its collider is the base shape"));
                }
            }
            // The node at rest: a shear in its world matrix is refused when the body is made.
            GltfTrs trs;
            if (!DecomposeMatrix(world[n], &trs))
            {
                AddImportWarning(report, path_text,
                                 StringView("has a shear or a zero scale in its world transform: it will get no "
                                            "physics body"));
            }
        }
        out->Push(collision);
    }
    return Status::Success();
}

void CollisionOnlyMeshes(const GltfNodeGraph& graph, Span<const GltfNodeCollision> marks, usize mesh_count,
                         Vector<bool>* out)
{
    out->Clear();
    Vector<bool> drawn;
    for (usize m = 0; m < mesh_count; ++m)
    {
        out->Push(false);
        drawn.Push(false);
    }
    for (usize n = 0; n < graph.nodes.Size(); ++n)
    {
        const u32 mesh = graph.nodes[n].mesh;
        if (mesh == kNoGltfIndex || mesh >= mesh_count)
        {
            continue;
        }
        const bool collision_only = n < marks.Size() && marks[n].kind == GltfCollisionKind::Mesh &&
                                    marks[n].mesh == mesh && !marks[n].draw;
        if (collision_only)
        {
            (*out)[mesh] = true;
        }
        else
        {
            drawn[mesh] = true;
        }
    }
    for (usize m = 0; m < mesh_count; ++m)
    {
        (*out)[m] = (*out)[m] && !drawn[m];
    }
}

Status ImportColliders(const GltfDocument& document, const ImportOptions& options, const TypeRegistry& types,
                       Vector<GltfNodeCollision>* marks, ImportResult* result, GltfError* error)
{
    ImportReport& report = result->report;
    const JsonValue meshes = document.root.Get("meshes");
    // The glTF meshes a collider uses, in index order.
    Vector<bool> wanted;
    for (usize m = 0; m < meshes.Size(); ++m)
    {
        wanted.Push(false);
    }
    for (usize n = 0; n < marks->Size(); ++n)
    {
        const GltfNodeCollision& mark = (*marks)[n];
        if (mark.kind == GltfCollisionKind::Mesh && mark.mesh < wanted.Size())
        {
            wanted[mark.mesh] = true;
        }
    }
    Vector<u32> list;
    for (usize m = 0; m < wanted.Size(); ++m)
    {
        if (wanted[m])
        {
            list.Push(static_cast<u32>(m));
        }
    }
    // Ids by the glTF mesh index; paths `<prefix>/colliders/<mesh name>`, distinct.
    Vector<ManifestObject> objects;
    Vector<String<HeapAllocator>> segments;
    for (usize i = 0; i < list.Size(); ++i)
    {
        StringView name;
        bool has_name = false;
        ENGINE_TRY(meshes.At(list[i]).OptionalString("name", &name, &has_name, error));
        objects.Push(ManifestObject{list[i], has_name ? name : StringView()});
        String<HeapAllocator> segment(HeapAllocator::Default());
        CleanPathSegment(has_name ? name : StringView(), &segment);
        if (segment.Length() == 0)
        {
            char text[24];
            FormatTo(text, sizeof(text), "collider%u", static_cast<unsigned>(list[i]));
            segment = String<HeapAllocator>(HeapAllocator::Default(), StringView(text, CStrLength(text)));
        }
        segments.Push(static_cast<String<HeapAllocator>&&>(segment));
    }
    MakeNamesUnique(&segments);
    Vector<ResourceId> ids;
    AssignIds(options, ImportKind::MeshCollider, Span<const ManifestObject>(objects.Data(), objects.Size()), &ids);
    const String<HeapAllocator> prefix = ImportPathPrefix(options);
    Vector<bool> made;  // per glTF mesh: an MCOL was made
    for (usize m = 0; m < meshes.Size(); ++m)
    {
        made.Push(false);
    }
    u64 total_triangles = 0;
    for (usize i = 0; i < list.Size(); ++i)
    {
        const u32 m = list[i];
        const JsonValue mesh = meshes.At(m);
        const JsonValue primitives = mesh.Get("primitives");
        Vector<Array<f32, 3>> positions;
        Vector<u32> indices;
        u32 dropped = 0;
        bool too_large = false;
        for (usize p = 0; p < primitives.Size() && !too_large; ++p)
        {
            const JsonValue primitive = primitives.At(p);
            u64 mode = 4;
            bool has_mode = false;
            ENGINE_TRY(primitive.OptionalUnsigned("mode", &mode, 6, &has_mode, error));
            if (mode != 4)
            {
                AddImportWarning(&report, primitive.Path(),
                                 StringView("is not TRIANGLES: it is left out of the collider"));
                continue;
            }
            u32 position_accessor = 0;
            const JsonValue attributes = primitive.Get("attributes");
            if (!attributes.Get("POSITION").Exists())
            {
                return RefuseCollider(error, primitive.Path(), "has no POSITION");
            }
            ENGINE_TRY(attributes.RequiredIndex("POSITION", &position_accessor, document.accessors.Size(), error));
            Vector<f32> floats;
            ENGINE_TRY(ReadAccessorFloats(document, position_accessor, &floats, error));  // refuses a non-finite one
            if (floats.Size() % 3 != 0)
            {
                return RefuseCollider(error, attributes.Get("POSITION").Path(), "is not three numbers per vertex");
            }
            const usize vertex_count = floats.Size() / 3;
            Vector<u32> prim_indices;
            if (primitive.Get("indices").Exists())
            {
                u32 index_accessor = 0;
                ENGINE_TRY(primitive.RequiredIndex("indices", &index_accessor, document.accessors.Size(), error));
                ENGINE_TRY(ReadAccessorUnsigned(document, index_accessor, &prim_indices, error));
                for (usize k = 0; k < prim_indices.Size(); ++k)
                {
                    if (prim_indices[k] >= vertex_count)
                    {
                        return RefuseCollider(error, primitive.Get("indices").Path(),
                                              "has an index outside the vertices");
                    }
                }
            }
            else
            {
                for (usize k = 0; k < vertex_count; ++k)
                {
                    prim_indices.Push(static_cast<u32>(k));
                }
            }
            const usize base = positions.Size();
            if (base + vertex_count > kMaxMeshColliderVertices)
            {
                too_large = true;
                break;
            }
            for (usize v = 0; v < vertex_count; ++v)
            {
                Array<f32, 3> position = {{floats[v * 3] * options.scale, floats[v * 3 + 1] * options.scale,
                                           floats[v * 3 + 2] * options.scale}};
                positions.Push(position);
            }
            for (usize t = 0; t + 2 < prim_indices.Size(); t += 3)
            {
                const u32 a = static_cast<u32>(base) + prim_indices[t];
                const u32 b = static_cast<u32>(base) + prim_indices[t + 1];
                const u32 c = static_cast<u32>(base) + prim_indices[t + 2];
                if (!MeshColliderTriangleHasArea(positions[a], positions[b], positions[c]))
                {
                    ++dropped;
                    continue;
                }
                if (indices.Size() / 3 + 1 > kMaxMeshColliderTriangles)
                {
                    too_large = true;
                    break;
                }
                indices.Push(a);
                indices.Push(b);
                indices.Push(c);
            }
        }
        if (too_large)
        {
            AddImportWarning(&report, mesh.Path(),
                             StringView("has more than 1,048,576 vertices or triangles: it gives no collider"));
            continue;
        }
        if (dropped != 0)
        {
            char what[96];
            FormatTo(what, sizeof(what), "%u triangles with no area are left out of its collider",
                     static_cast<unsigned>(dropped));
            AddImportWarning(&report, mesh.Path(), StringView(what, CStrLength(what)));
        }
        if (indices.Size() == 0)
        {
            AddImportWarning(&report, mesh.Path(), StringView("has no triangle with an area: it gives no collider"));
            continue;
        }
        const u64 triangles = indices.Size() / 3;
        total_triangles += triangles;
        if (total_triangles > options.limits.max_collision_triangles)
        {
            char what[128];
            FormatTo(what, sizeof(what),
                     "takes the colliders past the limit of %llu triangles (assets.import.max_collision_triangles)",
                     static_cast<unsigned long long>(options.limits.max_collision_triangles));
            return RefuseCollider(error, mesh.Path(), what, ErrorCode::TooLarge);
        }
        ImportedResource resource;
        resource.id = ids[i];
        resource.type = types::kMeshCollider;
        resource.path = String<HeapAllocator>(HeapAllocator::Default(), prefix.View());
        resource.path.Append(StringView("/colliders/"));
        resource.path.Append(segments[i].View());
        const Status minted = BuildMeshColliderBlob(
            types, resource.id, Span<const Array<f32, 3>>(positions.Data(), positions.Size()),
            Span<const u32>(indices.Data(), indices.Size()), Span<const u8>(), &resource.blob);
        if (minted.Failed())
        {
            error->Set(mesh.Path(), StringView(minted.Message()));
            return minted;
        }
        report.output_bytes += resource.blob.Size();
        if (report.output_bytes > options.limits.max_output_bytes)
        {
            return RefuseCollider(error, mesh.Path(), "takes the import past the limit on the bytes it may make",
                                  ErrorCode::TooLarge);
        }
        if (options.cancel != nullptr && options.cancel->Load(MemoryOrder::Acquire))
        {
            error->Set(StringView(), StringView("the import was cancelled"));
            return Status::Make(ErrorCode::Cancelled, "the import was cancelled");
        }
        result->manifest.Add(ImportKind::MeshCollider, m, objects[i].name, resource.id);
        result->resources.Push(static_cast<ImportedResource&&>(resource));
        made[m] = true;
        ++report.collision_meshes;
        report.collision_triangles += triangles;
    }
    // The nodes: a mesh collider whose mesh gave none is dropped; the others are counted.
    for (usize n = 0; n < marks->Size(); ++n)
    {
        GltfNodeCollision& mark = (*marks)[n];
        if (mark.kind == GltfCollisionKind::None)
        {
            continue;
        }
        if (mark.kind == GltfCollisionKind::Mesh && (mark.mesh >= made.Size() || !made[mark.mesh]))
        {
            mark.kind = GltfCollisionKind::None;
            ++report.collision_dropped;
            continue;
        }
        ++report.colliders;
        if (mark.kind != GltfCollisionKind::Mesh)
        {
            ++report.collision_primitives;
        }
    }
    return Status::Success();
}

}  // namespace vse::asset
