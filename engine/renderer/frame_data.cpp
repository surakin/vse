#include "renderer/frame_data.h"

#include <cmath>
#include <cstring>

#include "log/assert.h"
#include "renderer/renderer_log.h"

namespace engine_placeholder::renderer {

namespace {

u64 AlignUp(u64 v, u64 a) { return (v + a - 1) / a * a; }

} // namespace

FrameData MakeFrameData(const Camera& camera, u32 width, u32 height, f32 time_seconds, f32 delta_seconds, u64 frame_index) {
    ENGINE_ASSERT(width > 0 && height > 0, "MakeFrameData: the viewport size must be at least 1x1");
    FrameData data;
    data.view = camera.View();
    data.projection = camera.Projection();
    data.view_projection = data.projection * data.view;
    data.inverse_view_projection = camera.InverseViewProjection();
    data.camera_position = Vec4(camera.position, camera.near_plane);
    data.viewport = Vec4(static_cast<f32>(width), static_cast<f32>(height), 1.0f / static_cast<f32>(width), 1.0f / static_cast<f32>(height));
    data.time = Vec4(time_seconds, delta_seconds, static_cast<f32>(frame_index), camera.far_plane);
    return data;
}

FrameResources::FrameResources(rhi::Device* device, u32 max_objects, u32 max_lights) : device_(device), max_objects_(max_objects), max_lights_(max_lights) {
    ENGINE_ASSERT(device != nullptr && max_objects > 0 && max_lights > 0, "FrameResources: need a device, at least one object and at least one light");
    const rhi::DeviceCaps& caps = device->Caps();
    frame_stride_ = AlignUp(sizeof(FrameData), caps.min_uniform_buffer_offset_alignment);
    object_stride_ = AlignUp(static_cast<u64>(max_objects) * sizeof(ObjectData), caps.min_storage_buffer_offset_alignment);
    light_stride_ = AlignUp(static_cast<u64>(max_lights) * sizeof(LightData), caps.min_storage_buffer_offset_alignment);

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

    if (frame_buffer_.IsValid() && object_buffer_.IsValid() && light_buffer_.IsValid()) {
        frame_mapped_ = static_cast<unsigned char*>(device->MapBuffer(frame_buffer_));
        object_mapped_ = static_cast<unsigned char*>(device->MapBuffer(object_buffer_));
        light_mapped_ = static_cast<unsigned char*>(device->MapBuffer(light_buffer_));
    }
    if (!Valid()) {
        RenderLog(LogVerbosity::Error, "FrameResources: creating or mapping the per-frame buffers failed");
        if (frame_mapped_ != nullptr) device->UnmapBuffer(frame_buffer_);
        if (object_mapped_ != nullptr) device->UnmapBuffer(object_buffer_);
        if (light_mapped_ != nullptr) device->UnmapBuffer(light_buffer_);
        frame_mapped_ = nullptr;
        object_mapped_ = nullptr;
        light_mapped_ = nullptr;
        if (frame_buffer_.IsValid()) device->DestroyBuffer(frame_buffer_);
        if (object_buffer_.IsValid()) device->DestroyBuffer(object_buffer_);
        if (light_buffer_.IsValid()) device->DestroyBuffer(light_buffer_);
        frame_buffer_ = {};
        object_buffer_ = {};
        light_buffer_ = {};
    }
}

FrameResources::~FrameResources() {
    if (frame_mapped_ != nullptr) device_->UnmapBuffer(frame_buffer_);
    if (object_mapped_ != nullptr) device_->UnmapBuffer(object_buffer_);
    if (light_mapped_ != nullptr) device_->UnmapBuffer(light_buffer_);
    if (frame_buffer_.IsValid()) device_->DestroyBuffer(frame_buffer_);
    if (object_buffer_.IsValid()) device_->DestroyBuffer(object_buffer_);
    if (light_buffer_.IsValid()) device_->DestroyBuffer(light_buffer_);
}

void FrameResources::BeginFrame(u64 frame_index) {
    ENGINE_ASSERT(Valid(), "FrameResources::BeginFrame: the resources are not valid");
    slot_ = static_cast<u32>(frame_index % kSlots);
    begun_ = true;
    objects_written_ = 0;
    lights_written_ = 0;
}

void FrameResources::WriteFrameData(const FrameData& data) {
    ENGINE_ASSERT(begun_, "FrameResources::WriteFrameData: BeginFrame was not called");
    std::memcpy(frame_mapped_ + frame_stride_ * slot_, &data, sizeof(FrameData));
}

u32 FrameResources::WriteObjects(const RenderScene& scene, const u32* slots, u32 count) {
    ENGINE_ASSERT(begun_, "FrameResources::WriteObjects: BeginFrame was not called");
    ENGINE_ASSERT(count <= max_objects_ && (slots != nullptr || count == 0), "FrameResources::WriteObjects: too many objects or null list");
    unsigned char* base = object_mapped_ + object_stride_ * slot_;
    for (u32 i = 0; i < count; ++i) {
        const RenderObject& object = scene.ObjectAt(slots[i]);
        ObjectData data;
        data.world = object.transform;
        data.normal = NormalMatrix(object.transform);
        std::memcpy(base + static_cast<usize>(i) * sizeof(ObjectData), &data, sizeof(ObjectData));
    }
    objects_written_ = count;
    return count;
}

LightSummary FrameResources::WriteLights(const RenderScene& scene) {
    ENGINE_ASSERT(begun_, "FrameResources::WriteLights: BeginFrame was not called");
    LightSummary summary;
    LightData* out = reinterpret_cast<LightData*>(light_mapped_ + light_stride_ * slot_);
    u32 n = 0;
    bool dropped = false;
    // Directional lights first, then point lights, so the shader's loops are contiguous ranges.
    for (int pass = 0; pass < 2; ++pass) {
        const LightType wanted = pass == 0 ? LightType::Directional : LightType::Point;
        for (u32 slot = 0; slot < scene.LightSlotCount(); ++slot) {
            if (!scene.LightSlotAlive(slot)) continue;
            const Light& light = scene.LightAt(slot);
            if (light.type == LightType::Ambient) {
                if (pass == 0) summary.ambient = summary.ambient + light.color * light.intensity;
                continue;
            }
            if (light.type != wanted) continue;
            if (n == max_lights_) {
                dropped = true;
                continue;
            }
            LightData d;
            Vec3 dir = light.direction;
            const f32 len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
            // The shader assumes a unit vector; a zero direction stays zero and lights nothing.
            if (wanted == LightType::Directional) dir = len > 1e-6f ? Vec3(dir.x / len, dir.y / len, dir.z / len) : Vec3(0, 0, 0);
            d.direction = Vec4(dir, wanted == LightType::Directional ? 0.0f : 1.0f);
            d.color = Vec4(light.color * light.intensity, 0.0f);
            d.position_radius = wanted == LightType::Point ? Vec4(light.position, light.radius) : Vec4(0, 0, 0, 0);
            out[n++] = d;
            if (wanted == LightType::Directional) ++summary.directional_count;
            else ++summary.point_count;
        }
    }
    if (dropped && !warned_lights_) {
        warned_lights_ = true;
        RenderLog(LogVerbosity::Warn, "FrameResources: more lights than the %u the buffer holds; the extra ones are dropped (logged once)", max_lights_);
    }
    lights_written_ = n;
    return summary;
}

void FrameResources::Flush() {
    ENGINE_ASSERT(begun_, "FrameResources::Flush: BeginFrame was not called");
    device_->FlushMappedBuffer(frame_buffer_, frame_stride_ * slot_, sizeof(FrameData));
    if (lights_written_ > 0) {
        device_->FlushMappedBuffer(light_buffer_, light_stride_ * slot_, static_cast<u64>(lights_written_) * sizeof(LightData));
    }
    if (objects_written_ > 0) {
        device_->FlushMappedBuffer(object_buffer_, object_stride_ * slot_, static_cast<u64>(objects_written_) * sizeof(ObjectData));
    }
}

BufferRegion FrameResources::FrameRegion() const {
    ENGINE_ASSERT(begun_, "FrameResources::FrameRegion: BeginFrame was not called");
    return BufferRegion{frame_buffer_, frame_stride_ * slot_, sizeof(FrameData)};
}

BufferRegion FrameResources::ObjectRegion() const {
    ENGINE_ASSERT(begun_, "FrameResources::ObjectRegion: BeginFrame was not called");
    return BufferRegion{object_buffer_, object_stride_ * slot_, static_cast<u64>(max_objects_) * sizeof(ObjectData)};
}

BufferRegion FrameResources::LightRegion() const {
    ENGINE_ASSERT(begun_, "FrameResources::LightRegion: BeginFrame was not called");
    return BufferRegion{light_buffer_, light_stride_ * slot_, static_cast<u64>(max_lights_) * sizeof(LightData)};
}

} // namespace engine_placeholder::renderer
