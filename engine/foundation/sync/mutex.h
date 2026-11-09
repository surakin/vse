#pragma once

#include <condition_variable>
#include <mutex>

#include "lang/utility.h"
#include "sync/atomic.h"
#include "types.h"

//! Mutual exclusion (Phase 16): the engine's spelling of `<mutex>` and `<condition_variable>`. Every type forwards inline to the
//! standard one.
namespace engine_placeholder {

/// A lock that one thread at a time can hold (`std::mutex`). Not recursive: locking it twice from one thread deadlocks.
///
/// Thread-safety: thread-safe. `Unlock` must come from the thread that locked.
/// Ownership: neither copyable nor movable.
class Mutex {
public:
    /// The standard type that implements this lock; used by [[LockGuard]], [[UniqueLock]] and [[ConditionVariable]] only.
    using Native = std::mutex;

    constexpr Mutex() noexcept = default;
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

    /// Blocks until the lock is held by the calling thread.
    void Lock() { native_.lock(); }
    /// Takes the lock if it is free; returns whether it did.
    bool TryLock() { return native_.try_lock(); }
    /// Releases the lock; the calling thread must hold it.
    void Unlock() { native_.unlock(); }
    /// The standard mutex behind this one, for the sync types only.
    Native& NativeMutex() { return native_; }

private:
    std::mutex native_;
};

/// A lock the same thread may take again (`std::recursive_mutex`); it must be unlocked as many times as it was locked.
///
/// Thread-safety: thread-safe, as [[Mutex]].
class RecursiveMutex {
public:
    /// The standard type that implements this lock.
    using Native = std::recursive_mutex;

    RecursiveMutex() = default;
    RecursiveMutex(const RecursiveMutex&) = delete;
    RecursiveMutex& operator=(const RecursiveMutex&) = delete;

    /// Blocks until the lock is held by the calling thread (it may already hold it).
    void Lock() { native_.lock(); }
    /// Takes the lock if it is free or already held by this thread; returns whether it did.
    bool TryLock() { return native_.try_lock(); }
    /// Releases one level of the lock.
    void Unlock() { native_.unlock(); }
    /// The standard mutex behind this one, for the sync types only.
    Native& NativeMutex() { return native_; }

private:
    std::recursive_mutex native_;
};

/// Holds a lock for the length of a scope (`std::lock_guard`): locks in the constructor, unlocks in the destructor.
///
/// Thread-safety: thread-compatible. The guard belongs to one thread.
template <class M>
class LockGuard {
public:
    /// Locks `mutex`, which must outlive the guard.
    explicit LockGuard(M& mutex) : guard_(mutex.NativeMutex()) {}
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;

private:
    std::lock_guard<typename M::Native> guard_;
};

/// A lock holder that can unlock early, relock, and wait on a [[ConditionVariable]] (`std::unique_lock`).
///
/// Thread-safety: thread-compatible. The holder belongs to one thread.
template <class M>
class UniqueLock {
public:
    /// A holder that owns no lock.
    UniqueLock() noexcept = default;
    /// Locks `mutex`, which must outlive the holder.
    explicit UniqueLock(M& mutex) : lock_(mutex.NativeMutex()) {}
    /// Associates with `mutex` without locking it.
    UniqueLock(M& mutex, bool lock_now) : lock_(mutex.NativeMutex(), std::defer_lock) {
        if (lock_now) lock_.lock();
    }
    UniqueLock(UniqueLock&& other) noexcept = default;
    UniqueLock& operator=(UniqueLock&& other) noexcept = default;

    /// Blocks until the lock is held.
    void Lock() { lock_.lock(); }
    /// Takes the lock if it is free; returns whether it did.
    bool TryLock() { return lock_.try_lock(); }
    /// Releases the lock early; the holder must own it.
    void Unlock() { lock_.unlock(); }
    /// Whether the holder owns the lock now.
    bool OwnsLock() const noexcept { return lock_.owns_lock(); }
    /// The standard holder, for [[ConditionVariable]] only.
    std::unique_lock<typename M::Native>& NativeLock() { return lock_; }

private:
    std::unique_lock<typename M::Native> lock_;
};

/// Lets threads sleep until another thread signals a change (`std::condition_variable`).
///
/// Waits can wake without a signal: use the predicate forms, or re-check the condition in a loop.
///
/// Thread-safety: thread-safe. The lock passed to a wait must be held by the calling thread.
class ConditionVariable {
public:
    ConditionVariable() = default;
    ConditionVariable(const ConditionVariable&) = delete;
    ConditionVariable& operator=(const ConditionVariable&) = delete;

    /// Unlocks `lock`, sleeps until notified (or spuriously), and relocks it before returning.
    void Wait(UniqueLock<Mutex>& lock) { native_.wait(lock.NativeLock()); }
    /// Sleeps until `ready()` is true, re-checking after every wake.
    template <class Predicate>
    void Wait(UniqueLock<Mutex>& lock, Predicate ready) {
        native_.wait(lock.NativeLock(), Move(ready));
    }
    /// As `Wait` but gives up after `milliseconds`; returns false when it timed out with `ready()` still false.
    template <class Predicate>
    bool WaitForMilliseconds(UniqueLock<Mutex>& lock, u64 milliseconds, Predicate ready) {
        return native_.wait_for(lock.NativeLock(), std::chrono::milliseconds(detail_sync::ClampDuration(milliseconds, detail_sync::kMaxWaitMilliseconds)), Move(ready));
    }
    /// Sleeps for at most `milliseconds` or until notified; returns false when the time ran out.
    bool WaitForMilliseconds(UniqueLock<Mutex>& lock, u64 milliseconds) {
        return native_.wait_for(lock.NativeLock(), std::chrono::milliseconds(detail_sync::ClampDuration(milliseconds, detail_sync::kMaxWaitMilliseconds))) == std::cv_status::no_timeout;
    }
    /// Wakes one waiting thread, if any.
    void NotifyOne() noexcept { native_.notify_one(); }
    /// Wakes every waiting thread.
    void NotifyAll() noexcept { native_.notify_all(); }

private:
    std::condition_variable native_;
};

/// Marks a one-time initialization for [[CallOnce]] (`std::once_flag`).
class OnceFlag {
public:
    constexpr OnceFlag() noexcept = default;
    OnceFlag(const OnceFlag&) = delete;
    OnceFlag& operator=(const OnceFlag&) = delete;
    /// The standard flag behind this one, for [[CallOnce]] only.
    std::once_flag& NativeFlag() { return flag_; }

private:
    std::once_flag flag_;
};

/// Runs `function(args...)` exactly once for `flag` across all threads; later callers wait for the first to finish (`std::call_once`).
template <class F, class... Args>
void CallOnce(OnceFlag& flag, F&& function, Args&&... args) {
    std::call_once(flag.NativeFlag(), static_cast<F&&>(function), static_cast<Args&&>(args)...);
}

} // namespace engine_placeholder
