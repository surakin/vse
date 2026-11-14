#pragma once

//! CPU-side geometry ([[renderer::MeshData]]), its GPU copy ([[renderer::GpuMesh]]) and procedural shapes.

#include "containers/shared_bytes.h"
#include "containers/shared_ptr.h"
#include "containers/vector.h"
#include "renderer/bounds.h"
#include "renderer/device_resources.h"
#include "renderer/shader_reflection.h"
#include "rhi/rhi.h"
#include "types.h"

namespace engine_placeholder::renderer
{

/// The vertex the procedural meshes use: position, normal, tangent (xyz plus handedness in w) and one UV set. 48 bytes,
/// interleaved in one stream.
///
/// The bitangent is `cross(normal, tangent.xyz) * tangent.w`. UV (0, 0) is the corner where the texture starts, and V
/// grows along the bitangent.
///
/// - Thread-safety: thread-compatible plain data.
struct StandardVertex
{
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
/// - Returns: the layout.
/// - Fails: never.
/// - Thread-safety: thread-safe. A pure function.
VertexLayoutData StandardVertexLayout();

/// A [[renderer::StandardVertex]] with a per-vertex color: the same fields plus four bytes of RGBA, 52 bytes, in the
/// same interleaved stream.
///
/// The color is stored as authored sRGB bytes (not linear): the geometry shader converts it to linear light, so a color
/// picked in an image editor reads as expected. Alpha is linear coverage. A material that does not declare the
/// vertex-color permutation ignores the attribute; a shader that reads it needs a mesh that has it (the material system
/// reports a mismatch when it builds the pipeline).
///
/// - Thread-safety: thread-compatible plain data.
struct ColorVertex
{
    /// The standard attributes.
    StandardVertex base;
    /// Red, green, blue, alpha, 0 to 255.
    u8 color[4];
};
static_assert(sizeof(ColorVertex) == 52, "ColorVertex is 52 bytes");

/// The layout of [[renderer::ColorVertex]]: the four standard attributes of [[renderer::StandardVertexLayout]] plus the
/// color at location 4 (`UNorm8x4`, offset 48), all in vertex binding 0.
/// - Returns: the layout.
/// - Fails: never.
/// - Thread-safety: thread-safe. A pure function.
VertexLayoutData ColorVertexLayout();

/// The skinned form of a mesh layout: `base` plus the skin stream in vertex binding 1, joint indices (`UInt8x4`) at
/// location [[renderer::kSkinJointsLocation]] and weights (`UNorm8x4`) at [[renderer::kSkinWeightsLocation]], 8 bytes
/// per vertex (`skin_stride`). Rigid layouts are unchanged.
///
/// - Param base: a layout without a skin stream.
/// - Returns: the layout.
/// - Aborts: `base` already has a skin stream or has no room for two more attributes.
/// - Thread-safety: thread-safe. A pure function of its argument.
VertexLayoutData SkinnedVertexLayout(const VertexLayoutData& base);

/// The shader input location of the skin joint indices (vertex binding 1).
inline constexpr u32 kSkinJointsLocation = 5;
/// The shader input location of the skin weights (vertex binding 1).
inline constexpr u32 kSkinWeightsLocation = 6;
/// Bytes of one vertex of the skin stream.
inline constexpr u32 kSkinVertexBytes = 8;

namespace detail
{
struct MeshUploadView;  // what one upload reads (defined in mesh.cpp)
}  // namespace detail

/// Geometry read in place: what [[renderer::GpuMesh]] uploads, and uploads again after a device reset, without copying
/// a byte of it. A loaded mesh blob hands its sections over this way (the minter laid them out exactly as the buffers
/// hold them). The bytes are not pointed at: the geometry holds a share of the region they are in and the offsets of
/// the three sections, so the region lives as long as the mesh that uploads from it, whoever else lets go.
///
/// - Thread-safety: thread-compatible plain data (the shared region is immutable).
/// - Ownership: shares `bytes`.
struct MeshGeometry
{
    VertexLayoutData layout;             ///< The attributes (any vertex format the RHI has); `skin_stride` is 8 or 0.
    SharedPtr<const SharedBytes> bytes;  ///< The region the three sections are in.
    u64 vertex_offset = 0;               ///< Where the vertices start in `bytes`; `vertex_count * layout.stride` bytes.
    u32 vertex_count = 0;                ///< At least 1.
    rhi::IndexType index_type = rhi::IndexType::UInt16;                      ///< The width of the indices.
    u64 index_offset = 0;                                                    ///< Where the indices start in `bytes`.
    u32 index_count = 0;                                                     ///< At least 1; fits `topology`.
    rhi::PrimitiveTopology topology = rhi::PrimitiveTopology::TriangleList;  ///< How the indices form primitives.
    Bounds bounds;                                                           ///< The object-space bounds.
    u64 skin_offset = 0;       ///< Where the skin stream starts (`vertex_count * 8` bytes); skinned only.
    u32 skin_joint_count = 0;  ///< The skeleton's joint count (1 to 256) for a skinned layout, else 0.

    /// Checks the sections against the region and the counts against each other (not the values in the bytes).
    ///
    /// - Returns: null when the geometry can be uploaded, otherwise a static message.
    /// - Fails: no region, a section outside it, a zero count or stride, an index count that does not fit the topology,
    ///   a topology the renderer does not draw, a skinned layout without a joint count of 1 to 256 or with a stride
    ///   other than 8, or a joint count with a rigid layout.
    /// - Ownership: reads the shared bytes only to learn their size; the message is static.
    const char* Validate() const;
};

/// Geometry on the CPU: interleaved vertex bytes, indices and bounds. Owns its memory.
///
/// A mesh is built once with [[renderer::MeshData::Set]] (or by one of the `Make...Mesh` functions) and is read-only
/// afterwards.
///
/// - Thread-safety: thread-compatible. Move-only.
/// - Ownership: owns copies of the vertex and index data it was given.
class MeshData
{
public:
    MeshData() = default;
    /// Frees the copied geometry.
    ~MeshData();
    /// Takes the geometry from `other`, which is left empty.
    MeshData(MeshData&& other) noexcept;
    /// Frees this mesh's geometry, then takes it from `other`, which is left empty.
    MeshData& operator=(MeshData&& other) noexcept;
    MeshData(const MeshData&) = delete;
    MeshData& operator=(const MeshData&) = delete;

    /// Replaces the contents with copies of the given geometry and computes its bounds.
    ///
    /// - Param layout: the vertex attributes; `layout.stride` is the byte size of one vertex.
    /// - Param position_offset: byte offset of the three-float position inside a vertex, used for the bounds.
    /// - Param vertices: `vertex_count * layout.stride` bytes.
    /// - Param vertex_count: number of vertices, at least 1.
    /// - Param index_type: 16-bit or 32-bit indices.
    /// - Param indices: `index_count` indices of `index_type`.
    /// - Param index_count: number of indices; for a list topology a positive multiple of the primitive size (3 for
    ///   `TriangleList`, 2 for `LineList`), for a strip at least 3 (`TriangleStrip`) or 2 (`LineStrip`).
    /// - Param topology: how the indices form primitives; defaults to a triangle list. In the strip topologies an index
    /// with
    ///   every bit set (0xFFFF or 0xFFFFFFFF) is a restart marker, not a vertex: it ends the strip and starts the next
    ///   one (see [[renderer::MeshData::HasPrimitiveRestart]]).
    /// - Returns: null on success, otherwise a static message naming the problem (this mesh is then empty).
    /// - Fails: a zero or oversized stride, a stride or position offset that is not a multiple of 4, a position outside
    /// the vertex, no vertices or indices, an index count that does not fit the
    ///   topology, 16-bit indices with more than 65536 vertices (65535 for a strip, whose last index value is the
    ///   restart marker), an index at or past `vertex_count`, or a non-finite position.
    /// - Ownership: `vertices` and `indices` are borrowed for the call and copied.
    const char* Set(const VertexLayoutData& layout, u32 position_offset, const void* vertices, u32 vertex_count,
                    rhi::IndexType index_type, const void* indices, u32 index_count,
                    rhi::PrimitiveTopology topology = rhi::PrimitiveTopology::TriangleList);

    /// Adds the skin stream of a skinned mesh: eight bytes per vertex, joint indices (`UInt8x4`) then weights
    /// (`UNorm8x4`), the layout of the mesh blob's skin section. The mesh's layout then carries `skin_stride` 8 and
    /// the two skin attributes ([[renderer::SkinnedVertexLayout]]). Call it after [[renderer::MeshData::Set]]; a later
    /// `Set` drops the skin stream.
    ///
    /// - Param skin: `VertexCount() * 8` bytes, copied.
    /// - Param joint_count: the skeleton's joint count; every index of a vertex with a nonzero weight must be below it.
    /// - Returns: null, or a message (the mesh is empty or already skinned, a weight does not sum to 255, a weighted
    ///   joint is out of range, out of memory); on failure the mesh is unchanged.
    /// - Fails: reported through the return value.
    /// - Ownership: the bytes are copied.
    /// - Thread-safety: thread-compatible.
    const char* SetSkin(const void* skin, u32 joint_count);

    /// Returns whether the mesh has a skin stream.
    /// - Fails: never.
    bool Skinned() const { return layout_.skin_stride != 0; }
    /// Returns the skin stream (`VertexCount() * 8` bytes), or null for a rigid mesh.
    /// - Fails: never.
    /// - Ownership: the bytes belong to the mesh.
    const void* SkinBytes() const { return skin_.Data(); }
    /// Returns the size of the skin stream in bytes (0 for a rigid mesh).
    /// - Fails: never.
    u64 SkinByteCount() const { return skin_.Size(); }
    /// Returns the joint count given to `SetSkin` (0 for a rigid mesh): a palette drawn with the mesh needs at least
    /// this many matrices.
    /// - Fails: never.
    u32 SkinJointCount() const { return skin_joint_count_; }

    /// Returns whether the mesh holds geometry.
    /// - Fails: never.
    bool Valid() const { return vertex_count_ != 0; }
    /// Returns the vertex layout.
    const VertexLayoutData& Layout() const { return layout_; }
    /// Returns the vertex bytes (`VertexBytes()` of them), or null if empty.
    /// - Fails: never.
    /// - Ownership: borrowed; valid until the mesh is changed, moved from or destroyed.
    const void* Vertices() const { return vertices_.Data(); }
    /// Returns the number of vertices.
    u32 VertexCount() const { return vertex_count_; }
    /// Returns the size of the vertex data in bytes.
    u64 VertexBytes() const { return static_cast<u64>(vertex_count_) * layout_.stride; }
    /// Returns the index bytes (`IndexBytes()` of them), or null if empty.
    /// - Fails: never.
    /// - Ownership: borrowed; valid until the mesh is changed, moved from or destroyed.
    const void* Indices() const { return indices_.Data(); }
    /// Returns the number of indices.
    u32 IndexCount() const { return index_count_; }
    /// Returns the width of the indices.
    rhi::IndexType IndexFormat() const { return index_type_; }
    /// Returns the size of the index data in bytes.
    u64 IndexBytes() const { return static_cast<u64>(index_count_) * (index_type_ == rhi::IndexType::UInt16 ? 2 : 4); }
    /// Returns the object-space bounds.
    const Bounds& GetBounds() const { return bounds_; }
    /// Returns how the indices form primitives.
    /// - Fails: never.
    rhi::PrimitiveTopology Topology() const { return topology_; }
    /// Returns whether a strip mesh contains restart markers; a pipeline that draws it must enable
    /// `GraphicsPipelineDesc::primitive_restart`. Always false for a non-strip topology.
    /// - Fails: never.
    bool HasPrimitiveRestart() const { return has_restart_; }

    /// Returns the byte offset of the three-float position inside a vertex, as given to `Set`.
    /// - Fails: never.
    u32 PositionOffset() const { return position_offset_; }
    /// Returns the index of triangle corner `i` (0 to `IndexCount() - 1`), whatever the index width.
    /// - Aborts: `i` is out of range.
    u32 IndexAt(u32 i) const;
    /// Returns a pointer to the position of vertex `i`.
    /// - Aborts: `i` is out of range.
    /// - Ownership: borrowed; valid until the mesh is changed, moved from or destroyed.
    const f32* PositionAt(u32 i) const;

private:
    void Release();

    VertexLayoutData layout_;
    u32 position_offset_ = 0;
    Vector<byte> vertices_;
    Vector<byte> skin_;
    u32 skin_joint_count_ = 0;
    u32 vertex_count_ = 0;
    Vector<byte> indices_;
    u32 index_count_ = 0;
    rhi::IndexType index_type_ = rhi::IndexType::UInt16;
    rhi::PrimitiveTopology topology_ = rhi::PrimitiveTopology::TriangleList;
    bool has_restart_ = false;
    Bounds bounds_;
};

/// Builds an axis-aligned cube centered on the origin: 24 vertices (4 per face) and 36 16-bit indices, outward normals,
/// counter-clockwise faces, UV (0, 0) to (1, 1) on every face.
///
/// - Param edge: edge length, greater than zero.
/// - Param out: receives the mesh; must not be null.
/// - Returns: null on success, otherwise a static message.
/// - Ownership: `out` is borrowed for the call; the message is static.
/// - Fails: `edge` is not positive and finite.
/// - Aborts: `out` is null.
/// - Thread-safety: thread-safe.
const char* MakeCubeMesh(f32 edge, MeshData* out);

/// Builds a UV sphere centered on the origin, Y up: `(rings + 1) * (segments + 1)` vertices (the seam and the poles are
/// duplicated for UVs), outward normals, counter-clockwise triangles, no degenerate triangles at the poles.
///
/// - Param radius: greater than zero.
/// - Param segments: slices around the Y axis, at least 3.
/// - Param rings: bands from pole to pole, at least 2.
/// - Param out: receives the mesh; must not be null.
/// - Returns: null on success, otherwise a static message.
/// - Ownership: `out` is borrowed for the call; the message is static.
/// - Fails: an argument out of range, or more vertices than 16-bit indices can address (then 32-bit indices are used,
/// so
///   this only fails for absurd sizes).
/// - Aborts: `out` is null.
/// - Thread-safety: thread-safe.
const char* MakeSphereMesh(f32 radius, u32 segments, u32 rings, MeshData* out);

/// Builds a flat grid in the XZ plane centered on the origin, normal +Y, tangent +X, UV (0, 0) at (-x, -z) to (1, 1) at
/// (+x, +z); counter-clockwise seen from above.
///
/// - Param width: size along X, greater than zero.
/// - Param depth: size along Z, greater than zero.
/// - Param x_segments: cells along X, at least 1.
/// - Param z_segments: cells along Z, at least 1.
/// - Param out: receives the mesh; must not be null.
/// - Returns: null on success, otherwise a static message.
/// - Ownership: `out` is borrowed for the call; the message is static.
/// - Fails: an argument out of range.
/// - Aborts: `out` is null.
/// - Thread-safety: thread-safe.
const char* MakePlaneMesh(f32 width, f32 depth, u32 x_segments, u32 z_segments, MeshData* out);

/// Builds the same grid as [[renderer::MakePlaneMesh]] with the same vertices, but as triangle strips: one strip per
/// row of cells, ended by a restart index, so the index count is about two thirds of the list version's. The triangles
/// are the same set with the same winding (counter-clockwise seen from above), so the mesh draws identically; its
/// topology is `TriangleStrip` and it has restart markers, so it needs a pipeline with `primitive_restart`.
/// - Param width: size along X, greater than zero.
/// - Param depth: size along Z, greater than zero.
/// - Param x_segments: cells along X, at least 1.
/// - Param z_segments: cells along Z, at least 1.
/// - Param out: receives the mesh; must not be null.
/// - Returns: null on success, otherwise a static message.
/// - Ownership: `out` is borrowed for the call; the message is static.
/// - Fails: an argument out of range, or more than 65535 vertices (16-bit strip indices reserve the last value).
/// - Aborts: `out` is null.
/// - Thread-safety: thread-safe.
const char* MakePlaneStripMesh(f32 width, f32 depth, u32 x_segments, u32 z_segments, MeshData* out);

/// Copies a mesh built with [[renderer::StandardVertexLayout]] into one with [[renderer::ColorVertexLayout]], giving
/// each vertex its own color.
/// - Param source: a valid mesh whose layout is the standard one.
/// - Param colors: `color_count` RGBA colors, four bytes each (sRGB bytes, see [[renderer::ColorVertex]]).
/// - Param color_count: must equal the source's vertex count.
/// - Param out: receives the colored mesh, which keeps the source's indices, topology and bounds; must not be null and
/// not
///   be `source`.
/// - Returns: null on success, otherwise a static message.
/// - Fails: the source is empty or not in the standard layout, or `color_count` differs from its vertex count.
/// - Ownership: `colors` is borrowed for the call and copied; `out` is borrowed for the call.
/// - Aborts: `out` is null or is `source`.
/// - Thread-safety: thread-safe.
const char* MakeColoredMesh(const MeshData& source, const u8* colors, u32 color_count, MeshData* out);

/// Like [[renderer::MakeColoredMesh]] with one color for every vertex.
/// - Param source: a valid mesh whose layout is the standard one.
/// - Param rgba: the color, four bytes.
/// - Param out: receives the colored mesh.
/// - Returns: null on success, otherwise a static message.
/// - Fails: as for [[renderer::MakeColoredMesh]].
/// - Ownership: `rgba` is borrowed for the call.
/// - Aborts: `out` is null or is `source`.
/// - Thread-safety: thread-safe.
const char* MakeUniformColoredMesh(const MeshData& source, const u8 rgba[4], MeshData* out);

/// How [[renderer::ListToStrip]] joins strips that cannot be continued.
enum class StripJoin : u8
{
    Restart = 0,  ///< Put a restart index between strips; the result needs a pipeline with `primitive_restart`.
    Degenerate,   ///< Join with repeated vertices (degenerate triangles); works on a plain strip pipeline.
};

/// Converts a triangle-strip mesh to a triangle list: the same vertices, one triangle per strip position, restart
/// markers honored, degenerate triangles (two equal indices) dropped, winding alternation undone so every triangle
/// keeps the orientation it is drawn with.
/// - Param strip: a valid mesh with topology `TriangleStrip`.
/// - Param out: receives the list mesh; must not be null and not be `strip`.
/// - Returns: null on success, otherwise a static message.
/// - Fails: the mesh is empty or not a triangle strip, or the strip holds no triangle.
/// - Ownership: `out` is borrowed for the call.
/// - Aborts: `out` is null or is `strip`.
/// - Thread-safety: thread-safe.
const char* StripToList(const MeshData& strip, MeshData* out);

/// Converts a triangle-list mesh to triangle strips by greedy edge walking: each strip is extended through the edge
/// shared with an unused neighbor of the right orientation until none is left, then the next strip starts, joined as
/// `join` says. The set of triangles, and every triangle's winding, is preserved; the number of indices is usually well
/// below the list's (three per triangle) and never above six per triangle (a lone triangle joined by degenerate
/// vertices).
/// - Param list: a valid mesh with topology `TriangleList`.
/// - Param join: how to join the strips.
/// - Param out: receives the strip mesh (topology `TriangleStrip`); must not be null and not be `list`.
/// - Returns: null on success, otherwise a static message.
/// - Fails: the mesh is empty or not a triangle list. A list with exactly 65536 vertices gets 32-bit strip indices,
/// because
///   0xFFFF is the restart marker.
/// - Ownership: `out` is borrowed for the call.
/// - Aborts: `out` is null or is the source list.
/// - Thread-safety: thread-safe.
const char* ListToStrip(const MeshData& list, StripJoin join, MeshData* out);

/// A mesh on the GPU: a vertex buffer and an index buffer with the draw information.
///
/// `Create` uploads through the device's staging ring and leaves the buffers in the `CopyDst` state; record
/// [[renderer::GpuMesh::RecordPrepare]] in every command list that draws it until a list containing it has been
/// submitted and [[renderer::GpuMesh::MarkReady]] was called, so the buffers move to their read states once.
///
/// - Thread-safety: thread-compatible. Move-only.
/// - Ownership: owns its two buffers; the device must outlive it and be idle (or at least done with the buffers'
/// frames)
///   for a clean exit. Destruction goes through the device's deferred destruction.
class GpuMesh final : public Recreatable
{
public:
    GpuMesh() = default;
    /// Destroys the buffers, as [[renderer::GpuMesh::Destroy]] does.
    ~GpuMesh() override { Destroy(); }
    /// Takes the buffers from `other`, which is left empty.
    GpuMesh(GpuMesh&& other) noexcept;
    /// Destroys this mesh's buffers, then takes them from `other`, which is left empty.
    GpuMesh& operator=(GpuMesh&& other) noexcept;
    GpuMesh(const GpuMesh&) = delete;
    GpuMesh& operator=(const GpuMesh&) = delete;

    /// Creates the buffers and uploads the geometry.
    ///
    /// - Param device: the device; must outlive this object.
    /// - Param data: the geometry; must be valid ([[renderer::MeshData::Valid]]).
    /// - Param debug_name: a label for the buffers; may be null.
    /// - Returns: true on success; on failure nothing stays allocated and the failure was logged.
    /// - Fails: the device cannot allocate or upload (out of memory, lost device).
    /// - Aborts: this mesh already holds buffers, or `data` is empty.
    /// - Ownership: `data` is read during the call only.
    bool Create(rhi::Device* device, const MeshData& data, const char* debug_name);

    /// Destroys the buffers through the device and forgets the source and the registration. Safe on an empty mesh and
    /// called by the destructor.
    void Destroy();

    /// How a mesh gets its geometry again after a device reset.
    ///
    /// - Thread-safety: thread-compatible plain data.
    /// - Ownership: `borrowed` is not owned; `load` and `user` are borrowed.
    struct Source
    {
        /// Geometry the caller keeps alive for as long as the mesh is registered, or null.
        const MeshData* borrowed = nullptr;
        /// Or a function that loads the geometry into `*out` (called at every recreate), or null.
        bool (*load)(void* user, MeshData* out) = nullptr;
        /// Passed to `load`.
        void* user = nullptr;
    };

    /// Creates the buffers from geometry read in place and keeps the geometry (and with it a share of its bytes) as the
    /// source for a device reset: the bytes are uploaded from where they are, now and after every reset, and the mesh
    /// registers with `resources` (a mesh without a registry does not survive a reset).
    ///
    /// - Param device: the device; must outlive this object.
    /// - Param geometry: the geometry; valid ([[renderer::MeshGeometry::Validate]]).
    /// - Param resources: the registry, or null.
    /// - Param debug_name: a label for the buffers; copied, may be null.
    /// - Returns: true on success; on failure nothing stays allocated and the failure was logged.
    /// - Fails: invalid geometry, or the device cannot allocate or upload.
    /// - Aborts: this mesh already holds buffers.
    /// - Ownership: shares the bytes `geometry` holds; they live as long as this mesh does.
    bool CreateFromGeometry(rhi::Device* device, const MeshGeometry& geometry, DeviceResources* resources,
                            const char* debug_name);

    /// Makes this mesh survive a device reset: records where the geometry comes from and registers with `resources`.
    ///
    /// After this, `Release` and `Recreate` work and the registry calls them on every reset. Call after a successful
    /// `Create`.
    ///
    /// - Param resources: the registry; null records the source but registers nothing.
    /// - Param source: where to get the geometry again; `borrowed` or `load` must be set.
    /// - Param debug_name: a label for the rebuilt buffers; copied, may be null.
    /// - Returns: true if registered (or `resources` is null).
    /// - Fails: returns false if the registry refused; the mesh is then still valid but will not survive a reset.
    /// - Aborts: the mesh is empty, or `source` names nothing.
    /// - Ownership: the registry and the source are borrowed.
    bool MakeRecreatable(DeviceResources* resources, const Source& source, const char* debug_name);

    /// As [[renderer::GpuMesh::MakeRecreatable]], keeping the geometry itself: `data` is moved into the mesh.
    ///
    /// - Param resources: the registry; may be null.
    /// - Param data: the geometry; must be what the mesh was created from.
    /// - Param debug_name: a label for the rebuilt buffers; copied, may be null.
    /// - Returns: true if registered (or `resources` is null).
    /// - Aborts: the mesh is empty or `data` is empty.
    /// - Ownership: borrowed for the call; nothing is kept unless the description above says so.
    bool AdoptSource(DeviceResources* resources, MeshData&& data, const char* debug_name);

    /// Destroys the buffers and keeps everything else (the source, the bounds, the sort id). The mesh reads as not
    /// `Valid` until `Recreate`. Safe on a released mesh.
    void Release() override;

    /// Uploads the geometry again on `device`; the buffer handles change, the sort id does not.
    ///
    /// - Param device: the new device.
    /// - Returns: true on success.
    /// - Fails: returns false, logging the reason, if the mesh has no source, the loader fails, or the upload fails.
    /// - Aborts: the mesh already holds buffers.
    /// - Ownership: borrowed for the call; nothing is kept unless the description above says so.
    bool Recreate(rhi::Device* device) override;

    /// Returns the debug name for logs.
    /// - Fails: never.
    /// - Ownership: the returned pointer is borrowed from the owner.
    bool Live() const override { return vertex_buffer_.IsValid(); }
    const char* RecreatableName() const override { return name_; }

    /// Returns whether the buffers exist.
    /// - Fails: never.
    bool Valid() const { return vertex_buffer_.IsValid(); }
    /// Returns the vertex buffer (binding 0).
    rhi::BufferHandle VertexBuffer() const { return vertex_buffer_; }
    /// Returns the index buffer.
    rhi::BufferHandle IndexBuffer() const { return index_buffer_; }
    /// Returns the skin stream (vertex binding 1), or an invalid handle for a rigid mesh.
    rhi::BufferHandle SkinBuffer() const { return skin_buffer_; }
    /// Returns the joint count of the skin stream (0 for a rigid mesh): a palette drawn with the mesh needs at least
    /// this many matrices.
    /// - Fails: never.
    u32 SkinJointCount() const { return skin_joint_count_; }
    /// Returns whether the mesh has a skin stream.
    /// - Fails: never.
    bool Skinned() const { return layout_.skin_stride != 0; }
    /// Returns the number of indices.
    u32 IndexCount() const { return index_count_; }
    /// Returns the number of vertices.
    u32 VertexCount() const { return vertex_count_; }
    /// Returns the width of the indices.
    rhi::IndexType IndexFormat() const { return index_type_; }
    /// Returns the vertex layout the buffer was filled with.
    const VertexLayoutData& Layout() const { return layout_; }
    /// Returns how the indices form primitives; the pipeline that draws this mesh must use the same topology.
    /// - Fails: never.
    rhi::PrimitiveTopology Topology() const { return topology_; }
    /// Returns whether the indices contain restart markers (see [[renderer::MeshData::HasPrimitiveRestart]]).
    /// - Fails: never.
    bool HasPrimitiveRestart() const { return has_restart_; }
    /// Returns the object-space bounds.
    const Bounds& GetBounds() const { return bounds_; }
    /// Returns a number from 1 to 2^24 - 1 that differs between meshes created one after another, for
    /// [[renderer::MakeDrawSortKey]]. It repeats only after 16 million meshes were created. 0 for an empty mesh.
    /// - Fails: never.
    u32 SortId() const { return sort_id_; }

    /// Records the barriers that move the buffers out of their upload state; does nothing once
    /// [[renderer::GpuMesh::MarkReady]] was called. Call outside a render pass.
    ///
    /// - Param list: the command list being recorded.
    /// - Ownership: `list` is borrowed for the call.
    /// - Aborts: this mesh is empty, or `list` is not recording.
    void RecordPrepare(rhi::CommandList* list);

    /// Says that a command list containing the prepare barriers has been submitted.
    void MarkReady() { ready_ = true; }

    /// Returns whether [[renderer::GpuMesh::MarkReady]] was called.
    /// - Fails: never.
    bool Ready() const { return ready_; }

    /// Binds the buffers and draws all indices for `instance_count` instances starting at `first_instance`. Call inside
    /// a pass, with a pipeline whose vertex layout matches [[renderer::GpuMesh::Layout]] bound. The prepare barriers
    /// must have been recorded, in this list before the pass or in an earlier submitted one.
    ///
    /// - Param list: the command list being recorded.
    /// - Param instance_count: number of instances, at least 1.
    /// - Param first_instance: the first instance index (the per-object data index, see [[renderer::FrameResources]]).
    /// - Ownership: `list` is borrowed for the call.
    /// - Aborts: this mesh is empty, or as for [[rhi::CommandList::DrawIndexed]].
    void Draw(rhi::CommandList* list, u32 instance_count, u32 first_instance) const;

private:
    bool Upload(rhi::Device* device, const detail::MeshUploadView& data, const char* debug_name, rhi::BufferHandle* vb,
                rhi::BufferHandle* ib, rhi::BufferHandle* sb);
    void MoveFrom(GpuMesh& other);

    Source source_;
    MeshGeometry geometry_;  // the in-place source of CreateFromGeometry
    bool has_geometry_ = false;
    MeshData owned_;
    char name_[48] = "mesh";
    bool recreatable_ = false;
    bool was_ready_ = false;  // the buffers were in their read states when released; Recreate restores that
    ScopedRegistration registration_;
    rhi::Device* device_ = nullptr;
    rhi::BufferHandle vertex_buffer_;
    rhi::BufferHandle index_buffer_;
    rhi::BufferHandle skin_buffer_;
    u32 skin_joint_count_ = 0;
    u32 vertex_count_ = 0;
    u32 index_count_ = 0;
    rhi::IndexType index_type_ = rhi::IndexType::UInt16;
    rhi::PrimitiveTopology topology_ = rhi::PrimitiveTopology::TriangleList;
    bool has_restart_ = false;
    VertexLayoutData layout_;
    Bounds bounds_;
    u32 sort_id_ = 0;
    bool prepared_ = false;  // RecordPrepare was called, in a list that may not be submitted yet
    bool ready_ = false;
};

}  // namespace engine_placeholder::renderer
