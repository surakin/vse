#version 450

// bindings_test.frag: exercises every kind of binding the RHI hands to a fragment shader. Used by the RHI tests.
//
//   color = texture(tex, uv) * params.scale + push.bias
//
// Interface
//   input  location 0: vec2 uv
//   output location 0: vec4 color (attachment 0)
//   set 0, binding 0: uniform buffer { vec4 scale }  (std140)
//   set 0, binding 1: sampled texture (2D)
//   set 0, binding 2: sampler
//   push constants: vec4 bias (16 bytes, fragment stage)

layout(set = 0, binding = 0, std140) uniform Params {
    vec4 scale;
} params;
layout(set = 0, binding = 1) uniform texture2D tex;
layout(set = 0, binding = 2) uniform sampler smp;
layout(push_constant) uniform Push {
    vec4 bias;
} push;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

void main() {
    out_color = texture(sampler2D(tex, smp), in_uv) * params.scale + push.bias;
}
