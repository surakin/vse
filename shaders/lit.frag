#version 450

// lit.frag: one directional light with Lambert diffuse and a constant ambient term, on a textured surface.
//
// Math (all in linear light)
//   N      = normalize(world normal)
//   L      = to_light.xyz                         unit vector from the surface toward the light (the application
//                                                 normalizes it; the zero vector means "no direct light")
//   albedo = texture(tex, uv).rgb                 an sRGB texture is decoded by the hardware
//   color  = albedo * (ambient.rgb + light_color.rgb * max(dot(N, L), 0))
// `light_color` already includes the light's intensity. A surface facing away from the light gets the ambient term
// only; one facing it directly gets ambient + light_color.
//
// Interface
//   input  location 0: vec3 world normal
//   input  location 1: vec2 uv
//   output location 0: vec4 color (attachment 0), alpha 1
//   set 0, binding 0: uniform buffer Params (same block as lit.vert)
//   set 0, binding 1: sampled texture (2D)
//   set 0, binding 2: sampler

layout(set = 0, binding = 0, std140) uniform Params {
    mat4 mvp;
    mat4 normal_matrix;
    vec4 to_light;
    vec4 light_color;
    vec4 ambient;
} params;
layout(set = 0, binding = 1) uniform texture2D tex;
layout(set = 0, binding = 2) uniform sampler smp;

layout(location = 0) in vec3 in_normal;
layout(location = 1) in vec2 in_uv;

layout(location = 0) out vec4 out_color;

void main() {
    vec3 n = normalize(in_normal);
    vec3 l = params.to_light.xyz;
    vec3 albedo = texture(sampler2D(tex, smp), in_uv).rgb;
    vec3 diffuse = params.light_color.rgb * max(dot(n, l), 0.0);
    out_color = vec4(albedo * (params.ambient.rgb + diffuse), 1.0);
}
