#pragma once

//! The scene a frame is built from: a fixed-capacity list of objects and lights with stable ids; the ECS replaces it in Phase 16.

#include "containers/vector.h"
#include "math/matrix.h"
#include "math/vector.h"
#include "renderer/bounds.h"
#include "renderer/frustum.h"
#include "renderer/mesh.h"
#include "types.h"

namespace engine_placeholder::renderer {

/// A generation-checked reference to an object in a [[renderer::RenderScene]], like an RHI handle: the all-zero value is
/// invalid, and an id whose object was removed no longer matches even if its slot is reused.
///
/// Thread-safety: thread-safe plain value.
struct ObjectId {
    /// The slot in the scene.
    u32 index = 0;
    /// The slot's generation when the id was made; 0 means invalid.
    u32 generation = 0;

    /// Returns whether this is not the invalid (all-zero) id; a valid-looking id can still be stale.
    /// Fails: never.
    constexpr bool IsValid() const { return generation != 0; }
    /// Returns whether both ids name the same object.
    constexpr bool operator==(const ObjectId& o) const { return index == o.index && generation == o.generation; }
    /// Returns whether the ids differ.
    constexpr bool operator!=(const ObjectId& o) const { return !(*this == o); }
};

/// A generation-checked reference to a light in a [[renderer::RenderScene]]; see [[renderer::ObjectId]].
///
/// Thread-safety: thread-safe plain value.
struct LightId {
    /// The slot in the scene.
    u32 index = 0;
    /// The slot's generation when the id was made; 0 means invalid.
    u32 generation = 0;

    /// Returns whether this is not the invalid (all-zero) id.
    /// Fails: never.
    constexpr bool IsValid() const { return generation != 0; }
    /// Returns whether both ids name the same light.
    constexpr bool operator==(const LightId& o) const { return index == o.index && generation == o.generation; }
    /// Returns whether the ids differ.
    constexpr bool operator!=(const LightId& o) const { return !(*this == o); }
};

/// Bits for [[renderer::RenderObject::flags]].
namespace ObjectFlags {
/// The object is skipped by culling and never drawn.
inline constexpr u32 kHidden = 1u << 0;
} // namespace ObjectFlags

/// What to give [[renderer::RenderScene::AddObject]].
///
/// Thread-safety: thread-compatible plain data.
/// Ownership: `mesh` is borrowed and must outlive the object.
struct RenderObjectDesc {
    /// The geometry; must not be null.
    const GpuMesh* mesh = nullptr;
    /// The material, an id assigned by the material system (0 is "none" until it exists).
    u32 material = 0;
    /// Object-space to world-space transform; affine.
    Mat4 transform = Mat4::Identity();
    /// `ObjectFlags` bits.
    u32 flags = 0;
};

/// One object in a scene, as stored.
///
/// Thread-safety: thread-compatible plain data.
/// Ownership: `mesh` is borrowed.
struct RenderObject {
    /// The geometry.
    const GpuMesh* mesh = nullptr;
    /// The material id.
    u32 material = 0;
    /// Object-space to world-space transform.
    Mat4 transform = Mat4::Identity();
    /// `ObjectFlags` bits.
    u32 flags = 0;
    /// World-space bounds, kept in step with `transform` by the scene.
    Bounds bounds;
};

/// The kind of a light.
enum class LightType : u8 {
    Directional = 0, ///< Infinitely far away; only `direction` matters.
    Point,           ///< A point with a radius of influence.
    Ambient,         ///< A uniform light from every direction; only `color` and `intensity` matter.
};

/// A light. Directional and ambient lights are lit by the fullscreen part of the deferred lighting pass, point lights by
/// light volumes (see [[renderer::LightingPass]]).
///
/// Thread-safety: thread-compatible plain data.
struct Light {
    /// Directional, point or ambient.
    LightType type = LightType::Directional;
    /// Linear RGB color; `intensity` scales it. For an ambient light this is the whole light.
    Vec3 color = Vec3(1, 1, 1);
    /// Scale applied to `color`; not negative.
    f32 intensity = 1.0f;
    /// For a directional light, the unit vector from the surface toward the light.
    Vec3 direction = Vec3(0, 1, 0);
    /// For a point light, the world position.
    Vec3 position = Vec3(0, 0, 0);
    /// For a point light, the distance at which its contribution reaches exactly zero (see [[renderer::PointLightAttenuation]]); greater than zero.
    f32 radius = 1.0f;
};

/// The most lights a scene or a [[renderer::FrameResources]] can hold: 16384.
///
/// A light is 48 bytes in the light buffer, so that is 768 KiB per frame slot. Asking for more aborts; a frame with more visible lights than the buffer holds reports
/// the excess in [[renderer::LightSummary::dropped]] and logs an error.
inline constexpr u32 kMaxLights = 16384;

/// How many objects and lights a scene can hold.
///
/// Thread-safety: thread-compatible plain data.
struct RenderSceneDesc {
    /// Capacity for objects, at least 1.
    u32 max_objects = 1024;
    /// Capacity for lights, from 1 to [[renderer::kMaxLights]].
    u32 max_lights = 64;
};

/// A fixed-capacity list of objects and lights.
///
/// Storage is allocated once at construction, so adding and removing never allocates or moves anything and slot
/// numbers stay valid for the life of an object. Objects are found by [[renderer::ObjectId]]; the frame iterates slots
/// (see [[renderer::RenderScene::SlotCount]]) and skips the dead ones.
///
/// Thread-safety: thread-compatible; reading from several threads at once is fine while nobody writes.
/// Ownership: owns its storage; the meshes the objects point at are borrowed and must outlive them.
class RenderScene {
public:
    /// Allocates the storage.
    ///
    /// Param desc: capacities; both at least 1.
    /// Aborts: a capacity is 0.
    explicit RenderScene(const RenderSceneDesc& desc);
    /// Frees the object and light lists.
    ~RenderScene();
    RenderScene(const RenderScene&) = delete;
    RenderScene& operator=(const RenderScene&) = delete;

    /// Adds an object and computes its world bounds.
    ///
    /// Param desc: the object.
    /// Returns: its id, or the invalid id (nothing added) if the scene is full.
    /// Fails: reported through the return value.
    /// Aborts: `desc.mesh` is null or empty.
    ObjectId AddObject(const RenderObjectDesc& desc);

    /// Removes an object; its id and every copy of it become stale.
    ///
    /// Param id: a live object.
    /// Aborts: `id` is invalid or stale.
    void RemoveObject(ObjectId id);

    /// Moves an object and recomputes its world bounds.
    ///
    /// Param id: a live object.
    /// Param transform: the new object-space to world-space transform; affine.
    /// Aborts: `id` is invalid or stale.
    void SetTransform(ObjectId id, const Mat4& transform);

    /// Replaces an object's flag bits.
    ///
    /// Param id: a live object.
    /// Param flags: `ObjectFlags` bits.
    /// Aborts: `id` is invalid or stale.
    void SetFlags(ObjectId id, u32 flags);

    /// Looks an object up.
    ///
    /// Param id: any id.
    /// Returns: the object, or null if `id` is invalid or stale. The pointer stays valid until the object is removed.
    /// Fails: reported through the return value.
    /// Ownership: borrowed from the scene.
    const RenderObject* FindObject(ObjectId id) const;

    /// Returns the number of live objects.
    u32 ObjectCount() const { return object_count_; }
    /// Returns the capacity for objects.
    u32 ObjectCapacity() const { return object_capacity_; }
    /// Returns the number of slots ever used: slot numbers below this may hold an object (see
    /// [[renderer::RenderScene::SlotAlive]]).
    u32 SlotCount() const { return slots_used_; }
    /// Returns whether a slot holds a live object.
    /// Aborts: `slot` is not below [[renderer::RenderScene::SlotCount]].
    bool SlotAlive(u32 slot) const;
    /// Returns the object in a live slot.
    /// Aborts: `slot` is out of range or dead.
    const RenderObject& ObjectAt(u32 slot) const;

    /// Adds a light.
    ///
    /// Param light: the light; a point light needs a positive radius.
    /// Returns: its id, or the invalid id if the scene is full.
    /// Fails: reported through the return value.
    /// Aborts: a point light has a radius that is not positive, or an intensity below zero, or any value of the light is not finite.
    LightId AddLight(const Light& light);

    /// Removes a light; its id becomes stale.
    ///
    /// Param id: a live light.
    /// Aborts: `id` is invalid or stale.
    void RemoveLight(LightId id);

    /// Replaces a light.
    ///
    /// Param id: a live light.
    /// Param light: the new values, checked as for [[renderer::RenderScene::AddLight]].
    /// Aborts: `id` is invalid or stale, or `light` is invalid.
    void SetLight(LightId id, const Light& light);

    /// Looks a light up.
    ///
    /// Param id: any id.
    /// Returns: the light, or null if `id` is invalid or stale.
    /// Fails: reported through the return value.
    /// Ownership: borrowed from the scene; valid until the light is removed.
    const Light* FindLight(LightId id) const;

    /// Returns the number of live lights.
    u32 LightCount() const { return light_count_; }
    /// Returns the number of light slots ever used.
    u32 LightSlotCount() const { return light_slots_used_; }
    /// Returns whether a light slot is live.
    /// Aborts: `slot` is not below [[renderer::RenderScene::LightSlotCount]].
    bool LightSlotAlive(u32 slot) const;
    /// Returns the light in a live slot.
    /// Aborts: `slot` is out of range or dead.
    const Light& LightAt(u32 slot) const;

    /// Finds the objects that may be visible: live, not hidden, and not culled by the frustum.
    ///
    /// Param frustum: the view volume, in world space.
    /// Param out_slots: receives the slot numbers in increasing order.
    /// Param capacity: size of `out_slots`; at least [[renderer::RenderScene::SlotCount]].
    /// Returns: the number of slots written.
    /// Aborts: `out_slots` is null or `capacity` is below the slot count.
    /// Ownership: `out_slots` is borrowed for the call.
    u32 Cull(const Frustum& frustum, u32* out_slots, u32 capacity) const;

private:
    struct ObjectSlot {
        RenderObject object;
        u32 generation = 1;
        u32 next_free = 0;
        bool alive = false;
    };
    struct LightSlot {
        Light light;
        u32 generation = 1;
        u32 next_free = 0;
        bool alive = false;
    };
    static constexpr u32 kNoSlot = 0xffffffffu;

    Vector<ObjectSlot> objects_;
    Vector<LightSlot> lights_;
    u32 object_capacity_ = 0;
    u32 light_capacity_ = 0;
    u32 object_count_ = 0;
    u32 light_count_ = 0;
    u32 slots_used_ = 0;
    u32 light_slots_used_ = 0;
    u32 free_object_ = kNoSlot;
    u32 free_light_ = kNoSlot;
};

} // namespace engine_placeholder::renderer
