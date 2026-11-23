// terrain.vert: the terrain tile vertex shader. Draws `32 * 32 * 6` vertices per tile (two triangles a cell, no vertex or
// index buffer) with one instance per selected tile; the vertex id picks the cell and the corner.
//
// Math (terrain space: x and z in metres from the first height sample, y up; world = push.world * terrain)
//   corner     the six vertices of cell (cx, cz) are (0,0) (1,0) (1,1) (0,0) (1,1) (0,1): the cell is cut from its minimum
//              corner to its maximum corner, as asset::HeightMapView::HeightAt and the height-field collider
//   g          = (cx, cz) + corner, 0..32: the vertex on the tile's grid
//   p          = tile.origin + g / 32 * tile.size                         the flat position
//   morph      m = saturate((distance(camera, p_world) - morph_start) / (range - morph_start)) with the level's range
//              first_range * 2^level and morph_start = range - (range - range / 2) * morph_fraction
//   g'         = g - frac(g / 2) * 2 * m                                  an odd vertex slides to its even neighbour
//   p'         = tile.origin + g' / 32 * tile.size                        and the height is read there; both p are
//                clamped to the map's far corner, so the cells of a clipped last tile collapse to nothing
//   mip        = level + m                                                 coarser tiles read coarser mips
//   height     = range_min + sample(u, v, mip) * range_span,  u = (p'.x / cell_size + 0.5) / width (the texel centres)
//   normal     = normalize(-dh/dx, 1, -dh/dz) by central differences of the heights one texel of the mip either side
//
// Interface
//   vertex output location 0: float3 world normal; 1: float2 uv (the terrain's 0..1 across); 2: float4 (terrain x, terrain
//                 z, distance to the camera, 0) in the place of gbuffer.frag's tangent
//   set 0, binding 0: uniform buffer FrameData (renderer::FrameData)
//   set 1, binding 0: storage buffer of TileInstance (32 bytes); binding 1: height texture (R16_UNorm, mips); binding 2: sampler
//   push constants (112 bytes): float4x4 world; float4 grid = (cell_size, range_min, range_span, mip_count);
//       float4 texture = (width, height, first_range, morph_fraction); float4 spare

#include "target_common.slang"

struct FrameData {
    float4x4 view;
    float4x4 projection;
    float4x4 view_projection;
    float4x4 inverse_view_projection;
    float4 camera_position;
    float4 viewport;
    float4 time;
};

struct TileInstance {
    float4 a;  // origin x, origin z, size (metres), level
    float4 b;  // unused
};

struct Push {
    float4x4 world;
    float4 grid;
    float4 tex;
    float4 spare;
};

[[vk::binding(0, 0)]] ConstantBuffer<FrameData> frame ENGINE_REGISTER(b0, space0);
[[vk::binding(0, 1)]] StructuredBuffer<TileInstance> tiles ENGINE_REGISTER(t0, space1);
[[vk::binding(1, 1)]] Texture2D<float> height_tex ENGINE_REGISTER(t1, space1);
[[vk::binding(2, 1)]] SamplerState height_sampler ENGINE_REGISTER(s2, space1);
[[vk::push_constant]] ConstantBuffer<Push> push ENGINE_REGISTER(b0, space15);

#if defined(ENGINE_TARGET_D3D12) || defined(ENGINE_TARGET_METAL)
#define TERRAIN_INSTANCE_ID SV_InstanceID
#else
#define TERRAIN_INSTANCE_ID SV_VulkanInstanceID
#endif

struct VertexInput {
    uint vertex : ENGINE_VERTEX_ID;
    uint instance : TERRAIN_INSTANCE_ID;
};

struct VertexOutput {
    [[vk::location(0)]] float3 normal : NORMAL;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] float4 tangent : TANGENT;
    float4 position : SV_Position;
};

float Height(float2 p, float mip) {
    float2 uv = (p / push.grid.x + 0.5) / push.tex.xy;
    return push.grid.y + height_tex.SampleLevel(height_sampler, uv, mip) * push.grid.z;
}

[shader("vertex")]
VertexOutput main(VertexInput input) {
    TileInstance tile = tiles[input.instance];
    uint cell = input.vertex / 6u;
    uint corner = input.vertex % 6u;
    float2 offsets[6] = {float2(0, 0), float2(1, 0), float2(1, 1), float2(0, 0), float2(1, 1), float2(0, 1)};
    float2 g = float2(float(cell % 32u), float(cell / 32u)) + offsets[corner];
    float level = tile.a.w;
    float size = tile.a.z;
    float2 extent = (push.tex.xy - 1.0) * push.grid.x;  // the map's far corner: cells of a clipped tile past it collapse
    float2 flat_p = min(tile.a.xy + g / 32.0 * size, extent);
    float y0 = Height(flat_p, level);
    float3 world_flat = mul(push.world, float4(flat_p.x, y0, flat_p.y, 1.0)).xyz;
    float range = push.tex.z * exp2(level);
    float below = level > 0.0 ? range * 0.5 : 0.0;
    float morph_start = range - (range - below) * push.tex.w;
    float m = saturate((distance(frame.camera_position.xyz, world_flat) - morph_start) / max(range - morph_start, 1e-4));
    float2 g2 = g - frac(g * 0.5) * 2.0 * m;
    float2 p = min(tile.a.xy + g2 / 32.0 * tile.a.z, extent);
    float mip = min(level + m, push.grid.w - 1.0);
    float h = Height(p, mip);
    float d = exp2(mip) * push.grid.x;
    float dx = (Height(p + float2(d, 0.0), mip) - Height(p - float2(d, 0.0), mip)) / (2.0 * d);
    float dz = (Height(p + float2(0.0, d), mip) - Height(p - float2(0.0, d), mip)) / (2.0 * d);
    float3 local = float3(p.x, h, p.y);
    VertexOutput output;
    output.position = ENGINE_CLIP(mul(frame.view_projection, mul(push.world, float4(local, 1.0))));
    output.normal = mul((float3x3)push.world, float3(-dx, 1.0, -dz));
    output.uv = p / (push.grid.x * (push.tex.xy - 1.0));
    // terrain-space x and z in metres (the layers tile over them) and the distance to the camera (the detail fade)
    output.tangent = float4(p.x, p.y, distance(frame.camera_position.xyz, mul(push.world, float4(local, 1.0)).xyz), 0.0);
    return output;
}
