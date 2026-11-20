// Phase 22 Day 322: GPU skinning without a GPU. The skin stream of a mesh, the joint palette budget of the frame
// resources, the skinned G-buffer templates and the pass's counts and named errors are checked on the null device; the
// pixels of a skinned mesh against a CPU reference are in tests/rhi/skinning_gpu_test.cpp.
#include "lang/scalar.h"

#include <catch2/catch_test_macros.hpp>

#include "containers/array.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "cstr/mem_ops.h"
#include "renderer/camera.h"
#include "renderer/frame_data.h"
#include "renderer/gbuffer.h"
#include "renderer/material.h"
#include "renderer/mesh.h"
#include "renderer/render_scene.h"
#include "renderer/renderer_log.h"
#include "rhi/null/null_device.h"

using namespace engine_placeholder;
using namespace engine_placeholder::renderer;

namespace
{

// The skin stream of a flat 4 x 1 ribbon along X: columns at x = -2 .. 2; joint 0 owns the left, joint 1 the right and
// the middle column is shared 128 / 127.
void BuildSkin(const MeshData& mesh, Vector<u8>* skin)
{
    REQUIRE(skin->TryResize(static_cast<usize>(mesh.VertexCount()) * kSkinVertexBytes));
    for (u32 i = 0; i < mesh.VertexCount(); ++i)
    {
        const f32 x = mesh.PositionAt(i)[0];
        u8* e = skin->Data() + static_cast<usize>(i) * kSkinVertexBytes;
        e[0] = 0;
        e[1] = 1;
        e[2] = 0;
        e[3] = 0;
        if (x <= -1.0f)
        {
            e[4] = 255;
            e[5] = 0;
        }
        else if (x >= 1.0f)
        {
            e[4] = 0;
            e[5] = 255;
        }
        else
        {
            e[4] = 128;
            e[5] = 127;
        }
        e[6] = 0;
        e[7] = 0;
    }
}

bool MakeSkinnedRibbon(MeshData* out)
{
    if (MakePlaneMesh(4.0f, 1.0f, 4, 1, out) != nullptr)
    {
        return false;
    }
    Vector<u8> skin;
    BuildSkin(*out, &skin);
    return out->SetSkin(skin.Data(), 2) == nullptr;
}

struct SkinRig
{
    rhi::NullDevicePtr device = rhi::CreateNullDevice();
    UniquePtr<MaterialSystem> sys_owner;
    UniquePtr<FrameResources> frame_owner;
    MaterialSystem* sys = nullptr;
    FrameResources* frame = nullptr;
    GBufferTemplates templates;
    SkinRig(u32 objects, u32 joints)
    {
        sys_owner = MakeUnique<MaterialSystem>(device.Get(), MaterialSystemDesc{});
        frame_owner = MakeUnique<FrameResources>(device.Get(), objects, 8, joints);
        sys = sys_owner.Get();
        frame = frame_owner.Get();
        templates = CreateGBufferTemplates(sys);
    }
    ~SkinRig()
    {
        frame_owner.Reset();
        sys_owner.Reset();
        device.Reset();
    }
    void MakeReady(GpuMesh* mesh)
    {
        device->BeginFrame();
        rhi::CommandList* list = device->CreateCommandList();
        list->Begin();
        mesh->RecordPrepare(list);
        list->End();
        rhi::CommandList* lists[] = {list};
        device->Submit(lists, 1);
        device->EndFrame();
        mesh->MarkReady();
    }
    void BeginFrame(u64 f)
    {
        device->BeginFrame();
        sys->BeginFrame(f);
        sys->FlushMaterials();
        frame->BeginFrame(f);
    }
};

Frustum SkinFrustum()
{
    Camera cam = MakeLookAtCamera(Vec3(0, 10, 0), Vec3(0, 0, 0), Vec3(0, 0, -1));
    cam.aspect = 1.0f;
    return Frustum::FromViewProjection(cam.ViewProjection());
}

// Two identity joint matrices (3x4 row-major).
void IdentityPalette(f32* out, u32 joints)
{
    for (u32 j = 0; j < joints; ++j)
    {
        const f32 m[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
        MemCopy(out + j * 12, m, sizeof(m));
    }
}

}  // namespace

TEST_CASE("A skin stream is checked, joins the mesh layout and uploads as binding 1", "[renderer][skinning]")
{
    SkinRig rig(8, 16);
    MeshData mesh;
    REQUIRE(MakePlaneMesh(4.0f, 1.0f, 4, 1, &mesh) == nullptr);
    CHECK_FALSE(mesh.Skinned());
    Vector<u8> skin;
    BuildSkin(mesh, &skin);

    // Rejected: weights that do not sum to 255, a weighted joint the skeleton lacks, a second skin stream, no bytes.
    Vector<u8> bad;
    REQUIRE(bad.TryResize(skin.Size()));
    MemCopy(bad.Data(), skin.Data(), skin.Size());
    bad[4] = 100;
    CHECK(mesh.SetSkin(bad.Data(), 2) != nullptr);
    CHECK(mesh.SetSkin(skin.Data(), 1) != nullptr);  // joint 1 carries weight but the skeleton has one joint
    CHECK(mesh.SetSkin(nullptr, 2) != nullptr);
    CHECK(mesh.SetSkin(skin.Data(), 257) != nullptr);
    CHECK_FALSE(mesh.Skinned());  // a refused stream leaves the mesh unchanged
    REQUIRE(mesh.SetSkin(skin.Data(), 2) == nullptr);
    CHECK(mesh.SetSkin(skin.Data(), 2) != nullptr);
    CHECK(mesh.Skinned());
    CHECK(mesh.SkinByteCount() == static_cast<u64>(mesh.VertexCount()) * 8);

    const VertexLayoutData layout = mesh.Layout();
    CHECK(layout.skin_stride == 8);
    CHECK(layout.stride == sizeof(StandardVertex));
    REQUIRE(layout.attribute_count == 6);
    CHECK(layout.attributes[4].location == kSkinJointsLocation);
    CHECK(layout.attributes[4].binding == 1);
    CHECK(layout.attributes[4].format == rhi::VertexFormat::UInt8x4);
    CHECK(layout.attributes[5].location == kSkinWeightsLocation);
    CHECK(layout.attributes[5].offset == 4);
    CHECK(layout.attributes[5].format == rhi::VertexFormat::UNorm8x4);

    GpuMesh gpu;
    REQUIRE(gpu.Create(rig.device.Get(), mesh, "ribbon"));
    CHECK(gpu.Skinned());
    CHECK(gpu.SkinBuffer().IsValid());
    GpuMesh moved(static_cast<GpuMesh&&>(gpu));
    CHECK(moved.SkinBuffer().IsValid());
    CHECK_FALSE(gpu.SkinBuffer().IsValid());

    // A rigid mesh has none.
    MeshData rigid;
    REQUIRE(MakePlaneMesh(1.0f, 1.0f, 1, 1, &rigid) == nullptr);
    GpuMesh rigid_gpu;
    REQUIRE(rigid_gpu.Create(rig.device.Get(), rigid, "rigid"));
    CHECK_FALSE(rigid_gpu.Skinned());
    CHECK_FALSE(rigid_gpu.SkinBuffer().IsValid());
    CHECK(MaterialSystem::ObjectIndexSlotFor(false) == 1);
    CHECK(MaterialSystem::ObjectIndexSlotFor(true) == 2);
}

TEST_CASE("The joint palette hands out offsets until it is full and starts again each frame", "[renderer][skinning]")
{
    SkinRig rig(4, 10);
    f32 matrices[12 * 6];
    IdentityPalette(matrices, 6);
    rig.BeginFrame(0);
    CHECK(rig.frame->MaxJointMatrices() == 10);
    CHECK(rig.frame->WriteJoints(matrices, 6) == 0);
    CHECK(rig.frame->WriteJoints(matrices, 6) ==
          kNoJoints);  // 4 left: nothing is written for a palette that does not fit
    CHECK(rig.frame->JointsWritten() == 6);
    CHECK(rig.frame->WriteJoints(matrices, 4) == 6);
    CHECK(rig.frame->WriteJoints(matrices, 1) == kNoJoints);
    CHECK(rig.frame->JointsWritten() == 10);
    CHECK(rig.frame->JointRegion().size == 10u * kJointMatrixBytes);
    rig.device->EndFrame();
    rig.BeginFrame(1);
    CHECK(rig.frame->JointsWritten() == 0);
    CHECK(rig.frame->WriteJoints(matrices, 6) == 0);
    rig.device->EndFrame();
}

TEST_CASE("Skinned objects share one pipeline and one instanced draw", "[renderer][skinning]")
{
    SkinRig rig(1024, 2048);
    REQUIRE(rig.templates.skinned.IsValid());
    MeshData data;
    REQUIRE(MakeSkinnedRibbon(&data));
    GpuMesh ribbon;
    REQUIRE(ribbon.Create(rig.device.Get(), data, "ribbon"));
    rig.MakeReady(&ribbon);
    GBufferOptions skinned;
    skinned.skinned = true;
    const MaterialId material = CreateGBufferMaterial(rig.sys, rig.templates, skinned, "skinned");
    REQUIRE(material.IsValid());

    static f32 palettes[1000][2 * 12];
    RenderScene scene(RenderSceneDesc{1024, 8});
    for (u32 i = 0; i < 1000; ++i)
    {
        IdentityPalette(palettes[i], 2);
        RenderObjectDesc od;
        od.mesh = &ribbon;
        od.material = material.index;
        od.transform = ModelMatrix(Vec3(static_cast<f32>(i % 10) * 0.01f, 0, 0), 0.1f, 0.0f);
        od.joint_matrices = palettes[i];
        od.joint_count = 2;
        REQUIRE(scene.AddObject(od).IsValid());
    }
    GBufferPass pass(rig.device.Get(), rig.sys, rig.frame);
    rig.BeginFrame(0);
    CHECK(pass.Prepare(scene, SkinFrustum()));
    CHECK(pass.Stats().visible == 1000);
    CHECK(pass.Stats().drawn_objects == 1000);
    CHECK(pass.Stats().draws == 1);  // one mesh, one material: one instanced draw
    CHECK(pass.Stats().pipeline_binds == 1);
    CHECK(pass.Stats().skinned_dropped == 0);
    CHECK(rig.frame->JointsWritten() == 2000);
    CHECK(rig.sys->Stats().pipelines == 1);
    rig.device->EndFrame();
}

TEST_CASE("An object whose palette does not fit is left out whole and counted", "[renderer][skinning]")
{
    SkinRig rig(16, 7);  // room for three 2-joint palettes and one matrix over
    MeshData data;
    REQUIRE(MakeSkinnedRibbon(&data));
    GpuMesh ribbon;
    REQUIRE(ribbon.Create(rig.device.Get(), data, "ribbon"));
    rig.MakeReady(&ribbon);
    GBufferOptions skinned;
    skinned.skinned = true;
    const MaterialId material = CreateGBufferMaterial(rig.sys, rig.templates, skinned, "skinned");
    REQUIRE(material.IsValid());
    f32 palettes[5][24];  // one palette per entity: objects that share one palette's memory share its room (ranges)
    for (u32 i = 0; i < 5; ++i)
    {
        IdentityPalette(palettes[i], 2);
    }
    RenderScene scene(RenderSceneDesc{16, 8});
    for (u32 i = 0; i < 5; ++i)
    {
        RenderObjectDesc od;
        od.mesh = &ribbon;
        od.material = material.index;
        od.transform = ModelMatrix(Vec3(static_cast<f32>(i) * 0.1f, 0, 0), 0.5f, 0.0f);
        od.joint_matrices = palettes[i];
        od.joint_count = 2;
        REQUIRE(scene.AddObject(od).IsValid());
    }
    GBufferPass pass(rig.device.Get(), rig.sys, rig.frame);
    rig.BeginFrame(0);
    const bool was = LogRender.Enabled();
    LogRender.SetEnabled(false);
    const bool ok = pass.Prepare(scene, SkinFrustum());
    LogRender.SetEnabled(was);
    CHECK(ok);  // a budget drop is counted, not an error of the pass
    CHECK(pass.Stats().visible == 5);
    CHECK(pass.Stats().drawn_objects == 3);
    CHECK(pass.Stats().skinned_dropped == 2);
    CHECK(pass.Stats().rejected == 0);
    CHECK(rig.frame->JointsWritten() == 6);
    rig.device->EndFrame();
}

TEST_CASE("A skinned mesh with a rigid material, a rigid mesh with a skinned one and a missing palette are refused",
          "[renderer][skinning]")
{
    SkinRig rig(16, 64);
    MeshData skinned_data, rigid_data;
    REQUIRE(MakeSkinnedRibbon(&skinned_data));
    REQUIRE(MakePlaneMesh(4.0f, 1.0f, 4, 1, &rigid_data) == nullptr);
    GpuMesh skinned_mesh, rigid_mesh;
    REQUIRE(skinned_mesh.Create(rig.device.Get(), skinned_data, "skinned"));
    REQUIRE(rigid_mesh.Create(rig.device.Get(), rigid_data, "rigid"));
    rig.MakeReady(&skinned_mesh);
    rig.MakeReady(&rigid_mesh);
    GBufferOptions skin_options;
    skin_options.skinned = true;
    const MaterialId skinned_material = CreateGBufferMaterial(rig.sys, rig.templates, skin_options, "skinned");
    const MaterialId rigid_material = CreateGBufferMaterial(rig.sys, rig.templates, GBufferOptions{}, "rigid");
    REQUIRE(skinned_material.IsValid());
    REQUIRE(rigid_material.IsValid());
    const PipelineTarget target = GBufferPipelineTarget();
    CHECK(rig.sys->GetPipeline(skinned_material, target, skinned_mesh.Layout()).pipeline.IsValid());
    CHECK(rig.sys->GetPipeline(rigid_material, target, rigid_mesh.Layout()).pipeline.IsValid());

    f32 palette[24];
    IdentityPalette(palette, 2);
    RenderScene scene(RenderSceneDesc{16, 8});
    RenderObjectDesc od;
    od.mesh = &skinned_mesh;
    od.material = rigid_material.index;  // wrong: a rigid material on a skinned mesh
    od.joint_matrices = palette;
    od.joint_count = 2;
    scene.AddObject(od);
    od.mesh = &rigid_mesh;
    od.material = skinned_material.index;  // wrong: a skinned material on a rigid mesh
    od.joint_matrices = nullptr;
    od.joint_count = 0;
    scene.AddObject(od);
    od.mesh = &skinned_mesh;
    od.material = skinned_material.index;  // right material, but no palette was given
    scene.AddObject(od);
    od.joint_matrices = palette;
    od.joint_count = 1;
    scene.AddObject(od);  // a palette shorter than the mesh's 2 joints would read its neighbor's matrices
    od.joint_count = 2;
    scene.AddObject(od);  // the one valid object

    GBufferPass pass(rig.device.Get(), rig.sys, rig.frame);
    rig.BeginFrame(0);
    const bool was = LogRender.Enabled();
    LogRender.SetEnabled(false);
    const bool ok = pass.Prepare(scene, SkinFrustum());
    LogRender.SetEnabled(was);
    CHECK_FALSE(ok);
    CHECK(pass.Stats().rejected == 4);
    CHECK(pass.Stats().drawn_objects == 1);
    CHECK(pass.Stats().skinned_dropped == 0);
    rig.device->EndFrame();
}

TEST_CASE("The culling bounds of a skinned object grow with its bounds scale", "[renderer][skinning]")
{
    SkinRig rig(4, 8);
    MeshData data;
    REQUIRE(MakeSkinnedRibbon(&data));
    GpuMesh ribbon;
    REQUIRE(ribbon.Create(rig.device.Get(), data, "ribbon"));
    f32 palette[24];
    IdentityPalette(palette, 2);
    RenderScene scene(RenderSceneDesc{4, 4});
    RenderObjectDesc od;
    od.mesh = &ribbon;
    od.joint_matrices = palette;
    od.joint_count = 2;
    const ObjectId plain = scene.AddObject(od);
    od.skin_bounds_scale = 2.0f;
    const ObjectId inflated = scene.AddObject(od);
    const Aabb a = scene.FindObject(plain)->bounds.box;
    const Aabb b = scene.FindObject(inflated)->bounds.box;
    CHECK(Abs((b.max.x - b.min.x) - 2.0f * (a.max.x - a.min.x)) < 1e-4f);
    CHECK(Abs(b.Center().x - a.Center().x) < 1e-4f);
    scene.SetTransform(inflated, ModelMatrix(Vec3(5, 0, 0), 1.0f, 0.0f));  // the scale survives a move
    CHECK(Abs((scene.FindObject(inflated)->bounds.box.max.x - scene.FindObject(inflated)->bounds.box.min.x) -
              2.0f * (a.max.x - a.min.x)) < 1e-4f);
    scene.SetJointMatrices(plain, nullptr, 0);
    CHECK(scene.FindObject(plain)->joint_matrices == nullptr);
}

// 2026-11-20 (a character whose rest pose is not its bind pose, drawn standing while its bind-pose box lay down): a
// skinned object with joint bounds is bounded, and culled, by its pose: each joint's box moved by its palette matrix.
TEST_CASE("A skinned object with joint bounds is culled by its pose, not by its bind-pose box", "[renderer][skinning]")
{
    SkinRig rig(4, 8);
    MeshData data;
    REQUIRE(MakeSkinnedRibbon(&data));
    MeshGeometry geometry = data.CopyGeometry();
    const ByteSpan region = geometry.bytes->Bytes();
    Vector<byte> bytes;
    const usize records = (region.Size() + 3) & ~static_cast<usize>(3);
    bytes.Resize(records + 2 * kJointBoundsBytes);
    MemSet(bytes.Data(), 0, bytes.Size());
    MemCopy(bytes.Data(), region.Data(), region.Size());
    const Aabb bind = geometry.bounds.box;
    for (u32 j = 0; j < 2; ++j)  // each joint's vertices: the whole ribbon (a box covers them)
    {
        byte* r = bytes.Data() + records + j * kJointBoundsBytes;
        const f32 lo[3] = {bind.min.x, bind.min.y, bind.min.z};
        const f32 hi[3] = {bind.max.x, bind.max.y, bind.max.z};
        const u32 used = 1;
        MemCopy(r, lo, 12);
        MemCopy(r + 12, hi, 12);
        MemCopy(r + 24, &used, 4);
    }
    geometry.bytes = MakeShared<OwnedBytes>(Move(bytes));
    geometry.joint_bounds_offset = records;
    geometry.has_joint_bounds = true;
    REQUIRE(geometry.Validate() == nullptr);
    GpuMesh ribbon;
    REQUIRE(ribbon.CreateFromGeometry(rig.device.Get(), geometry, nullptr, "posed ribbon"));

    // Both joints carry the ribbon 100 units along x.
    f32 palette[24];
    IdentityPalette(palette, 2);
    palette[3] = 100.0f;
    palette[15] = 100.0f;
    RenderScene scene(RenderSceneDesc{4, 4});
    RenderObjectDesc od;
    od.mesh = &ribbon;
    od.joint_matrices = palette;
    od.joint_count = 2;
    const ObjectId id = scene.AddObject(od);
    CHECK(Abs(scene.FindObject(id)->bounds.box.min.x - (bind.min.x + 100.0f)) < 1e-4f);
    CHECK(Abs(scene.FindObject(id)->bounds.box.max.x - (bind.max.x + 100.0f)) < 1e-4f);

    // A camera over the posed ribbon sees it; one over the bind pose does not.
    Camera over = MakeLookAtCamera(Vec3(100, 10, 0), Vec3(100, 0, 0), Vec3(0, 0, -1));
    over.aspect = 1.0f;
    u32 slots[4];
    CHECK(scene.Cull(Frustum::FromViewProjection(over.ViewProjection()), slots, 4) == 1);
    CHECK(scene.Cull(SkinFrustum(), slots, 4) == 0);

    // The animation system rewrites the matrices in place: the scene follows when asked.
    palette[3] = 0.0f;
    palette[15] = 0.0f;
    scene.UpdatePoseBounds(id);
    CHECK(scene.Cull(SkinFrustum(), slots, 4) == 1);
    CHECK(Abs(scene.FindObject(id)->bounds.box.min.x - bind.min.x) < 1e-4f);

    // A move reuses the bounds the pose gave and does not read the palette, which may be gone by then
    // (scene::RenderSync moves objects before it hands them their new palettes): with the palette freed, asan would see
    // a read.
    UniquePtr<Array<f32, 24>> owned = MakeUnique<Array<f32, 24>>();
    IdentityPalette(owned->Data(), 2);
    (*owned)[3] = 100.0f;
    (*owned)[15] = 100.0f;
    scene.SetJointMatrices(id, owned->Data(), 2);
    owned.Reset();
    scene.SetTransform(id, ModelMatrix(Vec3(0, 0, 5), 1.0f, 0.0f));
    CHECK(Abs(scene.FindObject(id)->bounds.box.min.x - (bind.min.x + 100.0f)) < 1e-4f);
    CHECK(Abs(scene.FindObject(id)->bounds.box.min.z - (bind.min.z + 5.0f)) < 1e-4f);
    scene.SetJointMatrices(id, nullptr, 0);

    // With morph targets the posed box grows by what the targets add to the mesh's bounds (here 3 units past the
    // joints' boxes on +x), so a face a morph pushes out is not culled.
    MeshGeometry morphed = geometry;
    Vector<byte> with_delta;
    const ByteSpan old_bytes = morphed.bytes->Bytes();
    with_delta.Resize(old_bytes.Size() + kMorphDeltaBytes);
    MemSet(with_delta.Data(), 0, with_delta.Size());
    MemCopy(with_delta.Data(), old_bytes.Data(), old_bytes.Size());
    morphed.bytes = MakeShared<OwnedBytes>(Move(with_delta));
    morphed.morph_delta_offset = old_bytes.Size();
    morphed.morph_delta_count = 1;
    morphed.morph_target_count = 1;
    morphed.morph_targets[0].first_delta = 0;
    morphed.morph_targets[0].delta_count = 1;
    morphed.bounds.box.max.x += 3.0f;
    if (morphed.Validate() == nullptr)  // morph targets need the full vertex layout
    {
        GpuMesh face;
        REQUIRE(face.CreateFromGeometry(rig.device.Get(), morphed, nullptr, "posed morphed ribbon"));
        f32 moved[24];
        IdentityPalette(moved, 2);
        moved[3] = 100.0f;
        moved[15] = 100.0f;
        Bounds posed;
        REQUIRE(face.PosedBounds(moved, 2, &posed));
        CHECK(Abs(posed.box.max.x - (bind.max.x + 103.0f)) < 1e-4f);
        CHECK(Abs(posed.box.min.x - (bind.min.x + 97.0f)) < 1e-4f);  // the pad is the same on every side
    }
    else
    {
        WARN("the ribbon's layout cannot hold morph targets: " << morphed.Validate());
        CHECK(false);
    }
}

TEST_CASE("The ranges of one skinned entity share one palette in the frame", "[renderer][skinning][ranges]")
{
    SkinRig rig(1024, 2048);
    MeshData data;
    REQUIRE(MakeSkinnedRibbon(&data));
    const u32 total = data.IndexCount();
    REQUIRE(total >= 6);
    const MeshRange ranges[] = {{0, 3}, {3, total - 3}};
    REQUIRE(data.SetRanges(ranges, 2) == nullptr);
    GpuMesh ribbon;
    REQUIRE(ribbon.Create(rig.device.Get(), data, "ribbon"));
    rig.MakeReady(&ribbon);
    GBufferOptions skinned;
    skinned.skinned = true;
    const MaterialId m0 = CreateGBufferMaterial(rig.sys, rig.templates, skinned, "skinned0");
    const MaterialId m1 = CreateGBufferMaterial(rig.sys, rig.templates, skinned, "skinned1");
    f32 palette[2 * 12];
    IdentityPalette(palette, 2);
    RenderScene scene(RenderSceneDesc{1024, 8});
    for (u32 r = 0; r < 2; ++r)
    {
        RenderObjectDesc od;
        od.mesh = &ribbon;
        od.material = (r == 0 ? m0 : m1).index;
        od.first_index = ranges[r].first_index;
        od.index_count = ranges[r].index_count;
        od.transform = ModelMatrix(Vec3(0, 0, 0), 0.1f, 0.0f);
        od.joint_matrices = palette;
        od.joint_count = 2;
        REQUIRE(scene.AddObject(od).IsValid());
    }
    GBufferPass pass(rig.device.Get(), rig.sys, rig.frame);
    rig.BeginFrame(0);
    CHECK(pass.Prepare(scene, SkinFrustum()));
    CHECK(pass.Stats().drawn_objects == 2);
    CHECK(pass.Stats().draws == 2);
    CHECK(rig.frame->JointsWritten() == 2);  // one palette of two matrices, not two
    rig.device->EndFrame();
}
