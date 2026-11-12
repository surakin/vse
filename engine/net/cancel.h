#pragma once

//! [[net::CancelToken]]: cancels the waits and operations that were started with it.

#include "sync/atomic.h"
#include "types.h"

namespace engine_placeholder::net
{

class Reactor;
struct Wait;

/// Cancels, from any thread, every wait of a [[net::Reactor]] that was started with this token: each completes with
/// [[net::ErrorCode::Cancelled]], once. A wait started with a token that is already cancelled completes at once the
/// same way. A token cancels for good; make a new one for the next round. A token belongs to one reactor while any of
/// its waits is registered (using it with two reactors at the same time is a bug and is checked).
///
/// - Thread-safety: thread-safe. [[net::CancelToken::Cancel]] may race with the waits it cancels, with their completion
/// by an event or a timeout, and with other `Cancel` calls: each wait ends exactly once, with one outcome.
/// - Ownership: the token must outlive every wait that uses it; it owns nothing. It also must not be cancelled from one
/// thread while another destroys the reactor it was used with (once all its waits have ended it holds no pointer to the
/// reactor, so cancelling it afterwards is safe).
class CancelToken
{
public:
    /// Creates a token that is not cancelled.
    CancelToken() = default;

    CancelToken(const CancelToken&) = delete;
    CancelToken& operator=(const CancelToken&) = delete;

    /// Cancels the token and every wait that is using it. Idempotent.
    void Cancel();

    /// Tells whether [[net::CancelToken::Cancel]] was called.
    /// - Returns: true once cancelled.
    bool IsCancelled() const { return cancelled_.Load(MemoryOrder::SeqCst); }

private:
    friend class Reactor;

    Atomic<bool> cancelled_{false};
    // The reactor that holds waits of this token: set when a wait registers, cleared when the last one ends.
    Atomic<Reactor*> reactor_{nullptr};
    // The registered waits of this token (an intrusive list through Wait::token_prev and token_next), guarded by the
    // reactor's lock.
    Wait* waits_ = nullptr;
};

}  // namespace engine_placeholder::net
