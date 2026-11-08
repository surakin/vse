#pragma once

//! Deferred lighting for directional, ambient and point light, and the tonemap that brings the result to the display.
//!
//! [[renderer::LightingPass]] reads the G-buffer ([[renderer::GBufferPass]]) and writes linear HDR light into an
//! `RGBA16_Float` accumulation target. One fullscreen triangle sums the ambient term and every directional light: per pixel
//! it rebuilds the world position from depth and the inverse view-projection, decodes the surface, and applies the lighting
//! model in `lighting_common.slang` (Lambert diffuse, GGX specular, metallic-roughness). Background pixels (depth 1.0) are
//! skipped. Then every visible point light is drawn as a light volume (see below), adding to the same target.
//! [[renderer::TonemapPass]] then applies an exposure and Narkowicz's ACES fit and writes the target, whose sRGB format does
//! the final encoding.
//!
//! Point lights. A point light has a position and a radius; its contribution is `color * intensity *
//! PointLightAttenuation(distance, radius)` shaded by the same model with the light vector pointing at the position. The
//! attenuation reaches exactly zero at the radius, so the sphere of that radius bounds the light. The light volume pass draws,
//! for each point light that survives frustum culling, a sphere of triangles (generated in the vertex shader from the vertex
//! index, [[renderer::kLightVolumeVertexCount]] vertices) with additive blending, front faces culled so that a camera inside
//! the sphere still draws its far side, no depth test, and the scissor set to the light's screen bounds. Every draw uses one
//! pipeline and one bind group; the light is picked by the instance index into the light storage buffer.
//!
//! The functions below are the CPU twins of the shader code, used by tests and tools; the real-device tests compare them with
//! the GPU output.
#include <atomic>

#include "containers/vector.h"
#include "math/matrix.h"
#include "math/vector.h"
#include "renderer/frame_data.h"
#include "renderer/gbuffer.h"
#include "renderer/material.h"
#include "renderer/render_graph.h"
#include "renderer/render_scene.h"
#include "rhi/rhi.h"
#include "types.h"

namespace engine_placeholder::renderer {

/// The light accumulation target's format (linear HDR).
inline constexpr rhi::Format kLightAccumFormat = rhi::Format::RGBA16_Float;

/// The surface the lighting model shades: what the G-buffer holds, decoded.
///
/// Thread-safety: thread-compatible plain data.
struct SurfacePoint {
    /// Linear base color.
    Vec3 albedo = Vec3(1, 1, 1);
    /// Perceptual roughness in [0, 1]; the shader clamps it to at least 0.045.
    f32 roughness = 0.5f;
    /// 0 for a dielectric, 1 for a metal.
    f32 metallic = 0.0f;
    /// Baked occlusion in [0, 1] applied to the ambient term.
    f32 occlusion = 1.0f;
    /// Emissive scale: the surface emits `albedo * emissive_scale`.
    f32 emissive_scale = 0.0f;
    /// Unit world-space normal.
    Vec3 normal = Vec3(0, 1, 0);
};

/// Returns one light's contribution to a surface: `(diffuse + specular) * radiance * max(dot(N, L), 0)`.
///
/// The diffuse BRDF is the albedo times `1 - metallic` (the 1/pi of a Lambertian surface is folded into the intensity) and the
/// specular lobe is GGX with height-correlated Smith visibility and Schlick Fresnel, multiplied by pi to match. The exact formulas
/// are in `lighting_common.slang`, which this mirrors.
///
/// Param surface: the decoded surface.
/// Param view: unit vector from the surface toward the viewer.
/// Param light: unit vector from the surface toward the light.
/// Param radiance: the light's color times its intensity (linear).
/// Returns: the linear RGB contribution; zero when the surface faces away from the light.
/// Fails: never.
Vec3 ShadeLight(const SurfacePoint& surface, const Vec3& view, const Vec3& light, const Vec3& radiance);

/// Returns the ambient and emissive terms: `albedo * (1 - metallic) * ambient * occlusion + albedo * emissive_scale`.
///
/// Param surface: the decoded surface.
/// Param ambient: the summed ambient light (linear).
/// Returns: the linear RGB contribution.
/// Fails: never.
Vec3 ShadeAmbient(const SurfacePoint& surface, const Vec3& ambient);

/// Applies Narkowicz's ACES fit to one linear channel: `clamp(x (2.51 x + 0.03) / (x (2.43 x + 0.59) + 0.14), 0, 1)`.
///
/// Param x: the channel after exposure, not negative.
/// Returns: the display-linear value in [0, 1] (the sRGB format of the target encodes it).
/// Fails: never.
f32 TonemapAces(f32 x);

/// Rebuilds a world position from a depth-buffer value, as the lighting shader does.
///
/// Param pixel_x: the pixel column; the pixel center, `pixel_x + 0.5`, is used.
/// Param pixel_y: the pixel row (0 at the top).
/// Param depth: the depth value in [0, 1].
/// Param width: the target width in pixels.
/// Param height: the target height in pixels.
/// Param inverse_view_projection: [[renderer::FrameData::inverse_view_projection]] (it already undoes the projection's Y flip).
/// Returns: the world-space position.
/// Fails: never.
Vec3 ReconstructWorldPosition(u32 pixel_x, u32 pixel_y, f32 depth, u32 width, u32 height, const Mat4& inverse_view_projection);

/// Returns a point light's attenuation at a distance: `saturate(1 - (d / R)^4)^2 / (d^2 + 1)` with `d` the distance and `R`
/// the radius.
///
/// It is 1 at the light, falls like an inverse square (the `+ 1` keeps it finite) over most of the range, and is smoothly
/// windowed to exactly 0 at `d = R` with zero slope there, staying 0 beyond. The shader multiplies the light's radiance by it.
/// The CPU twin of `PointAttenuation` in `lighting_common.slang`.
///
/// Param distance: the distance from the light, not negative.
/// Param radius: the light's radius, greater than zero.
/// Returns: a factor in [0, 1].
/// Fails: never.
f32 PointLightAttenuation(f32 distance, f32 radius);

/// Slices of the light volume sphere around the Y axis.
inline constexpr u32 kLightVolumeSegments = 16;
/// Bands of the light volume sphere from pole to pole.
inline constexpr u32 kLightVolumeRings = 8;
/// Vertices of one light volume: two triangles per quad, `kLightVolumeSegments * kLightVolumeRings` quads.
inline constexpr u32 kLightVolumeVertexCount = kLightVolumeSegments * kLightVolumeRings * 6;

/// Returns the factor that enlarges the light volume's unit sphere so it contains the light's sphere.
///
/// Every face then lies at least one radius from the center: the factor is `1 / (cos(pi / segments) cos(pi / rings))`.
/// Fails: never.
f32 LightVolumeScale();

/// Returns vertex `index` of the light volume, relative to the light's position, in units of its radius, scale included.
///
/// The CPU twin of the vertex generation in `point_light.vert`. Triangles are counter-clockwise seen from outside.
///
/// Param index: from 0 to `kLightVolumeVertexCount - 1`.
/// Returns: the offset from the light's position divided by its radius.
/// Aborts: `index` is out of range.
Vec3 LightVolumeVertex(u32 index);

/// Computes the pixel rectangle that bounds a point light's sphere on screen, for the scissor.
///
/// A sphere that reaches the camera's plane (any corner of its bounding box is not in front of the camera) gets the whole
/// target; otherwise the rectangle is the projection of the box, clamped to the target.
///
/// Param view: the frame block ([[renderer::FrameData::view_projection]] is used).
/// Param center: the light's position.
/// Param radius: the light's radius, greater than zero.
/// Param width: the target width in pixels.
/// Param height: the target height in pixels.
/// Param out: receives the rectangle (origin top-left, as the framebuffer); must not be null.
/// Returns: false when the rectangle is empty (the light cannot touch any pixel), true otherwise.
/// Fails: reported through the return value.
/// Ownership: `out` is borrowed for the call.
/// Aborts: `out` is null, the target size is zero or `radius` is not positive.
bool LightScreenRect(const FrameData& view, const Vec3& center, f32 radius, u32 width, u32 height, rhi::ScissorRect* out);

/// Lights the G-buffer with the scene's ambient, directional and point lights.
///
/// Thread-safety: thread-compatible; `Prepare` and `AddToGraph` run on the frame's thread.
/// Ownership: borrows the device, the material system (for the frame bind group) and the frame resources, which must outlive it;
///   owns its shaders and pipelines.
class LightingPass final : public Recreatable {
public:
    /// Creates the shaders, the bind group layout and the pipeline.
    ///
    /// Param device: the device.
    /// Param materials: the material system whose frame layout (set 0) the pipeline uses.
    /// Param frame: the frame resources the lights are written to.
    /// Ownership: all three are borrowed.
    /// Aborts: `device`, `materials` or `frame` is null.
    LightingPass(rhi::Device* device, MaterialSystem* materials, FrameResources* frame);
    /// Destroys the pipeline, shaders and layout.
    ~LightingPass() override;
    LightingPass(const LightingPass&) = delete;
    LightingPass& operator=(const LightingPass&) = delete;

    /// Registers the lighting pass with a registry so it survives a device reset.
    ///
    /// Param resources: the registry; null does nothing.
    /// Returns: true if registered or `resources` is null.
    /// Fails: returns false if the registry refused.
    /// Ownership: the registry is borrowed.
    bool RegisterWith(DeviceResources* resources) { return registration_.Attach(resources, this, tiers::kPasses); }

    /// Destroys the shaders, layout and both pipelines; nothing else is kept on the device.
    void Release() override;

    /// Makes the shaders, layout and both pipelines again.
    ///
    /// Param device: the new device.
    /// Returns: true if the objects were made.
    /// Fails: returns false, logging the reason, if the device refuses one of them.
    /// Ownership: the device is borrowed.
    bool Recreate(rhi::Device* device) override;
    bool Live() const override { return device_ != nullptr && pipeline_.IsValid() && volume_pipeline_.IsValid(); }
    const char* RecreatableName() const override { return "lighting pass"; }

    /// Returns whether the pipeline and shaders were created.
    /// Fails: never.
    bool Valid() const { return pipeline_.IsValid(); }

    /// Writes the scene's lights for this frame and plans the light volume draws. Call after `FrameResources::BeginFrame`,
    /// before `Flush`.
    ///
    /// Point lights outside the frustum, or whose screen rectangle is empty, get no draw.
    ///
    /// Param scene: the scene.
    /// Param view: the frame block of the view being lit (the one given to `FrameResources::WriteFrameData`); its viewport
    ///   size is the target size and must match the size given to `AddToGraph`.
    /// Returns: what was written (ambient sum and counts); see [[renderer::FrameResources::WriteLights]] for lights that
    ///   did not fit.
    /// Aborts: `view`'s viewport size is below 1 pixel.
    /// Ownership: `scene` and `view` are borrowed for the call.
    LightSummary Prepare(const RenderScene& scene, const FrameData& view);

    /// Returns the number of light volume draws planned by the last `Prepare`.
    /// Fails: never.
    u32 PointDrawsPlanned() const { return static_cast<u32>(point_draws_.Size()); }
    /// Returns the number of light volume draws the last executed graph recorded (0 before the first execution).
    /// Fails: never.
    u32 PointDrawsRecorded() const { return point_draws_recorded_.load(std::memory_order_relaxed); }

    /// Declares the accumulation target and the lighting pass.
    ///
    /// Param graph: the frame's graph.
    /// Param targets: the G-buffer targets; read as `Sampled`.
    /// Param width: the target width in pixels (the G-buffer's).
    /// Param height: the target height in pixels.
    /// Returns: the `RGBA16_Float` accumulation resource, written as a color attachment.
    /// Aborts: called twice since the last `Prepare`, `graph` is null, or the size differs from the viewport given to `Prepare`.
    /// Ownership: `graph` is borrowed; the pass keeps a pointer to this object until the graph is executed.
    GraphResource AddToGraph(RenderGraph* graph, const GBufferTargets& targets, u32 width, u32 height);

private:
    struct Push {
        Vec4 ambient;
        u32 directional_count = 0;
        u32 pad[3] = {};
    };
    static void Record(const PassExecuteContext& context, void* user);
    void Build();

    ScopedRegistration registration_;
    rhi::Device* device_ = nullptr;
    MaterialSystem* materials_ = nullptr;
    FrameResources* frame_ = nullptr;
    struct PointDraw {
        u32 light_index;
        rhi::ScissorRect rect;
    };
    rhi::ShaderHandle vertex_;
    rhi::ShaderHandle fragment_;
    rhi::ShaderHandle volume_vertex_;
    rhi::ShaderHandle volume_fragment_;
    rhi::BindGroupLayoutHandle layout_;
    rhi::PipelineHandle pipeline_;
    rhi::PipelineHandle volume_pipeline_;
    rhi::BindGroupHandle frame_group_;
    rhi::BufferHandle object_index_stream_; // MaterialSystem::ObjectIndexStream of this frame; invalid on Vulkan
    BufferRegion lights_;
    Vector<PointDraw> point_draws_;
    std::atomic<u32> point_draws_recorded_{0};
    u32 prepared_width_ = 0;
    u32 prepared_height_ = 0;
    GBufferTargets targets_;
    GraphResource accum_;
    u32 width_ = 0;
    u32 height_ = 0;
    bool added_ = false;
    Push push_;
};

/// Applies exposure and the ACES fit to the HDR accumulation and writes the display target.
///
/// Thread-safety: thread-compatible.
/// Ownership: borrows the device; owns its shaders, layout and the pipeline for the last output format.
class TonemapPass final : public Recreatable {
public:
    /// Creates the shaders and the bind group layout.
    ///
    /// Param device: the device; must outlive this object.
    /// Ownership: `device` is borrowed.
    /// Aborts: `device` is null.
    explicit TonemapPass(rhi::Device* device);
    /// Destroys the pipeline, shaders and layout.
    ~TonemapPass() override;
    TonemapPass(const TonemapPass&) = delete;
    TonemapPass& operator=(const TonemapPass&) = delete;

    /// Registers the tonemap pass with a registry so it survives a device reset. Its pipeline follows the output format, so a swapchain format change is handled by the next `AddToGraph`.
    ///
    /// Param resources: the registry; null does nothing.
    /// Returns: true if registered or `resources` is null.
    /// Fails: returns false if the registry refused.
    /// Ownership: the registry is borrowed.
    bool RegisterWith(DeviceResources* resources) { return registration_.Attach(resources, this, tiers::kPasses); }

    /// Destroys the shaders, layout and pipeline; nothing else is kept on the device.
    void Release() override;

    /// Makes the shaders and layout again; the pipeline is made on first use for the output format then.
    ///
    /// Param device: the new device.
    /// Returns: true if the objects were made.
    /// Fails: returns false, logging the reason, if the device refuses one of them.
    /// Ownership: the device is borrowed.
    bool Recreate(rhi::Device* device) override;
    bool Live() const override { return device_ != nullptr && vertex_.IsValid(); }
    const char* RecreatableName() const override { return "tonemap pass"; }

    /// Returns whether the shaders and layout were created.
    /// Fails: never.
    bool Valid() const;

    /// Declares the tonemap pass.
    ///
    /// Param graph: the frame's graph.
    /// Param hdr: the light accumulation resource; read as `Sampled`.
    /// Param output: the display target (the swapchain image or an offscreen color target), written as a color attachment
    ///   and cleared; its extent must equal the accumulation target's.
    /// Param output_format: the format of `output` (an import carries no description); use an `*_SRGB` format for the display.
    /// Param exposure: the scale applied to the HDR value before the operator; positive.
    /// Aborts: `graph` is null or `output_format` is undefined.
    /// Ownership: the pass keeps a pointer to this object until the graph is executed.
    void AddToGraph(RenderGraph* graph, GraphResource hdr, GraphResource output, rhi::Format output_format, f32 exposure);

private:
    struct Push {
        f32 exposure = 1.0f;
        f32 pad[3] = {};
    };
    static void Record(const PassExecuteContext& context, void* user);
    void Build();

    ScopedRegistration registration_;
    rhi::Device* device_ = nullptr;
    rhi::ShaderHandle vertex_;
    rhi::ShaderHandle fragment_;
    rhi::BindGroupLayoutHandle layout_;
    rhi::PipelineHandle pipeline_;
    rhi::Format pipeline_format_ = rhi::Format::Undefined;
    GraphResource hdr_;
    GraphResource output_;
    rhi::Format output_format_ = rhi::Format::Undefined;
    Push push_;
};

} // namespace engine_placeholder::renderer
