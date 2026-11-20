#pragma once

//! [[net::Signal]]: a one-shot event that another thread fires and a coroutine waits for.

#include "sync/atomic.h"
#include "types.h"

namespace vse::net
{

class Reactor;
struct Wait;

/// A one-shot event. A coroutine waits for it with [[net::Reactor::WaitSignal]]; any thread calls
/// [[net::Signal::Fire]]. It is how work done on another thread (a name resolution on the resolver thread, say) wakes
/// the coroutine that asked for it, with the timeout and cancellation of every other wait.
///
/// A signal fired before the wait starts makes the wait end at once with Ok. At most one coroutine waits for a signal.
///
/// - Thread-safety: thread-safe. [[net::Signal::Fire]] may race with the wait starting, ending by timeout or cancel,
/// and with itself; the wait ends once.
/// - Ownership: the signal must outlive its wait. Once the wait has ended it holds no pointer to the reactor, so firing
/// it later is safe even after the reactor is gone (firing it while the reactor is being destroyed is not: stop the
/// threads that fire signals, such as the [[net::Resolver]], before the reactor).
class Signal
{
public:
    /// Creates a signal that has not fired.
    Signal() = default;

    Signal(const Signal&) = delete;
    Signal& operator=(const Signal&) = delete;

    /// Fires the signal: ends the wait on it with Ok, or makes the next wait end at once. Idempotent.
    void Fire();

    /// Tells whether the signal has fired.
    /// - Returns: true once fired.
    bool IsFired() const { return fired_.Load(MemoryOrder::SeqCst); }

private:
    friend class Reactor;

    Atomic<bool> fired_{false};
    Atomic<Reactor*> reactor_{nullptr};  // set while a wait on this signal is registered
    Wait* wait_ = nullptr;               // that wait, guarded by the reactor's lock
};

}  // namespace vse::net
