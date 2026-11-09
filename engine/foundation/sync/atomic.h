#pragma once

#include <atomic>

#include "lang/traits.h"
#include "types.h"

//! Atomic variables (Phase 16): the engine's spelling of `<atomic>`. `Atomic<T>` forwards every operation to `std::atomic<T>`
//! inline, so it costs exactly what the standard type costs. This header and the other `sync/` headers are the only files that
//! include the standard concurrency headers.
namespace engine_placeholder {

/// How an atomic operation orders the memory accesses around it (`std::memory_order`).
///
/// `Relaxed` orders nothing; `Acquire` on a load and `Release` on a store pair up so what the writer did before the store is visible
/// after the load; `AcqRel` does both for a read-modify-write; `SeqCst` is a single total order and the default. `Consume` is
/// treated as `Acquire`, as every implementation does.
enum class MemoryOrder : u8 {
    /// No ordering beyond the atomicity of the operation itself.
    Relaxed,
    /// Treated as `Acquire`.
    Consume,
    /// A load that sees a `Release` store also sees everything written before that store.
    Acquire,
    /// A store that publishes everything written before it to a thread that loads it with `Acquire`.
    Release,
    /// Both `Acquire` and `Release`, for a read-modify-write.
    AcqRel,
    /// A single total order across all `SeqCst` operations (the default).
    SeqCst,
};

namespace detail_sync {

/// The standard order for an engine order.
constexpr std::memory_order ToStd(MemoryOrder order) noexcept {
    switch (order) {
    case MemoryOrder::Relaxed: return std::memory_order_relaxed;
    case MemoryOrder::Consume: return std::memory_order_acquire;
    case MemoryOrder::Acquire: return std::memory_order_acquire;
    case MemoryOrder::Release: return std::memory_order_release;
    case MemoryOrder::AcqRel: return std::memory_order_acq_rel;
    case MemoryOrder::SeqCst: return std::memory_order_seq_cst;
    }
    return std::memory_order_seq_cst;
}

/// The strongest failure order that is valid for a given success order (a failure cannot release).
constexpr MemoryOrder FailureOrder(MemoryOrder success) noexcept {
    switch (success) {
    case MemoryOrder::Release: return MemoryOrder::Relaxed;
    case MemoryOrder::AcqRel: return MemoryOrder::Acquire;
    default: return success;
    }
}

/// A duration count limited to what the standard clocks can add to "now" without overflowing their nanosecond count: `max_count` is about
/// 100 years in the caller's unit, so a "wait forever" such as ~0 becomes a very long wait instead of wrapping into the past.
constexpr i64 ClampDuration(u64 count, u64 max_count) noexcept {
    return static_cast<i64>(count > max_count ? max_count : count);
}
/// About 100 years in milliseconds.
inline constexpr u64 kMaxWaitMilliseconds = 3'155'760'000'000ull;
/// About 100 years in microseconds.
inline constexpr u64 kMaxWaitMicroseconds = kMaxWaitMilliseconds * 1000;

} // namespace detail_sync

/// A variable that several threads may read and write without a lock (`std::atomic<T>`).
///
/// `T` is a trivially copyable type: an integer, a pointer, `bool`, an enumeration or a float. Every operation takes a
/// [[MemoryOrder]] (default `SeqCst`); the arithmetic operations exist for integers and pointers (and `FetchAdd`/`FetchSub` for floats).
///
/// Thread-safety: thread-safe. Every member may be called from any thread at once.
/// Ownership: the object owns its value; it is neither copyable nor movable.
template <class T>
class Atomic {
public:
    /// Whether the operations never take a lock on this platform.
    static constexpr bool kAlwaysLockFree = std::atomic<T>::is_always_lock_free;

    /// Creates the variable holding `T{}`.
    constexpr Atomic() noexcept(IsNothrowDefault) : value_() {}
    /// Creates the variable holding `initial` (not an atomic operation).
    constexpr Atomic(T initial) noexcept : value_(initial) {}
    Atomic(const Atomic&) = delete;
    Atomic& operator=(const Atomic&) = delete;

    /// Returns the current value.
    T Load(MemoryOrder order = MemoryOrder::SeqCst) const noexcept { return value_.load(detail_sync::ToStd(order)); }
    /// Stores `value`.
    void Store(T value, MemoryOrder order = MemoryOrder::SeqCst) noexcept { value_.store(value, detail_sync::ToStd(order)); }
    /// Stores `value` and returns the value it replaced.
    T Exchange(T value, MemoryOrder order = MemoryOrder::SeqCst) noexcept { return value_.exchange(value, detail_sync::ToStd(order)); }

    /// Replaces the value with `desired` if it equals `expected`; otherwise loads the current value into `expected`. May fail spuriously.
    ///
    /// Param success: the order when the exchange happens.
    /// Param failure: the order of the load when it does not; it must not be `Release` or `AcqRel`.
    /// Returns: true when the value was replaced.
    bool CompareExchangeWeak(T& expected, T desired, MemoryOrder success, MemoryOrder failure) noexcept {
        return value_.compare_exchange_weak(expected, desired, detail_sync::ToStd(success), detail_sync::ToStd(failure));
    }
    /// As above with one order; the failure order is the strongest valid one.
    bool CompareExchangeWeak(T& expected, T desired, MemoryOrder order = MemoryOrder::SeqCst) noexcept {
        return CompareExchangeWeak(expected, desired, order, detail_sync::FailureOrder(order));
    }
    /// As `CompareExchangeWeak` but never fails spuriously.
    bool CompareExchangeStrong(T& expected, T desired, MemoryOrder success, MemoryOrder failure) noexcept {
        return value_.compare_exchange_strong(expected, desired, detail_sync::ToStd(success), detail_sync::ToStd(failure));
    }
    /// As above with one order; the failure order is the strongest valid one.
    bool CompareExchangeStrong(T& expected, T desired, MemoryOrder order = MemoryOrder::SeqCst) noexcept {
        return CompareExchangeStrong(expected, desired, order, detail_sync::FailureOrder(order));
    }

    /// Adds `delta` and returns the value before the addition (integers, floats; for a pointer `T*`, `delta` counts elements).
    template <class D>
    T FetchAdd(D delta, MemoryOrder order = MemoryOrder::SeqCst) noexcept {
        return value_.fetch_add(delta, detail_sync::ToStd(order));
    }
    /// Subtracts `delta` and returns the value before the subtraction.
    template <class D>
    T FetchSub(D delta, MemoryOrder order = MemoryOrder::SeqCst) noexcept {
        return value_.fetch_sub(delta, detail_sync::ToStd(order));
    }
    /// Bitwise-ands `mask` into the value and returns the value before (integers only).
    T FetchAnd(T mask, MemoryOrder order = MemoryOrder::SeqCst) noexcept { return value_.fetch_and(mask, detail_sync::ToStd(order)); }
    /// Bitwise-ors `mask` into the value and returns the value before (integers only).
    T FetchOr(T mask, MemoryOrder order = MemoryOrder::SeqCst) noexcept { return value_.fetch_or(mask, detail_sync::ToStd(order)); }
    /// Bitwise-xors `mask` into the value and returns the value before (integers only).
    T FetchXor(T mask, MemoryOrder order = MemoryOrder::SeqCst) noexcept { return value_.fetch_xor(mask, detail_sync::ToStd(order)); }

    /// Sequentially consistent load.
    operator T() const noexcept { return Load(); }
    /// Sequentially consistent store; returns `value`.
    T operator=(T value) noexcept {
        Store(value);
        return value;
    }
    /// Sequentially consistent increment; returns the new value (integers and pointers).
    T operator++() noexcept { return FetchAdd(1) + 1; }
    /// Sequentially consistent post-increment; returns the old value.
    T operator++(int) noexcept { return FetchAdd(1); }
    /// Sequentially consistent decrement; returns the new value.
    T operator--() noexcept { return FetchSub(1) - 1; }
    /// Sequentially consistent post-decrement; returns the old value.
    T operator--(int) noexcept { return FetchSub(1); }

private:
    static constexpr bool IsNothrowDefault = true;
    std::atomic<T> value_;
};

/// Orders the memory accesses around it between threads without touching a variable (`std::atomic_thread_fence`).
inline void AtomicThreadFence(MemoryOrder order) noexcept {
    std::atomic_thread_fence(detail_sync::ToStd(order));
}

/// Orders the memory accesses around it against a signal handler on the same thread (`std::atomic_signal_fence`).
inline void AtomicSignalFence(MemoryOrder order) noexcept {
    std::atomic_signal_fence(detail_sync::ToStd(order));
}

} // namespace engine_placeholder
