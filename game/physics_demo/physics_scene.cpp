#include "physics_scene.h"

#include "cstr/format.h"
#include "ecs/query.h"
#include "ecs/schedule.h"
#include "ecs/transform.h"
#include "ecs/world.h"
#include "lang/scalar.h"
#include "physics/components.h"
#include "physics/system.h"
#include "physics/sync.h"
#include "physics/world.h"
#include "renderer/gbuffer.h"
#include "rhi/rhi_log.h"
#include "scene/components.h"
#include "scene/render_sync.h"

using namespace engine_placeholder::renderer;

namespace physics_demo
{

namespace
{

constexpr f32 kFloorHalf = 8.0f;  // the floor is 16 by 16 metres, 0.5 thick, its top face at y = 0
constexpr u32 kColors = 6;        // the shapes' materials; the floor's is the one after them
constexpr f32 kPi = 3.14159265f;

// A small deterministic generator (an LCG): the shapes are the same on every machine.
u32 NextRandom(u32* state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state >> 8;
}

f32 RandomRange(u32* state, f32 low, f32 high)
{
    return low + (high - low) * (static_cast<f32>(NextRandom(state)) / static_cast<f32>(1u << 24));
}

// A capsule of radius 0.5 whose straight part is 1 long, axis along Y (a collider with radius 0.5 and half height
// 0.5, scaled uniformly by the entity's scale). The renderer has no capsule builder: two half spheres and a tube.
const char* MakeCapsule(MeshData* out)
{
    constexpr u32 kSegments = 24;
    constexpr u32 kRings = 8;  // per hemisphere
    constexpr u32 kColumns = kSegments + 1;
    constexpr u32 kRows = 2 * (kRings + 1);
    Vector<StandardVertex> vertices;
    Vector<u16> indices;
    if (!vertices.TryResize(kRows * kColumns) || !indices.TryResize((kRows - 1) * kSegments * 6))
    {
        return "capsule: out of memory";
    }
    u32 row = 0;
    for (u32 half = 0; half < 2; ++half)
    {
        for (u32 r = 0; r <= kRings; ++r, ++row)
        {
            // Latitude from the pole (0) to the equator (pi / 2) on the top half, the equator to the pole below.
            const f32 theta = (kPi * 0.5f) * (static_cast<f32>(r) / static_cast<f32>(kRings)) +
                              (half == 0 ? 0.0f : kPi * 0.5f);
            const f32 shift = half == 0 ? 0.5f : -0.5f;
            for (u32 s = 0; s < kColumns; ++s)
            {
                const f32 phi = 2.0f * kPi * static_cast<f32>(s) / static_cast<f32>(kSegments);
                const f32 nx = Sin(theta) * Cos(phi);
                const f32 ny = Cos(theta);
                const f32 nz = Sin(theta) * Sin(phi);
                StandardVertex& v = vertices[row * kColumns + s];
                v.position[0] = 0.5f * nx;
                v.position[1] = 0.5f * ny + shift;
                v.position[2] = 0.5f * nz;
                v.normal[0] = nx;
                v.normal[1] = ny;
                v.normal[2] = nz;
                v.tangent[0] = -Sin(phi);
                v.tangent[1] = 0.0f;
                v.tangent[2] = Cos(phi);
                v.tangent[3] = 1.0f;
                v.uv[0] = static_cast<f32>(s) / static_cast<f32>(kSegments);
                v.uv[1] = static_cast<f32>(row) / static_cast<f32>(kRows - 1);
            }
        }
    }
    u32 at = 0;
    for (u32 r = 0; r + 1 < kRows; ++r)
    {
        for (u32 s = 0; s < kSegments; ++s)
        {
            const u32 a = r * kColumns + s;
            const u32 b = a + kColumns;
            // Counter-clockwise from outside (the order of the renderer's sphere: a step along phi then theta).
            indices[at++] = static_cast<u16>(a + 1);
            indices[at++] = static_cast<u16>(b + 1);
            indices[at++] = static_cast<u16>(b);
            indices[at++] = static_cast<u16>(a);
            indices[at++] = static_cast<u16>(a + 1);
            indices[at++] = static_cast<u16>(b);
        }
    }
    return out->Set(StandardVertexLayout(), 0, vertices.Data(), kRows * kColumns, rhi::IndexType::UInt16,
                    indices.Data(), at);
}

}  // namespace

PhysicsScene::PhysicsScene() = default;

PhysicsScene::~PhysicsScene()
{
    Destroy();
}

void PhysicsScene::Destroy()
{
    if (device_ != nullptr)
    {
        device_->WaitIdle();
    }
    schedule_.Reset();
    world_.Reset();  // the entities hold shares of the meshes
    scene_.Reset();
    bodies_.Clear();
    box_.Reset();
    sphere_.Reset();
    capsule_.Reset();
    device_ = nullptr;
}

bool PhysicsScene::Create(rhi::Device* device, MaterialSystem* materials, const GBufferTemplates& templates,
                          const SceneOptions& options)
{
    if (device_ != nullptr)
    {
        Destroy();
    }
    device_ = device;
    max_bodies_ = options.max_bodies < 1 ? 1 : options.max_bodies;
    spawn_interval_ = options.spawn_interval;
    rng_ = options.seed * 2654435761u + 12345u;

    // Meshes: a unit box, a sphere of radius 0.5 and the capsule, shared by every entity that draws them.
    box_ = MakeShared<GpuMesh>();
    sphere_ = MakeShared<GpuMesh>();
    capsule_ = MakeShared<GpuMesh>();
    MeshData box, sphere, capsule;
    const char* error = MakeCubeMesh(1.0f, &box);
    if (error == nullptr)
    {
        error = MakeSphereMesh(0.5f, 32, 16, &sphere);
    }
    if (error == nullptr)
    {
        error = MakeCapsule(&capsule);
    }
    if (error != nullptr)
    {
        rhi::RhiLogV(LogVerbosity::Error, "physics_demo: building a mesh failed: %s", error);
        Destroy();
        return false;
    }
    if (!box_->Create(device, box, "box") || !sphere_->Create(device, sphere, "sphere") ||
        !capsule_->Create(device, capsule, "capsule"))
    {
        rhi::RhiLogV(LogVerbosity::Error, "physics_demo: uploading a mesh failed");
        Destroy();
        return false;
    }
    if (options.resources != nullptr && (!box_->MakeRecreatable(options.resources, box, "box") ||
                                         !sphere_->MakeRecreatable(options.resources, sphere, "sphere") ||
                                         !capsule_->MakeRecreatable(options.resources, capsule, "capsule")))
    {
        rhi::RhiLogV(LogVerbosity::Error, "physics_demo: registering the meshes failed");
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
        box_->RecordPrepare(list);
        sphere_->RecordPrepare(list);
        capsule_->RecordPrepare(list);
        list->End();
        rhi::CommandList* lists[] = {list};
        const bool submitted = device->Submit(lists, 1) != 0;
        device->EndFrame();
        device->WaitIdle();
        if (!submitted)
        {
            rhi::RhiLogV(LogVerbosity::Error, "physics_demo: submitting the mesh uploads failed");
            Destroy();
            return false;
        }
        box_->MarkReady();
        sphere_->MarkReady();
        capsule_->MarkReady();
    }

    // Materials: six colors for the shapes, then the floor's.
    static const Vec4 kColor[kColors + 1] = {Vec4(0.90f, 0.22f, 0.20f, 1), Vec4(0.20f, 0.45f, 0.95f, 1),
                                             Vec4(0.98f, 0.78f, 0.15f, 1), Vec4(0.25f, 0.80f, 0.35f, 1),
                                             Vec4(0.72f, 0.32f, 0.85f, 1), Vec4(0.98f, 0.55f, 0.18f, 1),
                                             Vec4(0.55f, 0.58f, 0.62f, 1)};
    for (u32 i = 0; i <= kColors; ++i)
    {
        char name[32] = "floor";
        if (i < kColors)
        {
            FormatTo(name, sizeof(name), "shape %u", i);
        }
        const MaterialId id = CreateGBufferMaterial(materials, templates, GBufferOptions(), name);
        if (!id.IsValid())
        {
            rhi::RhiLogV(LogVerbosity::Error, "physics_demo: creating a material failed");
            Destroy();
            return false;
        }
        materials->SetVec4(id, "base_color", kColor[i]);
        materials->SetFloat(id, "roughness", i < kColors ? 0.45f : 0.85f);
        materials->SetFloat(id, "metallic", 0.0f);
        material_ids_[i] = id.index;
    }

    // The world: the render scene the sync mirrors into, and the schedule that runs the systems in order.
    RenderSceneDesc desc;
    desc.max_objects = max_bodies_ + 8;
    desc.max_lights = 8;
    scene_ = MakeUnique<RenderScene>(desc);
    world_ = MakeUnique<ecs::World>();
    schedule_ = MakeUnique<ecs::Schedule>();
    world_->SetResource(ecs::TransformReport{});
    world_->SetResource(physics::PhysicsWorld());
    world_->SetResource(scene::SceneLink{scene_.Get()});
    world_->SetResource(scene::SceneSyncReport{});
    world_->SetResource(scene::ActiveCamera{});
    world_->SetResource(scene::CameraOutput{});
    schedule_->Add(physics::MakePhysicsStepSystem().InStage(ecs::kUpdate));
    schedule_->Add(ecs::MakeTransformSystem("transforms").InStage(ecs::kPostUpdate));
    schedule_->Add(scene::MakeCameraSystem("camera").InStage(ecs::kRender));
    schedule_->Add(scene::MakeRenderSyncSystem("render sync").InStage(ecs::kRender));
    if (!schedule_->Build().ok)
    {
        rhi::RhiLogV(LogVerbosity::Error, "physics_demo: the schedule does not build");
        Destroy();
        return false;
    }

    // The floor: a wide, thin static box, its top face at y = 0.
    {
        ecs::LocalTransform at;
        at.position = Vec3(0.0f, -0.25f, 0.0f);
        at.scale = Vec3(2.0f * kFloorHalf, 0.5f, 2.0f * kFloorHalf);
        scene::MeshRenderer mr;
        mr.mesh = box_;
        mr.material = material_ids_[kColors];
        physics::RigidBody body;
        body.type = physics::BodyType::Static;
        physics::Collider collider;
        collider.shape = physics::Shape::MakeBox(Vec3(kFloorHalf, 0.25f, kFloorHalf));
        collider.material.friction = 0.7f;
        collider.material.restitution = 0.2f;
        world_->Create(at, ecs::WorldTransform{}, mr, body, collider);
    }
    // Four low walls around the floor, so shapes pile up on it instead of rolling off its edge.
    for (u32 w = 0; w < 4; ++w)
    {
        const f32 side = (w & 1u) != 0 ? 1.0f : -1.0f;
        const bool along_x = w < 2;
        const f32 half_length = kFloorHalf + 0.25f;
        ecs::LocalTransform at;
        at.position = along_x ? Vec3(side * (kFloorHalf + 0.25f), 1.5f, 0.0f)
                              : Vec3(0.0f, 1.5f, side * (kFloorHalf + 0.25f));
        const Vec3 half = along_x ? Vec3(0.25f, 1.5f, half_length) : Vec3(half_length, 1.5f, 0.25f);
        at.scale = Vec3(2.0f * half.x, 2.0f * half.y, 2.0f * half.z);
        scene::MeshRenderer mr;
        mr.mesh = box_;
        mr.material = material_ids_[kColors];
        physics::RigidBody body;
        body.type = physics::BodyType::Static;
        physics::Collider collider;
        collider.shape = physics::Shape::MakeBox(half);
        collider.material.friction = 0.5f;
        collider.material.restitution = 0.3f;
        world_->Create(at, ecs::WorldTransform{}, mr, body, collider);
    }
    // The light: a sun and a little ambient (entities, as in the renderer demo).
    scene::LightSource sun;
    sun.type = LightType::Directional;
    sun.direction = Vec3(0.4f, 0.85f, 0.35f);
    sun.color = Vec3(1.0f, 0.96f, 0.88f);
    sun.intensity = 1.1f;
    world_->Create(ecs::LocalTransform{}, ecs::WorldTransform{}, sun);
    scene::LightSource ambient;
    ambient.type = LightType::Ambient;
    ambient.color = Vec3(0.45f, 0.5f, 0.6f);
    ambient.intensity = 0.25f;
    world_->Create(ecs::LocalTransform{}, ecs::WorldTransform{}, ambient);

    // The camera is an entity too; `SetCameraAngle` moves it.
    scene::Camera lens;
    lens.fov_y = 0.9f;
    lens.near_plane = 0.1f;
    lens.far_plane = 120.0f;
    camera_ = world_->Create(ecs::LocalTransform{}, ecs::WorldTransform{}, lens);
    world_->Resource<scene::ActiveCamera>().entity = camera_;
    SetCameraAngle(0.6f);

    for (u32 i = 0; i < options.start_bodies; ++i)
    {
        const f32 x = RandomRange(&rng_, -5.0f, 5.0f);
        const f32 y = RandomRange(&rng_, 0.8f, 2.5f) + 1.3f * static_cast<f32>(i % 3);
        const f32 z = RandomRange(&rng_, -5.0f, 5.0f);
        Spawn(x, y, z);
    }
    // Let the first shapes settle before the first picture: the bodies are made and stepped.
    for (u32 i = 0; i < 4; ++i)
    {
        Step(0.0f);
    }
    return true;
}

void PhysicsScene::Spawn(f32 x, f32 y, f32 z)
{
    const u32 shape = NextRandom(&rng_) % 3;
    const u32 color = NextRandom(&rng_) % kColors;
    ecs::LocalTransform at;
    at.position = Vec3(x, y, z);
    // Each random value is drawn in its own statement: the order of evaluation of function arguments is unspecified,
    // and the shapes must be the same whatever the compiler.
    const f32 axis_x = RandomRange(&rng_, -1.0f, 1.0f);
    const f32 axis_y = RandomRange(&rng_, -1.0f, 1.0f);
    const f32 axis_z = RandomRange(&rng_, 0.2f, 1.0f);
    const f32 angle = RandomRange(&rng_, 0.0f, 2.0f * kPi);
    at.rotation = Quaternion::FromAxisAngle(Vec3(axis_x, axis_y, axis_z), angle);
    scene::MeshRenderer mr;
    mr.material = material_ids_[color];
    physics::Collider collider;
    collider.material.friction = 0.6f;
    collider.material.restitution = 0.3f;
    collider.material.density = 600.0f;
    if (shape == 0)
    {
        const f32 sx = RandomRange(&rng_, 0.5f, 1.2f);
        const f32 sy = RandomRange(&rng_, 0.5f, 1.2f);
        const f32 sz = RandomRange(&rng_, 0.5f, 1.2f);
        at.scale = Vec3(sx, sy, sz);
        mr.mesh = box_;
        collider.shape = physics::Shape::MakeBox(Vec3(0.5f * sx, 0.5f * sy, 0.5f * sz));
    }
    else if (shape == 1)
    {
        const f32 s = RandomRange(&rng_, 0.6f, 1.4f);
        at.scale = Vec3(s, s, s);
        mr.mesh = sphere_;
        collider.shape = physics::Shape::MakeSphere(0.5f * s);
    }
    else
    {
        const f32 s = RandomRange(&rng_, 0.6f, 1.1f);
        at.scale = Vec3(s, s, s);
        mr.mesh = capsule_;
        collider.shape = physics::Shape::MakeCapsule(0.5f * s, 0.5f * s);
    }
    physics::RigidBody body;
    body.linear_damping = 0.02f;
    body.angular_damping = 0.05f;
    body.flags = physics::kBodyContinuous;  // shapes dropped from 10 m must not tunnel through the floor
    if (bodies_.Size() >= max_bodies_)
    {  // the oldest shape goes (its body is removed by the next SyncBodies)
        world_->Destroy(bodies_[oldest_]);
        bodies_[oldest_] = world_->Create(at, ecs::WorldTransform{}, mr, body, collider);
        oldest_ = (oldest_ + 1) % max_bodies_;
    }
    else
    {
        bodies_.Push(world_->Create(at, ecs::WorldTransform{}, mr, body, collider));
    }
}

void PhysicsScene::Step(f32 dt)
{
    until_spawn_ -= dt;
    while (until_spawn_ <= 0.0f && dt > 0.0f)
    {
        until_spawn_ += spawn_interval_;
        const f32 x = RandomRange(&rng_, -6.0f, 6.0f);
        const f32 y = RandomRange(&rng_, 9.0f, 12.0f);
        const f32 z = RandomRange(&rng_, -6.0f, 6.0f);
        Spawn(x, y, z);
    }
    // The bodies follow the entities, then the schedule steps them, writes the poses back, propagates the transforms
    // and mirrors the result into the render scene.
    physics::SyncBodies(*world_, world_->Resource<physics::PhysicsWorld>());
    schedule_->Run(*world_, dt);
}

f32 PhysicsScene::LowestBodyY() const
{
    f32 lowest = 0.0f;
    bool any = false;
    for (usize i = 0; i < bodies_.Size(); ++i)
    {
        const ecs::LocalTransform* t = world_->Peek<ecs::LocalTransform>(bodies_[i]);
        if (t != nullptr && (!any || t->position.y < lowest))
        {
            lowest = t->position.y;
            any = true;
        }
    }
    return lowest;
}

u64 PhysicsScene::PhysicsSteps() const
{
    return world_->Resource<physics::PhysicsWorld>().GetStepStats().steps;
}

void PhysicsScene::SetCameraAngle(f32 angle)
{
    const f32 radius = 15.0f;
    const Vec3 eye(radius * Sin(angle), 15.0f, radius * Cos(angle));
    *world_->Get<ecs::LocalTransform>(camera_) = ecs::LookAt(eye, Vec3(0.0f, 0.5f, 0.0f), Vec3(0, 1, 0));
}

renderer::Camera PhysicsScene::Camera(f32 aspect) const
{
    renderer::Camera cam = world_->Resource<scene::CameraOutput>().camera;
    cam.aspect = aspect;
    return cam;
}

}  // namespace physics_demo
