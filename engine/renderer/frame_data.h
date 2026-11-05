#pragma once

//! The data every frame feeds to the shaders: one `FrameData` uniform and one storage buffer of per-object data, both
//! rewritten each frame. A draw picks its object by instance index, so no bind group is created per draw.

#include "containers/vector.h"
#include "renderer/camera.h"
#include "renderer/render_scene.h"
#include "rhi/rhi.h"
#include "types.h"

namespace engine_placeholder::renderer {

/// The per-frame uniform block (set 0, binding 0 in the shaders). Laid out as std140: every member is a `mat4` or a
/// `vec4`, so the C++ struct needs no padding.
///
/// Thread-safety: thread-compatible plain data.
struct FrameData {
    /// World to view.
    Mat4 view;
    /// View to clip ([[renderer::PerspectiveVulkan]]).
    Mat4 projection;
    /// World to clip.
    Mat4 view_projection;
    /// Clip to world (homogeneous: divide by w).
    Mat4 inverse_view_projection;
    /// xyz: the camera position; w: the near plane distance.
    Vec4 camera_position;
    /// x, y: the viewport size in pixels; z, w: their reciprocals.
    Vec4 viewport;
    /// x: time in seconds; y: the last frame's duration in seconds; z: the frame number as a float (exact up to 2^24 frames, about 78 hours at 60 fps); w: the far plane distance.
    Vec4 time;
};
static_assert(sizeof(FrameData) == 304, "FrameData is 4 mat4 + 3 vec4 (std140)");

/// One entry of the per-object storage buffer (std430): the draw's instance index selects it.
///
/// Thread-safety: thread-compatible plain data.
struct ObjectData {
    /// Object to world.
    Mat4 world;
    /// Carries normals to world space ([[renderer::NormalMatrix]] of `world`).
    Mat4 normal;
};
static_assert(sizeof(ObjectData) == 128, "ObjectData is 2 mat4");

/// Fills a [[renderer::FrameData]].
///
/// Param camera: the camera; its projection parameters must be valid.
/// Param width: render target width in pixels, at least 1.
/// Param height: render target height in pixels, at least 1.
/// Param time_seconds: seconds since the start.
/// Param delta_seconds: duration of the previous frame in seconds.
/// Param frame_index: the frame number.
/// Returns: the block.
/// Aborts: the camera's projection parameters are out of range, or the size is 0.
/// Thread-safety: thread-safe. A pure function of its arguments.
FrameData MakeFrameData(const Camera& camera, u32 width, u32 height, f32 time_seconds, f32 delta_seconds, u64 frame_index);

/// A region of a buffer to bind: `offset` and `size` go in a bind group entry.
///
/// Thread-safety: thread-compatible plain data.
struct BufferRegion {
    /// The buffer; invalid if the resources could not be created.
    rhi::BufferHandle buffer;
    /// Byte offset of the region.
    u64 offset = 0;
    /// Byte size of the region.
    u64 size = 0;
};

/// One light as the lighting shaders read it (std430, 48 bytes).
///
/// Thread-safety: thread-compatible plain data.
struct LightData {
    /// xyz: for a directional light the unit vector from the surface toward the light; w: the type (0 directional, 1 point).
    Vec4 direction;
    /// rgb: the light's color times its intensity (linear); w unused.
    Vec4 color;
    /// xyz: a point light's position; w: its radius. Unused (zero) for a directional light.
    Vec4 position_radius;
};
static_assert(sizeof(LightData) == 48, "LightData is 3 vec4");

/// What [[renderer::FrameResources::WriteLights]] found in the scene.
///
/// Thread-safety: thread-compatible plain data.
struct LightSummary {
    /// The sum of every ambient light's color times its intensity (linear).
    Vec3 ambient = Vec3(0, 0, 0);
    /// Directional lights written to the buffer (they come first).
    u32 directional_count = 0;
    /// Point lights written to the buffer after the directional ones: the visible ones, nearest the camera first.
    u32 point_count = 0;
    /// Point lights left out because their sphere is entirely outside the view frustum.
    u32 point_culled = 0;
    /// Lights that did not fit in the buffer and were not written (directional, or visible point lights).
    ///
    /// Not zero is an error: the picture is missing light. It is also logged once per [[renderer::FrameResources]].
    u32 dropped = 0;
};

/// The per-frame buffers, in `kSlots` rotating regions so the CPU never writes data the GPU is still reading.
///
/// Call [[renderer::FrameResources::BeginFrame]] after `Device::BeginFrame`, write the frame block, the objects and the
/// lights, then bind the regions. The buffers are persistently mapped `CpuToGpu` memory; they are flushed for devices
/// whose memory is not coherent by [[renderer::FrameResources::Flush]].
///
/// Thread-safety: thread-compatible. Not copyable.
/// Ownership: owns its buffers; the device must outlive it.
class FrameResources {
public:
    /// The number of rotating regions; the largest frames in flight a device allows.
    static constexpr u32 kSlots = 3;

    /// Creates the buffers.
    ///
    /// Param device: the device; must outlive this object.
    /// Param max_objects: the most objects one frame writes, at least 1.
    /// Param max_lights: the most lights one frame writes, from 1 to [[renderer::kMaxLights]].
    /// Ownership: check [[renderer::FrameResources::Valid]].
    /// Aborts: `device` is null, `max_objects` is 0, or `max_lights` is 0 or above [[renderer::kMaxLights]].
    FrameResources(rhi::Device* device, u32 max_objects, u32 max_lights = 64);
    /// Unmaps and destroys the buffers.
    ~FrameResources();
    FrameResources(const FrameResources&) = delete;
    FrameResources& operator=(const FrameResources&) = delete;

    /// Returns whether both buffers were created and mapped.
    /// Fails: never; false means creation failed (logged) and every write will fail.
    bool Valid() const { return frame_mapped_ != nullptr && object_mapped_ != nullptr && light_mapped_ != nullptr; }

    /// Selects the region for this frame: `frame_index % kSlots`.
    ///
    /// Param frame_index: the device's frame number.
    /// Aborts: the resources are not valid.
    void BeginFrame(u64 frame_index);

    /// Writes the frame block into this frame's region.
    ///
    /// Param data: the block.
    /// Aborts: [[renderer::FrameResources::BeginFrame]] was not called.
    void WriteFrameData(const FrameData& data);

    /// Writes one [[renderer::ObjectData]] per listed object, in list order: draw `i` uses instance index `i`.
    ///
    /// Param scene: the scene.
    /// Param slots: object slots, as [[renderer::RenderScene::Cull]] returns them; each must be live.
    /// Param count: number of slots; at most the capacity given at construction.
    /// Returns: the number written (`count`).
    /// Aborts: `BeginFrame` was not called, `count` exceeds the capacity, or a slot is dead.
    /// Ownership: `slots` is borrowed for the call.
    u32 WriteObjects(const RenderScene& scene, const u32* slots, u32 count);

    /// Writes the scene's lights into this frame's region: directional lights first, then the visible point lights, and sums
    /// the ambient lights.
    ///
    /// Directional light directions are normalized (a zero direction lights nothing). A point light whose sphere (position,
    /// radius) is entirely outside the view frustum is left out ([[renderer::LightSummary::point_culled]]); the rest are
    /// sorted by squared distance from the camera to the light position, nearest first, ties broken by the light's slot, so
    /// the order is the same on every run. If the lights do not fit in `max_lights`, directional lights are written first
    /// and the farthest point lights are left out; the number left out is [[renderer::LightSummary::dropped]] and an error
    /// is logged (once per object). The capacity should be at least the scene's `RenderSceneDesc::max_lights`, which makes
    /// that impossible.
    ///
    /// Param scene: the scene.
    /// Param view: the frame block of the view being lit; its `view_projection` and `camera_position` are used.
    /// Returns: the ambient sum, the counts written and the counts left out.
    /// Aborts: [[renderer::FrameResources::BeginFrame]] was not called.
    LightSummary WriteLights(const RenderScene& scene, const FrameData& view);

    /// Returns the lights written by the last `WriteLights`, in buffer order.
    ///
    /// It is a CPU copy, because the mapped buffer is write-only memory. Valid until the next `BeginFrame`.
    /// Fails: never.
    /// Ownership: borrowed.
    const LightData* WrittenLights() const { return staged_.Data(); }
    /// Returns the number of lights written by the last `WriteLights`.
    u32 WrittenLightCount() const { return lights_written_; }

    /// Makes this frame's writes visible to the GPU on devices without coherent host memory; call before submitting.
    ///
    /// Aborts: `BeginFrame` was not called.
    void Flush();

    /// Returns the region of the frame uniform for this frame (a uniform binding).
    /// Aborts: `BeginFrame` was not called.
    BufferRegion FrameRegion() const;
    /// Returns the region of the object storage buffer for this frame (a storage binding): all `max_objects` entries.
    /// Aborts: `BeginFrame` was not called.
    BufferRegion ObjectRegion() const;
    /// Returns the region of the light storage buffer for this frame: all `max_lights` entries.
    /// Aborts: `BeginFrame` was not called.
    BufferRegion LightRegion() const;
    /// Returns the capacity in lights.
    u32 MaxLights() const { return max_lights_; }
    /// Returns the capacity in objects.
    u32 MaxObjects() const { return max_objects_; }

private:
    rhi::Device* device_ = nullptr;
    rhi::BufferHandle frame_buffer_;
    rhi::BufferHandle object_buffer_;
    rhi::BufferHandle light_buffer_;
    unsigned char* light_mapped_ = nullptr;
    struct PointEntry {
        f32 distance_squared;
        u32 slot;
    };
    static int ComparePoints(const void* a, const void* b);
    Vector<LightData> staged_;       // the written lights, in buffer order (the mapped memory is write-only)
    Vector<PointEntry> point_scratch_; // visible point lights while sorting; keeps its capacity between frames
    u64 light_stride_ = 0;
    u32 max_lights_ = 0;
    u32 lights_written_ = 0;
    bool warned_lights_ = false;
    unsigned char* frame_mapped_ = nullptr;
    unsigned char* object_mapped_ = nullptr;
    u64 frame_stride_ = 0;  // bytes between the frame regions: sizeof(FrameData) rounded to the offset alignment
    u64 object_stride_ = 0; // bytes between the object regions
    u32 max_objects_ = 0;
    u32 slot_ = 0;
    bool begun_ = false;
    u32 objects_written_ = 0;
};

} // namespace engine_placeholder::renderer
