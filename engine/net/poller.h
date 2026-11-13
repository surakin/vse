#pragma once

//! [[net::Poller]]: the platform's readiness primitive, one per [[net::Reactor]]. The only file of the readiness code
//! that differs between operating systems (epoll on Linux, `select` on Windows, kqueue on macOS).

#include "build_defs.h"
#include "containers/unique_ptr.h"
#include "net/status.h"
#include "types.h"

namespace engine_placeholder::net
{

/// Interest and readiness bit: the socket can be read (data, a pending connection, or the end of the stream).
inline constexpr u32 kPollRead = 1;
/// Interest and readiness bit: the socket can be written (or a connect finished).
inline constexpr u32 kPollWrite = 2;
/// Readiness bit only: the socket has an error pending.
inline constexpr u32 kPollError = 4;
/// Readiness bit only: the peer hung up.
inline constexpr u32 kPollHangup = 8;

/// One socket that became ready.
///
/// - Thread-safety: thread-compatible plain data.
struct PollEvent
{
    i64 handle = -1;  ///< The socket's [[net::Socket::NativeHandle]].
    u32 ready = 0;    ///< The `kPoll` bits that apply.
};

/// Waits for sockets to become ready.
///
/// A socket is armed for one notification: it is reported once, when it is ready (at once if it already is: the check
/// is by level, so a registration after the event is not lost), and then disarmed until armed again. A socket the
/// poller does not know is added by [[net::Poller::Arm]].
///
/// - Thread-safety: thread-safe except that [[net::Poller::Wait]] is called by one thread at a time. `Arm`, `Disarm`
/// and `Wake` may be called from any thread while another waits.
/// - Ownership: owns the operating-system objects (the epoll descriptor and its wake-up descriptor; on macOS the
/// kqueue; on Windows the two wake-up sockets and the set of armed sockets).
class Poller
{
public:
    /// Creates the poller; check [[net::Poller::IsValid]].
    Poller();
    /// Closes the operating-system objects.
    ~Poller();

    Poller(const Poller&) = delete;
    Poller& operator=(const Poller&) = delete;

    /// True when the operating-system objects were created.
    /// - Fails: never.
    bool IsValid() const { return poller_ >= 0 && wake_ >= 0; }

    /// Arms `handle` for one notification of `interest` (`kPollRead`, `kPollWrite` or both), adding it if it is new.
    ///
    /// - Backend: level-triggered one-shot registration (`EPOLLONESHOT`, `EPOLL_CTL_MOD`, falling back to
    /// `EPOLL_CTL_ADD`; on Windows the poller's own armed set, checked with `getsockopt(SO_TYPE)`, waking a wait in
    /// progress; on macOS a one-shot kqueue filter per wanted direction, the other direction's deleted).
    /// - Param handle: the socket's native handle.
    /// - Param interest: the `kPoll` bits to wait for.
    /// - Fails: [[net::ErrorCode::Invalid]] for a handle the system refuses; [[net::ErrorCode::LimitExceeded]] when out
    /// of resources.
    Status Arm(i64 handle, u32 interest);

    /// Forgets `handle`: no notification for it is reported afterwards, and the poller keeps nothing of it.
    ///
    /// - Backend: `EPOLL_CTL_DEL` (Windows: removal from the armed set; macOS: both kqueue filters deleted), ignoring a
    /// handle that is not registered or already closed.
    /// - Param handle: the socket's native handle.
    /// - Fails: never.
    void Disarm(i64 handle);

    /// The most events one [[net::Poller::Wait]] reports.
    static constexpr usize kMaxBatch = 64;

    /// Waits for armed sockets to become ready, for a [[net::Poller::Wake]], or for the timeout.
    ///
    /// - Backend: `epoll_wait` (the timeout is rounded up to a millisecond, so it never returns early), draining the
    /// wake-up descriptor without reporting it; on Windows `select` over a copy of the armed set (not `WSAPoll`, which
    /// reports a half-closed socket whatever was asked for); on macOS `kevent`, a socket's two filters merged into one
    /// event.
    /// - Param events: receives the ready sockets.
    /// - Param capacity: how many events fit; at most [[net::Poller::kMaxBatch]] are reported.
    /// - Param timeout_nanos: the longest wait, or negative to wait for ever.
    /// - Param count: receives the number of events written; 0 on a timeout, a wake-up or an interruption.
    /// - Ownership: `events` is written during the call; nothing is retained.
    /// - Fails: [[net::ErrorCode::Invalid]] when `capacity` is 0; the system refuses the wait for a reason other than
    /// an interruption (the poller is broken); `*count` is 0 either way.
    Status Wait(PollEvent* events, usize capacity, i64 timeout_nanos, usize* count);

    /// Makes a [[net::Poller::Wait]] in progress, or the next one, return at once.
    ///
    /// - Backend: writes the wake-up descriptor (`eventfd`; on macOS `NOTE_TRIGGER` on a kqueue user event; on Windows
    /// a byte to a loopback UDP socket the wait watches).
    /// - Fails: never.
    void Wake();

private:
    i64 poller_ = -1;  // the epoll descriptor (Windows: the wake-up socket the wait reads)
    i64 wake_ = -1;    // the wake-up descriptor (Windows: the socket that writes to it)
#if ENGINE_PLATFORM_WINDOWS
    // select has no kernel object to hold the armed sockets: the poller keeps them, under a lock
    // (net/windows/poller_select.cpp).
    struct ArmedSet;
    UniquePtr<ArmedSet> armed_;
#endif
};

}  // namespace engine_placeholder::net
