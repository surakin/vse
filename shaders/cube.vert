#version 450

// cube.vert: transforms a cube vertex by a model-view-projection matrix.
//
// Interface
//   vertex input  location 0: vec3 position
//   vertex input  location 1: vec3 normal
//   vertex input  location 2: vec2 uv
//   vertex output location 0: vec3 color (the normal mapped to 0..1, so each face has its own color)
//   vertex output location 1: vec2 uv
//   set 0, binding 0: uniform buffer { mat4 mvp }  (std140, column-major, vertex stage)
//   no push constants

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;

layout(set = 0, binding = 0, std140) uniform Params {
    mat4 mvp;
} params;

layout(location = 0) out vec3 out_color;
layout(location = 1) out vec2 out_uv;

void main() {
    gl_Position = params.mvp * vec4(in_position, 1.0);
    out_color = 0.5 + 0.5 * in_normal;
    out_uv = in_uv;
}
