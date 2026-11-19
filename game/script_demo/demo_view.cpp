// The script demo's picture (Phase 27 Day 475). See demo_view.h.
#include "demo_view.h"

#include "containers/array.h"
#include "cstr/format.h"
#include "lang/scalar.h"
#include "rhi/rhi_log.h"

namespace script_demo
{

namespace
{

constexpr u32 kKinds = 6;  // VisualKind's values; the floor's material follows them

// A body's size in the world: the radius of its ball (the marker is a box of this half size).
f32 SizeOf(VisualKind kind)
{
    switch (kind)
    {
    case VisualKind::Player: return 0.5f;
    case VisualKind::Pickup: return 0.45f;  // a beacon in the middle of its trigger, not the 1.5 m trigger itself
    case VisualKind::Marker: return 0.35f;
    default: return 0.4f;
    }
}

// A translation with a uniform scale (the unit meshes: the sphere's radius is 0.5, the box's edge 1).
Mat4 Place(const Vec3& at, const Vec3& scale)
{
    return Mat4::FromBasis(Vec3(scale.x, 0.0f, 0.0f), Vec3(0.0f, scale.y, 0.0f), Vec3(0.0f, 0.0f, scale.z), at);
}

Mat4 PlaceOf(VisualKind kind, const Vec3& at)
{
    const f32 size = 2.0f * SizeOf(kind);
    const Vec3 lift = kind == VisualKind::Pickup ? Vec3(0.0f, 0.6f, 0.0f) : Vec3(0.0f, 0.0f, 0.0f);
    return Place(at + lift, Vec3(size, size, size));
}

}  // namespace

DemoView::~DemoView()
{
    Destroy();
}

void DemoView::Destroy()
{
    if (device_ != nullptr)
    {
        device_->WaitIdle();
    }
    scene_.Reset();
    objects_.Clear();
    kinds_.Clear();
    materials_.Clear();
    sphere_.Reset();
    box_.Reset();
    device_ = nullptr;
}

bool DemoView::Create(rhi::Device* device, renderer::MaterialSystem* materials,
                      const renderer::GBufferTemplates& templates, renderer::DeviceResources* resources,
                      const DemoWorld& world)
{
    Destroy();
    device_ = device;
    sphere_ = MakeShared<renderer::GpuMesh>();
    box_ = MakeShared<renderer::GpuMesh>();
    renderer::MeshData sphere;
    renderer::MeshData box;
    const char* error = renderer::MakeSphereMesh(0.5f, 16, 8, &sphere);
    if (error == nullptr)
    {
        error = renderer::MakeCubeMesh(1.0f, &box);
    }
    if (error != nullptr || !sphere_->Create(device, sphere, "ball") || !box_->Create(device, box, "box") ||
        (resources != nullptr &&
         (!sphere_->MakeRecreatable(resources, sphere, "ball") || !box_->MakeRecreatable(resources, box, "box"))))
    {
        rhi::RhiLogV(LogVerbosity::Error, "script_demo: making the meshes failed%s%s", error != nullptr ? ": " : "",
                     error != nullptr ? error : "");
        Destroy();
        return false;
    }
    {  // barriers that move the buffers out of their upload state, in one submission
        device->BeginFrame();
        rhi::CommandList* list = device->CreateCommandList();
        if (list == nullptr)
        {
            device->EndFrame();
            Destroy();
            return false;
        }
        list->Begin();
        sphere_->RecordPrepare(list);
        box_->RecordPrepare(list);
        list->End();
        const Array<rhi::CommandList*, 1> lists = {list};
        const bool submitted = device->Submit(lists.Data(), 1) != 0;
        device->EndFrame();
        device->WaitIdle();
        if (!submitted)
        {
            rhi::RhiLogV(LogVerbosity::Error, "script_demo: submitting the mesh uploads failed");
            Destroy();
            return false;
        }
        sphere_->MarkReady();
        box_->MarkReady();
    }

    // Colors by kind: wander blue, flock green, flee orange, the player white, the pickups gold (glowing), the marker
    // red (glowing); then the floor.
    static const Array<Vec4, kKinds + 1> kColor = {Vec4(0.25f, 0.45f, 0.95f, 1), Vec4(0.25f, 0.80f, 0.35f, 1),
                                                   Vec4(0.98f, 0.55f, 0.18f, 1), Vec4(0.95f, 0.95f, 0.95f, 1),
                                                   Vec4(1.00f, 0.80f, 0.20f, 1), Vec4(0.95f, 0.15f, 0.15f, 1),
                                                   Vec4(0.16f, 0.17f, 0.20f, 1)};
    static const Array<f32, kKinds + 1> kGlow = {0.0f, 0.0f, 0.0f, 0.8f, 2.0f, 2.5f, 0.0f};
    for (u32 i = 0; i <= kKinds; ++i)
    {
        Array<char, 32> name;
        FormatTo(name.Data(), name.Size(), "script_demo %u", i);
        const renderer::MaterialId id = renderer::CreateGBufferMaterial(materials, templates,
                                                                        renderer::GBufferOptions(), name.Data());
        if (!id.IsValid())
        {
            rhi::RhiLogV(LogVerbosity::Error, "script_demo: creating a material failed");
            Destroy();
            return false;
        }
        materials->SetVec4(id, "base_color", kColor[i]);
        materials->SetFloat(id, "roughness", i < kKinds ? 0.45f : 0.9f);
        materials->SetFloat(id, "metallic", 0.0f);
        materials->SetVec4(id, "emissive_color",
                           Vec4(kColor[i].x * kGlow[i], kColor[i].y * kGlow[i], kColor[i].z * kGlow[i], 1));
        materials_.Push(id.index);
    }

    renderer::RenderSceneDesc desc;
    desc.max_objects = static_cast<u32>(world.VisualCount()) + 4;
    desc.max_lights = 4;
    scene_ = MakeUnique<renderer::RenderScene>(desc);
    {  // the floor: the physics floor's box, its top at y = 0
        renderer::RenderObjectDesc floor;
        floor.mesh = box_.Get();
        floor.material = materials_[kKinds];
        floor.transform = Place(Vec3(0.0f, -1.0f, 0.0f), Vec3(120.0f, 2.0f, 120.0f));
        scene_->AddObject(floor);
    }
    world.ForEachVisual(
        [&](VisualKind kind, const Vec3& at)
        {
            renderer::RenderObjectDesc d;
            d.mesh = kind == VisualKind::Marker ? box_.Get() : sphere_.Get();
            d.material = materials_[static_cast<u32>(kind)];
            d.transform = PlaceOf(kind, at);
            objects_.Push(scene_->AddObject(d));
            kinds_.Push(kind);
        });
    renderer::Light sun;
    sun.type = renderer::LightType::Directional;
    sun.direction = Vec3(0.4f, 0.85f, 0.35f);
    sun.color = Vec3(1.0f, 0.96f, 0.88f);
    sun.intensity = 1.1f;
    scene_->AddLight(sun);
    renderer::Light ambient;
    ambient.type = renderer::LightType::Ambient;
    ambient.color = Vec3(0.45f, 0.5f, 0.6f);
    ambient.intensity = 0.3f;
    scene_->AddLight(ambient);
    return true;
}

void DemoView::Sync(const DemoWorld& world)
{
    usize i = 0;
    world.ForEachVisual(
        [&](VisualKind kind, const Vec3& at)
        {
            if (i < objects_.Size())
            {
                scene_->SetTransform(objects_[i], PlaceOf(kind, at));
            }
            ++i;
        });
}

renderer::Camera DemoView::CameraAt(f32 angle, f32 aspect) const
{
    const f32 radius = 48.0f;
    renderer::Camera camera = renderer::MakeLookAtCamera(Vec3(radius * Sin(angle), 34.0f, radius * Cos(angle)),
                                                         Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));
    camera.fov_y = 0.9f;
    camera.aspect = aspect;
    camera.near_plane = 0.5f;
    camera.far_plane = 250.0f;
    return camera;
}

}  // namespace script_demo
