#include "asset_import/gltf_skin.h"

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

Status RefuseSkin(GltfError* error, StringView where, StringView what, ErrorCode code = ErrorCode::Invalid)
{
    error->Set(where, what);
    return Status::Make(code, "a glTF skin is refused");
}

// One joint's name: the node's, else `joint_<node>`; cut to the format's length with a hash of the whole name; made
// distinct from the names before it with `_<n>`.
void JointName(StringView node_name, u32 node, const Vector<SkeletonJoint>& before, const char* skin_path, u32 slot,
               ImportReport* report, Array<char, kMaxJointNameBytes + 1>* out)
{
    String<HeapAllocator> name(HeapAllocator::Default(), node_name);
    bool renamed = false;
    if (name.Length() == 0)
    {
        char text[24];
        FormatTo(text, sizeof(text), "joint_%u", static_cast<unsigned>(node));
        name = String<HeapAllocator>(HeapAllocator::Default(), StringView(text, CStrLength(text)));
    }
    if (name.Length() > kMaxJointNameBytes)
    {
        char suffix[16];
        FormatTo(suffix, sizeof(suffix), "_%08x", static_cast<unsigned>(JointNameHash(name.View())));
        usize cut = kMaxJointNameBytes - CStrLength(suffix);
        while (cut > 0 && (static_cast<u8>(name.View()[cut]) & 0xC0) == 0x80)
        {
            --cut;
        }
        String<HeapAllocator> shortened(HeapAllocator::Default(), name.View().Substring(0, cut));
        shortened.Append(StringView(suffix, CStrLength(suffix)));
        name = static_cast<String<HeapAllocator>&&>(shortened);
        renamed = true;
    }
    const String<HeapAllocator> base(HeapAllocator::Default(), name.View());
    for (u32 n = 1;; ++n)
    {
        bool taken = false;
        for (usize k = 0; k < before.Size() && !taken; ++k)
        {
            taken = StringView(before[k].name.Data(), CStrLength(before[k].name.Data())) == name.View();
        }
        if (!taken)
        {
            break;
        }
        char suffix[16];
        FormatTo(suffix, sizeof(suffix), "_%u", static_cast<unsigned>(n));
        usize cut = base.Length();
        if (cut + CStrLength(suffix) > kMaxJointNameBytes)
        {
            cut = kMaxJointNameBytes - CStrLength(suffix);
            while (cut > 0 && (static_cast<u8>(base.View()[cut]) & 0xC0) == 0x80)
            {
                --cut;
            }
        }
        name = String<HeapAllocator>(HeapAllocator::Default(), base.View().Substring(0, cut));
        name.Append(StringView(suffix, CStrLength(suffix)));
        renamed = true;
    }
    *out = {};
    MemCopy(out->Data(), name.View().Data(), name.Length());
    if (renamed)
    {
        String<HeapAllocator> where(HeapAllocator::Default(), StringView(skin_path));
        char slot_text[24];
        FormatTo(slot_text, sizeof(slot_text), ".joints[%u]", static_cast<unsigned>(slot));
        where.Append(StringView(slot_text, CStrLength(slot_text)));
        String<HeapAllocator> what(HeapAllocator::Default(), StringView("joint renamed to "));
        what.Append(name.View());
        AddImportNote(report, where.View(), what.View());
    }
}

}  // namespace

Status ImportSkins(const GltfDocument& document, const GltfNodeGraph& graph, const ImportOptions& options,
                   const TypeRegistry& types, ImportResult* result, Vector<ImportedSkin>* skins, GltfError* error)
{
    skins->Clear();
    const JsonValue skin_array = document.root.Get("skins");
    const usize skin_count = skin_array.Size();
    if (skin_count == 0)
    {
        return Status::Success();
    }
    Vector<StringView> names;
    Vector<ManifestObject> objects;
    for (usize s = 0; s < skin_count; ++s)
    {
        StringView name;
        bool has_name = false;
        ENGINE_TRY(skin_array.At(s).OptionalString("name", &name, &has_name, error));
        names.Push(has_name ? name : StringView());
        objects.Push(ManifestObject{static_cast<u32>(s), has_name ? name : StringView()});
    }
    Vector<ResourceId> ids;
    AssignIds(options, ImportKind::Skeleton, Span<const ManifestObject>(objects.Data(), objects.Size()), &ids);
    Vector<String<HeapAllocator>> segments;
    for (usize s = 0; s < skin_count; ++s)
    {
        String<HeapAllocator> segment(HeapAllocator::Default());
        CleanPathSegment(names[s], &segment);
        if (segment.Length() == 0)
        {
            char text[24];
            FormatTo(text, sizeof(text), "skin%u", static_cast<unsigned>(s));
            segment = String<HeapAllocator>(HeapAllocator::Default(), StringView(text, CStrLength(text)));
        }
        segments.Push(static_cast<String<HeapAllocator>&&>(segment));
    }
    MakeNamesUnique(&segments);
    const String<HeapAllocator> prefix = ImportPathPrefix(options);
    const usize node_count = graph.nodes.Size();
    Vector<u32> node_slot;  // per node: its slot in the skin being read, or none (reset after each skin)
    node_slot.Resize(node_count);
    for (usize n = 0; n < node_count; ++n)
    {
        node_slot[n] = kNoGltfIndex;
    }
    for (usize s = 0; s < skin_count; ++s)
    {
        const JsonValue skin = skin_array.At(s);
        const JsonValue joint_array = skin.Get("joints");
        if (!joint_array.IsArray() || joint_array.Size() == 0)
        {
            return RefuseSkin(error, skin.Path(), StringView("has no joints"));
        }
        if (joint_array.Size() > kMaxSkeletonJoints)
        {
            error->SetNumber(joint_array.Path(), StringView("has more joints than a skeleton may hold (256): "),
                             joint_array.Size());
            return Status::Make(ErrorCode::TooLarge, "too many joints");
        }
        const usize joint_count = joint_array.Size();
        Vector<u32> slot_node;  // skin.joints[k]: the node
        for (usize k = 0; k < joint_count; ++k)
        {
            u32 node = 0;
            ENGINE_TRY(joint_array.At(k).AsIndex(&node, node_count, error));
            if (node_slot[node] != kNoGltfIndex)
            {
                return RefuseSkin(error, joint_array.At(k).Path(), StringView("lists a node that is a joint already"));
            }
            node_slot[node] = static_cast<u32>(k);
            slot_node.Push(node);
        }
        // The joint above each joint: the nearest ancestor that is a joint of this skin.
        Vector<u32> parent_slot;
        bool in_order = true;
        for (usize k = 0; k < joint_count; ++k)
        {
            u32 up = graph.nodes[slot_node[k]].parent;
            while (up != kNoGltfIndex && node_slot[up] == kNoGltfIndex)
            {
                up = graph.nodes[up].parent;
            }
            parent_slot.Push(up == kNoGltfIndex ? kNoGltfIndex : node_slot[up]);
            in_order = in_order && (up == kNoGltfIndex || node_slot[up] < k);
        }
        // Parent first: the skin's own order when it is, else the stable order by node index.
        Vector<u32> order;  // skeleton joint -> slot
        if (in_order)
        {
            for (usize k = 0; k < joint_count; ++k)
            {
                order.Push(static_cast<u32>(k));
            }
        }
        else
        {
            Vector<bool> emitted;
            emitted.Resize(joint_count);
            while (order.Size() < joint_count)
            {
                u32 best = kNoGltfIndex;
                for (usize k = 0; k < joint_count; ++k)
                {
                    if (!emitted[k] && (parent_slot[k] == kNoGltfIndex || emitted[parent_slot[k]]) &&
                        (best == kNoGltfIndex || slot_node[k] < slot_node[best]))
                    {
                        best = static_cast<u32>(k);
                    }
                }
                emitted[best] = true;  // the node graph has no cycle, so some joint is always ready
                order.Push(best);
            }
        }
        Vector<u32> joint_of_slot;
        joint_of_slot.Resize(joint_count);
        for (usize j = 0; j < joint_count; ++j)
        {
            joint_of_slot[order[j]] = static_cast<u32>(j);
        }
        // The joints: local transforms with the non-joint nodes between them folded in.
        Vector<SkeletonJoint> joints;
        Vector<GltfMatrix> world;
        Vector<GltfMatrix> ancestors;  // per joint, in skeleton order
        Array<f32, 3> bounds_min = {{0, 0, 0}};
        Array<f32, 3> bounds_max = {{0, 0, 0}};
        for (usize j = 0; j < joint_count; ++j)
        {
            const u32 slot = order[j];
            const u32 node = slot_node[slot];
            GltfMatrix ancestor;  // the non-joint nodes above this joint, up to the nearest joint or the scene root
            u32 up = graph.nodes[node].parent;
            while (up != kNoGltfIndex && node_slot[up] == kNoGltfIndex)
            {
                ancestor = MultiplyMatrices(graph.nodes[up].local, ancestor);
                up = graph.nodes[up].parent;
            }
            const GltfMatrix folded = MultiplyMatrices(ancestor, graph.nodes[node].local);
            ancestors.Push(ancestor);
            GltfTrs trs;
            if (!DecomposeMatrix(folded, &trs))
            {
                char what[80];
                FormatTo(what, sizeof(what), "node %u has a bind transform with a shear or no volume",
                         static_cast<unsigned>(node));
                return RefuseSkin(error, joint_array.At(slot).Path(), StringView(what, CStrLength(what)));
            }
            SkeletonJoint joint;
            JointName(graph.nodes[node].name, node, joints, skin.Path().Data(), slot, &result->report, &joint.name);
            joint.parent = parent_slot[slot] == kNoGltfIndex ? kNoParent
                                                             : static_cast<u16>(joint_of_slot[parent_slot[slot]]);
            const f64 sign = trs.rotation[3] < 0 ? -1.0 : 1.0;  // q and -q are one rotation; the format wants w >= 0
            const f64 unit = static_cast<f64>(options.scale);
            for (usize c = 0; c < 3; ++c)
            {
                joint.translation[c] = static_cast<f32>(trs.translation[c] * unit);
                joint.scale[c] = static_cast<f32>(trs.scale[c]);
            }
            for (usize c = 0; c < 4; ++c)
            {
                joint.rotation[c] = static_cast<f32>(trs.rotation[c] * sign);
            }
            for (usize c = 0; c < 3; ++c)
            {
                if (!IsFinite(joint.translation[c]) || !IsFinite(joint.scale[c]))
                {
                    return RefuseSkin(error, joint_array.At(slot).Path(),
                                      StringView("has a translation or scale past the range of a float"));
                }
            }
            GltfMatrix model = folded;
            if (joint.parent != kNoParent)
            {
                model = MultiplyMatrices(world[joint.parent], folded);
            }
            world.Push(model);
            for (usize c = 0; c < 3; ++c)
            {
                const f32 v = static_cast<f32>(model.m[12 + c] * static_cast<f64>(options.scale));
                bounds_min[c] = j == 0 || v < bounds_min[c] ? v : bounds_min[c];
                bounds_max[c] = j == 0 || v > bounds_max[c] ? v : bounds_max[c];
            }
            joints.Push(joint);
        }
        ComputeInverseBindMatrices(Span<SkeletonJoint>(joints.Data(), joints.Size()));
        // The inverse bind matrices the file stores replace the computed ones (a difference is a warning).
        const JsonValue ibm_value = skin.Get("inverseBindMatrices");
        if (ibm_value.Exists())
        {
            u32 accessor_index = 0;
            ENGINE_TRY(ibm_value.AsIndex(&accessor_index, document.accessors.Size(), error));
            const GltfAccessor& accessor = document.accessors[accessor_index];
            if (accessor.type != GltfAccessorType::Mat4 || accessor.component_type != 5126)
            {
                return RefuseSkin(error, ibm_value.Path(), StringView("is not a MAT4 accessor of floats"));
            }
            if (accessor.count != joint_count)
            {
                error->SetNumber(ibm_value.Path(),
                                 StringView("has a different number of matrices than the skin has joints: "),
                                 accessor.count);
                return Status::Make(ErrorCode::Invalid, "a glTF skin is refused");
            }
            Vector<f32> stored;
            ENGINE_TRY(ReadAccessorFloats(document, accessor_index, &stored, error));
            usize differing = 0;
            for (usize k = 0; k < joint_count; ++k)
            {
                const f32* m = stored.Data() + k * 16;  // column-major 4x4
                if (Abs(m[3]) > 1e-4f || Abs(m[7]) > 1e-4f || Abs(m[11]) > 1e-4f || Abs(m[15] - 1.0f) > 1e-4f)
                {
                    char what[64];
                    FormatTo(what, sizeof(what), "matrix %u is not an affine transform", static_cast<unsigned>(k));
                    return RefuseSkin(error, ibm_value.Path(), StringView(what, CStrLength(what)));
                }
                SkeletonJoint& joint = joints[joint_of_slot[k]];
                bool differs = false;
                for (usize row = 0; row < 3; ++row)
                {
                    for (usize col = 0; col < 4; ++col)
                    {
                        // The translation column scales with the positions it takes back to the origin.
                        const f32 value = col == 3 ? m[col * 4 + row] * options.scale : m[col * 4 + row];
                        differs = differs || Abs(value - joint.inverse_bind[row * 4 + col]) > 1e-4f;
                        joint.inverse_bind[row * 4 + col] = value;
                    }
                }
                differing += differs ? 1 : 0;
            }
            if (differing != 0)
            {
                char what[96];
                FormatTo(
                    what, sizeof(what),
                    "%u inverse bind matrices differ from the bind pose by more than 1e-4; the stored ones are used",
                    static_cast<unsigned>(differing));
                AddImportWarning(&result->report, ibm_value.Path(), StringView(what, CStrLength(what)));
            }
        }
        ImportedResource resource;
        resource.id = ids[s];
        resource.type = types::kSkeleton;
        resource.path = String<HeapAllocator>(HeapAllocator::Default(), prefix.View());
        resource.path.Append(StringView("/skeletons/"));
        resource.path.Append(segments[s].View());
        const Status built = BuildSkeletonBlob(
            types, resource.id, Span<const SkeletonJoint>(joints.Data(), joints.Size()), true, &resource.blob);
        if (built.Failed())
        {
            error->Set(skin.Path(), StringView(built.Message()));
            return built;
        }
        result->report.output_bytes += resource.blob.Size();
        if (result->report.output_bytes > options.limits.max_output_bytes)
        {
            return RefuseSkin(error, skin.Path(),
                              StringView("takes the import past the limit on the bytes it may make"),
                              ErrorCode::TooLarge);
        }
        ImportedSkin imported;
        imported.skin_index = static_cast<u32>(s);
        imported.name = String<HeapAllocator>(HeapAllocator::Default(), segments[s].View());
        imported.id = resource.id;
        imported.hash = ComputeSkeletonHash(Span<const SkeletonJoint>(joints.Data(), joints.Size()));
        imported.joint_count = static_cast<u32>(joint_count);
        imported.joint_of_slot = static_cast<Vector<u32>&&>(joint_of_slot);
        for (usize k = 0; k < joint_count; ++k)
        {
            imported.node_joints.Push(NodeJoint{slot_node[k], imported.joint_of_slot[k]});
            node_slot[slot_node[k]] = kNoGltfIndex;  // the next skin starts with no joints
        }
        Sort(Span<NodeJoint>(imported.node_joints.Data(), imported.node_joints.Size()),
             [](const NodeJoint& x, const NodeJoint& y) { return x.node < y.node; });
        imported.ancestors = static_cast<Vector<GltfMatrix>&&>(ancestors);
        imported.bounds_min = bounds_min;
        imported.bounds_max = bounds_max;
        result->manifest.Add(ImportKind::Skeleton, static_cast<u32>(s), names[s], resource.id);
        result->resources.Push(static_cast<ImportedResource&&>(resource));
        skins->Push(static_cast<ImportedSkin&&>(imported));
    }
    return Status::Success();
}

}  // namespace engine_placeholder::asset
