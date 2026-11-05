#include "renderer/render_scene.h"

#include <cmath>
#include <new>

#include "log/assert.h"

namespace engine_placeholder::renderer {

namespace {

void CheckLight(const Light& light) {
    ENGINE_ASSERT(std::isfinite(light.intensity) && light.intensity >= 0.0f, "light: intensity must be finite and not negative");
    ENGINE_ASSERT(std::isfinite(light.color.x) && std::isfinite(light.color.y) && std::isfinite(light.color.z), "light: the color must be finite");
    ENGINE_ASSERT(std::isfinite(light.position.x) && std::isfinite(light.position.y) && std::isfinite(light.position.z), "light: the position must be finite");
    ENGINE_ASSERT(std::isfinite(light.direction.x) && std::isfinite(light.direction.y) && std::isfinite(light.direction.z), "light: the direction must be finite");
    ENGINE_ASSERT(light.type != LightType::Point || (std::isfinite(light.radius) && light.radius > 0.0f), "light: a point light needs a positive radius");
}

// Next generation for a slot, skipping 0 (which means "invalid").
u32 NextGeneration(u32 g) { return g + 1 == 0 ? 1 : g + 1; }

} // namespace

RenderScene::RenderScene(const RenderSceneDesc& desc) {
    ENGINE_ASSERT(desc.max_objects > 0 && desc.max_lights > 0 && desc.max_lights <= kMaxLights, "RenderScene: capacities must be at least 1 and the light capacity at most kMaxLights");
    object_capacity_ = desc.max_objects;
    light_capacity_ = desc.max_lights;
    objects_.Resize(object_capacity_);
    lights_.Resize(light_capacity_);
}

RenderScene::~RenderScene() = default;

ObjectId RenderScene::AddObject(const RenderObjectDesc& desc) {
    ENGINE_ASSERT(desc.mesh != nullptr && desc.mesh->Valid(), "AddObject: the mesh is null or empty");
    u32 slot;
    if (free_object_ != kNoSlot) {
        slot = free_object_;
        free_object_ = objects_[slot].next_free;
    } else if (slots_used_ < object_capacity_) {
        slot = slots_used_++;
    } else {
        return ObjectId{};
    }
    ObjectSlot& s = objects_[slot];
    s.alive = true;
    s.object.mesh = desc.mesh;
    s.object.material = desc.material;
    s.object.transform = desc.transform;
    s.object.flags = desc.flags;
    s.object.bounds = TransformBounds(desc.mesh->GetBounds(), desc.transform);
    ++object_count_;
    return ObjectId{slot, s.generation};
}

void RenderScene::RemoveObject(ObjectId id) {
    ENGINE_ASSERT(FindObject(id) != nullptr, "RemoveObject: the id is invalid or stale");
    ObjectSlot& s = objects_[id.index];
    s.alive = false;
    s.object = RenderObject();
    s.generation = NextGeneration(s.generation);
    s.next_free = free_object_;
    free_object_ = id.index;
    --object_count_;
}

void RenderScene::SetTransform(ObjectId id, const Mat4& transform) {
    ENGINE_ASSERT(FindObject(id) != nullptr, "SetTransform: the id is invalid or stale");
    RenderObject& o = objects_[id.index].object;
    o.transform = transform;
    o.bounds = TransformBounds(o.mesh->GetBounds(), transform);
}

void RenderScene::SetFlags(ObjectId id, u32 flags) {
    ENGINE_ASSERT(FindObject(id) != nullptr, "SetFlags: the id is invalid or stale");
    objects_[id.index].object.flags = flags;
}

const RenderObject* RenderScene::FindObject(ObjectId id) const {
    if (!id.IsValid() || id.index >= slots_used_) {
        return nullptr;
    }
    const ObjectSlot& s = objects_[id.index];
    return s.alive && s.generation == id.generation ? &s.object : nullptr;
}

bool RenderScene::SlotAlive(u32 slot) const {
    ENGINE_ASSERT(slot < slots_used_, "SlotAlive: slot out of range");
    return objects_[slot].alive;
}

const RenderObject& RenderScene::ObjectAt(u32 slot) const {
    ENGINE_ASSERT(slot < slots_used_ && objects_[slot].alive, "ObjectAt: slot out of range or dead");
    return objects_[slot].object;
}

LightId RenderScene::AddLight(const Light& light) {
    CheckLight(light);
    u32 slot;
    if (free_light_ != kNoSlot) {
        slot = free_light_;
        free_light_ = lights_[slot].next_free;
    } else if (light_slots_used_ < light_capacity_) {
        slot = light_slots_used_++;
    } else {
        return LightId{};
    }
    LightSlot& s = lights_[slot];
    s.alive = true;
    s.light = light;
    ++light_count_;
    return LightId{slot, s.generation};
}

void RenderScene::RemoveLight(LightId id) {
    ENGINE_ASSERT(FindLight(id) != nullptr, "RemoveLight: the id is invalid or stale");
    LightSlot& s = lights_[id.index];
    s.alive = false;
    s.light = Light();
    s.generation = NextGeneration(s.generation);
    s.next_free = free_light_;
    free_light_ = id.index;
    --light_count_;
}

void RenderScene::SetLight(LightId id, const Light& light) {
    ENGINE_ASSERT(FindLight(id) != nullptr, "SetLight: the id is invalid or stale");
    CheckLight(light);
    lights_[id.index].light = light;
}

const Light* RenderScene::FindLight(LightId id) const {
    if (!id.IsValid() || id.index >= light_slots_used_) {
        return nullptr;
    }
    const LightSlot& s = lights_[id.index];
    return s.alive && s.generation == id.generation ? &s.light : nullptr;
}

bool RenderScene::LightSlotAlive(u32 slot) const {
    ENGINE_ASSERT(slot < light_slots_used_, "LightSlotAlive: slot out of range");
    return lights_[slot].alive;
}

const Light& RenderScene::LightAt(u32 slot) const {
    ENGINE_ASSERT(slot < light_slots_used_ && lights_[slot].alive, "LightAt: slot out of range or dead");
    return lights_[slot].light;
}

u32 RenderScene::Cull(const Frustum& frustum, u32* out_slots, u32 capacity) const {
    ENGINE_ASSERT(out_slots != nullptr && capacity >= slots_used_, "Cull: the output array is null or smaller than the slot count");
    u32 count = 0;
    for (u32 slot = 0; slot < slots_used_; ++slot) {
        const ObjectSlot& s = objects_[slot];
        if (!s.alive || (s.object.flags & ObjectFlags::kHidden) != 0) {
            continue;
        }
        if (frustum.Visible(s.object.bounds)) {
            out_slots[count++] = slot;
        }
    }
    return count;
}

} // namespace engine_placeholder::renderer
