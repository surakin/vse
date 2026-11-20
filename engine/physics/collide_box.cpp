// Box-box and box-capsule narrow phase tests (Phase 25 Day 392). Only `+ - * /` and `Sqrt` are used.
#include "containers/array.h"
#include "lang/scalar.h"
#include "physics/collide_math.h"
#include "physics/contact.h"

namespace vse::physics::collide_math
{

namespace
{

// A later face axis replaces the best only when it is better by this much, so equal faces (a cube on a cube) keep the
// first one and the feature ids stay the same from step to step.
constexpr f32 kFaceBias = 1e-4f;
// An edge axis wins over the best face only when it is better by this much.
constexpr f32 kEdgeBias = 1e-3f;
// Edge axes whose cross product is shorter than this (squared) are skipped: the edges are parallel.
constexpr f32 kParallelSquared = 1e-6f;

// The separation of two boxes along a unit axis: negative when they overlap along it (minus the overlap).
f32 BoxSeparation(const Vec3& d, const Vec3& n, const Array<Vec3, 3>& ax, const Vec3& ha, const Array<Vec3, 3>& bx,
                  const Vec3& hb)
{
    f32 sep = Abs(d.Dot(n));
    for (u32 k = 0; k < 3; ++k)
    {
        sep -= ha[k] * Abs(ax[k].Dot(n)) + hb[k] * Abs(bx[k].Dot(n));
    }
    return sep;
}

}  // namespace

bool BoxBox(const Shape& a, const Pose& pa, const Shape& b, const Pose& pb, f32 margin, ContactManifold* out)
{
    const Mat3 ra = pa.rotation.ToMat3();
    const Mat3 rb = pb.rotation.ToMat3();
    Array<Vec3, 3> ax;
    Array<Vec3, 3> bx;
    for (u32 k = 0; k < 3; ++k)
    {
        ax[k] = ra.col[k];
        bx[k] = rb.col[k];
    }
    const Vec3 ha = a.half_extents;
    const Vec3 hb = b.half_extents;
    const Vec3 d = pb.position - pa.position;

    // The 6 face axes: the best is the one with the largest separation (the least overlap).
    f32 best_face = -1e30f;
    u32 face = 0;
    for (u32 f = 0; f < 6; ++f)
    {
        const Vec3& n = f < 3 ? ax[f] : bx[f - 3];
        const f32 sep = BoxSeparation(d, n, ax, ha, bx, hb);
        if (sep > margin)
        {
            return false;
        }
        if (sep > best_face + kFaceBias)
        {
            best_face = sep;
            face = f;
        }
    }
    // The 9 edge axes.
    f32 best_edge = -1e30f;
    u32 edge_i = 0;
    u32 edge_j = 0;
    Vec3 edge_axis(0.0f, 1.0f, 0.0f);
    bool have_edge = false;
    for (u32 i = 0; i < 3; ++i)
    {
        for (u32 j = 0; j < 3; ++j)
        {
            const Vec3 c = ax[i].Cross(bx[j]);
            const f32 l2 = c.LengthSquared();
            if (l2 < kParallelSquared)
            {
                continue;
            }
            const Vec3 n = c * (1.0f / Sqrt(l2));
            const f32 sep = BoxSeparation(d, n, ax, ha, bx, hb);
            if (sep > margin)
            {
                return false;
            }
            if (sep > best_edge)
            {
                best_edge = sep;
                edge_i = i;
                edge_j = j;
                edge_axis = n;
                have_edge = true;
            }
        }
    }

    if (have_edge && best_edge > best_face + kEdgeBias)
    {
        // Edge against edge: one point between the two closest edges.
        const Vec3 nd = d.Dot(edge_axis) >= 0.0f ? edge_axis : Negate(edge_axis);
        Vec3 ea = pa.position;
        Vec3 eb = pb.position;
        u32 signs = 0;
        for (u32 k = 0; k < 3; ++k)
        {
            if (k != edge_i)
            {
                const bool plus = ax[k].Dot(nd) >= 0.0f;
                ea = ea + ax[k] * (plus ? ha[k] : -ha[k]);
                signs = signs * 2u + (plus ? 1u : 0u);
            }
            if (k != edge_j)
            {
                const bool plus = bx[k].Dot(nd) < 0.0f;
                eb = eb + bx[k] * (plus ? hb[k] : -hb[k]);
                signs = signs * 2u + (plus ? 1u : 0u);
            }
        }
        const Segment sa{ea - ax[edge_i] * ha[edge_i], ea + ax[edge_i] * ha[edge_i]};
        const Segment sb{eb - bx[edge_j] * hb[edge_j], eb + bx[edge_j] * hb[edge_j]};
        Vec3 ca;
        Vec3 cb;
        ClosestSegments(sa, sb, &ca, &cb);
        out->normal = nd;
        out->count = 1;
        out->points[0].position_a = ca;
        out->points[0].position_b = cb;
        out->points[0].depth = (ca - cb).Dot(nd);
        out->points[0].feature_id = 0x80000000u | (edge_i << 10) | (edge_j << 8) | signs;
        return true;
    }

    // Face against face: clip the incident face of one box to the reference face of the other.
    const bool ref_is_b = face >= 3;
    const u32 k = face % 3;
    const Array<Vec3, 3>& rax = ref_is_b ? bx : ax;
    const Array<Vec3, 3>& iax = ref_is_b ? ax : bx;
    const Vec3& hr = ref_is_b ? hb : ha;
    const Vec3& hi = ref_is_b ? ha : hb;
    const Vec3& pos_r = ref_is_b ? pb.position : pa.position;
    const Vec3& pos_i = ref_is_b ? pa.position : pb.position;
    const f32 s = rax[k].Dot(pos_i - pos_r) >= 0.0f ? 1.0f : -1.0f;
    const Vec3 nr = rax[k] * s;  // from the reference box toward the incident one

    u32 m = 0;
    f32 most = -1.0f;
    for (u32 q = 0; q < 3; ++q)
    {
        const f32 dot = Abs(nr.Dot(iax[q]));
        if (dot > most + 1e-6f)
        {
            most = dot;
            m = q;
        }
    }
    const f32 si = nr.Dot(iax[m]) > 0.0f ? -1.0f : 1.0f;
    const Vec3 ci = pos_i + iax[m] * (si * hi[m]);
    const u32 p1 = (m + 1) % 3;
    const u32 p2 = (m + 2) % 3;
    const Vec3 e1 = iax[p1] * hi[p1];
    const Vec3 e2 = iax[p2] * hi[p2];

    Array<ClipVertex, 16> buf0;
    Array<ClipVertex, 16> buf1;
    buf0[0].p = ci + e1 + e2;
    buf0[1].p = ci - e1 + e2;
    buf0[2].p = ci - e1 - e2;
    buf0[3].p = ci + e1 - e2;
    for (u32 v = 0; v < 4; ++v)
    {
        buf0[v].id = v;
    }
    u32 count = 4;
    const u32 u = (k + 1) % 3;
    const u32 w = (k + 2) % 3;
    for (u32 plane = 0; plane < 4; ++plane)
    {
        const u32 side = plane < 2 ? u : w;
        const Vec3 n = (plane % 2 == 0) ? rax[side] : Negate(rax[side]);
        const f32 offset = pos_r.Dot(n) + hr[side];
        const ClipVertex* in = (plane % 2 == 0) ? buf0.Data() : buf1.Data();
        ClipVertex* dest = (plane % 2 == 0) ? buf1.Data() : buf0.Data();
        count = ClipPolygon(in, count, n, offset, plane, dest, 16);
        if (count == 0)
        {
            break;
        }
    }
    // Four clips leave the result in buf0 (0 -> 1 -> 0 -> 1 -> 0).
    const Vec3 cr = pos_r + nr * hr[k];
    Array<Candidate, 16> cand;
    u32 kept = 0;
    for (u32 v = 0; v < count; ++v)
    {
        const f32 depth = -(buf0[v].p - cr).Dot(nr);
        if (depth >= -margin)
        {
            cand[kept].p = buf0[v].p;
            cand[kept].depth = depth;
            cand[kept].id = buf0[v].id;
            ++kept;
        }
    }
    if (kept == 0)
    {
        // The clipped face is empty or beyond the margin (a nearly edge-to-edge touch): the deepest corner of the
        // incident face stands for it.
        u32 deepest = 0;
        f32 deepest_depth = -1e30f;
        for (u32 v = 0; v < 4; ++v)
        {
            const Vec3 corner = v == 0 ? ci + e1 + e2
                                       : (v == 1 ? ci - e1 + e2 : (v == 2 ? ci - e1 - e2 : ci + e1 - e2));
            const f32 depth = -(corner - cr).Dot(nr);
            if (depth > deepest_depth)
            {
                deepest_depth = depth;
                deepest = v;
                cand[0].p = corner;
            }
        }
        cand[0].depth = deepest_depth;
        cand[0].id = deepest;
        kept = 1;
    }

    Array<u32, 4> pick;
    const u32 picked = ReduceCandidates(cand.Data(), kept, nr, &pick);

    out->normal = ref_is_b ? Negate(nr) : nr;
    out->count = picked;
    const u32 ref_face = k * 2u + (s < 0.0f ? 1u : 0u);
    const u32 inc_face = m * 2u + (si < 0.0f ? 1u : 0u);
    const u32 face_code = (ref_is_b ? (1u << 30) : 0u) | (ref_face << 27) | (inc_face << 24);
    for (u32 v = 0; v < picked; ++v)
    {
        const Candidate& c = cand[pick[v]];
        const Vec3 on_ref = c.p + nr * c.depth;
        out->points[v].position_a = ref_is_b ? c.p : on_ref;
        out->points[v].position_b = ref_is_b ? on_ref : c.p;
        out->points[v].depth = c.depth;
        out->points[v].feature_id = face_code | (c.id & 0xFFFFu);
    }
    return true;
}

bool BoxCapsule(const Shape& a, const Pose& pa, const Shape& b, const Pose& pb, f32 margin, ContactManifold* out)
{
    const Mat3 ra = pa.rotation.ToMat3();
    const Vec3 h = a.half_extents;
    const f32 r = b.radius;
    // The capsule's axis in the box's frame (R^T x).
    const Segment world = CapsuleSegment(b, pb);
    const Vec3 wp = world.p - pa.position;
    const Vec3 wq = world.q - pa.position;
    const Vec3 p(ra.col[0].Dot(wp), ra.col[1].Dot(wp), ra.col[2].Dot(wp));
    const Vec3 q(ra.col[0].Dot(wq), ra.col[1].Dot(wq), ra.col[2].Dot(wq));
    const Vec3 g = q - p;

    // The axis crosses the planes of the box's slabs at most six times; between two crossings the nearest point of
    // the box moves linearly with the axis, so the distance has a closed-form minimum on each piece.
    Array<f32, 8> cuts;
    u32 cut_count = 0;
    cuts[cut_count++] = 0.0f;
    for (u32 k = 0; k < 3; ++k)
    {
        if (Abs(g[k]) > 1e-12f)
        {
            const f32 t0 = (-h[k] - p[k]) / g[k];
            const f32 t1 = (h[k] - p[k]) / g[k];
            if (t0 > 0.0f && t0 < 1.0f)
            {
                cuts[cut_count++] = t0;
            }
            if (t1 > 0.0f && t1 < 1.0f)
            {
                cuts[cut_count++] = t1;
            }
        }
    }
    cuts[cut_count++] = 1.0f;
    for (u32 i = 1; i < cut_count; ++i)  // insertion sort
    {
        const f32 value = cuts[i];
        u32 j = i;
        while (j > 0 && cuts[j - 1] > value)
        {
            cuts[j] = cuts[j - 1];
            --j;
        }
        cuts[j] = value;
    }

    f32 best_d2 = 1e30f;
    f32 best_t = 0.0f;
    f32 best_t0 = 0.0f;
    f32 best_t1 = 0.0f;
    Vec3 best_gv(0.0f, 0.0f, 0.0f);
    for (u32 i = 0; i + 1 < cut_count; ++i)
    {
        const f32 t0 = cuts[i];
        const f32 t1 = cuts[i + 1];
        if (!(t1 > t0) && cut_count > 2)
        {
            continue;
        }
        const Vec3 mid = p + g * (0.5f * (t0 + t1));
        Vec3 e(0.0f, 0.0f, 0.0f);
        Vec3 gv(0.0f, 0.0f, 0.0f);
        for (u32 k = 0; k < 3; ++k)
        {
            if (mid[k] > h[k])
            {
                e[k] = p[k] - h[k];
                gv[k] = g[k];
            }
            else if (mid[k] < -h[k])
            {
                e[k] = p[k] + h[k];
                gv[k] = g[k];
            }
        }
        const f32 gg = gv.LengthSquared();
        const f32 t = gg > 1e-12f ? Clamp(-e.Dot(gv) / gg, t0, t1) : t0;
        const Vec3 delta = e + gv * t;
        const f32 d2 = delta.LengthSquared();
        if (d2 < best_d2 - 1e-9f)
        {
            best_d2 = d2;
            best_t = t;
            best_t0 = t0;
            best_t1 = t1;
            best_gv = gv;
        }
    }

    const f32 reach = r + margin;
    Vec3 n_local;
    u32 count = 0;
    Array<f32, 2> ts;
    Array<u32, 2> ids;
    Array<Vec3, 2> surface;  // the point of the box's surface, per contact
    Array<Vec3, 2> axis;     // the point of the capsule's axis, per contact
    // The nearest point of the axis, recomputed from the point itself: the closed-form minimum is only used to find it.
    const Vec3 nearest_axis = p + g * best_t;
    const Vec3 nearest_box(Clamp(nearest_axis.x, -h.x, h.x), Clamp(nearest_axis.y, -h.y, h.y),
                           Clamp(nearest_axis.z, -h.z, h.z));
    const Vec3 gap = nearest_axis - nearest_box;
    const f32 gap2 = gap.LengthSquared();
    if (gap2 > 1e-10f)
    {
        if (gap2 > reach * reach)
        {
            return false;
        }
        n_local = gap * (1.0f / Sqrt(gap2));
        const f32 span = (best_t1 - best_t0);
        const bool flat = best_gv.LengthSquared() * span * span <= 1e-8f && span * Sqrt(g.LengthSquared()) > 1e-3f;
        ts[0] = flat ? best_t0 : best_t;
        count = 1;
        if (flat)
        {
            ts[1] = best_t1;
            count = 2;
        }
        // The region is read at the middle of the stretch: the ends of a flat stretch lie exactly on the boundary of a
        // slab, where rounding would flip the region from step to step.
        const Vec3 mid = p + g * (flat ? 0.5f * (best_t0 + best_t1) : best_t);
        const u32 rx = mid.x < -h.x ? 1u : (mid.x > h.x ? 2u : 0u);
        const u32 ry = mid.y < -h.y ? 1u : (mid.y > h.y ? 2u : 0u);
        const u32 rz = mid.z < -h.z ? 1u : (mid.z > h.z ? 2u : 0u);
        for (u32 c = 0; c < count; ++c)
        {
            const Vec3 pt = p + g * ts[c];
            ids[c] = 1u + rx + 3u * ry + 9u * rz + (flat ? 32u * (c + 1u) : 0u);
            axis[c] = pt;
            surface[c] = Vec3(Clamp(pt.x, -h.x, h.x), Clamp(pt.y, -h.y, h.y), Clamp(pt.z, -h.z, h.z));
        }
    }
    else
    {
        // The axis meets the box: leave through the face with the least penetration of the whole capsule.
        f32 least = 1e30f;
        u32 face = 0;
        for (u32 k = 0; k < 3; ++k)
        {
            const f32 lo = FMin(p[k], q[k]);
            const f32 hi = FMax(p[k], q[k]);
            const f32 up = h[k] + r - lo;    // pushed toward +k
            const f32 down = h[k] + r + hi;  // pushed toward -k
            if (up < least)
            {
                least = up;
                face = k * 2u;
            }
            if (down < least)
            {
                least = down;
                face = k * 2u + 1u;
            }
        }
        const u32 k = face / 2u;
        const f32 sign = (face % 2u == 0u) ? 1.0f : -1.0f;
        n_local = Vec3(0.0f, 0.0f, 0.0f);
        n_local[k] = sign;
        const f32 hp = sign * p[k];
        const f32 hq = sign * q[k];
        // The end that goes deepest (the least coordinate along the normal) first; the other too when it is as deep.
        const bool p_first = hp <= hq;
        ts[0] = p_first ? 0.0f : 1.0f;
        count = 1;
        if (Abs(hp - hq) < 1e-3f)
        {
            ts[1] = p_first ? 1.0f : 0.0f;
            count = 2;
        }
        for (u32 c = 0; c < count; ++c)
        {
            const Vec3 pt = p + g * ts[c];
            axis[c] = pt;
            Vec3 on = pt;
            on[k] = sign * h[k];
            surface[c] = on;
            ids[c] = 100u + face * 2u + c;
        }
    }

    out->normal = ra * n_local;  // from the box (A) toward the capsule (B)
    out->count = count;
    for (u32 c = 0; c < count; ++c)
    {
        const Vec3 on_box = surface[c];
        const Vec3 on_capsule = axis[c] - n_local * r;
        out->points[c].position_a = pa.position + ra * on_box;
        out->points[c].position_b = pa.position + ra * on_capsule;
        out->points[c].depth = (on_box - on_capsule).Dot(n_local);
        out->points[c].feature_id = ids[c];
    }
    return true;
}

}  // namespace vse::physics::collide_math
