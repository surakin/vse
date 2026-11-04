#version 450

// lit.vert: transforms a cube vertex to clip space and passes the world-space normal and the uv to the fragment shader.
//
// Math
//   clip   = mvp * position
//   normal = normalize(mat3(normal_matrix) * in_normal)    (done in the fragment shader after interpolation)
// `normal_matrix` is the inverse transpose of the model matrix's upper 3x3 up to a positive scale (the application
// computes it as the cofactor matrix divided by the sign of the determinant, so mirroring does not flip normals), so
// normals stay perpendicular to surfaces under non-uniform scale. For a rotation it is the rotation.
//
// Interface
//   vertex input  location 0: vec3 position
//   vertex input  location 1: vec3 normal
//   vertex input  location 2: vec2 uv
//   vertex output location 0: vec3 world normal (unnormalized)
//   vertex output location 1: vec2 uv
//   set 0, binding 0: uniform buffer Params (std140, vertex and fragment stages), shared with lit.frag:
//       mat4 mvp; mat4 normal_matrix; vec4 to_light; vec4 light_color; vec4 ambient;   (176 bytes)

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;

layout(set = 0, binding = 0, std140) uniform Params {
    mat4 mvp;
    mat4 normal_matrix;
    vec4 to_light;
    vec4 light_color;
    vec4 ambient;
} params;

layout(location = 0) out vec3 out_normal;
layout(location = 1) out vec2 out_uv;

void main() {
    gl_Position = params.mvp * vec4(in_position, 1.0);
    out_normal = mat3(params.normal_matrix) * in_normal;
    out_uv = in_uv;
}
