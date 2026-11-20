#pragma once

//! [[asset::LoadBufferPool]]: reusable, pre-faulted buffers that resources are read into. Phase 22 Day 313 (Spike 5)
//! measured a read into a fresh buffer at four times the cost of a read into one whose pages were already touched, so
//! the pool touches the pages once, when it creates a buffer, and keeps the buffer for the next load of that size.

#include "asset/status.h"
#include "containers/array.h"
#include "containers/vector.h"
#include "sync/mutex.h"
#include "types.h"

namespace vse::asset
{

class LoadBufferPool;

/// One buffer handed out by [[asset::LoadBufferPool::Acquire]]; returning it to the pool is its destructor.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns the buffer until it is released; the pool must outlive it.
class LoadBuffer
{
public:
    LoadBuffer() = default;
    LoadBuffer(const LoadBuffer&) = delete;
    LoadBuffer& operator=(const LoadBuffer&) = delete;
    /// Takes the buffer of `other`, which becomes empty.
    LoadBuffer(LoadBuffer&& other) noexcept;
    /// Releases this buffer, then takes the buffer of `other`, which becomes empty.
    LoadBuffer& operator=(LoadBuffer&& other) noexcept;
    /// Returns the buffer to the pool.
    ~LoadBuffer() { Release(); }

    /// The first byte; 64-byte aligned (page-aligned from 1 MiB), null for an empty handle.
    ///
    /// - Ownership: borrows its pointer arguments for the call only; nothing is retained.
    /// - Fails: never.
    u8* Data() const { return data_; }
    /// The size that was asked for.
    usize Size() const { return size_; }
    /// The size of the buffer behind it (the size class).
    usize Capacity() const { return capacity_; }
    /// True when the handle holds a buffer.
    ///
    /// - Fails: never.
    bool IsValid() const { return data_ != nullptr; }

    /// Hands the buffer back to the pool now.
    void Release();

private:
    friend class LoadBufferPool;
    LoadBufferPool* pool_ = nullptr;
    u8* data_ = nullptr;
    usize size_ = 0;
    usize capacity_ = 0;
};

/// A pool of buffers in power-of-two size classes, bounded by a byte budget.
///
/// - Thread-safety: thread-safe.
/// - Ownership: owns every buffer it created, in use or idle; it must outlive the buffers it handed out.
class LoadBufferPool
{
public:
    /// Makes a pool bounded by `budget_bytes` (`assets.buffer_pool_bytes`): the most bytes of buffers, in use plus
    /// idle.
    explicit LoadBufferPool(u64 budget_bytes);
    LoadBufferPool(const LoadBufferPool&) = delete;
    LoadBufferPool& operator=(const LoadBufferPool&) = delete;
    /// Frees every idle buffer; the buffers still handed out must have been released.
    ~LoadBufferPool();

    /// Hands out a buffer of at least `size` bytes, reusing an idle one of its class when there is one.
    ///
    /// - Fails: `LimitExceeded` when the buffer would take the pool past its budget even after dropping every idle
    ///   buffer; `TooLarge` for a size of `kMaxBlobBytes` or more; `Invalid` for a size of 0; `Other` when memory ran
    ///   out.
    /// - Ownership: `*out` receives the buffer on success; the pool must outlive it.
    Status Acquire(usize size, LoadBuffer* out);

    /// Buffers created since the pool was made (each one is a fresh allocation).
    u64 AllocationCount() const;
    /// Acquisitions served by an idle buffer.
    u64 ReuseCount() const;
    /// Bytes of buffers in use now.
    u64 BytesInUse() const;
    /// Bytes of idle buffers kept for reuse.
    u64 BytesIdle() const;

private:
    friend class LoadBuffer;
    static constexpr usize kClassCount = 40;

    void Give(u8* data, usize capacity);
    static usize ClassOf(usize capacity);
    void FreeIdleLocked();

    mutable Mutex mutex_;
    u64 budget_;
    u64 in_use_ = 0;
    u64 idle_ = 0;
    u64 allocations_ = 0;
    u64 reuses_ = 0;
    Array<Vector<u8*>, kClassCount> free_;
};

}  // namespace vse::asset
