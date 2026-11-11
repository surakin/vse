#pragma once

#include <chrono>
#include <thread>

#include "lang/traits.h"
#include "lang/utility.h"
#include "sync/atomic.h"
#include "types.h"

//! Threads (Phase 16): the engine's spelling of `<thread>`. Names, priorities and affinity are `os`'s, not this
//! header's.
namespace engine_placeholder
{

/// Identifies a thread for comparison and hashing (`std::thread::id`).
///
/// - Thread-safety: thread-compatible plain data.
class ThreadId
{
public:
    /// The id that names no thread.
    ThreadId() noexcept = default;
    /// Wraps a standard id (for the sync module's own use).
    explicit ThreadId(std::thread::id id) noexcept : id_(id) {}
    friend bool operator==(ThreadId a, ThreadId b) noexcept { return a.id_ == b.id_; }
    friend bool operator!=(ThreadId a, ThreadId b) noexcept { return a.id_ != b.id_; }
    /// A hash of the id, stable within the process.
    usize Hash() const noexcept { return std::hash<std::thread::id>()(id_); }
    /// The standard id, for [[sync]] internals only.
    std::thread::id Native() const noexcept { return id_; }

private:
    std::thread::id id_;
};

/// An operating-system thread that runs a function (`std::thread`).
///
/// A joinable `Thread` must be joined or detached before it is destroyed; destroying a joinable thread terminates the
/// program.
///
/// - Thread-safety: thread-compatible. One thread owns a `Thread` object.
/// - Ownership: movable, not copyable; the object owns the right to join.
class Thread
{
public:
    /// A thread object that runs nothing.
    Thread() noexcept = default;
    /// Starts a thread that calls `function(args...)` with copies of `args`.
    template <class F, class... Args, EnableIf<!IsSame<RemoveCVRef<F>, Thread>, int> = 0>
    explicit Thread(F&& function, Args&&... args) : native_(static_cast<F&&>(function), static_cast<Args&&>(args)...)
    {
    }
    Thread(Thread&&) noexcept = default;
    Thread& operator=(Thread&&) noexcept = default;
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;

    /// Whether the object represents a running thread that was neither joined nor detached.
    bool Joinable() const noexcept { return native_.joinable(); }
    /// Waits for the thread to finish.
    void Join() { native_.join(); }
    /// Lets the thread run on its own.
    void Detach() { native_.detach(); }
    /// The thread's id; the empty id when not joinable.
    ThreadId Id() const noexcept { return ThreadId(native_.get_id()); }
    /// Exchanges two thread objects.
    void Swap(Thread& other) noexcept { native_.swap(other.native_); }

    /// A hint for the number of threads the hardware runs at once; 0 when unknown.
    static u32 HardwareConcurrency() noexcept { return static_cast<u32>(std::thread::hardware_concurrency()); }

private:
    std::thread native_;
};

/// Operations on the calling thread (`std::this_thread`).
namespace ThisThread
{

/// The calling thread's id.
inline ThreadId Id() noexcept
{
    return ThreadId(std::this_thread::get_id());
}
/// Offers the processor to another thread.
inline void Yield() noexcept
{
    std::this_thread::yield();
}
/// Blocks the calling thread for at least `milliseconds`.
inline void SleepForMilliseconds(u64 milliseconds)
{
    std::this_thread::sleep_for(
        std::chrono::milliseconds(detail_sync::ClampDuration(milliseconds, detail_sync::kMaxWaitMilliseconds)));
}
/// Blocks the calling thread for at least `microseconds`.
inline void SleepForMicroseconds(u64 microseconds)
{
    std::this_thread::sleep_for(
        std::chrono::microseconds(detail_sync::ClampDuration(microseconds, detail_sync::kMaxWaitMicroseconds)));
}

}  // namespace ThisThread

}  // namespace engine_placeholder
