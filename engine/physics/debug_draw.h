// Drawing the simulation for debugging (Phase 25 Day 405): line segments for the shapes, the contacts, the joints, the
// islands and the broad phase. The module holds no renderer type: a program turns the lines into a line pass (the same
// `spatial::DebugLine` as the spatial module's drawing).
#pragma once

#include "containers/vector.h"
#include "physics/world.h"
#include "spatial/spatial_debug.h"
#include "types.h"

namespace vse::physics
{

/// Colors of the debug drawing (0xAABBGGRR).
inline constexpr u32 kDebugStaticColor = 0xFF909090u;      ///< A static body's shapes.
inline constexpr u32 kDebugKinematicColor = 0xFFD08030u;   ///< A kinematic body's shapes.
inline constexpr u32 kDebugSleepingColor = 0xFF505050u;    ///< A sleeping body's shapes.
inline constexpr u32 kDebugContactColor = 0xFF20E0FFu;     ///< A contact point and its normal.
inline constexpr u32 kDebugJointColor = 0xFFFF40FFu;       ///< A joint's frame and axis.
inline constexpr u32 kDebugBroadPhaseColor = 0xFF804020u;  ///< A collider's box.

/// What `DebugDraw` draws.
///
/// - Thread-safety: thread-compatible plain data.
struct DebugDrawOptions
{
    /// The outline of every collider (a hull, mesh, height field or compound is drawn as its local box).
    bool shapes = true;
    /// The points of the contacts of the last step, each with a short line along the normal.
    bool contacts = true;
    /// The two anchors of each joint, the line between them and a hinge's axis.
    bool joints = true;
    /// Colors a dynamic body by its island (a sleeping one is grey); off: every awake dynamic body has one color.
    bool islands = true;
    /// The box of every collider as the broad phase sees it.
    bool boxes = false;
    /// The cells of a height field under the boxes of the other colliders (a cell's triangle edges, at most 64
    /// triangles near each other collider), so the surface a body rests on is seen; the field's own outline is its box.
    bool field_cells = true;
    /// The length of a contact normal's line, in metres.
    f32 normal_length = 0.3f;
};

/// Appends the line segments that show the world as of the last step.
///
/// An outline of a sphere is three circles of 16 segments, a box 12 edges, a capsule two circles, four side lines and
/// four arcs of 8 segments. It reads the world and allocates only if `lines` must grow: a program that clears `lines`
/// (keeping its capacity) each frame allocates nothing after the first.
///
/// - Param world: the world.
/// - Param options: what to draw.
/// - Param lines: the vector the segments are appended to.
/// - Returns: the number of segments appended.
/// - Thread-safety: thread-compatible; not while the world steps.
/// - Ownership: `lines` belongs to the caller.
/// - Fails: never.
u32 DebugDraw(const PhysicsWorld& world, const DebugDrawOptions& options, Vector<spatial::DebugLine>* lines);

}  // namespace vse::physics
