#pragma once

//! [[net::Socket]]: a non-blocking TCP or UDP socket behind one interface, the only door to the operating system's
//! socket API.

#include "containers/span.h"
#include "net/endpoint.h"
#include "net/status.h"
#include "types.h"

namespace vse::net
{

/// Which half of a connection [[net::Socket::Shutdown]] closes.
enum class ShutdownMode : u8
{
    Read,   ///< No more reads: the peer's data is refused.
    Write,  ///< No more writes: the peer reads the end of the stream after what was sent (a graceful close).
    Both,   ///< Both halves.
};

/// A move-only handle to a TCP stream, a TCP listener or a UDP socket.
///
/// Every socket is non-blocking and not inherited by child processes from the moment it exists. A call that cannot
/// finish returns
/// [[net::ErrorCode::WouldBlock]] and did nothing; wait for readiness (the reactor, or a poll in a test) and
/// call again. Partial reads and writes are normal results: a count below the request is not an error. An interrupted
/// system call is retried inside the backend. A write to a peer that has gone never raises a signal; it reports
/// [[net::ErrorCode::Closed]] or [[net::ErrorCode::Reset]].
///
/// A socket has an address family from the moment it is opened. An IPv6 socket accepts IPv4 endpoints as IPv4-mapped
/// addresses and an IPv4 socket refuses IPv6 endpoints (except an IPv4-mapped one, which is unmapped), and an
/// IPv4-mapped peer address is reported as IPv4. A closed or default-constructed socket answers every operation with
/// [[net::ErrorCode::Invalid]]; none crashes.
///
/// - Thread-safety: thread-compatible. One thread at a time per socket; a socket may be moved to another thread.
/// - Ownership: owns the operating-system handle and closes it in the destructor.
class Socket
{
public:
    /// Creates a socket that is not open.
    Socket() = default;
    /// Closes the handle, if open, ignoring errors.
    ~Socket();

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    /// Takes the handle of `other`, which becomes not open.
    Socket(Socket&& other) noexcept;
    /// Closes this socket's handle, then takes the handle of `other`, which becomes not open.
    Socket& operator=(Socket&& other) noexcept;

    /// Opens a TCP socket of `family` into `*out`.
    ///
    /// - Backend: opens a non-blocking, close-on-exec stream socket (`SOCK_NONBLOCK | SOCK_CLOEXEC` on Linux;
    /// `WSASocketW` with `WSA_FLAG_NO_HANDLE_INHERIT`, then `FIONBIO`, on Windows, which also makes an IPv6 socket dual
    /// stack and the port exclusive, the interface's defaults) and keeps its family.
    /// - Fails: [[net::ErrorCode::Invalid]] when `family` is [[net::AddressFamily::None]] or `*out` is already open;
    /// [[net::ErrorCode::LimitExceeded]] when the process or the system has no descriptors left;
    /// [[net::ErrorCode::Unsupported]] when the family is not available.
    /// - Ownership: `*out` is written on success only; the socket owns its handle.
    static Status OpenTcp(AddressFamily family, Socket* out);
    /// Opens a UDP socket of `family` into `*out`.
    ///
    /// - Backend: as [[net::Socket::OpenTcp]], for a datagram socket.
    /// - Fails: as [[net::Socket::OpenTcp]].
    /// - Ownership: `*out` is written on success only; the socket owns its handle.
    static Status OpenUdp(AddressFamily family, Socket* out);

    /// True while the socket holds an operating-system handle.
    /// - Fails: never.
    bool IsOpen() const { return handle_ >= 0; }
    /// The address family the socket was opened with; [[net::AddressFamily::None]] when not open.
    /// - Fails: never.
    AddressFamily Family() const { return family_; }
    /// True for a UDP socket.
    /// - Fails: never.
    bool IsDatagram() const { return datagram_; }
    /// The operating-system handle (a descriptor on POSIX, a `SOCKET` on Windows), for the readiness code of the same
    /// backend; -1 when not open.
    /// - Fails: never.
    i64 NativeHandle() const { return handle_; }

    /// Binds to `local`; port 0 asks for a free port (see [[net::Socket::LocalEndpoint]]).
    ///
    /// - Backend: `bind`.
    /// - Fails: [[net::ErrorCode::AddressInUse]], [[net::ErrorCode::AddressNotAvailable]], [[net::ErrorCode::Invalid]]
    /// (closed socket, or an endpoint of the wrong family), [[net::ErrorCode::Other]] for a refused privileged port.
    Status Bind(const Endpoint& local);
    /// Starts listening with a queue of `backlog` pending connections.
    ///
    /// - Backend: `listen`.
    /// - Fails: [[net::ErrorCode::Invalid]] for a closed or UDP socket, [[net::ErrorCode::AddressInUse]].
    Status Listen(i32 backlog = 128);
    /// Accepts one pending connection into `*connection`; `*peer` receives its address.
    ///
    /// - Backend: `accept4`, or `accept` plus the non-blocking and close-on-exec (Windows: not inherited) flags. A
    /// connection aborted or reset before it was accepted is skipped.
    /// - Fails: [[net::ErrorCode::WouldBlock]] when none is pending; [[net::ErrorCode::LimitExceeded]] when out of
    /// descriptors;
    /// [[net::ErrorCode::Invalid]] for a socket that does not listen or an open `*connection`.
    /// - Ownership: `*connection` and `*peer` are written on success; the new socket owns its handle.
    Status Accept(Socket* connection, Endpoint* peer);
    /// Starts connecting to `remote`.
    ///
    /// Returns success when the connection is made at once, and [[net::ErrorCode::WouldBlock]] when it is under way:
    /// wait until the socket is writable and call [[net::Socket::FinishConnect]]. A UDP socket "connects" at once: it
    /// then sends to and receives from `remote` only.
    ///
    /// - Backend: `connect` on a non-blocking socket; `EINPROGRESS` and `EINTR` (Windows: `WSAEWOULDBLOCK`) mean "under
    /// way". Windows turns off SYN retransmission for a loopback destination first, so a refusal there is as quick as
    /// on Linux.
    /// - Fails: [[net::ErrorCode::Refused]], [[net::ErrorCode::Unreachable]], [[net::ErrorCode::TimedOut]],
    /// [[net::ErrorCode::AlreadyConnected]],
    /// [[net::ErrorCode::Invalid]].
    Status Connect(const Endpoint& remote);
    /// Completes a connection started by [[net::Socket::Connect]] that returned WouldBlock.
    ///
    /// - Backend: reads the socket's pending error (`SO_ERROR`) and checks the peer.
    /// - Fails: [[net::ErrorCode::WouldBlock]] when still under way; the connection's error
    /// ([[net::ErrorCode::Refused]], [[net::ErrorCode::TimedOut]],
    /// [[net::ErrorCode::Unreachable]], ...) when it failed, once; [[net::ErrorCode::Invalid]] when no connect is under
    /// way (never started, or the outcome was already reported).
    Status FinishConnect();

    /// Sends bytes of a connected socket; `*sent` receives how many were taken (possibly fewer than `bytes.Size()`,
    /// possibly zero with WouldBlock).
    ///
    /// - Backend: `send` with the no-signal flag (none on Windows, which raises no signal; it takes a whole write when
    /// its send buffer has any room).
    /// - Fails: [[net::ErrorCode::WouldBlock]], [[net::ErrorCode::Closed]] (the peer has shut down),
    /// [[net::ErrorCode::Reset]], [[net::ErrorCode::NotConnected]],
    /// [[net::ErrorCode::MessageTooLarge]] (a datagram above the limit), [[net::ErrorCode::Invalid]].
    /// - Ownership: `bytes` is read during the call; nothing is retained.
    Status Send(ByteSpan bytes, usize* sent);
    /// Receives into `buffer`; `*received` receives how many bytes arrived.
    ///
    /// On a UDP socket a datagram larger than `buffer` is dropped and reported as [[net::ErrorCode::MessageTooLarge]],
    /// as for [[net::Socket::ReceiveFrom]]. On a stream, the end of the stream (the peer shut down its writing half) is
    /// [[net::ErrorCode::Closed]] with `*received` zero. `buffer` must not be empty.
    ///
    /// - Backend: `recv`.
    /// - Fails: [[net::ErrorCode::WouldBlock]], [[net::ErrorCode::MessageTooLarge]], [[net::ErrorCode::Closed]],
    /// [[net::ErrorCode::Reset]], [[net::ErrorCode::Refused]] (a connected UDP socket that got an ICMP refusal),
    /// [[net::ErrorCode::NotConnected]], [[net::ErrorCode::Invalid]].
    /// - Ownership: `buffer` is written during the call; nothing is retained.
    Status Receive(MutableByteSpan buffer, usize* received);
    /// Sends one datagram to `to`; `*sent` receives its size (a datagram is sent whole or not at all).
    ///
    /// - Backend: `sendto` with the no-signal flag.
    /// - Fails: [[net::ErrorCode::WouldBlock]], [[net::ErrorCode::MessageTooLarge]], [[net::ErrorCode::Unreachable]],
    /// [[net::ErrorCode::Invalid]].
    /// - Ownership: `bytes` is read during the call; nothing is retained.
    Status SendTo(ByteSpan bytes, const Endpoint& to, usize* sent);
    /// Receives one datagram into `buffer`; `*received` receives its size and `*from` its sender.
    ///
    /// A datagram larger than `buffer` is dropped and reported as [[net::ErrorCode::MessageTooLarge]].
    ///
    /// - Backend: `recvfrom` with the truncation-detecting flag (Windows: `WSAEMSGSIZE` reports the cut).
    /// - Fails: [[net::ErrorCode::WouldBlock]], [[net::ErrorCode::MessageTooLarge]], [[net::ErrorCode::Invalid]].
    /// - Ownership: `buffer` is written during the call; nothing is retained.
    Status ReceiveFrom(MutableByteSpan buffer, usize* received, Endpoint* from);

    /// Closes one or both halves of a connection without closing the handle.
    ///
    /// - Backend: `shutdown`.
    /// - Fails: [[net::ErrorCode::NotConnected]], [[net::ErrorCode::Invalid]].
    Status Shutdown(ShutdownMode mode);
    /// Closes the handle. The destructor does the same and ignores the result.
    ///
    /// - Backend: `close` (`closesocket` on Windows).
    /// - Fails: [[net::ErrorCode::Invalid]] when the socket is not open (so a second close reports itself and does
    /// nothing else).
    Status Close();

    /// Reads the local endpoint (after a bind to port 0 it holds the chosen port).
    ///
    /// - Backend: `getsockname`.
    /// - Fails: [[net::ErrorCode::Invalid]].
    /// - Ownership: `*out` is written on success.
    Status LocalEndpoint(Endpoint* out) const;
    /// Reads the connected peer's endpoint.
    ///
    /// - Backend: `getpeername`.
    /// - Fails: [[net::ErrorCode::NotConnected]], [[net::ErrorCode::Invalid]].
    /// - Ownership: `*out` is written on success.
    Status PeerEndpoint(Endpoint* out) const;

    /// Allows binding a port that has connections in the waiting state (`SO_REUSEADDR`).
    /// - Backend: `setsockopt`; on Windows `SO_EXCLUSIVEADDRUSE` off for on, since its `SO_REUSEADDR` would let another
    /// socket take the port over.
    /// - Fails: [[net::ErrorCode::Invalid]].
    Status SetReuseAddress(bool on);
    /// Turns off (on = true) the delay of small TCP segments (`TCP_NODELAY`). Invalid for a UDP socket.
    /// - Backend: `setsockopt`.
    /// - Fails: [[net::ErrorCode::Invalid]].
    Status SetNoDelay(bool on);
    /// Turns TCP keep-alive probes on or off (`SO_KEEPALIVE`).
    /// - Backend: `setsockopt`.
    /// - Fails: [[net::ErrorCode::Invalid]].
    Status SetKeepAlive(bool on);
    /// Sets how long keep-alive probes wait and repeat on a stream socket whose keep-alive is on (`TCP_KEEPIDLE`,
    /// `TCP_KEEPINTVL`, `TCP_KEEPCNT`): after `idle_seconds` without data the first probe is sent, then one every
    /// `interval_seconds`, and the connection is reset after `probes` unanswered ones. Zero leaves a value unchanged.
    /// - Backend: `setsockopt`.
    /// - Fails: [[net::ErrorCode::Invalid]] (a UDP socket), [[net::ErrorCode::Unsupported]].
    Status SetKeepAliveTiming(u32 idle_seconds, u32 interval_seconds, u32 probes);
    /// Chooses what closing does with unsent data (`SO_LINGER`). With `on` and `seconds` of 0 the close is abortive:
    /// unsent data is dropped and the peer sees a reset (`RST`) instead of an orderly end. Otherwise the close is the
    /// system's default (orderly, in the background).
    /// - Backend: `setsockopt`.
    /// - Fails: [[net::ErrorCode::Invalid]].
    Status SetLinger(bool on, u32 seconds);
    /// Asks for a receive buffer of `bytes`; `*granted` (optional) receives what the system reports afterwards, which
    /// may differ (Linux doubles the request and caps it at its configured maximum).
    /// - Backend: `setsockopt`, then `getsockopt`.
    /// - Fails: [[net::ErrorCode::Invalid]].
    /// - Ownership: `*granted` (optional) is written on success.
    Status SetReceiveBuffer(usize bytes, usize* granted = nullptr);
    /// Asks for a send buffer of `bytes`; `*granted` as for [[net::Socket::SetReceiveBuffer]].
    /// - Backend: `setsockopt`, then `getsockopt`.
    /// - Fails: [[net::ErrorCode::Invalid]].
    /// - Ownership: `*granted` (optional) is written on success.
    Status SetSendBuffer(usize bytes, usize* granted = nullptr);
    /// For an IPv6 socket: accept IPv4 peers as mapped addresses (`on`) or IPv6 only (`IPV6_V6ONLY`). Invalid for an
    /// IPv4 socket. Set it before binding.
    /// - Backend: `setsockopt`.
    /// - Fails: [[net::ErrorCode::Invalid]].
    Status SetDualStack(bool on);
    /// Allows sending to broadcast addresses (`SO_BROADCAST`); off for every new socket.
    /// - Backend: `setsockopt`.
    /// - Fails: [[net::ErrorCode::Invalid]].
    Status SetBroadcast(bool on);

private:
    i64 handle_ = -1;
    AddressFamily family_ = AddressFamily::None;
    bool datagram_ = false;
    bool connecting_ = false;  // a Connect returned WouldBlock and FinishConnect has not yet reported the outcome

    static Status Open(AddressFamily family, bool datagram, Socket* out);
};

}  // namespace vse::net
