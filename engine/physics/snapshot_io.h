// The byte helpers of the physics snapshot (Phase 25 Day 406). Internal to the physics module: a writer and a reader
// over `bytes::ByteWriter` and `bytes::ByteReader` (the canonical encoding of the storage module) for the types the
// snapshot holds. The reader has a sticky failure: after the first bad read every read returns zero and `ok` is false,
// so a parser checks once at the end of a group.
#pragma once

#include "bytes/byte_reader.h"
#include "bytes/byte_writer.h"
#include "lang/scalar.h"
#include "math/matrix.h"
#include "math/quaternion.h"
#include "math/vector.h"
#include "physics/components.h"
#include "physics/ids.h"
#include "types.h"

namespace engine_placeholder::physics
{

/// Writes the snapshot's value types.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: borrows the writer; it must outlive this object.
class SnapshotOut
{
public:
    /// Wraps a writer.
    /// - Ownership: borrows the writer.
    /// - Fails: never.
    explicit SnapshotOut(bytes::ByteWriter* w) : w_(w) {}
    /// Writes an unsigned byte.
    void U8(u32 v) { w_->WriteU8(static_cast<u8>(v)); }
    /// Writes a 32-bit unsigned integer.
    void U32(u32 v) { w_->WriteU32(v); }
    /// Writes a 64-bit unsigned integer.
    void U64(u64 v) { w_->WriteU64(v); }
    /// Writes a float as its bit pattern.
    void F32(f32 v) { w_->WriteF32(v); }
    /// Writes a double as its bit pattern.
    void F64(f64 v) { w_->WriteF64(v); }
    /// Writes a bool.
    void Bool(bool v) { w_->WriteBool(v); }
    /// Writes a count as a varint.
    void Count(usize v) { w_->WriteVarint(v); }
    /// Writes a vector.
    void V3(const Vec3& v)
    {
        F32(v.x);
        F32(v.y);
        F32(v.z);
    }
    /// Writes a quaternion.
    void Q(const Quaternion& q)
    {
        F32(q.x);
        F32(q.y);
        F32(q.z);
        F32(q.w);
    }
    /// Writes a matrix by columns.
    void M3(const Mat3& m)
    {
        V3(m.col[0]);
        V3(m.col[1]);
        V3(m.col[2]);
    }
    /// Writes a slot id.
    void Id(u32 index, u32 generation)
    {
        U32(index);
        U32(generation);
    }

private:
    bytes::ByteWriter* w_;
};

/// Reads them back, with a sticky failure.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: borrows the data; it must outlive this object.
class SnapshotIn
{
public:
    /// Wraps a span.
    explicit SnapshotIn(ByteSpan data) : r_(data) {}
    /// Whether every read so far succeeded.
    /// - Fails: never.
    bool Ok() const { return ok_; }
    /// Marks the read as failed (a value that is read well but is not valid).
    void Fail() { ok_ = false; }
    /// The bytes left.
    usize Remaining() const { return r_.Remaining(); }
    /// Reads an unsigned byte.
    u32 U8()
    {
        u8 v = 0;
        Check(r_.ReadU8(&v));
        return v;
    }
    /// Reads a 32-bit unsigned integer.
    u32 U32()
    {
        u32 v = 0;
        Check(r_.ReadU32(&v));
        return v;
    }
    /// Reads a 64-bit unsigned integer.
    u64 U64()
    {
        u64 v = 0;
        Check(r_.ReadU64(&v));
        return v;
    }
    /// Reads a float.
    f32 F32()
    {
        f32 v = 0.0f;
        Check(r_.ReadF32(&v));
        return v;
    }
    /// Reads a double.
    f64 F64()
    {
        f64 v = 0.0;
        Check(r_.ReadF64(&v));
        return v;
    }
    /// Reads a bool (anything but 0 or 1 fails).
    /// - Fails: sets the failure `Ok` reports.
    bool Bool()
    {
        bool v = false;
        Check(r_.ReadBool(&v));
        return v;
    }
    /// Reads a count that is at most `limit`; a larger one fails.
    usize Count(usize limit)
    {
        u64 v = 0;
        Check(r_.ReadVarint(&v));
        if (v > limit)
        {
            ok_ = false;
            return 0;
        }
        return static_cast<usize>(v);
    }
    /// Reads a vector.
    Vec3 V3()
    {
        const f32 x = F32();
        const f32 y = F32();
        const f32 z = F32();
        return Vec3(x, y, z);
    }
    /// Reads a quaternion.
    Quaternion Q()
    {
        const f32 x = F32();
        const f32 y = F32();
        const f32 z = F32();
        const f32 w = F32();
        return Quaternion(x, y, z, w);
    }
    /// Reads a matrix.
    Mat3 M3()
    {
        Mat3 m = Mat3::Identity();
        m.col[0] = V3();
        m.col[1] = V3();
        m.col[2] = V3();
        return m;
    }

private:
    void Check(bytes::ByteError error)
    {
        if (error != bytes::ByteError::Ok)
        {
            ok_ = false;
        }
    }
    bytes::ByteReader r_;
    bool ok_ = true;
};

/// Writes a collider's data (shape, offset, rotation, material, layer, trigger flag, group and mask).
///
/// - Ownership: borrows the writer for the call.
/// - Fails: never.
void WriteColliderValue(SnapshotOut& out, const Collider& collider);
/// Reads what `WriteColliderValue` wrote (the values are not checked: call `Collider::Validate`).
///
/// - Ownership: borrows the reader for the call.
/// - Fails: the reader goes into failure on truncated data (`SnapshotIn::Ok`).
void ReadColliderValue(SnapshotIn& in, Collider* collider);

/// Whether a number is a usable value of simulation state: finite and at most 1e9 in size (a snapshot refuses NaN,
/// infinities and magnitudes at which the step's arithmetic would overflow).
///
/// - Fails: never.
inline bool SnapshotFinite(f32 v)
{
    return Abs(v) <= 1.0e9f;
}
/// Whether all three components are finite.
///
/// - Fails: never.
inline bool SnapshotFinite(const Vec3& v)
{
    return SnapshotFinite(v.x) && SnapshotFinite(v.y) && SnapshotFinite(v.z);
}
/// Whether the four components are finite.
///
/// - Fails: never.
inline bool SnapshotFinite(const Quaternion& q)
{
    return SnapshotFinite(q.x) && SnapshotFinite(q.y) && SnapshotFinite(q.z) && SnapshotFinite(q.w);
}

}  // namespace engine_placeholder::physics
