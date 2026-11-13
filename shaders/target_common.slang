// What differs between the bytecode targets of one shader source (Phase 13 Day 163; shaders/README.md, "Targets").
//
// The SPIR-V build defines nothing and keeps Vulkan's conventions. The DXIL build passes -DENGINE_TARGET_D3D12=1 and the
// Metal build -DENGINE_TARGET_METAL=1 (Phase 21 Day 299), and these macros adapt the things they do differently (Metal
// as Direct3D 12 for the first three):
//
//   ENGINE_CLIP(p)               the clip-space position a vertex shader writes. Vulkan's clip space has Y down and the engine's
//                                projection (renderer::PerspectiveVulkan) already flips Y; Direct3D 12's has Y up, so the D3D12 build
//                                negates Y once more. Everything else (the CPU projection, scissor rectangles, the UV to NDC mapping
//                                in fragment shaders, SV_Position in pixels, front faces) stays the same on both.
//   ENGINE_VERTEX_ID             the vertex index of a non-indexed draw: gl_VertexIndex on Vulkan, SV_VertexID on D3D12 (equal for
//                                draws that start at vertex 0, the only ones the engine's vertex-pulling shaders make).
//   ENGINE_OBJECT_INDEX_STREAM   defined when the per-draw object index comes from an instance-rate vertex attribute at location 15
//                                (semantic TEXCOORD15), fed by renderer::ObjectIndexStream, because D3D12 shaders have no base
//                                instance. Otherwise the shader reads SV_VulkanInstanceID (gl_InstanceIndex: the draw's first
//                                instance plus the instance), and the renderer draws object i with first_instance = i.
//
//   ENGINE_REGISTER(r, s)        a resource's D3D12 register, `: register(r, s)` in the DXIL build and nothing in the others:
//                                Slang also takes register() as the Metal slot, and Metal has no spaces, so b0 in two spaces
//                                would be two parameters at [[buffer(0)]] (Phase 21 Day 282). Without it Slang gives each
//                                Metal resource its own slot, which shader_mint records in the reflection.
//
// Rules every engine shader follows so one source serves every target (shader_mint checks the first two when it mints DXIL):
//   * A vertex input at location N has the semantic TEXCOORD<N>; the D3D12 backend names input elements that way.
//   * A resource at [[vk::binding(b, g)]] is declared with ENGINE_REGISTER(<class><b>, space<g>): b for a constant buffer, t for a
//     texture or a StructuredBuffer, u for a RW buffer or storage texture, s for a sampler. Push constants are
//     ENGINE_REGISTER(b0, space15).

#if defined(ENGINE_TARGET_D3D12) || defined(ENGINE_TARGET_METAL)
#define ENGINE_CLIP(p) ((p) * float4(1.0, -1.0, 1.0, 1.0))
#define ENGINE_VERTEX_ID SV_VertexID
#define ENGINE_OBJECT_INDEX_STREAM 1
#else
#define ENGINE_CLIP(p) (p)
#define ENGINE_VERTEX_ID SV_VulkanVertexID
#endif

#ifdef ENGINE_TARGET_D3D12
#define ENGINE_REGISTER(r, s) : register(r, s)
#else
#define ENGINE_REGISTER(r, s)
#endif
