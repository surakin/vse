#version 450

// textured.frag: the texture sampled at the interpolated uv, multiplied by the vertex color.
//
// Color spaces: the math is linear. A texture in an sRGB format is decoded to linear by the hardware when it is
// sampled; the color target decides how the result is stored (an sRGB target encodes on write, a UNORM target
// stores the linear value as it is).
//
// Interface
//   input  location 0: vec3 color
//   input  location 1: vec2 uv
//   output location 0: vec4 color (attachment 0)
//   set 0, binding 1: sampled texture (2D), fragment stage
//   set 0, binding 2: sampler, fragment stage

layout(set = 0, binding = 1) uniform texture2D tex;
layout(set = 0, binding = 2) uniform sampler smp;

layout(location = 0) in vec3 in_color;
layout(location = 1) in vec2 in_uv;

layout(location = 0) out vec4 out_color;

void main() {
    out_color = texture(sampler2D(tex, smp), in_uv) * vec4(in_color, 1.0);
}
