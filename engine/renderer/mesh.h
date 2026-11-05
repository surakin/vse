#pragma once

//! CPU-side geometry ([[renderer::MeshData]]), its GPU copy ([[renderer::GpuMesh]]) and procedural shapes.

#include "renderer/bounds.h"
#include "renderer/shader_reflection.h"
#include "rhi/rhi.h"
#include "types.h"

namespace engine_placeholder::renderer {

/// The vertex the procedural meshes use: position, normal, tangent (xyz plus handedness in w) and one UV set. 48 bytes,
/// interleaved in one stream.
///
/// The bitangent is `cross(normal, tangent.xyz) * tangent.w`. UV (0, 0) is the corner where the texture starts, and V
/// grows along the bitangent.
///
/// Thread-safety: thread-compatible plain data.
struct StandardVertex {
    /// Object-space position.
    f32 position[3];
    /// Unit normal.
    f32 normal[3];
    /// Unit tangent in xyz, handedness (+1 or -1) in w.
    f32 tangent[4];
    /// Texture coordinates.
    f32 uv[2];
};
static_assert(sizeof(StandardVertex) == 48, "StandardVertex is 48 bytes");

/// The layout of [[renderer::StandardVertex]] as the shaders read it: locations 0 to 3 (position, normal, tangent, uv)
/// in vertex binding 0.
///
/// Returns: the layout.
/// Fails: never.
/// Thread-safety: thread-safe. A pure function.
VertexLayoutData StandardVertexLayout();

/// A [[renderer::StandardVertex]] with a per-vertex color: the same fields plus four bytes of RGBA, 52 bytes, in the same
/// interleaved stream.
///
/// The color is stored as authored sRGB bytes (not linear): the geometry shader converts it to linear light, so a color
/// picked in an image editor reads as expected. Alpha is linear coverage. A material that does not declare the vertex-color
/// permutation ignores the attribute; a shader that reads it needs a mesh that has it (the material system reports a
/// mismatch when it builds the pipeline).
///
/// Thread-safety: thread-compatible plain data.
struct ColorVertex {
    /// The standard attributes.
    StandardVertex base;
    /// Red, green, blue, alpha, 0 to 255.
    u8 color[4];
};
static_assert(sizeof(ColorVertex) == 52, "ColorVertex is 52 bytes");

/// The layout of [[renderer::ColorVertex]]: the four standard attributes of [[renderer::StandardVertexLayout]] plus the
/// color at location 4 (`UNorm8x4`, offset 48), all in vertex binding 0.
/// Returns: the layout.
/// Fails: never.
/// Thread-safety: thread-safe. A pure function.
VertexLayoutData ColorVertexLayout();

/// Geometry on the CPU: interleaved vertex bytes, indices and bounds. Owns its memory.
///
/// A mesh is built once with [[renderer::MeshData::Set]] (or by one of the `Make...Mesh` functions) and is read-only
/// afterwards.
///
/// Thread-safety: thread-compatible. Move-only.
/// Ownership: owns copies of the vertex and index data it was given.
class MeshData {
public:
    MeshData() = default;
    /// Frees the copied geometry.
    ~MeshData();
    /// Takes the geometry from `other`, which is left empty.
    MeshData(MeshData&& other);
    /// Frees this mesh's geometry, then takes it from `other`, which is left empty.
    MeshData& operator=(MeshData&& other);
    MeshData(const MeshData&) = delete;
    MeshData& operator=(const MeshData&) = delete;

    /// Replaces the contents with copies of the given geometry and computes its bounds.
    ///
    /// Param layout: the vertex attributes; `layout.stride` is the byte size of one vertex.
    /// Param position_offset: byte offset of the three-float position inside a vertex, used for the bounds.
    /// Param vertices: `vertex_count * layout.stride` bytes.
    /// Param vertex_count: number of vertices, at least 1.
    /// Param index_type: 16-bit or 32-bit indices.
    /// Param indices: `index_count` indices of `index_type`.
    /// Param index_count: number of indices; for a list topology a positive multiple of the primitive size (3 for
    ///   `TriangleList`, 2 for `LineList`), for a strip at least 3 (`TriangleStrip`) or 2 (`LineStrip`).
    /// Param topology: how the indices form primitives; defaults to a triangle list. In the strip topologies an index with
    ///   every bit set (0xFFFF or 0xFFFFFFFF) is a restart marker, not a vertex: it ends the strip and starts the next one
    ///   (see [[renderer::MeshData::HasPrimitiveRestart]]).
    /// Returns: null on success, otherwise a static message naming the problem (this mesh is then empty).
    /// Fails: a zero or oversized stride, a stride or position offset that is not a multiple of 4, a position outside the vertex, no vertices or indices, an index count that does not fit the
    ///   topology, 16-bit indices with more than 65536 vertices (65535 for a strip, whose last index value is the restart
    ///   marker), an index at or past `vertex_count`, or a non-finite position.
    /// Ownership: `vertices` and `indices` are borrowed for the call and copied.
    const char* Set(const VertexLayoutData& layout, u32 position_offset, const void* vertices, u32 vertex_count, rhi::IndexType index_type,
                    const void* indices, u32 index_count, rhi::PrimitiveTopology topology = rhi::PrimitiveTopology::TriangleList);

    /// Returns whether the mesh holds geometry.
    /// Fails: never.
    bool Valid() const { return vertex_count_ != 0; }
    /// Returns the vertex layout.
    const VertexLayoutData& Layout() const { return layout_; }
    /// Returns the vertex bytes (`VertexBytes()` of them), or null if empty.
    /// Fails: never.
    /// Ownership: borrowed; valid until the mesh is changed, moved from or destroyed.
    const void* Vertices() const { return vertices_; }
    /// Returns the number of vertices.
    u32 VertexCount() const { return vertex_count_; }
    /// Returns the size of the vertex data in bytes.
    u64 VertexBytes() const { return static_cast<u64>(vertex_count_) * layout_.stride; }
    /// Returns the index bytes (`IndexBytes()` of them), or null if empty.
    /// Fails: never.
    /// Ownership: borrowed; valid until the mesh is changed, moved from or destroyed.
    const void* Indices() const { return indices_; }
    /// Returns the number of indices.
    u32 IndexCount() const { return index_count_; }
    /// Returns the width of the indices.
    rhi::IndexType IndexFormat() const { return index_type_; }
    /// Returns the size of the index data in bytes.
    u64 IndexBytes() const { return static_cast<u64>(index_count_) * (index_type_ == rhi::IndexType::UInt16 ? 2 : 4); }
    /// Returns the object-space bounds.
    const Bounds& GetBounds() const { return bounds_; }
    /// Returns how the indices form primitives.
    /// Fails: never.
    rhi::PrimitiveTopology Topology() const { return topology_; }
    /// Returns whether a strip mesh contains restart markers; a pipeline that draws it must enable
    /// `GraphicsPipelineDesc::primitive_restart`. Always false for a non-strip topology.
    /// Fails: never.
    bool HasPrimitiveRestart() const { return has_restart_; }

    /// Returns the byte offset of the three-float position inside a vertex, as given to `Set`.
    /// Fails: never.
    u32 PositionOffset() const { return position_offset_; }
    /// Returns the index of triangle corner `i` (0 to `IndexCount() - 1`), whatever the index width.
    /// Aborts: `i` is out of range.
    u32 IndexAt(u32 i) const;
    /// Returns a pointer to the position of vertex `i`.
    /// Aborts: `i` is out of range.
    /// Ownership: borrowed; valid until the mesh is changed, moved from or destroyed.
    const f32* PositionAt(u32 i) const;

private:
    void Release();

    VertexLayoutData layout_;
    u32 position_offset_ = 0;
    unsigned char* vertices_ = nullptr;
    u32 vertex_count_ = 0;
    unsigned char* indices_ = nullptr;
    u32 index_count_ = 0;
    rhi::IndexType index_type_ = rhi::IndexType::UInt16;
    rhi::PrimitiveTopology topology_ = rhi::PrimitiveTopology::TriangleList;
    bool has_restart_ = false;
    Bounds bounds_;
};

/// Builds an axis-aligned cube centered on the origin: 24 vertices (4 per face) and 36 16-bit indices, outward normals,
/// counter-clockwise faces, UV (0, 0) to (1, 1) on every face.
///
/// Param edge: edge length, greater than zero.
/// Param out: receives the mesh; must not be null.
/// Returns: null on success, otherwise a static message.
/// Ownership: `out` is borrowed for the call; the message is static.
/// Fails: `edge` is not positive and finite.
/// Thread-safety: thread-safe.
const char* MakeCubeMesh(f32 edge, MeshData* out);

/// Builds a UV sphere centered on the origin, Y up: `(rings + 1) * (segments + 1)` vertices (the seam and the poles are
/// duplicated for UVs), outward normals, counter-clockwise triangles, no degenerate triangles at the poles.
///
/// Param radius: greater than zero.
/// Param segments: slices around the Y axis, at least 3.
/// Param rings: bands from pole to pole, at least 2.
/// Param out: receives the mesh; must not be null.
/// Returns: null on success, otherwise a static message.
/// Ownership: `out` is borrowed for the call; the message is static.
/// Fails: an argument out of range, or more vertices than 16-bit indices can address (then 32-bit indices are used, so
///   this only fails for absurd sizes).
/// Thread-safety: thread-safe.
const char* MakeSphereMesh(f32 radius, u32 segments, u32 rings, MeshData* out);

/// Builds a flat grid in the XZ plane centered on the origin, normal +Y, tangent +X, UV (0, 0) at (-x, -z) to (1, 1) at
/// (+x, +z); counter-clockwise seen from above.
///
/// Param width: size along X, greater than zero.
/// Param depth: size along Z, greater than zero.
/// Param x_segments: cells along X, at least 1.
/// Param z_segments: cells along Z, at least 1.
/// Param out: receives the mesh; must not be null.
/// Returns: null on success, otherwise a static message.
/// Ownership: `out` is borrowed for the call; the message is static.
/// Fails: an argument out of range.
/// Thread-safety: thread-safe.
const char* MakePlaneMesh(f32 width, f32 depth, u32 x_segments, u32 z_segments, MeshData* out);

/// Builds the same grid as [[renderer::MakePlaneMesh]] with the same vertices, but as triangle strips: one strip per row of
/// cells, ended by a restart index, so the index count is about two thirds of the list version's. The triangles are the
/// same set with the same winding (counter-clockwise seen from above), so the mesh draws identically; its topology is
/// `TriangleStrip` and it has restart markers, so it needs a pipeline with `primitive_restart`.
/// Param width: size along X, greater than zero.
/// Param depth: size along Z, greater than zero.
/// Param x_segments: cells along X, at least 1.
/// Param z_segments: cells along Z, at least 1.
/// Param out: receives the mesh; must not be null.
/// Returns: null on success, otherwise a static message.
/// Ownership: `out` is borrowed for the call; the message is static.
/// Fails: an argument out of range, or more than 65535 vertices (16-bit strip indices reserve the last value).
/// Thread-safety: thread-safe.
const char* MakePlaneStripMesh(f32 width, f32 depth, u32 x_segments, u32 z_segments, MeshData* out);

/// Copies a mesh built with [[renderer::StandardVertexLayout]] into one with [[renderer::ColorVertexLayout]], giving each
/// vertex its own color.
/// Param source: a valid mesh whose layout is the standard one.
/// Param colors: `color_count` RGBA colors, four bytes each (sRGB bytes, see [[renderer::ColorVertex]]).
/// Param color_count: must equal the source's vertex count.
/// Param out: receives the colored mesh, which keeps the source's indices, topology and bounds; must not be null and not
///   be `source`.
/// Returns: null on success, otherwise a static message.
/// Fails: the source is empty or not in the standard layout, or `color_count` differs from its vertex count.
/// Ownership: `colors` is borrowed for the call and copied; `out` is borrowed for the call.
/// Thread-safety: thread-safe.
const char* MakeColoredMesh(const MeshData& source, const u8* colors, u32 color_count, MeshData* out);

/// Like [[renderer::MakeColoredMesh]] with one color for every vertex.
/// Param source: a valid mesh whose layout is the standard one.
/// Param rgba: the color, four bytes.
/// Param out: receives the colored mesh.
/// Returns: null on success, otherwise a static message.
/// Fails: as for [[renderer::MakeColoredMesh]].
/// Ownership: `rgba` is borrowed for the call.
/// Thread-safety: thread-safe.
const char* MakeUniformColoredMesh(const MeshData& source, const u8 rgba[4], MeshData* out);

/// How [[renderer::ListToStrip]] joins strips that cannot be continued.
enum class StripJoin : u8 {
    Restart = 0, ///< Put a restart index between strips; the result needs a pipeline with `primitive_restart`.
    Degenerate,  ///< Join with repeated vertices (degenerate triangles); works on a plain strip pipeline.
};

/// Converts a triangle-strip mesh to a triangle list: the same vertices, one triangle per strip position, restart markers
/// honored, degenerate triangles (two equal indices) dropped, winding alternation undone so every triangle keeps the
/// orientation it is drawn with.
/// Param strip: a valid mesh with topology `TriangleStrip`.
/// Param out: receives the list mesh; must not be null and not be `strip`.
/// Returns: null on success, otherwise a static message.
/// Fails: the mesh is empty or not a triangle strip, or the strip holds no triangle.
/// Ownership: `out` is borrowed for the call.
/// Thread-safety: thread-safe.
const char* StripToList(const MeshData& strip, MeshData* out);

/// Converts a triangle-list mesh to triangle strips by greedy edge walking: each strip is extended through the edge shared
/// with an unused neighbor of the right orientation until none is left, then the next strip starts, joined as `join` says.
/// The set of triangles, and every triangle's winding, is preserved; the number of indices is usually well below the list's
/// (three per triangle) and never above six per triangle (a lone triangle joined by degenerate vertices).
/// Param list: a valid mesh with topology `TriangleList`.
/// Param join: how to join the strips.
/// Param out: receives the strip mesh (topology `TriangleStrip`); must not be null and not be `list`.
/// Returns: null on success, otherwise a static message.
/// Fails: the mesh is empty or not a triangle list. A list with exactly 65536 vertices gets 32-bit strip indices, because
///   0xFFFF is the restart marker.
/// Ownership: `out` is borrowed for the call.
/// Thread-safety: thread-safe.
const char* ListToStrip(const MeshData& list, StripJoin join, MeshData* out);

/// A mesh on the GPU: a vertex buffer and an index buffer with the draw information.
///
/// `Create` uploads through the device's staging ring and leaves the buffers in the `CopyDst` state; record
/// [[renderer::GpuMesh::RecordPrepare]] in every command list that draws it until a list containing it has been submitted
/// and [[renderer::GpuMesh::MarkReady]] was called, so the buffers move to their read states once.
///
/// Thread-safety: thread-compatible. Move-only.
/// Ownership: owns its two buffers; the device must outlive it and be idle (or at least done with the buffers' frames)
///   for a clean exit. Destruction goes through the device's deferred destruction.
class GpuMesh {
public:
    GpuMesh() = default;
    /// Destroys the buffers, as [[renderer::GpuMesh::Destroy]] does.
    ~GpuMesh() { Destroy(); }
    /// Takes the buffers from `other`, which is left empty.
    GpuMesh(GpuMesh&& other);
    /// Destroys this mesh's buffers, then takes them from `other`, which is left empty.
    GpuMesh& operator=(GpuMesh&& other);
    GpuMesh(const GpuMesh&) = delete;
    GpuMesh& operator=(const GpuMesh&) = delete;

    /// Creates the buffers and uploads the geometry.
    ///
    /// Param device: the device; must outlive this object.
    /// Param data: the geometry; must be valid ([[renderer::MeshData::Valid]]).
    /// Param debug_name: a label for the buffers; may be null.
    /// Returns: true on success; on failure nothing stays allocated and the failure was logged.
    /// Fails: the device cannot allocate or upload (out of memory, lost device).
    /// Aborts: this mesh already holds buffers, or `data` is empty.
    /// Ownership: `data` is read during the call only.
    bool Create(rhi::Device* device, const MeshData& data, const char* debug_name);

    /// Destroys the buffers through the device. Safe on an empty mesh and called by the destructor.
    void Destroy();

    /// Returns whether the buffers exist.
    /// Fails: never.
    bool Valid() const { return vertex_buffer_.IsValid(); }
    /// Returns the vertex buffer (binding 0).
    rhi::BufferHandle VertexBuffer() const { return vertex_buffer_; }
    /// Returns the index buffer.
    rhi::BufferHandle IndexBuffer() const { return index_buffer_; }
    /// Returns the number of indices.
    u32 IndexCount() const { return index_count_; }
    /// Returns the number of vertices.
    u32 VertexCount() const { return vertex_count_; }
    /// Returns the width of the indices.
    rhi::IndexType IndexFormat() const { return index_type_; }
    /// Returns the vertex layout the buffer was filled with.
    const VertexLayoutData& Layout() const { return layout_; }
    /// Returns how the indices form primitives; the pipeline that draws this mesh must use the same topology.
    /// Fails: never.
    rhi::PrimitiveTopology Topology() const { return topology_; }
    /// Returns whether the indices contain restart markers (see [[renderer::MeshData::HasPrimitiveRestart]]).
    /// Fails: never.
    bool HasPrimitiveRestart() const { return has_restart_; }
    /// Returns the object-space bounds.
    const Bounds& GetBounds() const { return bounds_; }

    /// Records the barriers that move the buffers out of their upload state; does nothing once [[renderer::GpuMesh::MarkReady]]
    /// was called. Call outside a render pass.
    ///
    /// Param list: the command list being recorded.
    /// Ownership: `list` is borrowed for the call.
    /// Aborts: this mesh is empty, or `list` is not recording.
    void RecordPrepare(rhi::CommandList* list);

    /// Says that a command list containing the prepare barriers has been submitted.
    void MarkReady() { ready_ = true; }

    /// Returns whether [[renderer::GpuMesh::MarkReady]] was called.
    /// Fails: never.
    bool Ready() const { return ready_; }

    /// Binds the buffers and draws all indices for `instance_count` instances starting at `first_instance`. Call inside a
    /// pass, with a pipeline whose vertex layout matches [[renderer::GpuMesh::Layout]] bound. The prepare barriers must
    /// have been recorded, in this list before the pass or in an earlier submitted one.
    ///
    /// Param list: the command list being recorded.
    /// Param instance_count: number of instances, at least 1.
    /// Param first_instance: the first instance index (the per-object data index, see [[renderer::FrameResources]]).
    /// Ownership: `list` is borrowed for the call.
    /// Aborts: this mesh is empty, or as for [[rhi::CommandList::DrawIndexed]].
    void Draw(rhi::CommandList* list, u32 instance_count, u32 first_instance) const;

private:
    rhi::Device* device_ = nullptr;
    rhi::BufferHandle vertex_buffer_;
    rhi::BufferHandle index_buffer_;
    u32 vertex_count_ = 0;
    u32 index_count_ = 0;
    rhi::IndexType index_type_ = rhi::IndexType::UInt16;
    rhi::PrimitiveTopology topology_ = rhi::PrimitiveTopology::TriangleList;
    bool has_restart_ = false;
    VertexLayoutData layout_;
    Bounds bounds_;
    bool prepared_ = false; // RecordPrepare was called, in a list that may not be submitted yet
    bool ready_ = false;
};

} // namespace engine_placeholder::renderer
