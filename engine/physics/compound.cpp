#include "physics/compound.h"

#include "lang/scalar.h"

namespace vse::physics
{

CompoundShapeConstPtr CompoundShape::Create(Span<const CompoundChild> children, const HullLibrary* hulls,
                                            Validation* result)
{
    Validation ignored;
    Validation& r = result != nullptr ? *result : ignored;
    r = Validation::Ok;
    if (children.Size() == 0 || children.Size() > kMaxCompoundChildren)
    {
        r = Validation::OutOfRange;
        return CompoundShapeConstPtr();
    }
    for (usize i = 0; i < children.Size(); ++i)
    {
        const CompoundChild& c = children[i];
        if (c.shape.kind == ShapeKind::Mesh || c.shape.kind == ShapeKind::Compound ||
            (c.shape.kind == ShapeKind::Hull && (hulls == nullptr || hulls->Find(c.shape.hull) == nullptr)))
        {
            r = Validation::OutOfRange;
            return CompoundShapeConstPtr();
        }
        Collider probe;
        probe.shape = c.shape;
        probe.offset = c.offset;
        probe.rotation = c.rotation;
        probe.material = c.material;
        r = probe.Validate();
        if (r != Validation::Ok)
        {
            return CompoundShapeConstPtr();
        }
    }
    CompoundShapePtr compound = MakeShared<CompoundShape>();
    f64 volume = 0.0;
    f64 mass = 0.0;
    f64 first[3] = {0.0, 0.0, 0.0};
    Vector<MassProperties> parts;
    Vector<Vec3> centres;
    Aabb bounds = Aabb::Empty();
    for (usize i = 0; i < children.Size(); ++i)
    {
        const CompoundChild& c = children[i];
        compound->children_.Push(c);
        const MassProperties m = MassFromDensity(c.shape, c.material.density, hulls);
        parts.Push(m);
        const Vec3 centre = c.offset + c.rotation.RotateVector(m.center);
        centres.Push(centre);
        volume += f64{m.volume};
        mass += f64{m.mass};
        first[0] += f64{m.mass} * f64{centre.x};
        first[1] += f64{m.mass} * f64{centre.y};
        first[2] += f64{m.mass} * f64{centre.z};
        const Aabb box = c.shape.Bounds(c.offset, c.rotation);
        bounds.min = Vec3(FMin(bounds.min.x, box.min.x), FMin(bounds.min.y, box.min.y), FMin(bounds.min.z, box.min.z));
        bounds.max = Vec3(FMax(bounds.max.x, box.max.x), FMax(bounds.max.y, box.max.y), FMax(bounds.max.z, box.max.z));
    }
    if (!(mass > 0.0) || !IsFinite(static_cast<f32>(mass)))
    {
        r = Validation::NotPositive;
        return CompoundShapeConstPtr();
    }
    compound->extent_ = Vec3(FMax(1e-3f, FMax(Abs(bounds.min.x), Abs(bounds.max.x))),
                             FMax(1e-3f, FMax(Abs(bounds.min.y), Abs(bounds.max.y))),
                             FMax(1e-3f, FMax(Abs(bounds.min.z), Abs(bounds.max.z))));
    const Vec3 com(static_cast<f32>(first[0] / mass), static_cast<f32>(first[1] / mass),
                   static_cast<f32>(first[2] / mass));
    // The inertia tensor about the centre of mass: each child's tensor turned into the compound's space plus the
    // parallel-axis term.
    f64 t[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
    for (usize i = 0; i < children.Size(); ++i)
    {
        const MassProperties& m = parts[i];
        const Mat3 frame = (children[i].rotation * m.principal_rotation).ToMat3();
        const f64 diag[3] = {f64{m.inertia.x}, f64{m.inertia.y}, f64{m.inertia.z}};
        const Vec3 d = centres[i] - com;
        const f64 dd[3] = {f64{d.x}, f64{d.y}, f64{d.z}};
        const f64 d2 = dd[0] * dd[0] + dd[1] * dd[1] + dd[2] * dd[2];
        for (u32 row = 0; row < 3; ++row)
        {
            for (u32 col = 0; col < 3; ++col)
            {
                f64 sum = 0.0;
                for (u32 k = 0; k < 3; ++k)
                {
                    sum += f64{frame.col[k][row]} * diag[k] * f64{frame.col[k][col]};
                }
                sum += f64{m.mass} * ((row == col ? d2 : 0.0) - dd[row] * dd[col]);
                t[row][col] += sum;
            }
        }
    }
    const Mat3 tensor = Mat3::FromBasis(
        Vec3(static_cast<f32>(t[0][0]), static_cast<f32>(t[1][0]), static_cast<f32>(t[2][0])),
        Vec3(static_cast<f32>(t[0][1]), static_cast<f32>(t[1][1]), static_cast<f32>(t[2][1])),
        Vec3(static_cast<f32>(t[0][2]), static_cast<f32>(t[1][2]), static_cast<f32>(t[2][2])));
    Vec3 values;
    Mat3 vectors;
    PrincipalAxes(tensor, &values, &vectors);
    compound->mass_.volume = static_cast<f32>(volume);
    compound->mass_.mass = static_cast<f32>(mass);
    compound->mass_.center = com;
    compound->mass_.inertia = values;
    compound->mass_.principal_rotation = Quaternion::FromMat3(vectors).Normalized();
    return compound;
}

const CompoundChild& CompoundShape::Child(u32 i) const
{
    ENGINE_ASSERT(i < children_.Size(), "CompoundShape::Child: index out of range");
    return children_[i];
}

u32 CompoundLibrary::Add(const CompoundShapeConstPtr& compound)
{
    if (!compound)
    {
        return 0;
    }
    compounds_.Push(compound);
    return static_cast<u32>(compounds_.Size());
}

const CompoundShape* CompoundLibrary::Find(u32 id) const
{
    return (id >= 1 && id <= compounds_.Size()) ? compounds_[id - 1].Get() : nullptr;
}

}  // namespace vse::physics
