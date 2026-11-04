#version 450

// triangle.vert: passes a 2D position and a color through.
//
// Interface
//   vertex input  location 0: vec2 position (clip space)
//   vertex input  location 1: vec3 color
//   vertex output location 0: vec3 color (interpolated)
//   no descriptors, no push constants

layout(location = 0) in vec2 in_position;
layout(location = 1) in vec3 in_color;

layout(location = 0) out vec3 out_color;

void main() {
    gl_Position = vec4(in_position, 0.0, 1.0);
    out_color = in_color;
}
