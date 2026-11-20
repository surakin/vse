#pragma once

//! [[SharedBytes]]: a region of immutable bytes whose owner is shared. Two classes that need the same bytes (a loaded
//! file and the GPU object uploaded from it, and uploaded again after a device reset) do not point into each other:
//! both hold a [[SharedPtr]] to one of these.

#include "containers/shared_ptr.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "cstr/mem_ops.h"

namespace vse
{

/// Immutable bytes with a shared lifetime. A derived class owns the storage (a pooled read buffer, a vector); holders
/// keep the object alive through a `SharedPtr<const SharedBytes>` and read the bytes through [[SharedBytes::Bytes]]
/// when they need them, never keeping a raw pointer into them.
///
/// - Thread-safety: thread-safe to read; the bytes never change while the object lives.
/// - Ownership: the derived class owns the storage; it is freed when the last `SharedPtr` goes.
class SharedBytes
{
public:
    virtual ~SharedBytes() = default;

    /// The bytes.
    ///
    /// - Fails: never.
    /// - Ownership: the span borrows the storage for as long as the caller holds a `SharedPtr` to this object.
    virtual ByteSpan Bytes() const = 0;
};

/// A [[SharedBytes]] that owns its bytes in a vector: the way to make a shared region from bytes built in memory (a
/// generated mesh, a test's data).
///
/// - Thread-safety: thread-safe to read once constructed.
/// - Ownership: owns the vector it was given.
class OwnedBytes final : public SharedBytes
{
public:
    /// Takes `bytes`.
    explicit OwnedBytes(Vector<byte>&& bytes)
        : bytes_(static_cast<Vector<byte>&&>(bytes)), skip_(0), length_(bytes_.Size())
    {
    }
    /// Takes `bytes`, of which the region is `length` bytes starting `skip` bytes in (so a copy can be placed at an
    /// aligned address).
    OwnedBytes(Vector<byte>&& bytes, usize skip, usize length)
        : bytes_(static_cast<Vector<byte>&&>(bytes)), skip_(skip), length_(length)
    {
    }

    /// The bytes.
    ///
    /// - Fails: never.
    /// - Ownership: borrowed from this object.
    ByteSpan Bytes() const override { return ByteSpan(bytes_.Data() + skip_, length_); }

private:
    Vector<byte> bytes_;
    usize skip_;
    usize length_;
};

/// The way to hold a [[SharedBytes]]: shared and read-only.
using SharedBytesPtr = SharedPtr<const SharedBytes>;

/// Copies `bytes` into a new shared region: for a caller that has a span or a vector and needs to hand a share to
/// something that keeps it (a tool or a test; a loader already holds the region it read).
///
/// - Fails: never.
/// - Ownership: `bytes` is read during the call only; the share owns the copy, which is 64-byte aligned.
inline SharedBytesPtr CopyShared(ByteSpan bytes)
{
    // The copy starts on a 64-byte boundary, as a loaded blob does (sections are aligned relative to the start).
    Vector<byte> copy;
    copy.Resize(bytes.Size() + 63);
    const usize skip = (64 - (reinterpret_cast<usize>(copy.Data()) & 63)) & 63;
    if (bytes.Size() != 0)
    {
        MemCopy(copy.Data() + skip, bytes.Data(), bytes.Size());
    }
    return MakeShared<OwnedBytes>(static_cast<Vector<byte>&&>(copy), skip, bytes.Size());
}

/// As above, for a vector.
///
/// - Fails: never.
/// - Ownership: as above.
inline SharedBytesPtr CopyShared(const Vector<byte>& bytes)
{
    return CopyShared(ByteSpan(bytes.Data(), bytes.Size()));
}

}  // namespace vse
