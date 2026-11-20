#include "asset_import/gltf_collision.h"

#include "asset/mesh_collider.h"
#include "asset_import/gltf_accessor.h"
#include "asset_import/gltf_names.h"
#include "containers/array.h"
#include "cstr/cstr.h"
#include "cstr/format.h"
#include "lang/status_try.h"

namespace engine_placeholder::asset
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

}  // namespace

Status ReadCollisionMarks(const GltfDocument& document, const GltfNodeGraph& graph, ImportReport* report,
                          Vector<GltfNodeCollision>* out, GltfError*)
{
    out->Clear();
    const JsonValue meshes = document.root.Get("meshes");
    // Only the nodes of the level (under the default scene's roots) are read: the others make no entity. A file the
    // level import will refuse (a bad scene index) marks nothing here; the level import reports it.
    Vector<bool> in_scene;
    for (usize n = 0; n < graph.nodes.Size(); ++n)
    {
        in_scene.Push(false);
    }
    {
        const JsonValue scenes = document.root.Get("scenes");
        u64 scene_index = 0;
        bool present = false;
        GltfError ignored;
        const bool scene_ok = document.root.OptionalUnsigned("scene", &scene_index, 0xFFFFFFFFull, &present, &ignored)
                                  .Ok() &&
                              scene_index < scenes.Size();
        const JsonValue roots = scene_ok ? scenes.At(static_cast<usize>(scene_index)).Get("nodes") : JsonValue();
        Vector<bool> root;
        for (usize n = 0; n < graph.nodes.Size(); ++n)
        {
            root.Push(false);
        }
        for (usize i = 0; roots.Exists() && i < roots.Size(); ++i)
        {
            u64 index = 0;
            if (roots.At(i).AsUnsigned(&index, 0xFFFFFFFFull, &ignored).Ok() && index < root.Size())
            {
                root[static_cast<usize>(index)] = true;
            }
        }
        for (usize n = 0; n < graph.nodes.Size(); ++n)
        {
            u32 top = static_cast<u32>(n);
            for (u32 depth = 0; graph.nodes[top].parent != kNoGltfIndex && depth < 1024; ++depth)
            {
                top = graph.nodes[top].parent;
            }
            in_scene[n] = root[top];
        }
    }
    for (usize n = 0; n < graph.nodes.Size(); ++n)
    {
        const GltfNode& node = graph.nodes[n];
        GltfNodeCollision collision;
        const SuffixMark mark = in_scene[n] ? ReadSuffix(node.name) : SuffixMark();
        if (mark.marked)
        {
            char where[32];
            NodePath(static_cast<u32>(n), where, sizeof(where));
            const StringView path(where, CStrLength(where));
            if (node.mesh == kNoGltfIndex)
            {
                AddImportWarning(report, path, StringView("is named as a collider but has no mesh: it is not solid"));
                ++report->collision_dropped;
            }
            else
            {
                collision.kind = GltfCollisionKind::Mesh;
                collision.draw = mark.draw;
                collision.mesh = node.mesh;
                if (mark.convex)
                {
                    AddImportWarning(report, path,
                                     StringView("asks for convex collision: it is a triangle mesh (convex hulls are "
                                                "not imported)"));
                }
                if (node.skin != kNoGltfIndex)
                {
                    AddImportWarning(report, path,
                                     StringView("is skinned: its collider is the mesh's bind-pose shape, it does not "
                                                "follow the joints"));
                }
                if (node.mesh < meshes.Size())
                {
                    const JsonValue primitives = meshes.At(node.mesh).Get("primitives");
                    bool morphed = false;
                    for (usize p = 0; p < primitives.Size(); ++p)
                    {
                        morphed = morphed || primitives.At(p).Get("targets").Exists();
                    }
                    if (morphed)
                    {
                        AddImportWarning(report, path, StringView("has morph targets: its collider is the base shape"));
                    }
                }
                // The node at rest: a shear in its world matrix is refused when the body is made.
                GltfMatrix world = node.local;
                for (u32 parent = node.parent, depth = 0; parent != kNoGltfIndex && depth < 1024;
                     parent = graph.nodes[parent].parent, ++depth)
                {
                    world = MultiplyMatrices(graph.nodes[parent].local, world);
                }
                GltfTrs trs;
                if (!DecomposeMatrix(world, &trs))
                {
                    AddImportWarning(report, path,
                                     StringView("has a shear or a zero scale in its world transform: it will get no "
                                                "physics body"));
                }
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

}  // namespace engine_placeholder::asset
