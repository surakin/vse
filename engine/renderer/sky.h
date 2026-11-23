#pragma once

//! The sky: what shows behind the world where no surface was drawn.
//!
//! [[renderer::SkySettings]] is a plain value. The mode `Color` is the flat colour the lighting pass clears to (the
//! default); `Procedural` draws a gradient from a ground colour through a horizon colour to a zenith colour, with a sun
//! disc; `Cube` is reserved for the environment cube map. [[renderer::SkyColor]] is the CPU twin of the sky shader:
//! tests and tools compare the GPU output with it. [[renderer::SkyPass]] draws the sky after the lighting pass, only
//! where the depth buffer holds the far plane, so the order of other draws does not matter.

#include "math/vector.h"
#include "renderer/device_resources.h"
#include "renderer/frame_data.h"
#include "renderer/gbuffer.h"
#include "renderer/material.h"
#include "renderer/render_graph.h"
#include "renderer/texture.h"
#include "rhi/rhi.h"
#include "types.h"

namespace vse::renderer
{

/// How the sky is drawn.
enum class SkyMode : u8
{
    None = 0,    ///< Nothing is drawn; the background is black.
    Color,       ///< One flat colour (`SkySettings::color`), the colour the lighting pass clears to.
    Procedural,  ///< A gradient and a sun disc from the settings' colours.
    Cube,        ///< The environment cube map.
};

/// The sky's parameters. All colours are linear HDR; directions are world space with +Y up.
///
/// - Thread-safety: thread-compatible plain data.
struct SkySettings
{
    SkyMode mode = SkyMode::Color;  ///< How the sky is drawn.
    GpuTexturePtr cube;             ///< The environment cube map of mode `Cube`; null or not ready draws `Procedural`.
    Vec3 tint = Vec3(1.0f, 1.0f, 1.0f);           ///< Multiplies the cube map colour; not negative.
    Vec3 color = Vec3(0.0f, 0.0f, 0.0f);          ///< The flat colour of mode `Color`.
    Vec3 zenith = Vec3(0.18f, 0.35f, 0.75f);      ///< The colour straight up (procedural).
    Vec3 horizon = Vec3(0.65f, 0.75f, 0.9f);      ///< The colour at the horizon (procedural).
    Vec3 ground = Vec3(0.25f, 0.24f, 0.22f);      ///< The colour straight down (procedural).
    Vec3 sun_direction = Vec3(0.4f, 0.6f, 0.3f);  ///< Toward the sun; any non-zero length.
    Vec3 sun_color = Vec3(8.0f, 7.2f, 6.0f);      ///< The sun disc's radiance; black hides the sun.
    f32 sun_size = 0.05f;                         ///< The disc's angular radius in radians.
    f32 horizon_blend = 0.1f;      ///< The elevation below the horizon where the ground colour is reached.
    f32 ambient_intensity = 1.0f;  ///< Scale of the ambient light the sky gives the lit surfaces; above 0 it replaces
                                   ///< the flat ambient lights, 0 keeps them.
                                   ///< ambient lights.
    f32 exposure = 1.0f;           ///< Scale of the whole sky.
    f32 rotation = 0.0f;  ///< Turns the cube map about +Y in radians, right-handed: its +X face then looks toward (cos,
                          ///< 0, -sin).
};

/// Checks the settings.
///
/// - Param settings: any value.
/// - Returns: true when every number is finite, colours and the tint are not negative, the sun direction is not zero,
/// the sun size is
///   in (0, pi/2], the horizon blend is in (0, 1] and the exposure is positive.
/// - Fails: never.
bool SkySettingsValid(const SkySettings& settings);

/// The sky colour along a direction: the CPU twin of `sky.frag`.
///
/// Above the horizon the colour runs from `horizon` to `zenith` by a smooth step of the elevation; below it from
/// `horizon` to `ground` over `horizon_blend`; the sun adds `sun_color` times a smooth step across a soft edge of 25%
/// of its size; the sum is multiplied by `exposure`. Modes `Color` and `None` give the flat colour and black.
///
/// - Param settings: valid settings.
/// - Param direction: the view direction; need not be unit, not zero.
/// - Returns: the linear colour.
/// - Fails: never.
/// - Thread-safety: thread-safe.
Vec3 SkyColor(const SkySettings& settings, const Vec3& direction);

/// The direction to sample the cube map with for a view direction: the view direction turned by `-rotation` about +Y,
/// so `rotation` turns the sky itself. The CPU twin of the cube sky shader's lookup.
///
/// - Param settings: valid settings.
/// - Param direction: the view direction (any length).
/// - Returns: the direction in the cube map's own space, of the same length.
/// - Fails: never.
/// - Thread-safety: thread-safe.
Vec3 SkyCubeDirection(const SkySettings& settings, const Vec3& direction);

/// Whether two settings give the same procedural environment cube (the gradient colours and the exposure; the sun,
/// which the directional lights carry, is left out of it).
///
/// - Fails: never.
/// - Thread-safety: thread-safe.
bool SkyEnvironmentSame(const SkySettings& a, const SkySettings& b);

/// Makes a small cube map of the procedural sky (16 texels a face, a full chain of `RGBA16_Float`, the sun left out)
/// for the lighting pass to reflect and take ambient light from. The texture keeps its pixels, so it survives a device
/// reset.
///
/// - Param device: the device.
/// - Param resources: the registry the texture registers with, or null.
/// - Param settings: valid settings; the gradient colours and the exposure are used.
/// - Returns: the texture, or null when the device has no cube textures or refused it.
/// - Fails: returns null, logging the reason.
/// - Ownership: borrows `device` and `resources`; the result is shared.
GpuTexturePtr MakeSkyEnvironmentCube(rhi::Device* device, DeviceResources* resources, const SkySettings& settings);

/// Draws the sky into the light accumulation target after the lighting pass.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: borrows the device, the material system and the frame resources; owns its shaders, layout and pipeline.
class SkyPass final : public Recreatable
{
public:
    /// Creates the shaders, layout and pipeline; if the device cannot make them, `Valid` is false and no sky is drawn.
    ///
    /// - Param device: the device; must outlive this object.
    /// - Param materials: provides the frame bind group layout and group; must outlive this object.
    /// - Param frame: the per-frame buffers; must outlive this object.
    /// - Aborts: an argument is null.
    /// - Ownership: all three are borrowed.
    SkyPass(rhi::Device* device, MaterialSystem* materials, FrameResources* frame);
    /// Destroys the device objects.
    ~SkyPass() override;
    SkyPass(const SkyPass&) = delete;
    SkyPass& operator=(const SkyPass&) = delete;

    /// Registers the pass with a registry so it survives a device reset.
    ///
    /// - Param resources: the registry; null does nothing.
    /// - Returns: true if registered or `resources` is null.
    /// - Fails: returns false if the registry refused.
    /// - Ownership: the registry is borrowed.
    bool RegisterWith(DeviceResources* resources) { return registration_.Attach(resources, this, tiers::kPasses); }

    /// Destroys the shaders, layout and pipeline.
    void Release() override;
    /// Makes them again on the new device. A device that cannot make them (no shader for its backend) draws no sky:
    /// `Valid` is false and the pass is left out of the graph.
    ///
    /// - Param device: the new device.
    /// - Returns: true.
    /// - Fails: never; a missing shader is logged as a warning.
    /// - Ownership: the device is borrowed.
    bool Recreate(rhi::Device* device) override;
    bool Live() const override { return device_ != nullptr && pipeline_.IsValid(); }
    const char* RecreatableName() const override { return "sky pass"; }

    /// Returns whether the pipeline was created.
    /// - Fails: never.
    bool Valid() const { return pipeline_.IsValid(); }

    /// Takes the frame's sky. Call after `FrameResources::BeginFrame`.
    ///
    /// - Param settings: valid settings; the modes `Procedural` and `Cube` draw. A `Cube` sky whose texture is null,
    /// not
    ///   created, not a cube or on a device without cube textures draws the procedural sky instead. The pass keeps a
    ///   share of the cube until the next `Prepare`; a texture swapped between frames needs no pipeline rebuild.
    /// - Aborts: the settings are invalid.
    /// - Ownership: shares `settings.cube`.
    void Prepare(const SkySettings& settings);

    /// Declares the sky pass: a pass that reads depth and adds to the accumulation target. Does nothing unless the
    /// prepared mode is `Procedural` or `Cube`.
    ///
    /// - Param graph: the frame's graph.
    /// - Param targets: the G-buffer targets (the depth is read).
    /// - Param accum: the light accumulation target [[renderer::LightingPass::AddToGraph]] returned.
    /// - Returns: true if a pass was added.
    /// - Aborts: `graph` is null.
    /// - Ownership: the pass keeps a pointer to this object until the graph is executed.
    bool AddToGraph(RenderGraph* graph, const GBufferTargets& targets, GraphResource accum);

private:
    struct Push
    {
        f32 zenith[4];     // rgb, cos_inner
        f32 horizon[4];    // rgb, cos_outer
        f32 ground[4];     // rgb, horizon_blend
        f32 sun[4];        // unit direction, exposure
        f32 sun_color[4];  // rgb, 0
    };
    struct CubePush
    {
        f32 tint_exposure[4];  // rgb: tint * exposure
        f32 yaw[4];            // cos(rotation), sin(rotation), 0, 0
    };
    static void Record(const PassExecuteContext& context, void* user);
    void Build();

    ScopedRegistration registration_;
    rhi::Device* device_ = nullptr;
    MaterialSystem* materials_ = nullptr;
    FrameResources* frame_ = nullptr;
    rhi::ShaderHandle vertex_;
    rhi::ShaderHandle fragment_;
    rhi::BindGroupLayoutHandle layout_;
    rhi::PipelineHandle pipeline_;
    rhi::ShaderHandle cube_fragment_;
    rhi::BindGroupLayoutHandle cube_layout_;
    rhi::PipelineHandle cube_pipeline_;
    rhi::SamplerHandle sampler_;
    GpuTexturePtr cube_;  // the texture of the prepared frame; read through Handle() when recording
    CubePush cube_push_ = {};
    bool draw_cube_ = false;
    rhi::BindGroupHandle frame_group_;
    GBufferTargets targets_;
    GraphResource accum_;
    bool draw_ = false;
    Push push_ = {};
};

}  // namespace vse::renderer
