// The byte helpers of the audio snapshot (Phase 26 Day 437): a writer and a reader over `bytes::ByteWriter` and
// `bytes::ByteReader` for the values the mixer, the effects and the limiter keep. The reader has a sticky failure:
// after the first bad read every read returns zero and `Ok()` is false, so a parser checks once at the end of a group.
#pragma once

#include "bytes/byte_reader.h"
#include "bytes/byte_writer.h"
#include "containers/vector.h"
#include "types.h"

namespace vse::audio
{

/// Writes the snapshot's value types.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: borrows the writer; it must outlive this object.
class StateOut
{
public:
    /// Wraps a writer.
    /// - Fails: never.
    /// - Ownership: borrows the writer.
    explicit StateOut(bytes::ByteWriter* w) : w_(w) {}
    /// Writes a 32-bit unsigned integer.
    /// - Fails: never.
    void U32(u32 v) { w_->WriteU32(v); }
    /// Writes a 64-bit unsigned integer.
    /// - Fails: never.
    void U64(u64 v) { w_->WriteU64(v); }
    /// Writes a float as its bit pattern.
    /// - Fails: never.
    void F32(f32 v) { w_->WriteF32(v); }
    /// Writes a bool.
    /// - Fails: never.
    void Bool(bool v) { w_->WriteBool(v); }
    /// Writes `count` floats.
    /// - Fails: never.
    void F32s(const f32* v, usize count)
    {
        for (usize i = 0; i < count; ++i)
        {
            w_->WriteF32(v[i]);
        }
    }
    /// Writes a vector of floats with its length.
    /// - Fails: never.
    void Floats(const Vector<f32>& v)
    {
        w_->WriteVarint(v.Size());
        F32s(v.Data(), v.Size());
    }

private:
    bytes::ByteWriter* w_;
};

/// Reads the snapshot's value types, failing by a sticky flag.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: borrows the reader; it must outlive this object.
class StateIn
{
public:
    /// Wraps a reader.
    /// - Fails: never.
    /// - Ownership: borrows the reader.
    explicit StateIn(bytes::ByteReader* r) : r_(r) {}
    /// Whether every read so far succeeded.
    /// - Fails: never.
    bool Ok() const { return ok_; }
    /// Marks the input bad (a value out of range).
    /// - Fails: never.
    void Fail() { ok_ = false; }
    /// Reads a 32-bit unsigned integer.
    /// - Fails: sticky, returns 0.
    u32 U32()
    {
        u32 v = 0;
        Check(r_->ReadU32(&v));
        return ok_ ? v : 0;
    }
    /// Reads a 64-bit unsigned integer.
    /// - Fails: sticky, returns 0.
    u64 U64()
    {
        u64 v = 0;
        Check(r_->ReadU64(&v));
        return ok_ ? v : 0;
    }
    /// Reads a float.
    /// - Fails: sticky, returns 0.
    f32 F32()
    {
        f32 v = 0.0f;
        Check(r_->ReadF32(&v));
        return ok_ ? v : 0.0f;
    }
    /// Reads a bool.
    /// - Fails: sticky, returns false.
    bool Bool()
    {
        bool v = false;
        Check(r_->ReadBool(&v));
        return ok_ && v;
    }
    /// Reads `count` floats.
    /// - Fails: sticky.
    /// - Ownership: `v` is borrowed for the call.
    void F32s(f32* v, usize count)
    {
        for (usize i = 0; i < count; ++i)
        {
            v[i] = F32();
        }
    }
    /// Reads floats into `v`, which must already have the length the writer wrote.
    /// - Fails: sticky, when the length differs.
    /// - Ownership: `v` is borrowed for the call.
    void Floats(Vector<f32>* v)
    {
        u64 n = 0;
        Check(r_->ReadVarint(&n));
        if (!ok_ || n != v->Size() || n > r_->Remaining() / 4)
        {
            ok_ = false;
            return;
        }
        F32s(v->Data(), v->Size());
    }

private:
    void Check(bytes::ByteError e)
    {
        if (e != bytes::ByteError::Ok)
        {
            ok_ = false;
        }
    }

    bytes::ByteReader* r_;
    bool ok_ = true;
};

}  // namespace vse::audio
