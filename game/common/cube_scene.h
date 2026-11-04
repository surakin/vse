#pragma once

// Renderer-side helpers shared by rhi_demo and the RHI tests: the Vulkan-convention projection, a look-at view, the
// cube mesh and a `CubeRenderer` that draws it. None of this belongs to the RHI itself.
//
// Conventions
//   * World and view space are right-handed with Y up; the camera looks down -Z in view space.
//   * `PerspectiveVulkan` maps view space to Vulkan clip space: X right, Y *down* (the matrix flips Y), depth 0..1
//     (near -> 0, far -> 1). `SetViewport` takes a positive height; the flip lives in the projection.
//   * Meshes are authored counter-clockwise when seen from outside. With the projection above, a face that looks
//     counter-clockwise on screen is a front face for `FrontFace::CounterClockwise` (the default), and
//     `CullMode::Back` removes the faces that point away. (This was checked on a real device: the Y flip in the
//     projection and Vulkan's Y-down framebuffer cancel out, so the usual OpenGL-style authoring convention holds.)
//   * Matrices are column-major (`Mat4`), which is also std140's layout for a `mat4`, so a `Mat4` can be copied into a
//     uniform buffer as it is.

#include "math/matrix.h"
#include "math/vector.h"
#include "rhi/rhi.h"
#include "rhi/uniform_ring.h"

namespace demo {

using namespace engine_placeholder;

// A perspective projection for a right-handed view space and Vulkan clip space (Y down, depth 0..1).
//
// Param fov_y: vertical field of view in radians, in (0, pi).
// Param aspect: width / height, greater than zero.
// Param near_plane: distance to the near plane, greater than zero.
// Param far_plane: distance to the far plane, greater than `near_plane`.
Mat4 PerspectiveVulkan(f32 fov_y, f32 aspect, f32 near_plane, f32 far_plane);

// A view matrix for a camera at `eye` looking at `target`, with `up` as the approximate up direction.
Mat4 LookAt(const Vec3& eye, const Vec3& target, const Vec3& up);

// Translation, uniform scale and rotation about Y, applied in the order scale, rotate, translate.
Mat4 ModelMatrix(const Vec3& position, f32 scale, f32 yaw_radians);

struct CubeVertex {
    f32 position[3];
    f32 normal[3];
    f32 uv[2];
};

constexpr u32 kCubeVertexCount = 24;
constexpr u32 kCubeIndexCount = 36;

// The unit cube centered on the origin: 24 vertices (4 per face), 36 indices.
const CubeVertex* CubeVertices();
const u16* CubeIndices();

struct CubeOptions {
    rhi::Format color_format = rhi::Format::RGBA8_UNorm;
    rhi::Format depth_format = rhi::Format::Undefined; // Undefined: no depth attachment
    bool depth_test = true;                            // ignored without a depth format
    bool indexed = true;                               // false draws 36 expanded vertices
    rhi::CullMode cull = rhi::CullMode::Back;
};

// Draws the cube. Thread-compatible: use from one thread.
class CubeRenderer {
public:
    // Creates the shaders, pipeline, bind group layout and the vertex (and index) buffer. Returns false, after
    // cleaning up, if anything could not be created.
    bool Create(rhi::Device* device, const CubeOptions& options);

    CubeRenderer() = default;
    CubeRenderer(const CubeRenderer&) = delete;
    CubeRenderer& operator=(const CubeRenderer&) = delete;
    // Destroys what is left; the device must still exist and, for a clean exit, be idle.
    ~CubeRenderer() { Destroy(); }

    // Destroys everything created; idle the GPU first. Safe on a renderer that was never created.
    void Destroy();

    // Records what must happen before the pass: the first frames move the geometry out of its upload state. Call it
    // once in every list that draws (not once per draw), outside a pass, until `Submitted` has been called.
    void Prepare(rhi::CommandList* list);
    void Submitted() { geometry_ready_ = true; }

    // Records one cube inside an open pass of `width` x `height`: a uniform-ring slice holding `mvp`, a transient
    // bind group, the pipeline and the draw. Returns false (drawing nothing) if the ring is full.
    bool Draw(rhi::CommandList* list, rhi::UniformRing* ring, const Mat4& mvp, u32 width, u32 height);

private:
    rhi::Device* device_ = nullptr;
    rhi::ShaderHandle vs_;
    rhi::ShaderHandle fs_;
    rhi::BindGroupLayoutHandle layout_;
    rhi::PipelineHandle pipeline_;
    rhi::BufferHandle vertices_;
    rhi::BufferHandle indices_;
    bool indexed_ = true;
    bool geometry_ready_ = false;
};

} // namespace demo
