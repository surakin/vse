// gbuffer_common.slang: functions shared by the G-buffer shaders. Included, never compiled on its own.
//
// Octahedral normal encoding (unit vector <-> two floats in [-1, 1]): project onto the octahedron |x| + |y| + |z| = 1,
// fold the lower half (z < 0) over the diagonals.
//   encode(n): p = n.xy / (|n.x| + |n.y| + |n.z|); if n.z < 0: p = (1 - |p.yx|) * (p >= 0 ? 1 : -1)
//   decode(f): n = (f, 1 - |f.x| - |f.y|); if n.z < 0: n.xy = (1 - |n.yx|) * (n.xy >= 0 ? 1 : -1); normalize(n)
// The CPU reference is renderer::OctEncode / OctDecode (engine/renderer/gbuffer.cpp); a test checks they agree.
//
// sRGB to linear (per channel): c <= 0.04045 ? c / 12.92 : ((c + 0.055) / 1.055) ^ 2.4

float2 OctSignNotZero(float2 v) {
    return float2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0);
}

float2 OctEncode(float3 n) {
    float2 p = n.xy / (abs(n.x) + abs(n.y) + abs(n.z));
    if (n.z < 0.0) {
        p = (1.0 - abs(p.yx)) * OctSignNotZero(p);
    }
    return p;
}

float3 OctDecode(float2 f) {
    float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    if (n.z < 0.0) {
        n.xy = (1.0 - abs(n.yx)) * OctSignNotZero(n.xy);
    }
    return normalize(n);
}

float3 SrgbToLinear(float3 c) {
    float3 low = c / 12.92;
    float3 high = pow((c + 0.055) / 1.055, float3(2.4, 2.4, 2.4));
    return float3(c.x <= 0.04045 ? low.x : high.x, c.y <= 0.04045 ? low.y : high.y, c.z <= 0.04045 ? low.z : high.z);
}
