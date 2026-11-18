#include "asset_import/gltf_level.h"

#include "algorithm.h"
#include "animation/components.h"
#include "asset/level.h"
#include "asset_import/gltf_names.h"
#include "asset_import/gltf_summary.h"
#include "bytes/byte_writer.h"
#include "cstr/format.h"
#include "ecs/transform.h"
#include "lang/status_try.h"
#include "storage/builtin_components.h"
#include "storage/component_record.h"
#include "storage/identity.h"

namespace engine_placeholder::asset
{

namespace
{

constexpr u32 kUnset = 0xFFFFFFFEu;

struct Piece
{
    u32 id = 0;
    u32 version = 0;
    Vector<byte> bytes;
};

// A node name made valid for a level: other characters become `_`, and the name is cut short.
void LevelName(StringView name, u32 node, String<HeapAllocator>* out)
{
    *out = String<HeapAllocator>(HeapAllocator::Default());
    for (usize i = 0; i < name.Length() && out->Length() < 48; ++i)
    {
        const char c = name[i];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
                        c == '.' || c == ':' || c == '/' || c == '-';
        out->Append(ok ? c : '_');
    }
    if (out->Length() == 0 || !LevelNameValid(out->View()))
    {
        char text[24];
        FormatTo(text, sizeof(text), "node%u", static_cast<unsigned>(node));
        *out = String<HeapAllocator>(HeapAllocator::Default(), StringView(text, CStrLength(text)));
    }
}

void PushKey(Vector<byte>* bytes, const storage::EntityKey& key)
{
    for (u32 i = 0; i < 8; ++i)
    {
        bytes->Push(static_cast<byte>(key.high >> (8 * i)));
    }
    for (u32 i = 0; i < 8; ++i)
    {
        bytes->Push(static_cast<byte>(key.low >> (8 * i)));
    }
}

Status Refuse(GltfError* error, StringView where, const char* what, ErrorCode code)
{
    error->Set(where, StringView(what, CStrLength(what)));
    return Status::Make(code, what);
}

}  // namespace

Status ImportLevel(const GltfDocument& document, const GltfNodeGraph& graph, const ImportOptions& options,
                   const TypeRegistry& types, Span<const ImportedSkin> skins, ImportResult* result, GltfError* error)
{
    const JsonValue scenes = document.root.Get("scenes");
    const u32 node_count = static_cast<u32>(graph.nodes.Size());
    if (!scenes.Exists() || scenes.Size() == 0 || node_count == 0)
    {
        AddImportNote(&result->report, StringView(), StringView("the file has no scene: no level was made"));
        return Status::Success();
    }
    u32 scene_index = 0;
    const JsonValue scene_value = document.root.Get("scene");
    if (scene_value.Exists())
    {
        ENGINE_TRY(scene_value.AsIndex(&scene_index, scenes.Size(), error));
    }
    const JsonValue scene = scenes.At(scene_index);
    const JsonValue roots = scene.Get("nodes");
    Vector<u32> root_nodes;
    for (usize i = 0; roots.Exists() && i < roots.Size(); ++i)
    {
        u32 node = 0;
        ENGINE_TRY(roots.At(i).AsIndex(&node, node_count, error));
        if (graph.nodes[node].parent != kNoGltfIndex)
        {
            return Refuse(error, roots.At(i).Path(), "is a root of the scene and also a child of another node",
                          ErrorCode::Invalid);
        }
        root_nodes.Push(node);
    }
    if (root_nodes.Size() == 0)
    {
        AddImportNote(&result->report, scene.Path(), StringView("the scene has no nodes: no level was made"));
        return Status::Success();
    }

    // Depth and root of every node (the graph has no cycles: ReadNodeGraph refused them).
    Vector<u32> depth;
    Vector<u32> top;
    for (u32 n = 0; n < node_count; ++n)
    {
        depth.Push(kUnset);
        top.Push(kUnset);
    }
    Vector<u32> chain;
    for (u32 n = 0; n < node_count; ++n)
    {
        if (depth[n] != kUnset)
        {
            continue;
        }
        chain.Clear();
        u32 at = n;
        while (at != kNoGltfIndex && depth[at] == kUnset)
        {
            chain.Push(at);
            at = graph.nodes[at].parent;
        }
        // `chain.Back()` is the highest node not yet known: its parent `at` is a known node or none.
        u32 d = at == kNoGltfIndex ? 0 : depth[at] + 1;
        const u32 root = at == kNoGltfIndex ? chain[chain.Size() - 1] : top[at];
        for (usize k = chain.Size(); k > 0; --k)
        {
            depth[chain[k - 1]] = d;
            top[chain[k - 1]] = root;
            d += 1;
        }
    }
    Vector<bool> in_scene;
    for (u32 n = 0; n < node_count; ++n)
    {
        in_scene.Push(false);
    }
    for (usize r = 0; r < root_nodes.Size(); ++r)
    {
        in_scene[root_nodes[r]] = true;
    }
    Vector<u32> nodes;  // the nodes of the level, in index order
    for (u32 n = 0; n < node_count; ++n)
    {
        if (!in_scene[top[n]])
        {
            continue;
        }
        if (depth[n] > kMaxLevelDepth)
        {
            char what[96];
            FormatTo(what, sizeof(what), "is %u levels deep: a level holds %u", static_cast<unsigned>(depth[n]),
                     static_cast<unsigned>(kMaxLevelDepth));
            char where[32];
            FormatTo(where, sizeof(where), "/nodes/%u", static_cast<unsigned>(n));
            error->Set(StringView(where, CStrLength(where)), StringView(what, CStrLength(what)));
            return Status::Make(ErrorCode::TooLarge, "the node hierarchy is too deep");
        }
        nodes.Push(n);
    }
    if (nodes.Size() > kMaxLevelEntities)
    {
        return Refuse(error, scene.Path(), "has more nodes than a level holds", ErrorCode::TooLarge);
    }

    // Names, unique, and the keys.
    Vector<String<HeapAllocator>> names;
    for (usize i = 0; i < nodes.Size(); ++i)
    {
        String<HeapAllocator> name(HeapAllocator::Default());
        LevelName(graph.nodes[nodes[i]].name, nodes[i], &name);
        names.Push(static_cast<String<HeapAllocator>&&>(name));
    }
    MakeNamesUnique(&names);
    BuildImportSummary(document, graph, options, nodes, names, result);

    // The id and the path of the level.
    StringView scene_name;
    bool has_name = false;
    ENGINE_TRY(scene.OptionalString("name", &scene_name, &has_name, error));
    String<HeapAllocator> segment(HeapAllocator::Default());
    CleanPathSegment(has_name ? scene_name : StringView("scene"), &segment);
    if (segment.Length() == 0)
    {
        segment = String<HeapAllocator>(HeapAllocator::Default(), StringView("scene"));
    }
    Vector<ResourceId> ids;
    ManifestObject object;
    object.index = scene_index;
    object.name = has_name ? scene_name : StringView();
    AssignIds(options, ImportKind::Level, Span<const ManifestObject>(&object, 1), &ids);
    const ResourceId level_id = ids[0];
    const String<HeapAllocator> prefix = ImportPathPrefix(options);

    storage::ComponentRegistry registry;
    if (storage::RegisterBuiltinComponents(&registry).Failed() || RegisterLevelComponents(&registry).Failed() ||
        animation::RegisterAnimationComponents(&registry).Failed())
    {
        return Status::Make(ErrorCode::Other, "the level components could not be registered");
    }
    const storage::CodecLimits limits;
    const storage::RegisteredComponent* transform_component = registry.FindById(storage::kLocalTransformId);
    const storage::RegisteredComponent* mesh_component = registry.FindById(kMeshReferenceId);
    const storage::RegisteredComponent* name_component = registry.FindById(kEntityNameId);
    const storage::RegisteredComponent* slot_component = registry.FindById(kMeshSlotMaterialsId);
    const storage::RegisteredComponent* skeleton_component = registry.FindById(animation::kSkeletonInstanceId);
    const storage::RegisteredComponent* link_component = registry.FindById(storage::kParentLinkId);

    Vector<storage::EntityKey> keys;  // by position in `nodes`
    for (usize i = 0; i < nodes.Size(); ++i)
    {
        keys.Push(LevelEntityKey(level_id, names[i].View()));
    }
    // The position in `nodes` of every node (kUnset for a node outside the level).
    Vector<u32> position;
    for (u32 n = 0; n < node_count; ++n)
    {
        position.Push(kUnset);
    }
    for (usize i = 0; i < nodes.Size(); ++i)
    {
        position[nodes[i]] = static_cast<u32>(i);
    }

    LevelData data;
    const JsonValue meshes = document.root.Get("meshes");
    // What the nodes name, looked up once: the imported id of every glTF mesh and material and the material of each
    // mesh's first primitive (a hostile file has hundreds of thousands of nodes: no scan per node).
    Vector<ResourceId> mesh_ids;
    Vector<ResourceId> material_ids;
    for (usize m = 0; m < meshes.Size(); ++m)
    {
        mesh_ids.Push(ResourceId());
    }
    const JsonValue materials_array = document.root.Get("materials");
    for (usize m = 0; m < materials_array.Size(); ++m)
    {
        material_ids.Push(ResourceId());
    }
    {
        const Vector<ImportManifestEntry>& entries = result->manifest.Entries();
        for (usize e = 0; e < entries.Size(); ++e)
        {
            if (entries[e].kind == ImportKind::Mesh && entries[e].index < mesh_ids.Size())
            {
                mesh_ids[entries[e].index] = entries[e].id;
            }
            else if (entries[e].kind == ImportKind::Material && entries[e].index < material_ids.Size())
            {
                material_ids[entries[e].index] = entries[e].id;
            }
        }
    }
    Vector<ResourceId> material_of_mesh;           // the material of the first primitive, or the null id
    Vector<Vector<ResourceId>> materials_of_mesh;  // the material of every primitive (a mesh slot each), or null ids
    for (usize m = 0; m < meshes.Size(); ++m)
    {
        ResourceId material;
        materials_of_mesh.Push(Vector<ResourceId>());
        {
            const JsonValue all = meshes.At(m).Get("primitives");
            for (usize p = 0; all.Exists() && p < all.Size() && p < kMaxLevelSlotMaterials; ++p)
            {
                u64 index = 0;
                bool present = false;
                ENGINE_TRY(all.At(p).OptionalUnsigned("material", &index, 0xFFFFFFFFull, &present, error));
                materials_of_mesh[m].Push(
                    present && index < material_ids.Size() ? material_ids[static_cast<usize>(index)] : ResourceId());
            }
        }
        const JsonValue primitives = meshes.At(m).Get("primitives");
        if (primitives.Exists() && primitives.Size() != 0)
        {
            u64 material_index = 0;
            bool present = false;
            ENGINE_TRY(primitives.At(0).OptionalUnsigned("material", &material_index, 0xFFFFFFFFull, &present, error));
            if (present && material_index < material_ids.Size())
            {
                material = material_ids[static_cast<usize>(material_index)];
            }
        }
        material_of_mesh.Push(material);
    }
    Vector<u64> named;  // the ids the entities name
    for (usize i = 0; i < nodes.Size(); ++i)
    {
        const u32 n = nodes[i];
        const GltfNode& node = graph.nodes[n];
        char where[32];
        FormatTo(where, sizeof(where), "/nodes/%u", static_cast<unsigned>(n));
        Vector<Piece> pieces;
        GltfTrs trs;
        if (!DecomposeMatrix(node.local, &trs))
        {
            return Refuse(error, StringView(where, CStrLength(where)),
                          "has a transform with a shear or a zero scale, which a level cannot hold",
                          ErrorCode::Invalid);
        }
        {
            const f64 scale = static_cast<f64>(options.scale);
            for (u32 axis = 0; axis < 3; ++axis)  // a double outside f32's range would convert to infinity
            {
                const f64 t = trs.translation[axis] * scale;
                const f64 sc = trs.scale[axis];
                if (!(t > -3.0e38 && t < 3.0e38 && sc > -3.0e38 && sc < 3.0e38))
                {
                    return Refuse(error, StringView(where, CStrLength(where)),
                                  "has a translation or scale too large for a level", ErrorCode::Invalid);
                }
            }
            ecs::LocalTransform local;
            local.position.x = static_cast<f32>(trs.translation[0] * scale);
            local.position.y = static_cast<f32>(trs.translation[1] * scale);
            local.position.z = static_cast<f32>(trs.translation[2] * scale);
            local.rotation.x = static_cast<f32>(trs.rotation[0]);
            local.rotation.y = static_cast<f32>(trs.rotation[1]);
            local.rotation.z = static_cast<f32>(trs.rotation[2]);
            local.rotation.w = static_cast<f32>(trs.rotation[3]);
            local.scale.x = static_cast<f32>(trs.scale[0]);
            local.scale.y = static_cast<f32>(trs.scale[1]);
            local.scale.z = static_cast<f32>(trs.scale[2]);
            Piece piece;
            piece.id = storage::kLocalTransformId;
            piece.version = transform_component->Version();
            bytes::ByteWriter writer;
            if (transform_component->Encode(&local, writer, limits).Failed())
            {
                return Refuse(error, StringView(where, CStrLength(where)), "has a transform a level cannot hold",
                              ErrorCode::Invalid);
            }
            for (usize b = 0; b < writer.Bytes().Size(); ++b)
            {
                piece.bytes.Push(writer.Bytes()[b]);
            }
            pieces.Push(static_cast<Piece&&>(piece));
        }
        if (node.mesh != kNoGltfIndex)
        {
            const ResourceId mesh = node.mesh < mesh_ids.Size() ? mesh_ids[node.mesh] : ResourceId();
            if (mesh.IsNone())
            {
                AddImportWarning(&result->report, StringView(where, CStrLength(where)),
                                 StringView("its mesh was not imported: the entity draws nothing"));
            }
            else
            {
                const ResourceId material = material_of_mesh[node.mesh];
                named.Push(mesh.Value());
                if (!material.IsNone())
                {
                    named.Push(material.Value());
                }
                MeshReference reference;
                reference.mesh.id = mesh.Value();
                reference.material.id = material.Value();
                Piece piece;
                piece.id = kMeshReferenceId;
                piece.version = mesh_component->Version();
                bytes::ByteWriter writer;
                if (mesh_component->Encode(&reference, writer, limits).Failed())
                {
                    return Refuse(error, StringView(where, CStrLength(where)), "has a mesh a level cannot hold",
                                  ErrorCode::Invalid);
                }
                for (usize b = 0; b < writer.Bytes().Size(); ++b)
                {
                    piece.bytes.Push(writer.Bytes()[b]);
                }
                pieces.Push(static_cast<Piece&&>(piece));
                const Vector<ResourceId>& per_slot = materials_of_mesh[node.mesh];
                bool differs = false;
                for (usize k = 0; k < per_slot.Size(); ++k)
                {
                    differs = differs || per_slot[k].Value() != material.Value();
                }
                if (differs && slot_component != nullptr)
                {
                    MeshSlotMaterials slots;
                    for (usize k = 0; k < per_slot.Size(); ++k)
                    {
                        slots.At(static_cast<u32>(k)).id = per_slot[k].Value();
                        if (!per_slot[k].IsNone())
                        {
                            named.Push(per_slot[k].Value());
                        }
                    }
                    Piece slot_piece;
                    slot_piece.id = kMeshSlotMaterialsId;
                    slot_piece.version = slot_component->Version();
                    bytes::ByteWriter slot_writer;
                    if (slot_component->Encode(&slots, slot_writer, limits).Failed())
                    {
                        return Refuse(error, StringView(where, CStrLength(where)),
                                      "has slot materials a level cannot hold", ErrorCode::Invalid);
                    }
                    for (usize b = 0; b < slot_writer.Bytes().Size(); ++b)
                    {
                        slot_piece.bytes.Push(slot_writer.Bytes()[b]);
                    }
                    pieces.Push(static_cast<Piece&&>(slot_piece));
                }
                if (node.skin != kNoGltfIndex && node.skin < skins.Size())
                {
                    animation::SkeletonInstance instance;
                    instance.skeleton = skins[node.skin].id.Value();
                    named.Push(instance.skeleton);
                    Piece skin_piece;
                    skin_piece.id = animation::kSkeletonInstanceId;
                    skin_piece.version = skeleton_component->Version();
                    bytes::ByteWriter skin_writer;
                    if (skeleton_component->Encode(&instance, skin_writer, limits).Failed())
                    {
                        return Refuse(error, StringView(where, CStrLength(where)), "has a skin a level cannot hold",
                                      ErrorCode::Invalid);
                    }
                    for (usize b = 0; b < skin_writer.Bytes().Size(); ++b)
                    {
                        skin_piece.bytes.Push(skin_writer.Bytes()[b]);
                    }
                    pieces.Push(static_cast<Piece&&>(skin_piece));
                }
            }
        }
        {
            EntityName name;
            name.name = String<HeapAllocator>(HeapAllocator::Default(), names[i].View());
            Piece piece;
            piece.id = kEntityNameId;
            piece.version = name_component->Version();
            bytes::ByteWriter writer;
            if (name_component->Encode(&name, writer, limits).Failed())
            {
                return Refuse(error, StringView(where, CStrLength(where)), "has a name a level cannot hold",
                              ErrorCode::Invalid);
            }
            for (usize b = 0; b < writer.Bytes().Size(); ++b)
            {
                piece.bytes.Push(writer.Bytes()[b]);
            }
            pieces.Push(static_cast<Piece&&>(piece));
        }
        if (node.parent != kNoGltfIndex)
        {
            Piece piece;
            piece.id = storage::kParentLinkId;
            piece.version = link_component->Version();
            PushKey(&piece.bytes, keys[position[node.parent]]);
            pieces.Push(static_cast<Piece&&>(piece));
        }
        bytes::ByteWriter container;
        container.WriteU32(static_cast<u32>(pieces.Size()));
        for (usize p = 0; p < pieces.Size(); ++p)
        {
            storage::ComponentRecord record;
            record.id = pieces[p].id;
            record.version = pieces[p].version;
            record.bytes = ByteSpan(pieces[p].bytes.Data(), pieces[p].bytes.Size());
            if (storage::WriteComponentRecord(record, limits, container).Failed())
            {
                return Refuse(error, StringView(where, CStrLength(where)), "has components a level cannot hold",
                              ErrorCode::TooLarge);
            }
        }
        data.entities.Push(storage::EntityRecord::Make(keys[i], container.Bytes()));
    }
    // Strictly increasing key order, and the resources the entities name.
    Sort(Span<storage::EntityRecord>(data.entities.Data(), data.entities.Size()),
         [](const storage::EntityRecord& a, const storage::EntityRecord& b)
         { return a.key.high != b.key.high ? a.key.high < b.key.high : a.key.low < b.key.low; });
    for (usize i = 1; i < data.entities.Size(); ++i)
    {
        const storage::EntityKey& a = data.entities[i - 1].key;
        const storage::EntityKey& b = data.entities[i].key;
        if (a.high == b.high && a.low == b.low)
        {
            return Refuse(error, scene.Path(), "has two nodes whose entity keys collide", ErrorCode::Invalid);
        }
    }
    Sort(Span<u64>(named.Data(), named.Size()), [](u64 a, u64 b) { return a < b; });
    Vector<u32> by_id;  // positions in result->resources, by resource id
    for (usize r = 0; r < result->resources.Size(); ++r)
    {
        by_id.Push(static_cast<u32>(r));
    }
    Sort(Span<u32>(by_id.Data(), by_id.Size()),
         [&](u32 a, u32 b) { return result->resources[a].id.Value() < result->resources[b].id.Value(); });
    for (usize n = 0; n < named.Size(); ++n)
    {
        if (n != 0 && named[n] == named[n - 1])
        {
            continue;
        }
        // the type of the resource: found in the import's own list (sorted by id once, searched by halving)
        const ImportedResource* found = nullptr;
        {
            usize low = 0;
            usize high = by_id.Size();
            while (low < high)
            {
                const usize mid = low + (high - low) / 2;
                if (result->resources[by_id[mid]].id.Value() < named[n])
                {
                    low = mid + 1;
                }
                else
                {
                    high = mid;
                }
            }
            if (low < by_id.Size() && result->resources[by_id[low]].id.Value() == named[n])
            {
                found = &result->resources[by_id[low]];
            }
        }
        if (found == nullptr)
        {
            return Status::Make(ErrorCode::Other, "a level names a resource the import did not make");
        }
        asset::TypeInfo info;
        if (!types.Find(found->type, &info))
        {
            return Status::Make(ErrorCode::Other, "a resource type of the import is not registered");
        }
        LevelResource listed;
        listed.id = found->id;
        listed.type = found->type;
        listed.type_version = info.current_version;
        listed.roles = kLevelRoleReference;
        data.resources.Push(listed);
    }
    Sort(Span<LevelResource>(data.resources.Data(), data.resources.Size()),
         [](const LevelResource& a, const LevelResource& b) { return a.id.Value() < b.id.Value(); });

    ImportedResource level;
    level.id = level_id;
    level.type = types::kLevel;
    level.path = String<HeapAllocator>(HeapAllocator::Default(), prefix.View());
    level.path.Append(StringView("/levels/"));
    level.path.Append(segment.View());
    const Status built = BuildLevelBlob(types, registry, level_id, data, &level.blob);
    if (built.Failed())
    {
        error->Set(scene.Path(), StringView(built.Message()));
        return built;
    }
    result->report.output_bytes += level.blob.Size();
    if (result->report.output_bytes > options.limits.max_output_bytes)
    {
        return Refuse(error, scene.Path(), "takes the import past the limit on the bytes it may make",
                      ErrorCode::TooLarge);
    }
    result->manifest.Add(ImportKind::Level, scene_index, has_name ? scene_name : StringView(), level_id);
    result->resources.Push(static_cast<ImportedResource&&>(level));
    return Status::Success();
}

}  // namespace engine_placeholder::asset
