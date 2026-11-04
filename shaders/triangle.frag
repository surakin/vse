#version 450

// triangle.frag: writes the interpolated vertex color.
//
// Interface
//   input  location 0: vec3 color
//   output location 0: vec4 color (attachment 0)
//   no descriptors, no push constants

layout(location = 0) in vec3 in_color;

layout(location = 0) out vec4 out_color;

void main() {
    out_color = vec4(in_color, 1.0);
}
