#pragma once

//! The terrain draw: the tiles [[renderer::TerrainTiles]] chose, drawn into the G-buffer.
//!
//! [[renderer::TerrainPass]] runs after the mesh G-buffer pass and writes the same four targets and the same depth,
//! so the lighting pass, the sky pass and every later pass treat the terrain as any other surface, and a mesh half
//! buried in the terrain shows the intersection. A frame is one instanced draw: each instance is a tile of 32 by 32
//! cells whose 6144 vertices the vertex shader makes from the vertex id (two triangles a cell; no vertex or index
//! buffer), displaced by the height texture. A vertex slides toward the next coarser level as it nears the end of its
//! level's range, so levels meet without cracks or popping.

#include "containers/span.h"
#include "containers/vector.h"
#include "math/matrix.h"
#include "renderer/device_resources.h"
#include "renderer/frame_data.h"
#include "renderer/gbuffer.h"
#include "renderer/material.h"
#include "renderer/render_graph.h"
#include "renderer/terrain_material.h"
#include "renderer/terrain_tiles.h"
#include "renderer/texture.h"
#include "rhi/rhi.h"
#include "types.h"

namespace vse::renderer
{

/// The most tiles one frame draws, all terrains together; tiles beyond it are dropped (a log warning).
inline constexpr u32 kMaxTerrainDrawTiles = 4096;

/// The most terrains one frame draws.
inline constexpr u32 kMaxTerrainsPerFrame = 8;

/// What the terrain is drawn from, for one frame.
///
/// - Thread-safety: thread-compatible; shares `height`.
struct TerrainDraw
{
    GpuTexturePtr height;           ///< The height texture (`R16_UNorm`, the map's mips).
    u32 samples_x = 0;              ///< Samples across the texture.
    u32 samples_z = 0;              ///< Samples down.
    f32 cell_size = 1.0f;           ///< Metres between samples.
    f32 range_min = 0.0f;           ///< The height of sample value 0.
    f32 range_max = 1.0f;           ///< The height of sample value 65535.
    Mat4 world = Mat4::Identity();  ///< Terrain space to world space; a translation and a rotation (no scale).
    TerrainMaterial material;       ///< The surface; a default material draws flat grey.
    TerrainSelectSettings select;   ///< The settings the tiles were chosen with (the morph uses the same ranges).
};

/// The terrain pass.
///
/// - Thread-safety: owner-thread-only; used from the frame's thread.
/// - Ownership: owns its shaders, layout, pipeline, sampler and tile buffer; shares the frame's height texture.
class TerrainPass final : public Recreatable
{
public:
    /// Creates the shaders, layout, pipeline and the per-frame tile buffers; if the device cannot, `Valid` is false and
    /// no terrain is drawn.
    ///
    /// - Param device: the device; must outlive this object.
    /// - Param materials: provides the frame bind group layout and group; must outlive this object.
    /// - Param frame: the per-frame buffers; must outlive this object.
    /// - Aborts: an argument is null.
    /// - Ownership: all three are borrowed.
    TerrainPass(rhi::Device* device, MaterialSystem* materials, FrameResources* frame);
    /// Destroys the device objects.
    ~TerrainPass() override;
    TerrainPass(const TerrainPass&) = delete;
    TerrainPass& operator=(const TerrainPass&) = delete;

    /// Registers the pass with a registry so it survives a device reset.
    ///
    /// - Param resources: the registry; null does nothing.
    /// - Returns: true if registered or `resources` is null.
    /// - Fails: returns false if the registry refused.
    /// - Ownership: the registry is borrowed.
    bool RegisterWith(DeviceResources* resources) { return registration_.Attach(resources, this, tiers::kPasses); }

    /// Destroys the shaders, layout, pipeline and buffers.
    void Release() override;
    /// Makes them again on the new device; a device with no shader for the terrain draws none (`Valid` is false).
    ///
    /// - Param device: the new device.
    /// - Returns: true.
    /// - Fails: never; a missing shader is logged as a warning.
    /// - Ownership: the device is borrowed.
    bool Recreate(rhi::Device* device) override;
    bool Live() const override { return device_ != nullptr && pipeline_.IsValid(); }
    const char* RecreatableName() const override { return "terrain pass"; }

    /// Returns whether the pipeline was created.
    /// - Fails: never.
    bool Valid() const { return pipeline_.IsValid(); }

    /// Starts a frame: forgets the terrains of the last one and picks the buffer region for `frame_index`.
    ///
    /// - Param frame_index: the frame number (as `FrameResources::BeginFrame`).
    /// - Fails: never.
    void BeginFrame(u64 frame_index);

    /// Adds one terrain to the frame and writes its tile instances; call after `BeginFrame`, up to
    /// `kMaxTerrainsPerFrame` times.
    ///
    /// - Param draw: the terrain; its height texture must be a ready 2D `R16_UNorm` texture. An invalid material
    ///   ([[renderer::TerrainMaterialInvalid]]) is logged and drawn as the default material.
    /// - Param tiles: the tiles [[renderer::TerrainTiles::Select]] chose, in terrain space.
    /// - Returns: false if the terrain was not added (no pipeline, a bad texture, no tiles, too many terrains or
    /// tiles).
    /// - Fails: returns false as above.
    /// - Ownership: shares the textures until the next `BeginFrame`; copies the tiles.
    bool Add(const TerrainDraw& draw, Span<const TerrainTile> tiles);

    /// `BeginFrame` and `Add` for a frame of one terrain.
    ///
    /// - Param draw: as for `Add`.
    /// - Param tiles: as for `Add`.
    /// - Param frame_index: the frame number.
    /// - Ownership: as for `Add`.
    void Prepare(const TerrainDraw& draw, Span<const TerrainTile> tiles, u64 frame_index);

    /// Clears the frame's terrains: no pass is added until the next `Add`.
    /// - Fails: never.
    void Clear();

    /// Declares the terrain pass over the G-buffer targets, after the mesh pass. Does nothing unless a terrain with at
    /// least one tile was prepared.
    ///
    /// - Param graph: the frame's graph.
    /// - Param targets: the targets [[renderer::GBufferPass::AddToGraph]] returned.
    /// - Returns: true if a pass was added.
    /// - Aborts: `graph` is null.
    /// - Ownership: the pass keeps a pointer to this object until the graph is executed.
    bool AddToGraph(RenderGraph* graph, const GBufferTargets& targets);

    /// The tiles of the frame that will be drawn, all terrains together.
    /// - Fails: never.
    u32 DrawnTiles() const { return tile_count_; }

    /// The tiles the frame has room for, after the terrains added so far (a terrain's tiles start at a multiple of 8).
    /// - Fails: never.
    u32 TileRoom() const { return tile_cursor_ >= kMaxTerrainDrawTiles ? 0 : kMaxTerrainDrawTiles - tile_cursor_; }

    /// The tiles of the frame that were chosen and not drawn for lack of room (also those the caller counted with
    /// `CountDropped`).
    /// - Fails: never.
    u32 DroppedTiles() const { return dropped_tiles_; }

    /// Adds to the count of dropped tiles; a caller that trims its list to `TileRoom()` reports what it removed.
    /// - Fails: never.
    void CountDropped(u32 tiles) { dropped_tiles_ += tiles; }

private:
    struct Push
    {
        f32 world[16];
        f32 grid[4];     // cell_size, range_min, range_max - range_min, mip count
        f32 texture[4];  // width, height, first_range, morph_fraction
        f32 spare[4];
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
    rhi::SamplerHandle sampler_;
    rhi::SamplerHandle repeat_sampler_;
    rhi::SamplerHandle clamp_sampler_;
    rhi::BufferHandle params_buffer_;
    byte* params_mapped_ = nullptr;
    GpuTexturePtr fallback_layers_;  // one grey layer: what a material with no layer array draws
    GpuTexturePtr fallback_splat_;   // one texel, layer 0 alone
    rhi::BufferHandle tile_buffer_;
    byte* tile_mapped_ = nullptr;
    rhi::BindGroupHandle frame_group_;
    struct Entry
    {
        Push push = {};
        GpuTexturePtr height;
        GpuTexturePtr layers;
        GpuTexturePtr splat;
        u32 first_tile = 0;
        u32 tile_count = 0;
        u64 params_offset = 0;
    };
    Vector<Entry> entries_;
    u32 tile_cursor_ = 0;  // the next free instance in the slot's region
    u32 tile_count_ = 0;
    u32 dropped_tiles_ = 0;
    u32 slot_ = 0;
    GBufferTargets targets_;
};

}  // namespace vse::renderer
