// Mass properties of the shapes (Phase 25 Day 375): volume, mass, centre of mass and the inertia tensor in closed form.
#pragma once

#include "math/matrix.h"
#include "math/quaternion.h"
#include "math/vector.h"
#include "physics/hull.h"
#include "physics/shapes.h"
#include "types.h"

namespace engine_placeholder::physics
{

/// The mass of a body and how it resists rotation, in the body's own space.
///
/// The inertia tensor is stored as its diagonal in the **principal frame** plus the rotation from that frame to the
/// body's space, so the world tensor is two matrix products. Every shape of Day 375 has its principal axes along its
/// own axes (`principal_rotation` is the identity); a compound shape or a hull from Days 381-383 may not.
///
/// - Thread-safety: thread-compatible plain data.
struct MassProperties
{
    /// The volume of the shape, in cubic metres.
    f32 volume = 0.0f;
    /// The mass, in kilograms: the density times the volume, or a given mass.
    f32 mass = 0.0f;
    /// The centre of mass, in the body's space.
    Vec3 center = Vec3(0.0f, 0.0f, 0.0f);
    /// The moments of inertia about the principal axes, through the centre of mass, in kilogram square metres.
    Vec3 inertia = Vec3(0.0f, 0.0f, 0.0f);
    /// The rotation from the principal frame to the body's space.
    Quaternion principal_rotation = Quaternion::Identity();

    /// Returns the inverse of the mass, or zero for a body without mass (a static or kinematic body).
    ///
    /// - Returns: `1 / mass`, or zero.
    /// - Fails: never.
    f32 InverseMass() const { return mass > 0.0f ? 1.0f / mass : 0.0f; }

    /// Returns the inverse inertia tensor in the body's space: `R * diag(1 / inertia) * R^T`.
    ///
    /// An axis with no inertia gets a zero entry (it does not rotate); `R` is the principal rotation.
    ///
    /// - Returns: the matrix.
    /// - Fails: never.
    Mat3 InverseInertiaBody() const;

    /// Returns the inverse inertia tensor in the world for a body with the given orientation: `Q * InverseInertiaBody()
    /// * Q^T`.
    ///
    /// - Param orientation: the body's orientation (a unit quaternion).
    /// - Returns: the matrix.
    /// - Fails: never.
    Mat3 InverseInertiaWorld(const Quaternion& orientation) const;
};

/// Computes the volume, mass, centre and inertia of a shape of uniform density.
///
/// - Param shape: the shape; must be valid.
/// - Param density: kilograms per cubic metre, above zero.
/// - Param hulls: the library a hull shape's id refers to; may be null when the shape is not a hull.
/// - Returns: the properties; for a hull the centre is the hull's centroid and the principal frame is the one of its
/// inertia tensor; all zero when the shape or the density is refused or a hull is not in the library.
/// - Ownership: `hulls` is borrowed for the call.
/// - Fails: a shape or density that is not valid gives all-zero properties (check `Validate()` first to know why).
MassProperties MassFromDensity(const Shape& shape, f32 density, const HullLibrary* hulls = nullptr);

/// Returns the volume of a shape.
///
/// - Param shape: the shape; must be valid.
/// - Param hulls: the library a hull shape's id refers to; may be null when the shape is not a hull.
/// - Returns: the volume, or zero for a shape that is not valid or a hull that is not in the library.
/// - Ownership: `hulls` is borrowed for the call.
/// - Fails: as the return says.
f32 ShapeVolume(const Shape& shape, const HullLibrary* hulls = nullptr);

/// Returns the inertia tensor about a point offset from the centre of mass, in the body's space.
///
/// It is the parallel axis theorem: `I + m (|d|^2 E - d d^T)`.
///
/// - Param properties: the properties of a shape.
/// - Param offset: the vector from the shape's centre of mass to the point.
/// - Returns: the inertia tensor about the point, in the body's space.
/// - Fails: never.
Mat3 InertiaAbout(const MassProperties& properties, const Vec3& offset);

}  // namespace engine_placeholder::physics
