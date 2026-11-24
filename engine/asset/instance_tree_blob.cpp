#include "asset/instance_tree_blob.h"

#include "cstr/mem_ops.h"
#include "lang/scalar.h"
#include "log/assert.h"

namespace vse::asset
{

namespace
{

void ItStore(byte* p, u32 value)
{
    p[0] = static_cast<byte>(value & 0xFF);
    p[1] = static_cast<byte>((value >> 8) & 0xFF);
    p[2] = static_cast<byte>((value >> 16) & 0xFF);
    p[3] = static_cast<byte>((value >> 24) & 0xFF);
}

u32 ItLoad(const byte* p)
{
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) | (static_cast<u32>(p[2]) << 16) |
           (static_cast<u32>(p[3]) << 24);
}

void ItStore64(byte* p, u64 value)
{
    ItStore(p, static_cast<u32>(value & 0xFFFFFFFFu));
    ItStore(p + 4, static_cast<u32>(value >> 32));
}

u64 ItLoad64(const byte* p)
{
    return static_cast<u64>(ItLoad(p)) | (static_cast<u64>(ItLoad(p + 4)) << 32);
}

void ItStoreF(byte* p, f32 value)
{
    u32 bits = 0;
    MemCopy(&bits, &value, sizeof(bits));
    ItStore(p, bits);
}

f32 ItLoadF(const byte* p)
{
    const u32 bits = ItLoad(p);
    f32 value = 0.0f;
    MemCopy(&value, &bits, sizeof(value));
    return value;
}

void ItStoreD(byte* p, f64 value)
{
    u64 bits = 0;
    MemCopy(&bits, &value, sizeof(bits));
    ItStore64(p, bits);
}

f64 ItLoadD(const byte* p)
{
    const u64 bits = ItLoad64(p);
    f64 value = 0.0;
    MemCopy(&value, &bits, sizeof(value));
    return value;
}

void ItReadNode(const byte* at, InstanceTreeSourceNode* out)
{
    out->parent = ItLoad(at);
    out->desc.mesh = ItLoad(at + 4);
    out->desc.material_slot = ItLoad(at + 8);
    out->desc.flags = ItLoad(at + 12);
    out->desc.position = Vec3d(ItLoadD(at + 16), ItLoadD(at + 24), ItLoadD(at + 32));
    out->desc.rotation = Quaternion(ItLoadF(at + 40), ItLoadF(at + 44), ItLoadF(at + 48), ItLoadF(at + 52));
    out->desc.scale = Vec3(ItLoadF(at + 56), ItLoadF(at + 60), ItLoadF(at + 64));
    out->desc.extra.tint = Vec4(ItLoadF(at + 68), ItLoadF(at + 72), ItLoadF(at + 76), ItLoadF(at + 80));
    out->desc.extra.custom = ItLoad(at + 84);
}

// Feeds the nodes to a builder; returns Ok or the refusing status.
renderer::TreeStatus ItFeed(renderer::InstanceTreeBuilder* builder, Span<const InstanceTreeSourceNode> nodes)
{
    Vector<renderer::TreeNodeId> ids;
    for (usize i = 0; i < nodes.Size(); ++i)
    {
        renderer::TreeNodeId id;
        renderer::TreeStatus status = renderer::TreeStatus::Ok;
        if (nodes[i].parent == InstanceTreeSourceNode::kNoParent)
        {
            status = builder->AddRoot(nodes[i].desc, &id);
        }
        else if (nodes[i].parent >= i)
        {
            return renderer::TreeStatus::UnknownParent;
        }
        else
        {
            status = builder->AddChild(ids[nodes[i].parent], nodes[i].desc, &id);
        }
        if (status != renderer::TreeStatus::Ok)
        {
            return status;
        }
        ids.Push(id);
    }
    return renderer::TreeStatus::Ok;
}

}  // namespace

Status BuildInstanceTreeBlob(const TypeRegistry& types, ResourceId id, Span<const renderer::TreeMeshInfo> meshes,
                             Span<const InstanceTreeSourceNode> nodes, Vector<byte>* out, u32 target)
{
    if (nodes.Size() == 0 || nodes.Size() > renderer::kMaxTreeNodes)
    {
        return Status::Make(ErrorCode::Invalid, "an instance tree needs between 1 and 2^22 nodes");
    }
    {
        renderer::InstanceTreeBuilder check(meshes);
        if (ItFeed(&check, nodes) != renderer::TreeStatus::Ok)
        {
            return Status::Make(ErrorCode::Invalid, "a node of the instance tree is refused");
        }
    }
    Vector<byte> head;
    head.Resize(kInstanceTreeBlobHeaderBytes);
    MemSet(head.Data(), 0, head.Size());
    ItStore(head.Data(), static_cast<u32>(nodes.Size()));
    ItStore(head.Data() + 4, static_cast<u32>(meshes.Size()));
    Vector<byte> mesh_bytes;
    mesh_bytes.Resize(meshes.Size() * kInstanceTreeBlobMeshBytes);
    for (usize i = 0; i < meshes.Size(); ++i)
    {
        byte* at = mesh_bytes.Data() + i * kInstanceTreeBlobMeshBytes;
        const Aabb& b = meshes[i].bounds;
        ItStoreF(at, b.min.x);
        ItStoreF(at + 4, b.min.y);
        ItStoreF(at + 8, b.min.z);
        ItStoreF(at + 12, b.max.x);
        ItStoreF(at + 16, b.max.y);
        ItStoreF(at + 20, b.max.z);
    }
    Vector<byte> node_bytes;
    node_bytes.Resize(nodes.Size() * kInstanceTreeBlobNodeBytes);
    for (usize i = 0; i < nodes.Size(); ++i)
    {
        byte* at = node_bytes.Data() + i * kInstanceTreeBlobNodeBytes;
        const renderer::TreeNodeDesc& d = nodes[i].desc;
        ItStore(at, nodes[i].parent);
        ItStore(at + 4, d.mesh);
        ItStore(at + 8, d.material_slot);
        ItStore(at + 12, d.flags);
        ItStoreD(at + 16, d.position.x);
        ItStoreD(at + 24, d.position.y);
        ItStoreD(at + 32, d.position.z);
        ItStoreF(at + 40, d.rotation.x);
        ItStoreF(at + 44, d.rotation.y);
        ItStoreF(at + 48, d.rotation.z);
        ItStoreF(at + 52, d.rotation.w);
        ItStoreF(at + 56, d.scale.x);
        ItStoreF(at + 60, d.scale.y);
        ItStoreF(at + 64, d.scale.z);
        ItStoreF(at + 68, d.extra.tint.x);
        ItStoreF(at + 72, d.extra.tint.y);
        ItStoreF(at + 76, d.extra.tint.z);
        ItStoreF(at + 80, d.extra.tint.w);
        ItStore(at + 84, d.extra.custom);
    }
    BlobWriter writer(types, types::kInstanceTrees, id, target);
    ENGINE_TRY(writer.AddSection(kInstanceTreeBlobSectionHeader, ByteSpan(head.Data(), head.Size())));
    ENGINE_TRY(writer.AddSection(kInstanceTreeBlobSectionMeshes, ByteSpan(mesh_bytes.Data(), mesh_bytes.Size())));
    ENGINE_TRY(writer.AddSection(kInstanceTreeBlobSectionNodes, ByteSpan(node_bytes.Data(), node_bytes.Size())));
    return writer.Finish(out);
}

Status InstanceTreeBlobView::Open(const SharedBytesPtr& bytes, const TypeRegistry& types)
{
    *this = InstanceTreeBlobView();
    if (!bytes)
    {
        return Status::Make(ErrorCode::Invalid, "no bytes were given");
    }
    const ByteSpan data = bytes->Bytes();
    BlobReader blob;
    ENGINE_TRY(blob.Open(data, types));
    if (blob.Type() != types::kInstanceTrees)
    {
        return Status::Make(ErrorCode::TypeMismatch, "the blob is not an instance tree");
    }
    ByteSpan head;
    if (!blob.FindSection(kInstanceTreeBlobSectionHeader, &head) || head.Size() != kInstanceTreeBlobHeaderBytes)
    {
        return Status::Make(ErrorCode::Corrupt, "the instance tree header section is missing or has the wrong size");
    }
    const u32 nodes = ItLoad(head.Data());
    const u32 meshes = ItLoad(head.Data() + 4);
    bool reserved_zero = ItLoad(head.Data() + 8) == 0;
    for (usize k = 12; k < kInstanceTreeBlobHeaderBytes; k += 4)
    {
        reserved_zero = reserved_zero && ItLoad(head.Data() + k) == 0;
    }
    if (!reserved_zero || nodes == 0 || nodes > renderer::kMaxTreeNodes || meshes > renderer::kMaxTreeMeshes)
    {
        return Status::Make(ErrorCode::Corrupt, "an instance tree header field is outside its limits");
    }
    ByteSpan mesh_section;
    ByteSpan node_section;
    if (!blob.FindSection(kInstanceTreeBlobSectionMeshes, &mesh_section) ||
        mesh_section.Size() != static_cast<usize>(meshes) * kInstanceTreeBlobMeshBytes)
    {
        return Status::Make(ErrorCode::Corrupt, "the instance tree mesh section is missing or has the wrong size");
    }
    if (!blob.FindSection(kInstanceTreeBlobSectionNodes, &node_section) ||
        static_cast<u64>(node_section.Size()) != static_cast<u64>(nodes) * kInstanceTreeBlobNodeBytes)
    {
        return Status::Make(ErrorCode::Corrupt, "the instance tree node section is missing or has the wrong size");
    }
    node_count_ = nodes;
    mesh_count_ = meshes;
    id_ = blob.Id();
    bytes_ = bytes;
    meshes_ = static_cast<usize>(mesh_section.Data() - data.Data());
    nodes_ = static_cast<usize>(node_section.Data() - data.Data());
    return Status::Success();
}

Status MakeInstanceTree(const InstanceTreeBlobView& view, jobs::Scheduler* scheduler, renderer::InstanceTreePtr* out)
{
    if (!view.Valid())
    {
        return Status::Make(ErrorCode::Invalid, "the instance tree view is empty");
    }
    const byte* base = view.bytes_->Bytes().Data();
    Vector<renderer::TreeMeshInfo> meshes;
    for (u32 i = 0; i < view.mesh_count_; ++i)
    {
        const byte* at = base + view.meshes_ + static_cast<usize>(i) * kInstanceTreeBlobMeshBytes;
        renderer::TreeMeshInfo m;
        m.bounds = Aabb::FromMinMax(Vec3(ItLoadF(at), ItLoadF(at + 4), ItLoadF(at + 8)),
                                    Vec3(ItLoadF(at + 12), ItLoadF(at + 16), ItLoadF(at + 20)));
        meshes.Push(m);
    }
    Vector<InstanceTreeSourceNode> nodes;
    nodes.Resize(view.node_count_);
    for (u32 i = 0; i < view.node_count_; ++i)
    {
        ItReadNode(base + view.nodes_ + static_cast<usize>(i) * kInstanceTreeBlobNodeBytes, &nodes[i]);
    }
    renderer::InstanceTreeBuilder builder(Span<const renderer::TreeMeshInfo>(meshes.Data(), meshes.Size()));
    if (ItFeed(&builder, Span<const InstanceTreeSourceNode>(nodes.Data(), nodes.Size())) != renderer::TreeStatus::Ok)
    {
        return Status::Make(ErrorCode::Corrupt, "an instance tree blob holds a node the builder refuses");
    }
    renderer::InstanceTreePtr tree = builder.Finish(scheduler);
    if (tree.Get() == nullptr)
    {
        return Status::Make(ErrorCode::Corrupt, "an instance tree could not be made from the blob");
    }
    *out = tree;
    return Status::Success();
}

}  // namespace vse::asset
