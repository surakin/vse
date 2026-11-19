#include "renderer/frame_data.h"

#include "algorithm.h"

#include "lang/scalar.h"

#include "log/assert.h"
#include "renderer/renderer_log.h"
#include "cstr/mem_ops.h"

namespace engine_placeholder::renderer
{

namespace
{

u64 AlignUp(u64 v, u64 a)
{
    return (v + a - 1) / a * a;
}

u64 Gcd(u64 a, u64 b)
{
    while (b != 0)
    {
        const u64 t = a % b;
        a = b;
        b = t;
    }
    return a;
}

// A slice of a structured buffer starts on the binding alignment and on a whole element (a Direct3D 12 structured view
// begins at an element; Phase 13 Day 166): the least common multiple of the two.
u64 SliceAlignment(u64 binding_alignment, u64 element)
{
    const u64 a = binding_alignment == 0 ? 1 : binding_alignment;
    return a / Gcd(a, element) * element;
}

}  // namespace

FrameData MakeFrameData(const Camera& camera, u32 width, u32 height, f32 time_seconds, f32 delta_seconds,
                        u64 frame_index)
{
    ENGINE_ASSERT(width > 0 && height > 0, "MakeFrameData: the viewport size must be at least 1x1");
    FrameData data;
    data.view = camera.View();
    data.projection = camera.Projection();
    data.view_projection = data.projection * data.view;
    data.inverse_view_projection = camera.InverseViewProjection();
    data.camera_position = Vec4(camera.position, camera.near_plane);
    data.viewport = Vec4(static_cast<f32>(width), static_cast<f32>(height), 1.0f / static_cast<f32>(width),
                         1.0f / static_cast<f32>(height));
    data.time = Vec4(time_seconds, delta_seconds, static_cast<f32>(frame_index), camera.far_plane);
    return data;
}

FrameResources::FrameResources(rhi::Device* device, u32 max_objects, u32 max_lights, u32 max_joint_matrices)
    : device_(device), max_lights_(max_lights), max_objects_(max_objects), max_joints_(max_joint_matrices)
{
    ENGINE_ASSERT(device != nullptr && max_objects > 0 && max_lights > 0 && max_lights <= kMaxLights &&
                      max_joint_matrices > 0 && max_joint_matrices <= kMaxJointMatrices,
                  "FrameResources: need a device, at least one object, one to kMaxLights lights and a joint palette");
    Build();
}

void FrameResources::Build()
{
    rhi::Device* device = device_;
    const u32 max_objects = max_objects_;
    const u32 max_lights = max_lights_;
    const rhi::DeviceCaps& caps = device->Caps();
    frame_stride_ = AlignUp(sizeof(FrameData), caps.min_uniform_buffer_offset_alignment);
    object_stride_ = AlignUp(static_cast<u64>(max_objects) * sizeof(ObjectData),
                             SliceAlignment(caps.min_storage_buffer_offset_alignment, sizeof(ObjectData)));
    light_stride_ = AlignUp(static_cast<u64>(max_lights) * sizeof(LightData),
                            SliceAlignment(caps.min_storage_buffer_offset_alignment, sizeof(LightData)));

    joint_stride_ = AlignUp(static_cast<u64>(max_joints_) * kJointMatrixBytes,
                            SliceAlignment(caps.min_storage_buffer_offset_alignment, kJointMatrixBytes));

    rhi::BufferDesc bd;
    bd.location = rhi::MemoryLocation::CpuToGpu;
    bd.size = frame_stride_ * kSlots;
    bd.usage = rhi::BufferUsage::Uniform;
    bd.debug_name = "frame data";
    frame_buffer_ = device->CreateBuffer(bd);
    bd.size = object_stride_ * kSlots;
    bd.usage = rhi::BufferUsage::Storage;
    bd.debug_name = "object data";
    object_buffer_ = device->CreateBuffer(bd);
    bd.size = light_stride_ * kSlots;
    bd.debug_name = "light data";
    light_buffer_ = device->CreateBuffer(bd);
    bd.size = joint_stride_ * kSlots;
    bd.debug_name = "joint palette";
    joint_buffer_ = device->CreateBuffer(bd);

    if (frame_buffer_.IsValid() && object_buffer_.IsValid() && light_buffer_.IsValid() && joint_buffer_.IsValid() &&
        staged_.TryResize(max_lights) && point_scratch_.TryReserve(max_lights))
    {
        frame_mapped_ = static_cast<byte*>(device->MapBuffer(frame_buffer_));
        object_mapped_ = static_cast<byte*>(device->MapBuffer(object_buffer_));
        light_mapped_ = static_cast<byte*>(device->MapBuffer(light_buffer_));
        joint_mapped_ = static_cast<byte*>(device->MapBuffer(joint_buffer_));
    }
    if (!Valid())
    {
        RenderLog(LogVerbosity::Error, "FrameResources: creating or mapping the per-frame buffers failed");
        if (frame_mapped_ != nullptr)
        {
            device->UnmapBuffer(frame_buffer_);
        }
        if (object_mapped_ != nullptr)
        {
            device->UnmapBuffer(object_buffer_);
        }
        if (light_mapped_ != nullptr)
        {
            device->UnmapBuffer(light_buffer_);
        }
        if (joint_mapped_ != nullptr)
        {
            device->UnmapBuffer(joint_buffer_);
        }
        frame_mapped_ = nullptr;
        object_mapped_ = nullptr;
        light_mapped_ = nullptr;
        joint_mapped_ = nullptr;
        if (joint_buffer_.IsValid())
        {
            device->DestroyBuffer(joint_buffer_);
        }
        joint_buffer_ = {};
        if (frame_buffer_.IsValid())
        {
            device->DestroyBuffer(frame_buffer_);
        }
        if (object_buffer_.IsValid())
        {
            device->DestroyBuffer(object_buffer_);
        }
        if (light_buffer_.IsValid())
        {
            device->DestroyBuffer(light_buffer_);
        }
        frame_buffer_ = {};
        object_buffer_ = {};
        light_buffer_ = {};
    }
}

FrameResources::~FrameResources()
{
    registration_.Reset();
    Release();
}

void FrameResources::Release()
{
    if (device_ == nullptr)
    {
        return;
    }
    if (frame_mapped_ != nullptr)
    {
        device_->UnmapBuffer(frame_buffer_);
    }
    if (object_mapped_ != nullptr)
    {
        device_->UnmapBuffer(object_buffer_);
    }
    if (light_mapped_ != nullptr)
    {
        device_->UnmapBuffer(light_buffer_);
    }
    if (joint_mapped_ != nullptr)
    {
        device_->UnmapBuffer(joint_buffer_);
    }
    if (joint_buffer_.IsValid())
    {
        device_->DestroyBuffer(joint_buffer_);
    }
    if (frame_buffer_.IsValid())
    {
        device_->DestroyBuffer(frame_buffer_);
    }
    if (object_buffer_.IsValid())
    {
        device_->DestroyBuffer(object_buffer_);
    }
    if (light_buffer_.IsValid())
    {
        device_->DestroyBuffer(light_buffer_);
    }
    frame_mapped_ = nullptr;
    object_mapped_ = nullptr;
    light_mapped_ = nullptr;
    joint_mapped_ = nullptr;
    joint_buffer_ = {};
    frame_buffer_ = {};
    object_buffer_ = {};
    light_buffer_ = {};
    begun_ = false;
    joints_written_ = 0;
    objects_written_ = 0;
    lights_written_ = 0;
}

bool FrameResources::Recreate(rhi::Device* device)
{
    device_ = device;
    Build();
    return Valid();
}

void FrameResources::BeginFrame(u64 frame_index)
{
    ENGINE_ASSERT(Valid(), "FrameResources::BeginFrame: the resources are not valid");
    slot_ = static_cast<u32>(frame_index % kSlots);
    begun_ = true;
    objects_written_ = 0;
    lights_written_ = 0;
    joints_written_ = 0;
}

void FrameResources::WriteFrameData(const FrameData& data)
{
    ENGINE_ASSERT(begun_, "FrameResources::WriteFrameData: BeginFrame was not called");
    MemCopy(frame_mapped_ + frame_stride_ * slot_, &data, sizeof(FrameData));
}

u32 FrameResources::WriteObjects(const RenderScene& scene, const u32* slots, u32 count, const u32* joint_offsets)
{
    ENGINE_ASSERT(begun_, "FrameResources::WriteObjects: BeginFrame was not called");
    ENGINE_ASSERT(count <= max_objects_ && (slots != nullptr || count == 0),
                  "FrameResources::WriteObjects: too many objects or null list");
    byte* base = object_mapped_ + object_stride_ * slot_;
    for (u32 i = 0; i < count; ++i)
    {
        const RenderObject& object = scene.ObjectAt(slots[i]);
        ObjectData data;
        data.world = object.transform;
        data.normal = NormalMatrix(object.transform);
        if (joint_offsets != nullptr && joint_offsets[i] != kNoJoints)
        {
            // The palette offset rides in the normal matrix's unused translation column (exact below 2^24).
            data.normal.col[3].x = static_cast<f32>(joint_offsets[i]);
        }
        MemCopy(base + static_cast<usize>(i) * sizeof(ObjectData), &data, sizeof(ObjectData));
    }
    objects_written_ = count;
    return count;
}

u32 FrameResources::WriteJoints(const f32* matrices, u32 count)
{
    ENGINE_ASSERT(begun_ && matrices != nullptr, "FrameResources::WriteJoints: BeginFrame was not called or null");
    if (count == 0 || count > max_joints_ - joints_written_)
    {
        return kNoJoints;
    }
    const u32 offset = joints_written_;
    MemCopy(joint_mapped_ + joint_stride_ * slot_ + static_cast<usize>(offset) * kJointMatrixBytes, matrices,
            static_cast<usize>(count) * kJointMatrixBytes);
    joints_written_ += count;
    return offset;
}

LightSummary FrameResources::WriteLights(const RenderScene& scene, const FrameData& view)
{
    ENGINE_ASSERT(begun_, "FrameResources::WriteLights: BeginFrame was not called");
    LightSummary summary;
    LightData* out = staged_.Data();
    u32 n = 0;
    // Directional lights first, then point lights, so the shader's loops are contiguous ranges.
    for (u32 slot = 0; slot < scene.LightSlotCount(); ++slot)
    {
        if (!scene.LightSlotAlive(slot))
        {
            continue;
        }
        const Light& light = scene.LightAt(slot);
        if (light.type == LightType::Ambient)
        {
            summary.ambient = summary.ambient + light.color * light.intensity;
            continue;
        }
        if (light.type != LightType::Directional)
        {
            continue;
        }
        if (n == max_lights_)
        {
            ++summary.dropped;
            continue;
        }
        LightData d;
        Vec3 dir = light.direction;
        const f32 len = Sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
        // The shader assumes a unit vector; a zero direction stays zero and lights nothing.
        dir = len > 1e-6f ? Vec3(dir.x / len, dir.y / len, dir.z / len) : Vec3(0, 0, 0);
        d.direction = Vec4(dir, 0.0f);
        d.color = Vec4(light.color * light.intensity, 0.0f);
        d.position_radius = Vec4(0, 0, 0, 0);
        out[n++] = d;
        ++summary.directional_count;
    }
    // Point lights: cull against the frustum, sort the rest by distance (nearest first) and write what fits.
    const Frustum frustum = Frustum::FromViewProjection(view.view_projection);
    const Vec3 eye(view.camera_position.x, view.camera_position.y, view.camera_position.z);
    point_scratch_.Clear();
    for (u32 slot = 0; slot < scene.LightSlotCount(); ++slot)
    {
        if (!scene.LightSlotAlive(slot))
        {
            continue;
        }
        const Light& light = scene.LightAt(slot);
        if (light.type != LightType::Point && light.type != LightType::Spot)
        {
            continue;  // spot lights are point lights with a cone: the same volumes (glTF coverage Day 11)
        }
        BoundingSphere sphere;
        sphere.center = light.position;
        sphere.radius = light.radius;
        if (frustum.TestSphere(sphere) == Containment::Outside)
        {
            ++summary.point_culled;
            continue;
        }
        const Vec3 to = light.position - eye;
        point_scratch_.Push(PointEntry{to.x * to.x + to.y * to.y + to.z * to.z, slot});
    }
    if (point_scratch_.Size() > 1)
    {
        // Only the nearest that fit are written: select and sort those (container audit Day 6: every visible light was
        // sorted). The order is total (distance, then slot), so the lights and their order are those of a full sort.
        const usize room = n < max_lights_ ? max_lights_ - n : 0;
        PartialSort(Span<PointEntry>(point_scratch_.Data(), point_scratch_.Size()), room,
                    [](const PointEntry& x, const PointEntry& y)
                    {
                        return x.distance_squared < y.distance_squared ||
                               (x.distance_squared == y.distance_squared && x.slot < y.slot);
                    });
    }
    for (usize i = 0; i < point_scratch_.Size(); ++i)
    {
        if (n == max_lights_)
        {
            summary.dropped += static_cast<u32>(point_scratch_.Size() - i);
            break;
        }
        const Light& light = scene.LightAt(point_scratch_[i].slot);
        LightData d;
        d.direction = Vec4(0, 0, 0, 1.0f);
        d.color = Vec4(light.color * light.intensity, 0.0f);
        d.position_radius = Vec4(light.position, light.radius);
        if (light.type == LightType::Spot)
        {
            // The cone as glTF writes it: factor = saturate(cos(angle) * scale + offset)^2, carried in the unit
            // direction's length and in color.w.
            Vec3 dir = light.direction;
            const f32 len = Sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
            dir = len > 1e-6f ? Vec3(dir.x / len, dir.y / len, dir.z / len) : Vec3(0, 1, 0);
            const f32 cos_inner = Cos(light.inner_cone);
            const f32 cos_outer = Cos(light.outer_cone);
            const f32 scale = 1.0f / FMax(1e-3f, cos_inner - cos_outer);
            d.direction = Vec4(dir * scale, 1.0f);
            d.color.w = -cos_outer * scale;
        }
        out[n++] = d;
        ++summary.point_count;
    }
    if (summary.dropped > 0 && !warned_lights_)
    {
        warned_lights_ = true;
        RenderLog(
            LogVerbosity::Error,
            "FrameResources: %u lights do not fit in the %u the light buffer holds and were left out (logged once)",
            summary.dropped, max_lights_);
    }
    MemCopy(light_mapped_ + light_stride_ * slot_, out, static_cast<usize>(n) * sizeof(LightData));
    lights_written_ = n;
    return summary;
}

void FrameResources::Flush()
{
    ENGINE_ASSERT(begun_, "FrameResources::Flush: BeginFrame was not called");
    device_->FlushMappedBuffer(frame_buffer_, frame_stride_ * slot_, sizeof(FrameData));
    if (lights_written_ > 0)
    {
        device_->FlushMappedBuffer(light_buffer_, light_stride_ * slot_,
                                   static_cast<u64>(lights_written_) * sizeof(LightData));
    }
    if (joints_written_ > 0)
    {
        device_->FlushMappedBuffer(joint_buffer_, joint_stride_ * slot_,
                                   static_cast<u64>(joints_written_) * kJointMatrixBytes);
    }
    if (objects_written_ > 0)
    {
        device_->FlushMappedBuffer(object_buffer_, object_stride_ * slot_,
                                   static_cast<u64>(objects_written_) * sizeof(ObjectData));
    }
}

BufferRegion FrameResources::FrameRegion() const
{
    ENGINE_ASSERT(begun_, "FrameResources::FrameRegion: BeginFrame was not called");
    return BufferRegion{frame_buffer_, frame_stride_ * slot_, sizeof(FrameData)};
}

BufferRegion FrameResources::ObjectRegion() const
{
    ENGINE_ASSERT(begun_, "FrameResources::ObjectRegion: BeginFrame was not called");
    return BufferRegion{object_buffer_, object_stride_ * slot_, static_cast<u64>(max_objects_) * sizeof(ObjectData)};
}

BufferRegion FrameResources::JointRegion() const
{
    ENGINE_ASSERT(begun_, "FrameResources::JointRegion: BeginFrame was not called");
    return BufferRegion{joint_buffer_, joint_stride_ * slot_, static_cast<u64>(max_joints_) * kJointMatrixBytes};
}

BufferRegion FrameResources::LightRegion() const
{
    ENGINE_ASSERT(begun_, "FrameResources::LightRegion: BeginFrame was not called");
    return BufferRegion{light_buffer_, light_stride_ * slot_, static_cast<u64>(max_lights_) * sizeof(LightData)};
}

}  // namespace engine_placeholder::renderer
