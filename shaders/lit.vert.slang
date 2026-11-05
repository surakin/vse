// lit.vert: transforms a cube vertex to clip space and passes the world-space normal and the uv to the fragment shader.
//
// Math
//   clip   = mvp * position
//   normal = normalize(float3x3(normal_matrix) * in_normal)    (done in the fragment shader after interpolation)
// `normal_matrix` is the inverse transpose of the model matrix's upper 3x3 up to a positive scale (the application
// computes it as the cofactor matrix divided by the sign of the determinant, so mirroring does not flip normals), so
// normals stay perpendicular to surfaces under non-uniform scale. For a rotation it is the rotation.
//
// Interface
//   vertex input  location 0: float3 position
//   vertex input  location 1: float3 normal
//   vertex input  location 2: float2 uv
//   vertex output location 0: float3 world normal (unnormalized)
//   vertex output location 1: float2 uv
//   set 0, binding 0: uniform buffer Params (std140, vertex and fragment stages), shared with lit.frag:
//       float4x4 mvp; float4x4 normal_matrix; float4 to_light; float4 light_color; float4 ambient;   (176 bytes)

struct Params {
    float4x4 mvp;
    float4x4 normal_matrix;
    float4 to_light;
    float4 light_color;
    float4 ambient;
};

[[vk::binding(0, 0)]] ConstantBuffer<Params> params;

struct VertexInput {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float2 uv : TEXCOORD0;
};

struct VertexOutput {
    [[vk::location(0)]] float3 normal : NORMAL;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput main(VertexInput input) {
    VertexOutput output;
    output.position = mul(params.mvp, float4(input.position, 1.0));
    output.normal = mul((float3x3)params.normal_matrix, input.normal);
    output.uv = input.uv;
    return output;
}
