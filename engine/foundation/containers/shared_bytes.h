#pragma once

//! [[SharedBytes]]: a region of immutable bytes whose owner is shared. Two classes that need the same bytes (a loaded
//! file and the GPU object uploaded from it, and uploaded again after a device reset) do not point into each other:
//! both hold a [[SharedPtr]] to one of these.

#include "containers/span.h"

namespace engine_placeholder
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

}  // namespace engine_placeholder
