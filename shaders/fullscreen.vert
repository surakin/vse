#version 450

// fullscreen.vert: one triangle that covers the viewport, from gl_VertexIndex alone (no vertex buffer).
//
// Interface
//   no vertex inputs; draw 3 vertices
//   vertex output location 0: vec2 uv (0..1 across the viewport, origin top-left)
//   no descriptors, no push constants

layout(location = 0) out vec2 out_uv;

void main() {
    vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    out_uv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
