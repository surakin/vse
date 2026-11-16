#include "physics/solver.h"

#include "algorithm.h"
#include "containers/span.h"
#include "lang/scalar.h"
#include "physics/world.h"

namespace engine_placeholder::physics
{

namespace
{

constexpr f32 kWarmDistance = 0.05f;

Vec3 Perpendicular(const Vec3& n)
{
    // The axis least aligned with n, crossed with it.
    const Vec3 axis = Abs(n.x) <= Abs(n.y) && Abs(n.x) <= Abs(n.z) ? Vec3(1.0f, 0.0f, 0.0f)
                      : Abs(n.y) <= Abs(n.z)                       ? Vec3(0.0f, 1.0f, 0.0f)
                                                                   : Vec3(0.0f, 0.0f, 1.0f);
    const Vec3 t = n.Cross(axis);
    return t * (1.0f / Sqrt(t.LengthSquared()));
}

}  // namespace

struct ContactSolver::Row
{
    Vec3 ra = Vec3(0.0f, 0.0f, 0.0f);  // anchors from the centres of mass, fixed for the step
    Vec3 rb = Vec3(0.0f, 0.0f, 0.0f);
    f32 normal_mass = 0.0f;
    f32 tangent_mass[2] = {0.0f, 0.0f};
    f32 relative_velocity = 0.0f;  // along the normal before the solve, for the restitution
    f32 normal_impulse = 0.0f;
    f32 max_normal_impulse = 0.0f;
    f32 tangent_impulse[2] = {0.0f, 0.0f};
    f32 depth = 0.0f;
    u32 feature = 0;
    Vec3 local_a = Vec3(0.0f, 0.0f, 0.0f);
};

// What a substep knows of a body: where it started and how far it has moved since.
struct ContactSolver::BodyState
{
    Body* body = nullptr;
    Quaternion q0 = Quaternion::Identity();
    Vec3 com0 = Vec3(0.0f, 0.0f, 0.0f);
    Vec3 dp = Vec3(0.0f, 0.0f, 0.0f);
    Quaternion dq = Quaternion::Identity();
    bool moves = false;
};

struct ContactSolver::Constraint
{
    ColliderId a;
    ColliderId b;
    u32 sa = 0;
    u32 sb = 0;
    Vec3 normal = Vec3(0.0f, 1.0f, 0.0f);
    Vec3 tangent[2];
    f32 friction = 0.0f;
    f32 restitution = 0.0f;
    u32 count = 0;
    f32 bias_rate = 0.0f;
    f32 mass_scale = 1.0f;
    f32 impulse_scale = 0.0f;
    Row rows[4];
};

namespace
{

// A joint of the step: its record and bodies, and where the hinge angle stood when the step began.
struct JointWork
{
    JointRecord* record = nullptr;
    Body* a = nullptr;
    Body* b = nullptr;
    f32 angle_start = 0.0f;
    f32 raw_start = 0.0f;
};

f32 InverseMassOf(const Body& b)
{
    return b.settings.type == BodyType::Dynamic ? b.inverse_mass : 0.0f;
}

Vec3 WorldInertia(const Body& b, const Vec3& v)
{
    return b.settings.type == BodyType::Dynamic ? b.inverse_inertia_world * v : Vec3(0.0f, 0.0f, 0.0f);
}

Vec3 VelocityAt(const Body& b, const Vec3& r)
{
    return b.linear_velocity + b.angular_velocity.Cross(r);
}

void Apply(Body* a, Body* b, const Vec3& ra, const Vec3& rb, const Vec3& impulse)
{
    if (a->settings.type == BodyType::Dynamic)
    {
        a->linear_velocity = a->linear_velocity - impulse * a->inverse_mass;
        a->angular_velocity = a->angular_velocity - WorldInertia(*a, ra.Cross(impulse));
    }
    if (b->settings.type == BodyType::Dynamic)
    {
        b->linear_velocity = b->linear_velocity + impulse * b->inverse_mass;
        b->angular_velocity = b->angular_velocity + WorldInertia(*b, rb.Cross(impulse));
    }
}

// 1 / (m_a^-1 + m_b^-1 + the angular terms along a direction).
f32 EffectiveMass(const Body& a, const Body& b, const Vec3& ra, const Vec3& rb, const Vec3& d)
{
    const Vec3 ca = ra.Cross(d);
    const Vec3 cb = rb.Cross(d);
    const f32 k = InverseMassOf(a) + InverseMassOf(b) + ca.Dot(WorldInertia(a, ca)) + cb.Dot(WorldInertia(b, cb));
    return k > 1e-12f ? 1.0f / k : 0.0f;
}

Vec3 CenterOfMass(const Body& b)
{
    return b.position + b.orientation.ToMat3() * b.mass.center;
}

// The soft constraint of a spring of `hertz` and `ratio` solved over `h` (Box2D's formulation: the constraint reacts
// as a damped spring, and the same numbers work for any stiffness without a stability limit).
void MakeSoft(f32 hertz, f32 ratio, f32 h, f32* bias_rate, f32* mass_scale, f32* impulse_scale)
{
    constexpr f32 kTwoPi = 6.2831853f;
    if (hertz <= 0.0f)
    {
        *bias_rate = 0.0f;
        *mass_scale = 1.0f;
        *impulse_scale = 0.0f;
        return;
    }
    const f32 omega = kTwoPi * hertz;
    const f32 a1 = 2.0f * ratio + h * omega;
    const f32 a2 = h * omega * a1;
    const f32 a3 = 1.0f / (1.0f + a2);
    *bias_rate = omega / a1;
    *mass_scale = a2 * a3;
    *impulse_scale = a3;
}

// Where a joint's anchors are now: from the centres of mass, and the vector between them.
struct JointAnchors
{
    Vec3 ra = Vec3(0.0f, 0.0f, 0.0f);
    Vec3 rb = Vec3(0.0f, 0.0f, 0.0f);
    Vec3 separation = Vec3(0.0f, 0.0f, 0.0f);
};

JointAnchors ComputeAnchors(const JointRecord& joint, const Body& a, const Body& b)
{
    const Mat3 ma = a.orientation.ToMat3();
    const Mat3 mb = b.orientation.ToMat3();
    JointAnchors out;
    out.ra = ma * (joint.desc.anchor_a - a.mass.center);
    out.rb = mb * (joint.desc.anchor_b - b.mass.center);
    const Vec3 com_a = a.position + ma * a.mass.center;
    const Vec3 com_b = b.position + mb * b.mass.center;
    out.separation = (com_b + out.rb) - (com_a + out.ra);
    return out;
}

// The inverse of a 3x3 matrix by cofactors; false (and zero) when it is singular.
bool Invert3(const Mat3& m, Mat3* out)
{
    const Vec3& c0 = m.col[0];
    const Vec3& c1 = m.col[1];
    const Vec3& c2 = m.col[2];
    const Vec3 r0 = c1.Cross(c2);
    const Vec3 r1 = c2.Cross(c0);
    const Vec3 r2 = c0.Cross(c1);
    const f32 det = c0.Dot(r0);
    if (Abs(det) < 1e-30f)
    {
        out->col[0] = Vec3(0.0f, 0.0f, 0.0f);
        out->col[1] = Vec3(0.0f, 0.0f, 0.0f);
        out->col[2] = Vec3(0.0f, 0.0f, 0.0f);
        return false;
    }
    const f32 inv = 1.0f / det;
    // The rows of the inverse are r0, r1, r2 over the determinant; columns are their transpose.
    out->col[0] = Vec3(r0.x, r1.x, r2.x) * inv;
    out->col[1] = Vec3(r0.y, r1.y, r2.y) * inv;
    out->col[2] = Vec3(r0.z, r1.z, r2.z) * inv;
    return true;
}

// The inverse of the matrix that turns an impulse at the anchors into the change of their relative velocity.
Mat3 PointMassMatrix(const Body& a, const Body& b, const Vec3& ra, const Vec3& rb)
{
    const f32 m = InverseMassOf(a) + InverseMassOf(b);
    Mat3 k;
    const Vec3 axes[3] = {Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f)};
    for (u32 j = 0; j < 3; ++j)
    {
        k.col[j] = axes[j] * m + WorldInertia(a, ra.Cross(axes[j])).Cross(ra) +
                   WorldInertia(b, rb.Cross(axes[j])).Cross(rb);
    }
    Mat3 inverse;
    Invert3(k, &inverse);
    return inverse;
}

Mat3 AngularMassMatrix(const Body& a, const Body& b)
{
    Mat3 k;
    const Vec3 axes[3] = {Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f)};
    for (u32 j = 0; j < 3; ++j)
    {
        k.col[j] = WorldInertia(a, axes[j]) + WorldInertia(b, axes[j]);
    }
    Mat3 inverse;
    Invert3(k, &inverse);
    return inverse;
}

void ApplyAngular(Body* a, Body* b, const Vec3& impulse)
{
    if (a->settings.type == BodyType::Dynamic)
    {
        a->angular_velocity = a->angular_velocity - WorldInertia(*a, impulse);
    }
    if (b->settings.type == BodyType::Dynamic)
    {
        b->angular_velocity = b->angular_velocity + WorldInertia(*b, impulse);
    }
}

Vec3 SafeNormal(const Vec3& v, f32* length)
{
    const f32 l2 = v.LengthSquared();
    if (l2 < 1e-24f)
    {
        *length = 0.0f;
        return Vec3(1.0f, 0.0f, 0.0f);
    }
    *length = Sqrt(l2);
    return v * (1.0f / *length);
}

// The rotation that takes the frame of A to the frame of B, as a vector (twice the vector part, the small-angle
// rotation vector), in the world.
Vec3 FrameError(const JointRecord& joint, const Body& a, const Body& b)
{
    const Quaternion fa = a.orientation * joint.desc.frame_a;
    const Quaternion fb = b.orientation * joint.desc.frame_b;
    Quaternion q = fb * fa.Conjugate();
    if (q.w < 0.0f)
    {
        q = Quaternion(-q.x, -q.y, -q.z, -q.w);
    }
    return Vec3(q.x, q.y, q.z) * 2.0f;
}

Vec3 HingeAxis(const JointRecord& joint, const Body& a)
{
    return (a.orientation * joint.desc.frame_a).RotateVector(Vec3(0.0f, 0.0f, 1.0f));
}

// The joint's impulses as velocity changes, before the substep's solve.
void WarmStartJoint(JointWork* work)
{
    JointRecord* joint = work->record;
    Body* a = work->a;
    Body* b = work->b;
    const JointAnchors anchors = ComputeAnchors(*joint, *a, *b);
    switch (joint->desc.type)
    {
    case JointType::Ball: Apply(a, b, anchors.ra, anchors.rb, joint->linear_impulse); break;
    case JointType::Fixed:
        ApplyAngular(a, b, joint->angular_impulse);
        Apply(a, b, anchors.ra, anchors.rb, joint->linear_impulse);
        break;
    case JointType::Hinge:
    {
        const Vec3 axis = HingeAxis(*joint, *a);
        ApplyAngular(
            a, b,
            axis * (joint->spring_impulse + joint->motor_impulse + joint->lower_impulse - joint->upper_impulse) +
                joint->angular_impulse);
        Apply(a, b, anchors.ra, anchors.rb, joint->linear_impulse);
        break;
    }
    case JointType::Distance:
    {
        f32 length;
        const Vec3 u = SafeNormal(anchors.separation, &length);
        Apply(a, b, anchors.ra, anchors.rb, u * (joint->axial_impulse + joint->lower_impulse - joint->upper_impulse));
        break;
    }
    }
}

struct JointSoftness
{
    f32 bias_rate = 0.0f;
    f32 mass_scale = 1.0f;
    f32 impulse_scale = 0.0f;
};

// One pass of a joint's rows: `use_bias` adds the position feedback (soft), the relax pass does not.
void SolveHinge(JointWork* work, bool use_bias, const JointSoftness& soft, f32 inv_h);

void SolveJoint(JointWork* work, bool use_bias, const JointSoftness& soft, f32 inv_h)
{
    JointRecord* joint = work->record;
    Body* a = work->a;
    Body* b = work->b;
    if (joint->desc.type == JointType::Hinge)
    {
        SolveHinge(work, use_bias, soft, inv_h);
        return;
    }
    const JointAnchors anchors = ComputeAnchors(*joint, *a, *b);
    const Vec3& ra = anchors.ra;
    const Vec3& rb = anchors.rb;
    const JointDesc& desc = joint->desc;
    f32 mass_scale = 1.0f;
    f32 impulse_scale = 0.0f;
    if (use_bias)
    {
        mass_scale = soft.mass_scale;
        impulse_scale = soft.impulse_scale;
    }
    if (desc.type == JointType::Fixed)
    {
        // The angle rows first, then the point.
        const Vec3 error = FrameError(*joint, *a, *b);
        const Vec3 cdot = b->angular_velocity - a->angular_velocity;
        const Vec3 bias = use_bias ? error * soft.bias_rate : Vec3(0.0f, 0.0f, 0.0f);
        const Mat3 k = AngularMassMatrix(*a, *b);
        const Vec3 impulse = (k * (cdot + bias)) * -mass_scale - joint->angular_impulse * impulse_scale;
        joint->angular_impulse = joint->angular_impulse + impulse;
        ApplyAngular(a, b, impulse);
    }
    if (desc.type == JointType::Ball || desc.type == JointType::Fixed)
    {
        const Vec3 cdot = VelocityAt(*b, rb) - VelocityAt(*a, ra);
        const Vec3 bias = use_bias ? anchors.separation * soft.bias_rate : Vec3(0.0f, 0.0f, 0.0f);
        const Mat3 k = PointMassMatrix(*a, *b, ra, rb);
        const Vec3 impulse = (k * (cdot + bias)) * -mass_scale - joint->linear_impulse * impulse_scale;
        joint->linear_impulse = joint->linear_impulse + impulse;
        Apply(a, b, ra, rb, impulse);
        return;
    }
    // A distance joint: a rigid or spring row along the axis, and the limits of the range.
    f32 length;
    const Vec3 u = SafeNormal(anchors.separation, &length);
    const f32 axial_mass = EffectiveMass(*a, *b, ra, rb, u);
    const auto relative_speed = [&]() { return (VelocityAt(*b, rb) - VelocityAt(*a, ra)).Dot(u); };
    const bool has_spring = desc.hertz > 0.0f;
    const bool rigid = !has_spring && desc.min_length == desc.max_length;
    if (has_spring)
    {
        // The spring pulls toward the rest length; it is never switched off, so the relax pass keeps it.
        f32 bias_rate, spring_mass_scale, spring_impulse_scale;
        MakeSoft(desc.hertz, desc.damping_ratio, 1.0f / inv_h, &bias_rate, &spring_mass_scale, &spring_impulse_scale);
        const f32 c = length - desc.length;
        const f32 bias = bias_rate * c;
        const f32 impulse = -spring_mass_scale * axial_mass * (relative_speed() + bias) -
                            spring_impulse_scale * joint->axial_impulse;
        joint->axial_impulse += impulse;
        Apply(a, b, ra, rb, u * impulse);
    }
    else if (rigid)
    {
        const f32 c = length - desc.length;
        const f32 bias = use_bias ? soft.bias_rate * c : 0.0f;
        const f32 impulse = -mass_scale * axial_mass * (relative_speed() + bias) - impulse_scale * joint->axial_impulse;
        joint->axial_impulse += impulse;
        Apply(a, b, ra, rb, u * impulse);
    }
    if (desc.min_length < desc.max_length)
    {
        // Lower limit: the anchors may not come closer than min_length.
        if (desc.min_length > 0.0f)
        {
            const f32 c = length - desc.min_length;
            f32 bias = 0.0f;
            f32 ms = 1.0f;
            f32 is = 0.0f;
            if (c > 0.0f)
            {
                bias = c * inv_h;
            }
            else if (use_bias)
            {
                bias = soft.bias_rate * c;
                ms = soft.mass_scale;
                is = soft.impulse_scale;
            }
            const f32 impulse = -ms * axial_mass * (relative_speed() + bias) - is * joint->lower_impulse;
            const f32 next = Max(joint->lower_impulse + impulse, 0.0f);
            const f32 applied = next - joint->lower_impulse;
            joint->lower_impulse = next;
            Apply(a, b, ra, rb, u * applied);
        }
        // Upper limit: the anchors may not get farther than max_length.
        const f32 c = desc.max_length - length;
        f32 bias = 0.0f;
        f32 ms = 1.0f;
        f32 is = 0.0f;
        if (c > 0.0f)
        {
            bias = c * inv_h;
        }
        else if (use_bias)
        {
            bias = soft.bias_rate * c;
            ms = soft.mass_scale;
            is = soft.impulse_scale;
        }
        const f32 impulse = -ms * axial_mass * (-relative_speed() + bias) - is * joint->upper_impulse;
        const f32 next = Max(joint->upper_impulse + impulse, 0.0f);
        const f32 applied = next - joint->upper_impulse;
        joint->upper_impulse = next;
        Apply(a, b, ra, rb, u * -applied);
    }
}

// The angle of a hinge now, counted across turns from where the step began.
f32 CurrentHingeAngle(const JointWork& work)
{
    const f32 raw = HingeRawAngle(work.record->desc, work.a->orientation, work.b->orientation);
    return work.angle_start + WrapAngle(raw - work.raw_start);
}

// The hinge's rows: spring, motor, limits about the axis, then the two alignment rows and the point.
void SolveHinge(JointWork* work, bool use_bias, const JointSoftness& soft, f32 inv_h)
{
    JointRecord* joint = work->record;
    Body* a = work->a;
    Body* b = work->b;
    const JointDesc& desc = joint->desc;
    const Vec3 axis = HingeAxis(*joint, *a);
    const f32 h = 1.0f / inv_h;
    f32 mass_scale = 1.0f;
    f32 impulse_scale = 0.0f;
    if (use_bias)
    {
        mass_scale = soft.mass_scale;
        impulse_scale = soft.impulse_scale;
    }
    const f32 axis_inverse = axis.Dot(WorldInertia(*a, axis) + WorldInertia(*b, axis));
    const f32 axial_mass = axis_inverse > 1e-12f ? 1.0f / axis_inverse : 0.0f;
    const auto axial_speed = [&]() { return (b->angular_velocity - a->angular_velocity).Dot(axis); };
    const f32 angle = CurrentHingeAngle(*work);
    if (desc.hertz > 0.0f)
    {
        f32 bias_rate, spring_mass_scale, spring_impulse_scale;
        MakeSoft(desc.hertz, desc.damping_ratio, h, &bias_rate, &spring_mass_scale, &spring_impulse_scale);
        const f32 impulse = -spring_mass_scale * axial_mass * (axial_speed() + bias_rate * angle) -
                            spring_impulse_scale * joint->spring_impulse;
        joint->spring_impulse += impulse;
        ApplyAngular(a, b, axis * impulse);
    }
    if (desc.enable_motor)
    {
        const f32 impulse = -axial_mass * (axial_speed() - desc.motor_speed);
        const f32 limit = desc.max_motor_torque * h;
        const f32 next = Clamp(joint->motor_impulse + impulse, -limit, limit);
        const f32 applied = next - joint->motor_impulse;
        joint->motor_impulse = next;
        ApplyAngular(a, b, axis * applied);
    }
    if (desc.enable_limit)
    {
        const auto limit_row = [&](f32 c, f32 sign, f32* accumulated)
        {
            f32 bias = 0.0f;
            f32 ms = 1.0f;
            f32 is = 0.0f;
            if (c > 0.0f)
            {
                bias = c * inv_h;
            }
            else if (use_bias)
            {
                bias = soft.bias_rate * c;
                ms = soft.mass_scale;
                is = soft.impulse_scale;
            }
            const f32 impulse = -ms * axial_mass * (sign * axial_speed() + bias) - is * *accumulated;
            const f32 next = Max(*accumulated + impulse, 0.0f);
            const f32 applied = next - *accumulated;
            *accumulated = next;
            ApplyAngular(a, b, axis * (sign * applied));
        };
        limit_row(angle - desc.lower_angle, 1.0f, &joint->lower_impulse);
        limit_row(desc.upper_angle - angle, -1.0f, &joint->upper_impulse);
    }
    // The two rows that keep the axes together: about the two directions across the hinge axis.
    {
        const Vec3 axis_b = (b->orientation * desc.frame_b).RotateVector(Vec3(0.0f, 0.0f, 1.0f));
        const Vec3 error = axis.Cross(axis_b);
        const Vec3 p1 = Perpendicular(axis);
        const Vec3 p2 = axis.Cross(p1);
        const Vec3 pa = WorldInertia(*a, p1) + WorldInertia(*b, p1);
        const Vec3 pb = WorldInertia(*a, p2) + WorldInertia(*b, p2);
        const f32 k11 = p1.Dot(pa);
        const f32 k12 = p2.Dot(pa);
        const f32 k22 = p2.Dot(pb);
        const f32 det = k11 * k22 - k12 * k12;
        if (Abs(det) > 1e-24f)
        {
            const Vec3 w = b->angular_velocity - a->angular_velocity;
            const f32 bias1 = use_bias ? soft.bias_rate * error.Dot(p1) : 0.0f;
            const f32 bias2 = use_bias ? soft.bias_rate * error.Dot(p2) : 0.0f;
            const f32 c1 = w.Dot(p1) + bias1;
            const f32 c2 = w.Dot(p2) + bias2;
            const f32 inv = 1.0f / det;
            const f32 l1 = -mass_scale * (k22 * c1 - k12 * c2) * inv - impulse_scale * joint->angular_impulse.Dot(p1);
            const f32 l2 = -mass_scale * (k11 * c2 - k12 * c1) * inv - impulse_scale * joint->angular_impulse.Dot(p2);
            const Vec3 impulse = p1 * l1 + p2 * l2;
            joint->angular_impulse = joint->angular_impulse + impulse;
            ApplyAngular(a, b, impulse);
        }
    }
    // The point.
    const JointAnchors anchors = ComputeAnchors(*joint, *a, *b);
    const Vec3 cdot = VelocityAt(*b, anchors.rb) - VelocityAt(*a, anchors.ra);
    const Vec3 bias = use_bias ? anchors.separation * soft.bias_rate : Vec3(0.0f, 0.0f, 0.0f);
    const Mat3 k = PointMassMatrix(*a, *b, anchors.ra, anchors.rb);
    const Vec3 impulse = (k * (cdot + bias)) * -mass_scale - joint->linear_impulse * impulse_scale;
    joint->linear_impulse = joint->linear_impulse + impulse;
    Apply(a, b, anchors.ra, anchors.rb, impulse);
}

}  // namespace

void ContactSolver::Solve(PhysicsWorld* world, Span<const ContactInput> inputs, const SolverConfig& config)
{
    stats_ = SolverStats();
    // The constraints in the order of their colliders, whatever the order of the input.
    Vector<u32> order;
    for (usize i = 0; i < inputs.Size(); ++i)
    {
        order.Push(static_cast<u32>(i));
    }
    const auto key_a = [&](u32 i)
    { return inputs[i].a.ToKey() < inputs[i].b.ToKey() ? inputs[i].a.ToKey() : inputs[i].b.ToKey(); };
    const auto key_b = [&](u32 i)
    { return inputs[i].a.ToKey() < inputs[i].b.ToKey() ? inputs[i].b.ToKey() : inputs[i].a.ToKey(); };
    Sort(Span<u32>(order.Data(), order.Size()),
         [&](u32 x, u32 y)
         {
             if (key_a(x) != key_a(y))
             {
                 return key_a(x) < key_a(y);
             }
             return key_b(x) < key_b(y);
         });
    const u32 substeps = config.substeps < 1 ? 1 : (config.substeps > 16 ? 16 : config.substeps);
    const f32 h = config.dt / static_cast<f32>(substeps);
    const f32 inv_h = 1.0f / h;
    // The spring may not be stiffer than a quarter of the substep rate.
    const f32 hertz = Min(config.contact_hertz, 0.25f * inv_h);
    f32 dynamic_bias, dynamic_mass_scale, dynamic_impulse_scale;
    f32 static_bias, static_mass_scale, static_impulse_scale;
    MakeSoft(hertz, config.damping_ratio, h, &dynamic_bias, &dynamic_mass_scale, &dynamic_impulse_scale);
    MakeSoft(2.0f * hertz, config.damping_ratio, h, &static_bias, &static_mass_scale, &static_impulse_scale);
    // The bodies of the constraints, one state each.
    Vector<BodyState> states;
    Vector<i32> state_of;
    const u32 slots = world->Stats().body_slots;
    for (u32 i = 0; i < slots; ++i)
    {
        state_of.Push(-1);
    }
    const auto state_for = [&](Body* body) -> u32
    {
        i32& index = state_of[body->id.index];
        if (index < 0)
        {
            index = static_cast<i32>(states.Size());
            BodyState st;
            st.body = body;
            st.q0 = body->orientation;
            st.com0 = CenterOfMass(*body);
            st.moves = body->settings.type != BodyType::Static;
            states.Push(st);
        }
        return static_cast<u32>(index);
    };
    Vector<Constraint> constraints;
    constraints.Reserve(order.Size());
    for (usize o = 0; o < order.Size(); ++o)
    {
        const ContactInput& in = inputs[order[o]];
        ContactManifold m = in.manifold;
        ColliderId a = in.a;
        ColliderId b = in.b;
        BodyId ba = in.body_a;
        BodyId bb = in.body_b;
        if (a.ToKey() > b.ToKey())
        {
            m.Flip();
            const ColliderId tc = a;
            a = b;
            b = tc;
            const BodyId tb = ba;
            ba = bb;
            bb = tb;
        }
        Body* body_a = world->Find(ba);
        Body* body_b = world->Find(bb);
        if (body_a == nullptr || body_b == nullptr || m.count == 0 || body_a == body_b)
        {
            continue;
        }
        Constraint c;
        c.a = a;
        c.b = b;
        c.sa = state_for(body_a);
        c.sb = state_for(body_b);
        c.normal = m.normal;
        c.friction = m.friction;
        c.restitution = m.restitution;
        c.count = m.count;
        const bool with_static = body_a->settings.type != BodyType::Dynamic ||
                                 body_b->settings.type != BodyType::Dynamic;
        c.bias_rate = with_static ? static_bias : dynamic_bias;
        c.mass_scale = with_static ? static_mass_scale : dynamic_mass_scale;
        c.impulse_scale = with_static ? static_impulse_scale : dynamic_impulse_scale;
        const Vec3 com_a = states[c.sa].com0;
        const Vec3 com_b = states[c.sb].com0;
        const Quaternion inv_a = body_a->orientation.Conjugate();
        Vec3 mean(0.0f, 0.0f, 0.0f);
        for (u32 k = 0; k < m.count; ++k)
        {
            mean = mean + (m.points[k].position_a + m.points[k].position_b) * (0.5f / static_cast<f32>(m.count));
        }
        // The friction directions: along the sliding velocity when there is one, so the friction cone is round where
        // it matters, and a fixed perpendicular otherwise.
        const Vec3 rel = VelocityAt(*body_b, mean - com_b) - VelocityAt(*body_a, mean - com_a);
        const Vec3 slide = rel - m.normal * rel.Dot(m.normal);
        const f32 slide2 = slide.LengthSquared();
        c.tangent[0] = slide2 > 1e-8f ? slide * (1.0f / Sqrt(slide2)) : Perpendicular(m.normal);
        c.tangent[1] = m.normal.Cross(c.tangent[0]);
        for (u32 k = 0; k < m.count; ++k)
        {
            Row& row = c.rows[k];
            const ContactPoint& p = m.points[k];
            row.ra = p.position_a - com_a;
            row.rb = p.position_b - com_b;
            row.feature = p.feature_id;
            row.depth = p.depth;
            row.local_a = inv_a.RotateVector(p.position_a - body_a->position);
            row.normal_mass = EffectiveMass(*body_a, *body_b, row.ra, row.rb, m.normal);
            row.tangent_mass[0] = EffectiveMass(*body_a, *body_b, row.ra, row.rb, c.tangent[0]);
            row.tangent_mass[1] = EffectiveMass(*body_a, *body_b, row.ra, row.rb, c.tangent[1]);
            row.relative_velocity = (VelocityAt(*body_b, row.rb) - VelocityAt(*body_a, row.ra)).Dot(m.normal);
        }
        stats_.points += m.count;
        ++stats_.manifolds;
        constraints.Push(c);
    }
    // The joints, in the order of their slots. A body that is not static takes its substeps with the rest.
    Body world_body;
    world_body.settings.type = BodyType::Static;
    Vector<JointWork> joints;
    const u32 joint_slots = world->joints_.Slots();
    for (u32 slot = 0; slot < joint_slots; ++slot)
    {
        JointRecord* record = world->joints_.Find(slot, world->joints_.LiveGeneration(slot));
        if (record == nullptr)
        {
            continue;
        }
        JointWork work;
        work.record = record;
        work.a = record->desc.body_a.IsValid() ? world->Find(record->desc.body_a) : &world_body;
        work.b = record->desc.body_b.IsValid() ? world->Find(record->desc.body_b) : &world_body;
        if (work.a == nullptr || work.b == nullptr)
        {
            continue;
        }
        // A joint whose bodies all sleep or are fixed has nothing to do.
        const auto live = [](const Body* body) { return body->settings.type == BodyType::Dynamic && !body->sleeping; };
        if (!live(work.a) && !live(work.b))
        {
            continue;
        }
        if (work.a != &world_body)
        {
            state_for(work.a);
        }
        if (work.b != &world_body)
        {
            state_for(work.b);
        }
        if (record->desc.type == JointType::Hinge)
        {
            // The angle at the start of the step, counted across turns from where the last step ended.
            const f32 raw = HingeRawAngle(record->desc, work.a->orientation, work.b->orientation);
            work.angle_start = record->angle + WrapAngle(raw - record->angle_raw);
            work.raw_start = raw;
        }
        joints.Push(work);
    }
    stats_.joints = static_cast<u32>(joints.Size());
    const f32 joint_hertz = Min(config.joint_hertz, 0.25f * inv_h);
    JointSoftness joint_soft;
    MakeSoft(joint_hertz, config.joint_damping_ratio, h, &joint_soft.bias_rate, &joint_soft.mass_scale,
             &joint_soft.impulse_scale);
    // The cached impulses of the same points start the rows.
    for (usize ci = 0; ci < constraints.Size(); ++ci)
    {
        Constraint& c = constraints[ci];
        const Body& body_a = *states[c.sa].body;
        const Body& body_b = *states[c.sb].body;
        for (u32 k = 0; k < c.count; ++k)
        {
            Row& row = c.rows[k];
            usize lo = 0;
            usize hi = cache_.Size();
            const u64 ka = c.a.ToKey();
            const u64 kb = c.b.ToKey();
            while (lo < hi)
            {
                const usize mid = (lo + hi) / 2;
                const CacheEntry& e = cache_[mid];
                const bool less = e.key_a != ka ? e.key_a < ka
                                                : (e.key_b != kb ? e.key_b < kb : e.feature < row.feature);
                if (less)
                {
                    lo = mid + 1;
                }
                else
                {
                    hi = mid;
                }
            }
            if (lo < cache_.Size() && cache_[lo].key_a == ka && cache_[lo].key_b == kb &&
                cache_[lo].feature == row.feature)
            {
                const CacheEntry& e = cache_[lo];
                if (e.teleports_a == body_a.teleports && e.teleports_b == body_b.teleports &&
                    (e.local_a - row.local_a).LengthSquared() < kWarmDistance * kWarmDistance)
                {
                    row.normal_impulse = e.normal_impulse;
                    row.tangent_impulse[0] = e.tangent_impulse.Dot(c.tangent[0]);
                    row.tangent_impulse[1] = e.tangent_impulse.Dot(c.tangent[1]);
                    ++stats_.warm_started;
                }
            }
        }
    }
    // One pass over every constraint: friction first, then the normal row (non-penetration matters more, so it comes
    // last). `use_bias` is the position feedback; the relax pass runs without it.
    const auto solve = [&](bool use_bias, bool backward)
    {
        for (usize cj = 0; cj < constraints.Size(); ++cj)
        {
            Constraint& c = constraints[backward ? constraints.Size() - 1 - cj : cj];
            BodyState& sa = states[c.sa];
            BodyState& sb = states[c.sb];
            Body* a = sa.body;
            Body* b = sb.body;
            const Vec3 dp = sb.dp - sa.dp;
            for (u32 kj = 0; kj < c.count; ++kj)
            {
                const u32 k = backward ? c.count - 1 - kj : kj;
                Row& row = c.rows[k];
                // Where the points are now: the start separation, the bodies' motion and the turn of the anchors.
                const Vec3 prel = dp + (sb.dq.RotateVector(row.rb) - row.rb) - (sa.dq.RotateVector(row.ra) - row.ra);
                const f32 s = prel.Dot(c.normal) - row.depth;
                // Friction.
                const f32 limit = c.friction * row.normal_impulse;
                for (u32 t = 0; t < 2; ++t)
                {
                    const f32 vt = (VelocityAt(*b, row.rb) - VelocityAt(*a, row.ra)).Dot(c.tangent[t]);
                    f32 lambda = -vt * row.tangent_mass[t];
                    const f32 next = Clamp(row.tangent_impulse[t] + lambda, -limit, limit);
                    lambda = next - row.tangent_impulse[t];
                    row.tangent_impulse[t] = next;
                    Apply(a, b, row.ra, row.rb, c.tangent[t] * lambda);
                }
                // The normal row: a gap is closed in the time that remains, an overlap by the soft constraint.
                f32 bias = 0.0f;
                f32 mass_scale = 1.0f;
                f32 impulse_scale = 0.0f;
                if (s > 0.0f)
                {
                    bias = s * inv_h;
                }
                else if (use_bias)
                {
                    bias = Max(c.bias_rate * Min(s + config.slop, 0.0f), -config.push_velocity);
                    mass_scale = c.mass_scale;
                    impulse_scale = c.impulse_scale;
                }
                const f32 vn = (VelocityAt(*b, row.rb) - VelocityAt(*a, row.ra)).Dot(c.normal);
                f32 lambda = -row.normal_mass * mass_scale * (vn + bias) - impulse_scale * row.normal_impulse;
                const f32 next = Max(row.normal_impulse + lambda, 0.0f);
                lambda = next - row.normal_impulse;
                row.normal_impulse = next;
                row.max_normal_impulse = Max(row.max_normal_impulse, next);
                Apply(a, b, row.ra, row.rb, c.normal * lambda);
            }
        }
    };
    const auto solve_joints = [&](bool use_bias)
    {
        for (usize ji = 0; ji < joints.Size(); ++ji)
        {
            SolveJoint(&joints[ji], use_bias, joint_soft, inv_h);
        }
    };
    const auto warm_start = [&]()
    {
        for (usize ji = 0; ji < joints.Size(); ++ji)
        {
            WarmStartJoint(&joints[ji]);
        }
        for (usize ci = 0; ci < constraints.Size(); ++ci)
        {
            Constraint& c = constraints[ci];
            for (u32 k = 0; k < c.count; ++k)
            {
                const Row& row = c.rows[k];
                const Vec3 impulse = c.normal * row.normal_impulse + c.tangent[0] * row.tangent_impulse[0] +
                                     c.tangent[1] * row.tangent_impulse[1];
                Apply(states[c.sa].body, states[c.sb].body, row.ra, row.rb, impulse);
            }
        }
    };
    for (u32 sub = 0; sub < substeps; ++sub)
    {
        for (usize i = 0; i < states.Size(); ++i)
        {
            if (states[i].moves)
            {
                PhysicsWorld::IntegrateVelocity(config.gravity, states[i].body, h);
            }
        }
        warm_start();
        for (u32 it = 0; it < config.iterations; ++it)
        {
            solve_joints(true);
            solve(true, ((sub + it) & 1u) != 0);
        }
        for (usize i = 0; i < states.Size(); ++i)
        {
            BodyState& st = states[i];
            if (!st.moves)
            {
                continue;
            }
            PhysicsWorld::LimitVelocity(config.max_velocity, st.body);
            PhysicsWorld::IntegratePosition(st.body, h);
            st.dp = CenterOfMass(*st.body) - st.com0;
            st.dq = st.body->orientation * st.q0.Conjugate();
        }
        for (u32 it = 0; it < config.iterations; ++it)
        {
            solve_joints(false);
            solve(false, ((sub + it) & 1u) != 0);
        }
    }
    // Restitution: the points that met fast enough bounce with e times their approach speed.
    for (usize ci = 0; ci < constraints.Size(); ++ci)
    {
        Constraint& c = constraints[ci];
        if (c.restitution <= 0.0f)
        {
            continue;
        }
        Body* a = states[c.sa].body;
        Body* b = states[c.sb].body;
        for (u32 k = 0; k < c.count; ++k)
        {
            Row& row = c.rows[k];
            if (row.relative_velocity > -config.restitution_threshold || row.max_normal_impulse == 0.0f)
            {
                continue;
            }
            const f32 vn = (VelocityAt(*b, row.rb) - VelocityAt(*a, row.ra)).Dot(c.normal);
            f32 lambda = -row.normal_mass * (vn + c.restitution * row.relative_velocity);
            const f32 next = Max(row.normal_impulse + lambda, 0.0f);
            lambda = next - row.normal_impulse;
            row.normal_impulse = next;
            Apply(a, b, row.ra, row.rb, c.normal * lambda);
        }
    }
    // What each joint carried in the last substep, and the ones that carried too much.
    for (usize ji = 0; ji < joints.Size(); ++ji)
    {
        JointRecord* joint = joints[ji].record;
        const JointAnchors anchors = ComputeAnchors(*joint, *joints[ji].a, *joints[ji].b);
        Vec3 force = joint->linear_impulse * inv_h;
        if (joint->desc.type == JointType::Distance)
        {
            f32 length;
            const Vec3 u = SafeNormal(anchors.separation, &length);
            force = u * ((joint->axial_impulse + joint->lower_impulse - joint->upper_impulse) * inv_h);
        }
        joint->reaction_force = force;
        joint->reaction_torque = joint->angular_impulse * inv_h;
        if (joint->desc.type == JointType::Hinge)
        {
            const Vec3 axis = HingeAxis(*joint, *joints[ji].a);
            joint->reaction_torque = joint->reaction_torque + axis * ((joint->spring_impulse + joint->motor_impulse +
                                                                       joint->lower_impulse - joint->upper_impulse) *
                                                                      inv_h);
            const f32 raw = HingeRawAngle(joint->desc, joints[ji].a->orientation, joints[ji].b->orientation);
            joint->angle = joints[ji].angle_start + WrapAngle(raw - joints[ji].raw_start);
            joint->angle_raw = raw;
        }
        if ((joint->desc.break_force > 0.0f && force.Length() > joint->desc.break_force) ||
            (joint->desc.break_torque > 0.0f && joint->reaction_torque.Length() > joint->desc.break_torque))
        {
            joint->broken = true;
        }
    }
    for (usize i = 0; i < states.Size(); ++i)
    {
        if (states[i].moves)
        {
            states[i].body->integrated = true;
        }
    }
    stats_.iterations = constraints.Size() > 0 ? substeps * config.iterations : 0;
    for (usize ci = 0; ci < constraints.Size(); ++ci)
    {
        for (u32 k = 0; k < constraints[ci].count; ++k)
        {
            stats_.max_penetration = Max(stats_.max_penetration, constraints[ci].rows[k].depth);
            stats_.max_impulse = Max(stats_.max_impulse, constraints[ci].rows[k].max_normal_impulse);
        }
    }
    // The new cache: this step's impulses, in key order. The anchor is kept in body A's frame at the step's start.
    next_cache_.Clear();
    for (usize ci = 0; ci < constraints.Size(); ++ci)
    {
        const Constraint& c = constraints[ci];
        const Body* body_a = states[c.sa].body;
        const Body* body_b = states[c.sb].body;
        for (u32 k = 0; k < c.count; ++k)
        {
            CacheEntry e;
            e.key_a = c.a.ToKey();
            e.key_b = c.b.ToKey();
            e.feature = c.rows[k].feature;
            e.body_a = body_a->id;
            e.body_b = body_b->id;
            e.teleports_a = body_a->teleports;
            e.teleports_b = body_b->teleports;
            e.local_a = c.rows[k].local_a;
            e.normal_impulse = c.rows[k].normal_impulse;
            e.tangent_impulse = c.tangent[0] * c.rows[k].tangent_impulse[0] +
                                c.tangent[1] * c.rows[k].tangent_impulse[1];
            next_cache_.Push(e);
        }
    }
    Sort(Span<CacheEntry>(next_cache_.Data(), next_cache_.Size()),
         [](const CacheEntry& x, const CacheEntry& y)
         {
             if (x.key_a != y.key_a)
             {
                 return x.key_a < y.key_a;
             }
             if (x.key_b != y.key_b)
             {
                 return x.key_b < y.key_b;
             }
             return x.feature < y.feature;
         });
    cache_.Clear();
    for (usize i = 0; i < next_cache_.Size(); ++i)
    {
        cache_.Push(next_cache_[i]);
    }
}

f32 ContactSolver::PairImpulse(ColliderId a, ColliderId b) const
{
    const u64 ka = a.ToKey();
    const u64 kb = b.ToKey();
    usize lo = 0;
    usize hi = cache_.Size();
    while (lo < hi)
    {
        const usize mid = (lo + hi) / 2;
        const CacheEntry& e = cache_[mid];
        if (e.key_a != ka ? e.key_a < ka : e.key_b < kb)
        {
            lo = mid + 1;
        }
        else
        {
            hi = mid;
        }
    }
    f32 total = 0.0f;
    for (; lo < cache_.Size() && cache_[lo].key_a == ka && cache_[lo].key_b == kb; ++lo)
    {
        total += cache_[lo].normal_impulse;
    }
    return total;
}

void ContactSolver::ForgetBody(BodyId body)
{
    next_cache_.Clear();
    for (usize i = 0; i < cache_.Size(); ++i)
    {
        const CacheEntry& e = cache_[i];
        const bool mine = (e.body_a.index == body.index && e.body_a.generation == body.generation) ||
                          (e.body_b.index == body.index && e.body_b.generation == body.generation);
        if (!mine)
        {
            next_cache_.Push(e);
        }
    }
    cache_.Clear();
    for (usize i = 0; i < next_cache_.Size(); ++i)
    {
        cache_.Push(next_cache_[i]);
    }
}

void ContactSolver::Clear()
{
    cache_.Clear();
}

}  // namespace engine_placeholder::physics
