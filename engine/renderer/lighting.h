#pragma once

//! Deferred lighting for directional and ambient light, and the tonemap that brings the result to the display.
//!
//! [[renderer::LightingPass]] reads the G-buffer ([[renderer::GBufferPass]]) and writes linear HDR light into an
//! `RGBA16_Float` accumulation target with one fullscreen triangle: per pixel it rebuilds the world position from depth and
//! the inverse view-projection, decodes the surface, and sums the ambient term and every directional light with the
//! lighting model in `lighting_common.slang` (Lambert diffuse, GGX specular, metallic-roughness). Background pixels (depth
//! 1.0) are skipped. [[renderer::TonemapPass]] then applies an exposure and Narkowicz's ACES fit and writes the target,
//! whose sRGB format does the final encoding.
//!
//! The functions below are the CPU twins of the shader code, used by tests and tools; the real-device tests compare them with
//! the GPU output.
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

/// Lights the G-buffer with the scene's directional and ambient lights.
///
/// Thread-safety: thread-compatible; `Prepare` and `AddToGraph` run on the frame's thread.
/// Ownership: borrows the device, the material system (for the frame bind group) and the frame resources, which must outlive it;
///   owns its shaders and pipeline.
class LightingPass {
public:
    /// Creates the shaders, the bind group layout and the pipeline.
    ///
    /// Param device: the device.
    /// Param materials: the material system whose frame layout (set 0) the pipeline uses.
    /// Param frame: the frame resources the lights are written to.
    /// Ownership: all three are borrowed.
    LightingPass(rhi::Device* device, MaterialSystem* materials, FrameResources* frame);
    /// Destroys the pipeline, shaders and layout.
    ~LightingPass();
    LightingPass(const LightingPass&) = delete;
    LightingPass& operator=(const LightingPass&) = delete;

    /// Returns whether the pipeline and shaders were created.
    /// Fails: never.
    bool Valid() const { return pipeline_.IsValid(); }

    /// Writes the scene's lights for this frame. Call after `FrameResources::BeginFrame`, before `Flush`.
    ///
    /// Param scene: the scene.
    /// Returns: what was written (ambient sum and counts).
    /// Fails: never; lights beyond the buffer's capacity are dropped (logged).
    /// Ownership: `scene` is borrowed for the call.
    LightSummary Prepare(const RenderScene& scene);

    /// Declares the accumulation target and the lighting pass.
    ///
    /// Param graph: the frame's graph.
    /// Param targets: the G-buffer targets; read as `Sampled`.
    /// Param width: the target width in pixels (the G-buffer's).
    /// Param height: the target height in pixels.
    /// Returns: the `RGBA16_Float` accumulation resource, written as a color attachment.
    /// Aborts: called twice since the last `Prepare`, or `graph` is null.
    /// Ownership: `graph` is borrowed; the pass keeps a pointer to this object until the graph is executed.
    GraphResource AddToGraph(RenderGraph* graph, const GBufferTargets& targets, u32 width, u32 height);

private:
    struct Push {
        Vec4 ambient;
        u32 directional_count = 0;
        u32 pad[3] = {};
    };
    static void Record(const PassExecuteContext& context, void* user);

    rhi::Device* device_ = nullptr;
    MaterialSystem* materials_ = nullptr;
    FrameResources* frame_ = nullptr;
    rhi::ShaderHandle vertex_;
    rhi::ShaderHandle fragment_;
    rhi::BindGroupLayoutHandle layout_;
    rhi::PipelineHandle pipeline_;
    rhi::BindGroupHandle frame_group_;
    BufferRegion lights_;
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
class TonemapPass {
public:
    /// Creates the shaders and the bind group layout.
    ///
    /// Param device: the device; must outlive this object.
    /// Ownership: `device` is borrowed.
    explicit TonemapPass(rhi::Device* device);
    /// Destroys the pipeline, shaders and layout.
    ~TonemapPass();
    TonemapPass(const TonemapPass&) = delete;
    TonemapPass& operator=(const TonemapPass&) = delete;

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
