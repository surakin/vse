#pragma once

#include "algorithm.h"
#include "containers/vector.h"
#include "types.h"
#include "ui/geometry.h"

//! The 2D draw list (Phase 15, Day 189): the data [[ui::DrawListCanvas]] builds and the renderer draws. Vertices,
//! 32-bit indices and commands; no GPU type.
//!
//! Positions are physical pixels with the origin at the top-left of the surface. Every primitive is a quad (4 vertices,
//! 6 indices) except a triangle (3, 3); the fragment shader picks its work from the **mode** in the low bits of
//! `Vertex::flags`, so rectangles, glyphs and images of one frame share one pipeline and a command is split only by
//! clip, atlas page or image.

namespace engine_placeholder::ui
{

/// What the fragment shader does with a vertex' primitive (the low two bits of `Vertex::flags`).
inline constexpr u32 kModeSolid = 0;  ///< A plain colour: whole-pixel rectangles and triangles.
inline constexpr u32 kModeRect = 1;  ///< The signed distance to a rounded box, shaded with analytic anti-aliasing: `uv`
                                     ///< is the position from the box centre in pixels.
inline constexpr u32 kModeGlyph = 2;  ///< The colour times the coverage sampled from an atlas page (R8) at `uv`.
inline constexpr u32 kModeImage = 3;  ///< The colour (a tint) times the image sampled at `uv`.
inline constexpr u32 kModeMask = 3;   ///< Mask of the mode bits.
inline constexpr u32
    kFlagRing = 4;  ///< With `kModeRect`: only the ring of `border` pixels inside the edge is shaded (a border).
inline constexpr u32 kFlagShadow = 8;  ///< With `kModeRect`: the distance fades out over `border` pixels outside the
                                       ///< edge (a drop shadow).

/// One vertex, 40 bytes.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns nothing.
struct Vertex
{
    f32 x = 0;       ///< Position in physical pixels.
    f32 y = 0;       ///< Position in physical pixels.
    f32 u = 0;       ///< Texture coordinate (`kModeGlyph`, `kModeImage`), or the offset from the box centre in pixels
                     ///< (`kModeRect`).
    f32 v = 0;       ///< Second texture coordinate or offset.
    u32 color = 0;   ///< Packed RGBA, red in the lowest byte.
    f32 half_w = 0;  ///< `kModeRect`: half the width of the box in pixels.
    f32 half_h = 0;  ///< `kModeRect`: half the height of the box in pixels.
    f32 radius = 0;  ///< `kModeRect`: the corner radius at this vertex' corner, pixels.
    f32 border = 0;  ///< `kModeRect`: the ring width (`kFlagRing`) or the shadow blur (`kFlagShadow`), pixels.
    u32 flags = 0;   ///< A mode (`kModeSolid`...) and the flag bits.
};
static_assert(sizeof(Vertex) == 40, "the vertex layout is part of the renderer's contract (Spike B)");

/// What a draw command samples.
enum class TextureKind : u8
{
    None,   ///< No texture: the command's primitives are solid or rectangle-mode.
    Atlas,  ///< A glyph atlas page; `TextureRef::id` is the page.
    Image,  ///< An image; `TextureRef::id` is the [[ui::ImageId]].
};

/// A texture a command binds.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns nothing.
struct TextureRef
{
    TextureKind kind = TextureKind::None;  ///< Which kind of texture.
    u32 id = 0;                            ///< Atlas page or image id.

    /// Returns whether both name the same texture.
    bool operator==(const TextureRef& other) const { return kind == other.kind && id == other.id; }
};

/// How a command blends. Only one mode exists today.
enum class BlendMode : u8
{
    Alpha,  ///< Source over, with the colour premultiplied by the shader.
};

/// One draw call: a range of indices drawn with one clip rectangle and one texture.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns nothing.
struct DrawCommand
{
    u32 first_index = 0;  ///< Offset of the first index in `DrawList::indices`.
    u32 index_count = 0;  ///< Number of indices.
    PixelRect clip;       ///< The scissor rectangle in physical pixels.
    TextureRef texture;   ///< What the command samples (`None` when no primitive needs a texture).
    i32 layer = 0;        ///< The layer order the command was drawn in; `DrawList::SortLayers` orders by it.
    BlendMode blend = BlendMode::Alpha;  ///< How the command blends.
};

/// Counters of what a [[ui::DrawListCanvas]] did, for tests and the benchmark.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns nothing.
struct DrawListStats
{
    u64 culled = 0;      ///< Draws dropped before any vertex was made: outside the clip, empty, or fully transparent.
    u64 overflowed = 0;  ///< Draws dropped because the vertex count would pass `DrawList::max_vertices`.
    u64 glyphs_dropped = 0;       ///< Glyphs the atlas could not hold, or drawn with no atlas.
    u64 scaled_large_glyphs = 0;  ///< Glyphs of a size above [[text::kMaxAtlasPixelSize]], rasterised at the maximum
                                  ///< and scaled.
};

/// The vertices, indices and commands of one frame of UI.
///
/// `Clear` keeps the capacity, so a steady frame allocates nothing. Indices are 32-bit and relative to the whole vertex
/// array.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns its three arrays.
struct DrawList
{
    Vector<Vertex> vertices;           ///< All vertices, in draw order.
    Vector<u32> indices;               ///< Triangle indices into `vertices`.
    Vector<DrawCommand> commands;      ///< The draws, in the order to submit them once `SortLayers` has run.
    DrawListStats stats;               ///< Counters since the last `Clear`.
    usize max_vertices = 0xAAAAAAAAu;  ///< The most vertices the list holds; a draw past it is dropped and counted in
                                       ///< `stats.overflowed`. The default keeps both the vertex numbers and the index
                                       ///< count (1.5 per quad vertex) within 32 bits.

    /// Empties the list and its counters, keeping the capacity.
    ///
    /// - Fails: never.
    void Clear()
    {
        vertices.Clear();
        indices.Clear();
        commands.Clear();
        stats = DrawListStats();
    }

    /// Orders the commands by layer, keeping call order inside a layer: an already ordered list costs one pass, and
    /// any other is merge-sorted through `sort_scratch`, which keeps its capacity, so a frame allocates nothing once it
    /// has grown (container audit Day 6: it was an insertion sort, quadratic when a popup's commands come early). The
    /// index data stays where it is: a command names its own range.
    ///
    /// - Fails: never.
    void SortLayers()
    {
        StableSort(
            Span<DrawCommand>(commands.Data(), commands.Size()),
            [](const DrawCommand& a, const DrawCommand& b) { return a.layer < b.layer; }, sort_scratch);
    }

    /// The merge buffer of `SortLayers`, kept between frames; its contents mean nothing.
    Vector<DrawCommand> sort_scratch;
};

}  // namespace engine_placeholder::ui
