#include "asset_import/gltf_summary.h"

#include "algorithm.h"
#include "asset_import/gltf_accessor.h"
#include "cstr/format.h"
#include "cstr/number.h"
#include "lang/scalar.h"

namespace engine_placeholder::asset
{

namespace
{

void AppendNumber(String<HeapAllocator>* out, f64 value)
{
    if (!(value > -1.0e300 && value < 1.0e300))
    {
        value = 0;  // JSON has no infinity or NaN
    }
    char text[32];
    const usize length = FormatGeneral(text, sizeof(text), value, 9);  // '.' whatever the host's LC_NUMERIC is
    out->Append(StringView(text, length));
}

void AppendString(String<HeapAllocator>* out, StringView text)
{
    out->Append('"');
    for (usize i = 0; i < text.Length(); ++i)
    {
        const char c = text[i];
        if (c == '"' || c == '\\')
        {
            out->Append('\\');
            out->Append(c);
        }
        else if (static_cast<u8>(c) < 0x20)
        {
            char escape[8];
            FormatTo(escape, sizeof(escape), "\\u%04x", static_cast<u32>(static_cast<u8>(c)));
            out->Append(StringView(escape, CStrLength(escape)));
        }
        else
        {
            out->Append(c);
        }
    }
    out->Append('"');
}

void AppendVector(String<HeapAllocator>* out, const f64* v)
{
    out->Append('[');
    for (u32 i = 0; i < 3; ++i)
    {
        if (i != 0)
        {
            out->Append(',');
        }
        AppendNumber(out, v[i]);
    }
    out->Append(']');
}

// The `index`th number of an accessor's `min` or `max` array.
bool RangeValue(const JsonValue& accessor, StringView key, usize index, f64* out)
{
    const JsonValue array = accessor.Get(key);
    if (!array.Exists() || !array.IsArray() || index >= array.Size())
    {
        return false;
    }
    GltfError ignored;
    f64 value = 0;
    if (array.At(index).AsDouble(&value, &ignored).Failed() || !(value > -1.0e300 && value < 1.0e300))
    {
        return false;
    }
    *out = value;
    return true;
}

// The range of the first `components` components of an accessor: its `min` and `max` when the file gives them (the
// specification requires them for POSITION and for animation inputs), else computed from the elements.
bool AccessorRange(const GltfDocument& document, const JsonValue& index_value, u32 components, f64* low, f64* high)
{
    const JsonValue accessors = document.root.Get("accessors");
    GltfError ignored;
    u32 index = 0;
    if (!index_value.Exists() || !accessors.Exists() ||
        index_value.AsIndex(&index, accessors.Size(), &ignored).Failed())
    {
        return false;
    }
    const JsonValue accessor = accessors.At(index);
    bool given = true;
    for (u32 c = 0; given && c < components; ++c)
    {
        given = RangeValue(accessor, "min", c, &low[c]) && RangeValue(accessor, "max", c, &high[c]);
    }
    if (given)
    {
        return true;
    }
    u64 type_components = 1;
    StringView type;
    bool present = false;
    if (accessor.OptionalString("type", &type, &present, &ignored).Failed() || !present)
    {
        return false;
    }
    if (type == StringView("VEC3"))
    {
        type_components = 3;
    }
    else if (type != StringView("SCALAR"))
    {
        return false;
    }
    if (type_components < components)
    {
        return false;
    }
    Vector<f32> values;
    if (ReadAccessorFloats(document, index, &values, &ignored).Failed() || values.Size() < type_components)
    {
        return false;
    }
    for (u32 c = 0; c < components; ++c)
    {
        low[c] = static_cast<f64>(values[c]);
        high[c] = low[c];
    }
    for (usize at = 0; at + type_components <= values.Size(); at += static_cast<usize>(type_components))
    {
        for (u32 c = 0; c < components; ++c)
        {
            const f64 v = static_cast<f64>(values[at + c]);
            low[c] = Min(low[c], v);
            high[c] = Max(high[c], v);
        }
    }
    return true;
}

// The world matrix of a node: its local matrix under the parents' (the graph has no cycles).
GltfMatrix WorldOf(const GltfNodeGraph& graph, u32 node)
{
    Vector<u32> chain;
    for (u32 at = node; at != kNoGltfIndex && chain.Size() <= graph.nodes.Size(); at = graph.nodes[at].parent)
    {
        chain.Push(at);
    }
    GltfMatrix world;
    for (usize k = chain.Size(); k > 0; --k)
    {
        world = MultiplyMatrices(world, graph.nodes[chain[k - 1]].local);
    }
    return world;
}

void Transform(const GltfMatrix& m, const f64* p, f64* out)
{
    for (u32 row = 0; row < 3; ++row)
    {
        out[row] = m.m[row] * p[0] + m.m[4 + row] * p[1] + m.m[8 + row] * p[2] + m.m[12 + row];
    }
}

}  // namespace

void BuildImportSummary(const GltfDocument& document, const GltfNodeGraph& graph, const ImportOptions& options,
                        const Vector<u32>& nodes, const Vector<String<HeapAllocator>>& names, ImportResult* result)
{
    String<HeapAllocator>& out = result->summary;
    out = String<HeapAllocator>(HeapAllocator::Default());
    const f64 scale = static_cast<f64>(options.scale);
    const JsonValue meshes = document.root.Get("meshes");

    struct MeshBox
    {
        bool known = false;
        bool present = false;
        f64 low[3] = {0, 0, 0};
        f64 high[3] = {0, 0, 0};
    };
    Vector<MeshBox> boxes;
    for (usize m = 0; meshes.Exists() && m < meshes.Size(); ++m)
    {
        boxes.Push(MeshBox());
    }
    bool have_bounds = false;
    f64 low[3] = {0, 0, 0};
    f64 high[3] = {0, 0, 0};

    char head[32];
    FormatTo(head, sizeof(head), "{\"version\":%u,\"nodes\":[", static_cast<unsigned>(kImportSummaryVersion));
    out.Append(StringView(head, CStrLength(head)));
    for (usize i = 0; i < nodes.Size(); ++i)
    {
        const u32 n = nodes[i];
        const GltfNode& node = graph.nodes[n];
        const GltfMatrix world = WorldOf(graph, n);
        if (i != 0)
        {
            out.Append(',');
        }
        out.Append("{\"name\":");
        AppendString(&out, names[i].View());
        const f64 position[3] = {world.m[12] * scale, world.m[13] * scale, world.m[14] * scale};
        out.Append(",\"position\":");
        AppendVector(&out, position);
        char mesh_text[32];
        FormatTo(mesh_text, sizeof(mesh_text), ",\"mesh\":%d}",
                 node.mesh == kNoGltfIndex ? -1 : static_cast<int>(node.mesh));
        out.Append(StringView(mesh_text, CStrLength(mesh_text)));

        if (node.mesh == kNoGltfIndex || !meshes.Exists() || node.mesh >= meshes.Size())
        {
            continue;
        }
        MeshBox& box = boxes[node.mesh];
        if (!box.known)
        {
            // Once per mesh, not per node that uses it: reading an accessor's elements costs as much as the accessor.
            box.known = true;
            const JsonValue primitives = meshes.At(node.mesh).Get("primitives");
            for (usize p = 0; primitives.Exists() && p < primitives.Size(); ++p)
            {
                f64 lo[3];
                f64 hi[3];
                if (!AccessorRange(document, primitives.At(p).Get("attributes").Get("POSITION"), 3, lo, hi))
                {
                    continue;
                }
                for (u32 axis = 0; axis < 3; ++axis)
                {
                    box.low[axis] = box.present ? Min(box.low[axis], lo[axis]) : lo[axis];
                    box.high[axis] = box.present ? Max(box.high[axis], hi[axis]) : hi[axis];
                }
                box.present = true;
            }
        }
        if (!box.present)
        {
            continue;
        }
        for (u32 corner = 0; corner < 8; ++corner)
        {
            const f64 point[3] = {(corner & 1) != 0 ? box.high[0] : box.low[0],
                                  (corner & 2) != 0 ? box.high[1] : box.low[1],
                                  (corner & 4) != 0 ? box.high[2] : box.low[2]};
            f64 at[3];
            Transform(world, point, at);
            for (u32 axis = 0; axis < 3; ++axis)
            {
                at[axis] *= scale;
                if (!have_bounds)
                {
                    low[axis] = at[axis];
                    high[axis] = at[axis];
                }
                else
                {
                    low[axis] = Min(low[axis], at[axis]);
                    high[axis] = Max(high[axis], at[axis]);
                }
            }
            have_bounds = true;
        }
    }
    out.Append("],\"bounds\":");
    if (have_bounds)
    {
        out.Append("{\"min\":");
        AppendVector(&out, low);
        out.Append(",\"max\":");
        AppendVector(&out, high);
        out.Append('}');
    }
    else
    {
        out.Append("null");
    }

    out.Append(",\"materials\":[");
    const JsonValue materials = document.root.Get("materials");
    for (usize m = 0; materials.Exists() && m < materials.Size(); ++m)
    {
        if (m != 0)
        {
            out.Append(',');
        }
        GltfError ignored;
        StringView name;
        bool present = false;
        if (materials.At(m).OptionalString("name", &name, &present, &ignored).Failed() || !present)
        {
            name = StringView();
        }
        AppendString(&out, name);
    }

    out.Append("],\"animations\":[");
    const JsonValue animations = document.root.Get("animations");
    Vector<f64> input_end;  // by accessor index: the last time of an animation input, -1 not scanned yet
    for (usize a = 0; animations.Exists() && a < animations.Size(); ++a)
    {
        if (a != 0)
        {
            out.Append(',');
        }
        const JsonValue animation = animations.At(a);
        GltfError ignored;
        StringView name;
        bool present = false;
        if (animation.OptionalString("name", &name, &present, &ignored).Failed() || !present)
        {
            name = StringView();
        }
        f64 duration = 0;
        const JsonValue samplers = animation.Get("samplers");
        // Samplers of one file often name the same accessor; scan each accessor once (hostile files repeat one).
        for (usize s = 0; samplers.Exists() && s < samplers.Size(); ++s)
        {
            const JsonValue input = samplers.At(s).Get("input");
            const JsonValue accessors = document.root.Get("accessors");
            GltfError index_error;
            u32 index = 0;
            if (!input.Exists() || !accessors.Exists() ||
                input.AsIndex(&index, accessors.Size(), &index_error).Failed())
            {
                continue;
            }
            if (input_end.Size() != accessors.Size())
            {
                input_end.Resize(accessors.Size());
                for (usize i = 0; i < input_end.Size(); ++i)
                {
                    input_end[i] = -1.0;
                }
            }
            if (input_end[index] < 0)
            {
                f64 start = 0;
                f64 end = 0;
                input_end[index] = AccessorRange(document, input, 1, &start, &end) ? Max(end, 0.0) : 0.0;
            }
            duration = Max(duration, input_end[index]);
        }
        out.Append("{\"name\":");
        AppendString(&out, name);
        out.Append(",\"duration\":");
        AppendNumber(&out, duration);
        out.Append('}');
    }
    out.Append("]}");
}

}  // namespace engine_placeholder::asset
