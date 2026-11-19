// lighting_common.slang: the surface lighting model shared by the lighting shaders. Included, never compiled on its own.
//
// The model (renderer spec R11): Lambert diffuse plus GGX specular in a metallic-roughness workflow, kept in this one
// function so a later phase can replace it. All quantities are linear.
//
// Conventions
//   N, V, L are unit vectors: surface normal, toward the viewer, toward the light.
//   radiance = light color * intensity. The diffuse BRDF is albedo (the 1/pi of a Lambertian surface is folded into the
//   intensity, so a light of intensity 1 on a white surface facing it gives 1), and the specular lobe is multiplied by pi to
//   match: the result for one light is (diffuse + specular) * radiance * max(dot(N, L), 0).
//
// Math
//   a        = max(roughness, 0.045)^2                      a2 = a * a
//   H        = normalize(V + L), or N when V + L is (nearly) zero
//   D        = a2 / (pi * (dot(N,H)^2 * (a2 - 1) + 1)^2)       Trowbridge-Reitz (GGX) distribution
//   Vis      = 0.5 / (NL * sqrt(NV^2 * (1 - a2) + a2) + NV * sqrt(NL^2 * (1 - a2) + a2))
//                                                            height-correlated Smith visibility, includes 1 / (4 NL NV)
//   F        = F0 + (1 - F0) * (1 - dot(V,H))^5              Schlick Fresnel, F0 = lerp(0.04, albedo, metallic)
//   diffuse  = albedo * (1 - metallic)
//   specular = pi * D * Vis * F
// The ambient term is albedo * (1 - metallic) * ambient * occlusion. Emission is not here: the G-buffer's emissive target
// holds it, and the fullscreen lighting pass adds it once (lighting.frag).
// The CPU twin is renderer::ShadeLight (engine/renderer/lighting.cpp); a test checks them against each other.
//
// Point lights (renderer spec R11)
//   attenuation(d, R) = saturate(1 - (d / R)^4)^2 / (d^2 + 1)        d = distance to the light, R = its radius
// It falls like an inverse square close to the light (the +1 keeps it finite at d = 0, where it is 1), is smoothly windowed to
// exactly 0 at d = R with zero slope there, and is 0 beyond, so the light's sphere of radius R is a correct bound.
// The radiance of a point light at a surface is color * intensity * attenuation; the direction is toward the light position.
// The CPU twin is renderer::PointLightAttenuation.

static const float kPi = 3.14159265358979323846;

struct Surface {
    float3 albedo;
    float roughness;
    float metallic;
    float occlusion;
    float3 normal;
};

float3 ShadeLight(Surface s, float3 V, float3 L, float3 radiance) {
    float nl = saturate(dot(s.normal, L));
    if (nl <= 0.0) {
        return float3(0.0, 0.0, 0.0);
    }
    float nv = max(dot(s.normal, V), 1e-4);
    // V and L can be opposite (a normal facing away from the viewer, lit from behind the viewer): fall back to N then.
    float3 vl = V + L;
    float3 H = dot(vl, vl) > 1e-10 ? normalize(vl) : s.normal;
    float nh = saturate(dot(s.normal, H));
    float vh = saturate(dot(V, H));
    float r = max(s.roughness, 0.045);
    float a = r * r;
    float a2 = a * a;
    float d_term = nh * nh * (a2 - 1.0) + 1.0;
    float D = a2 / (kPi * d_term * d_term);
    float Vis = 0.5 / (nl * sqrt(nv * nv * (1.0 - a2) + a2) + nv * sqrt(nl * nl * (1.0 - a2) + a2));
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), s.albedo, s.metallic);
    float3 F = F0 + (1.0 - F0) * pow(1.0 - vh, 5.0);
    float3 diffuse = s.albedo * (1.0 - s.metallic);
    float3 specular = kPi * D * Vis * F;
    return (diffuse + specular) * radiance * nl;
}

float3 ShadeAmbient(Surface s, float3 ambient) {
    return s.albedo * (1.0 - s.metallic) * ambient * s.occlusion;
}

float PointAttenuation(float distance_to_light, float radius) {
    float r = distance_to_light / radius;
    float window = saturate(1.0 - r * r * r * r);
    return window * window / (distance_to_light * distance_to_light + 1.0);
}

// Spot lights (glTF coverage Day 11; KHR_lights_punctual's form). The light data carries the spot's unit axis (from the
// surface toward the light) times the angle scale 1 / (cos(inner) - cos(outer)), and the offset -cos(outer) * scale:
//   factor = saturate(dot(L, axis) * scale + offset)^2     (1 inside the inner cone, 0 outside the outer one)
// A point light's axis is zero: the factor is 1. The CPU twin is renderer::SpotLightFactor.
float SpotFactor(float3 L, float3 scaled_axis, float offset) {
    float scale = length(scaled_axis);
    if (scale <= 0.0) {
        return 1.0;
    }
    float t = saturate(dot(L, scaled_axis / scale) * scale + offset);
    return t * t;
}
