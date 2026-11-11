#pragma once

// Helpers shared by rhi_demo and the RHI tests: the cube mesh and a `CubeRenderer` that draws it. The projection and
// view helpers live in engine/renderer/camera.h. None of this belongs to the RHI itself.
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
#include "renderer/camera.h"
#include "rhi/rhi.h"
#include "rhi/uniform_ring.h"

namespace demo
{

using namespace engine_placeholder;

// The camera helpers moved to the renderer (Phase 5 Day 73); these names keep the demo and the RHI tests unchanged.
using renderer::LookAt;
using renderer::ModelMatrix;
using renderer::NormalMatrix;
using renderer::PerspectiveVulkan;

// One directional light with Lambert diffuse, plus a constant ambient term (the math is in shaders/lit.frag).
struct Lighting
{
    Vec3 to_light = Vec3(
        0, 1, 0);  // direction from the surface toward the light; need not be normalized; zero means no direct light
    Vec3 color = Vec3(1, 1, 1);
    f32 intensity = 1.0f;  // scales `color`
    Vec3 ambient = Vec3(0.1f, 0.1f, 0.1f);
};

struct CubeVertex
{
    f32 position[3];
    f32 normal[3];
    f32 uv[2];
};

constexpr u32 kCubeVertexCount = 24;
constexpr u32 kCubeIndexCount = 36;

// The unit cube centered on the origin: 24 vertices (4 per face), 36 indices.
const CubeVertex* CubeVertices();
const u16* CubeIndices();

struct CubeOptions
{
    rhi::Format color_format = rhi::Format::RGBA8_UNorm;
    rhi::Format depth_format = rhi::Format::Undefined;  // Undefined: no depth attachment
    bool depth_test = true;                             // ignored without a depth format
    bool indexed = true;                                // false draws 36 expanded vertices
    rhi::CullMode cull = rhi::CullMode::Back;
    bool textured = false;  // multiply the face color by a texture (see `SetTexture`)
    bool lit = false;       // a textured cube lit by one directional light; draw it with `DrawLit`
};

// Draws the cube. Thread-compatible: use from one thread.
class CubeRenderer
{
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

    // For a textured renderer: the texture (in `ShaderRead`) and sampler the next draws use. Both must outlive the
    // draws.
    void SetTexture(rhi::TextureHandle texture, rhi::SamplerHandle sampler)
    {
        texture_ = texture;
        sampler_ = sampler;
    }

    // Records one cube inside an open pass of `width` x `height`: a uniform-ring slice holding `mvp`, a transient
    // bind group, the pipeline and the draw. Returns false (drawing nothing) if the ring is full.
    bool Draw(rhi::CommandList* list, rhi::UniformRing* ring, const Mat4& mvp, u32 width, u32 height);

    // Like `Draw` for a renderer created with `lit` (a mismatch is a programming error): `view_projection * model` and
    // the normal matrix of `model` go to the vertex shader, the lighting to the fragment shader. Needs `SetTexture`.
    bool DrawLit(rhi::CommandList* list, rhi::UniformRing* ring, const Mat4& view_projection, const Mat4& model,
                 const Lighting& lighting, u32 width, u32 height);

private:
    bool DrawWith(rhi::CommandList* list, const rhi::UniformAllocation& u, u32 width, u32 height);

    rhi::Device* device_ = nullptr;
    rhi::ShaderHandle vs_;
    rhi::ShaderHandle fs_;
    rhi::BindGroupLayoutHandle layout_;
    rhi::PipelineHandle pipeline_;
    rhi::BufferHandle vertices_;
    rhi::BufferHandle indices_;
    bool indexed_ = true;
    bool textured_ = false;
    bool lit_ = false;
    bool geometry_ready_ = false;
    rhi::TextureHandle texture_;
    rhi::SamplerHandle sampler_;
};

}  // namespace demo
