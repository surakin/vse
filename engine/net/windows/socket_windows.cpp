// net::Socket on Windows (Phase 20 Day 278): Winsock 2, the one file of the engine besides the poller and the resolver
// beside it that calls the socket API on Windows. engine/net/BACKEND.md lists what every backend does and the Windows
// notes.

#include "containers/array.h"
#include "cstr/mem_ops.h"
#include "lang/limits.h"
#include "lang/status_try.h"
#include "lang/utility.h"
#include "net/socket.h"
#include "net/windows/windows_net.h"
#include "sync/mutex.h"

namespace vse::net
{

namespace windows
{

namespace
{

OnceFlag g_winsock_once;
int g_winsock_error = 0;  // WSAStartup's result, read after CallOnce (which orders it)

}  // namespace

Status EnsureWinsock()
{
    CallOnce(g_winsock_once,
             []
             {
                 WSADATA data{};
                 g_winsock_error = ::WSAStartup(MAKEWORD(2, 2), &data);
                 if (g_winsock_error == 0 && (LOBYTE(data.wVersion) != 2 || HIBYTE(data.wVersion) != 2))
                 {
                     ::WSACleanup();
                     g_winsock_error = WSAVERNOTSUPPORTED;
                 }
             });
    return g_winsock_error == 0 ? Status::Success() : StatusFromWsa(g_winsock_error);
}

int ToSockaddr(const Endpoint& endpoint, AddressFamily family, sockaddr_storage* out, Status* status)
{
    MemSet(out, 0, sizeof(*out));
    Address address = endpoint.GetAddress();
    if (family == AddressFamily::V6 && address.IsV4())
    {
        address = address.Mapped();
    }
    else if (family == AddressFamily::V4 && address.IsV4Mapped())
    {
        address = address.Unmapped();
    }
    if (!address.IsValid() || address.Family() != family)
    {
        *status = Status::Of(ErrorCode::Invalid);
        return 0;
    }
    if (family == AddressFamily::V4)
    {
        sockaddr_in in{};
        in.sin_family = AF_INET;
        in.sin_port = ::htons(endpoint.Port());
        MemCopy(&in.sin_addr, address.Bytes().Data(), 4);
        MemCopy(out, &in, sizeof(in));
        return static_cast<int>(sizeof(in));
    }
    sockaddr_in6 in6{};
    in6.sin6_family = AF_INET6;
    in6.sin6_port = ::htons(endpoint.Port());
    MemCopy(&in6.sin6_addr, address.Bytes().Data(), 16);
    in6.sin6_scope_id = address.ScopeId();
    if (address.ScopeName().Length() != 0)
    {
        Array<char, Address::kMaxScopeNameLength + 1> name{};
        MemCopy(name.Data(), address.ScopeName().Data(), address.ScopeName().Length());
        const NET_IFINDEX index = ::if_nametoindex(name.Data());
        if (index == 0)
        {
            *status = Status::Of(ErrorCode::AddressNotAvailable);
            return 0;
        }
        in6.sin6_scope_id = index;
    }
    MemCopy(out, &in6, sizeof(in6));
    return static_cast<int>(sizeof(in6));
}

bool FromSockaddr(const sockaddr_storage& storage, Endpoint* out)
{
    if (storage.ss_family == AF_INET)
    {
        sockaddr_in in{};
        MemCopy(&in, &storage, sizeof(in));
        const u8* b = reinterpret_cast<const u8*>(&in.sin_addr);
        *out = Endpoint(Address::V4(b[0], b[1], b[2], b[3]), ::ntohs(in.sin_port));
        return true;
    }
    if (storage.ss_family == AF_INET6)
    {
        sockaddr_in6 in6{};
        MemCopy(&in6, &storage, sizeof(in6));
        Array<u8, 16> bytes{};
        MemCopy(bytes.Data(), &in6.sin6_addr, 16);
        const Address a = Address::V6(bytes, in6.sin6_scope_id);
        *out = Endpoint(a.Unmapped(), ::ntohs(in6.sin6_port));
        return true;
    }
    return false;
}

}  // namespace windows

using windows::LastWsaStatus;
using windows::SocketOf;

namespace
{

// The most bytes one Winsock call moves: its lengths are `int`.
int WinsockLength(usize size)
{
    return size > static_cast<usize>(Limits<int>::Max()) ? Limits<int>::Max() : static_cast<int>(size);
}

Status SetSocketOption(i64 handle, int level, int name, int value)
{
    if (::setsockopt(SocketOf(handle), level, name, reinterpret_cast<const char*>(&value), sizeof(value)) != 0)
    {
        return LastWsaStatus();
    }
    return Status::Success();
}

// Makes a new socket non-blocking and not inherited (an accepted one may carry neither flag from its listener), and a
// UDP socket deaf to the ICMP "port unreachable" that Windows otherwise reports as WSAECONNRESET on the next receive.
Status PrepareSocket(SOCKET socket, bool datagram)
{
    u_long non_blocking = 1;
    if (::ioctlsocket(socket, FIONBIO, &non_blocking) != 0)
    {
        return LastWsaStatus();
    }
    if (::SetHandleInformation(reinterpret_cast<HANDLE>(socket), HANDLE_FLAG_INHERIT, 0) == 0)
    {
        return windows::StatusFromWsa(static_cast<int>(::GetLastError()));
    }
    if (datagram)
    {
        BOOL report = FALSE;
        DWORD returned = 0;
        if (::WSAIoctl(socket, SIO_UDP_CONNRESET, &report, sizeof(report), nullptr, 0, &returned, nullptr, nullptr) !=
            0)
        {
            return LastWsaStatus();
        }
    }
    return Status::Success();
}

}  // namespace

Socket::~Socket()
{
    if (IsOpen())
    {
        ::closesocket(SocketOf(handle_));
    }
}

Socket::Socket(Socket&& other) noexcept
    : handle_(other.handle_), family_(other.family_), datagram_(other.datagram_), connecting_(other.connecting_)
{
    other.handle_ = -1;
    other.connecting_ = false;
    other.family_ = AddressFamily::None;
    other.datagram_ = false;
}

Socket& Socket::operator=(Socket&& other) noexcept
{
    if (this != &other)
    {
        if (IsOpen())
        {
            ::closesocket(SocketOf(handle_));
        }
        handle_ = other.handle_;
        family_ = other.family_;
        datagram_ = other.datagram_;
        connecting_ = other.connecting_;
        other.connecting_ = false;
        other.handle_ = -1;
        other.family_ = AddressFamily::None;
        other.datagram_ = false;
    }
    return *this;
}

Status Socket::Open(AddressFamily family, bool datagram, Socket* out)
{
    if (family == AddressFamily::None || out->IsOpen())
    {
        return Status::Of(ErrorCode::Invalid);
    }
    ENGINE_TRY(windows::EnsureWinsock());
    const int domain = family == AddressFamily::V4 ? AF_INET : AF_INET6;
    // Not inherited from creation (WSA_FLAG_NO_HANDLE_INHERIT), so no child process started in between can get it.
    const SOCKET socket = ::WSASocketW(domain, datagram ? SOCK_DGRAM : SOCK_STREAM,
                                       datagram ? IPPROTO_UDP : IPPROTO_TCP, nullptr, 0,
                                       WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
    if (socket == INVALID_SOCKET)
    {
        return LastWsaStatus();
    }
    Socket made;
    made.handle_ = windows::HandleOf(socket);
    made.family_ = family;
    made.datagram_ = datagram;
    ENGINE_TRY(PrepareSocket(socket, datagram));
    // Windows' own defaults differ from the interface's: an IPv6 socket is IPv6-only there (Linux maps IPv4 by
    // default), and a port may be taken over by another socket that asks for SO_REUSEADDR. The interface's defaults
    // are dual stack and an exclusive port (SetReuseAddress, SetDualStack).
    ENGINE_TRY(SetSocketOption(made.handle_, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, 1));
    if (family == AddressFamily::V6)
    {
        ENGINE_TRY(SetSocketOption(made.handle_, IPPROTO_IPV6, IPV6_V6ONLY, 0));
    }
    *out = Move(made);
    return Status::Success();
}

Status Socket::OpenTcp(AddressFamily family, Socket* out)
{
    return Open(family, false, out);
}

Status Socket::OpenUdp(AddressFamily family, Socket* out)
{
    return Open(family, true, out);
}

Status Socket::Bind(const Endpoint& local)
{
    if (!IsOpen())
    {
        return Status::Of(ErrorCode::Invalid);
    }
    sockaddr_storage storage;
    Status status;
    const int length = windows::ToSockaddr(local, family_, &storage, &status);
    if (length == 0)
    {
        return status;
    }
    if (::bind(SocketOf(handle_), reinterpret_cast<const sockaddr*>(&storage), length) != 0)
    {
        // Windows refuses a port another socket holds exclusively, or one this socket may not share, with WSAEACCES;
        // the interface calls both AddressInUse.
        const int error = ::WSAGetLastError();
        return error == WSAEACCES ? Status::Make(ErrorCode::AddressInUse, error) : windows::StatusFromWsa(error);
    }
    return Status::Success();
}

Status Socket::Listen(i32 backlog)
{
    if (!IsOpen() || datagram_)
    {
        return Status::Of(ErrorCode::Invalid);
    }
    if (::listen(SocketOf(handle_), backlog) != 0)
    {
        return LastWsaStatus();
    }
    return Status::Success();
}

Status Socket::Accept(Socket* connection, Endpoint* peer)
{
    if (!IsOpen() || datagram_ || connection->IsOpen())
    {
        return Status::Of(ErrorCode::Invalid);
    }
    for (;;)
    {
        sockaddr_storage storage;
        int length = static_cast<int>(sizeof(storage));
        const SOCKET accepted = ::accept(SocketOf(handle_), reinterpret_cast<sockaddr*>(&storage), &length);
        if (accepted == INVALID_SOCKET)
        {
            const int error = ::WSAGetLastError();
            // A connection the peer reset or that was aborted before it was accepted is not a result for the listener.
            if (error == WSAECONNRESET || error == WSAECONNABORTED || error == WSAEINTR)
            {
                continue;
            }
            return windows::StatusFromWsa(error);
        }
        Socket made;
        made.handle_ = windows::HandleOf(accepted);
        made.family_ = family_;
        made.datagram_ = false;
        ENGINE_TRY(PrepareSocket(accepted, false));
        *connection = Move(made);
        if (!windows::FromSockaddr(storage, peer))
        {
            *peer = Endpoint();
        }
        return Status::Success();
    }
}

Status Socket::Connect(const Endpoint& remote)
{
    if (!IsOpen())
    {
        return Status::Of(ErrorCode::Invalid);
    }
    sockaddr_storage storage;
    Status status;
    const int length = windows::ToSockaddr(remote, family_, &storage, &status);
    if (length == 0)
    {
        return status;
    }
    if (!datagram_ && remote.GetAddress().IsLoopback())
    {
        // Windows answers a refused connect (a reset to its SYN) by sending the SYN again, twice, so a connect to a
        // closed port takes about two seconds where Linux and macOS report Refused at once. On loopback a SYN is never
        // lost, so the retransmissions buy nothing: turn them off and a refusal is immediate. Other destinations keep
        // the system's behavior. Best effort: a system without the control keeps the slow refusal.
        TCP_INITIAL_RTO_PARAMETERS rto{};
        rto.Rtt = TCP_INITIAL_RTO_UNSPECIFIED_RTT;
        rto.MaxSynRetransmissions = TCP_INITIAL_RTO_NO_SYN_RETRANSMISSIONS;
        DWORD returned = 0;
        [[maybe_unused]] const int ignored = ::WSAIoctl(SocketOf(handle_), SIO_TCP_INITIAL_RTO, &rto, sizeof(rto),
                                                        nullptr, 0, &returned, nullptr, nullptr);
    }
    if (::connect(SocketOf(handle_), reinterpret_cast<const sockaddr*>(&storage), length) == 0)
    {
        return Status::Success();
    }
    const int error = ::WSAGetLastError();
    if (error == WSAEWOULDBLOCK)
    {
        connecting_ = true;
        return Status::Of(ErrorCode::WouldBlock);
    }
    // A second connect while one is under way is WSAEALREADY (WSAEINVAL on old systems): the socket already has a
    // connection attempt.
    if (error == WSAEALREADY || (error == WSAEINVAL && connecting_))
    {
        return Status::Make(ErrorCode::AlreadyConnected, error);
    }
    return windows::StatusFromWsa(error);
}

Status Socket::FinishConnect()
{
    if (!IsOpen() || datagram_ || !connecting_)
    {
        return Status::Of(ErrorCode::Invalid);
    }
    int pending = 0;
    int size = static_cast<int>(sizeof(pending));
    if (::getsockopt(SocketOf(handle_), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&pending), &size) != 0)
    {
        return LastWsaStatus();
    }
    if (pending != 0)
    {
        connecting_ = false;  // the failure is reported once
        return windows::StatusFromWsa(pending);
    }
    sockaddr_storage storage;
    int length = static_cast<int>(sizeof(storage));
    if (::getpeername(SocketOf(handle_), reinterpret_cast<sockaddr*>(&storage), &length) != 0)
    {
        const int error = ::WSAGetLastError();
        if (error == WSAENOTCONN)
        {
            return Status::Of(ErrorCode::WouldBlock);  // no error recorded and no peer yet: still under way
        }
        connecting_ = false;
        return windows::StatusFromWsa(error);
    }
    connecting_ = false;
    return Status::Success();
}

Status Socket::Send(ByteSpan bytes, usize* sent)
{
    *sent = 0;
    if (!IsOpen())
    {
        return Status::Of(ErrorCode::Invalid);
    }
    // Winsock raises no signal for a vanished peer: no flag is needed.
    const int n = ::send(SocketOf(handle_), reinterpret_cast<const char*>(bytes.Data()), WinsockLength(bytes.Size()),
                         0);
    if (n == SOCKET_ERROR)
    {
        return LastWsaStatus();
    }
    *sent = static_cast<usize>(n);
    return Status::Success();
}

Status Socket::Receive(MutableByteSpan buffer, usize* received)
{
    *received = 0;
    if (!IsOpen() || buffer.Size() == 0)
    {
        return Status::Of(ErrorCode::Invalid);
    }
    const int n = ::recv(SocketOf(handle_), reinterpret_cast<char*>(buffer.Data()), WinsockLength(buffer.Size()), 0);
    if (n == SOCKET_ERROR)
    {
        // A datagram larger than the buffer is cut and reported with WSAEMSGSIZE; its rest is gone (MessageTooLarge,
        // as on POSIX).
        return LastWsaStatus();
    }
    if (n == 0 && !datagram_)
    {
        return Status::Of(ErrorCode::Closed);  // the peer shut down its writing half
    }
    *received = static_cast<usize>(n);
    return Status::Success();
}

Status Socket::SendTo(ByteSpan bytes, const Endpoint& to, usize* sent)
{
    *sent = 0;
    if (!IsOpen() || !datagram_)
    {
        return Status::Of(ErrorCode::Invalid);
    }
    sockaddr_storage storage;
    Status status;
    const int length = windows::ToSockaddr(to, family_, &storage, &status);
    if (length == 0)
    {
        return status;
    }
    const int n = ::sendto(SocketOf(handle_), reinterpret_cast<const char*>(bytes.Data()), WinsockLength(bytes.Size()),
                           0, reinterpret_cast<const sockaddr*>(&storage), length);
    if (n == SOCKET_ERROR)
    {
        return LastWsaStatus();
    }
    *sent = static_cast<usize>(n);
    return Status::Success();
}

Status Socket::ReceiveFrom(MutableByteSpan buffer, usize* received, Endpoint* from)
{
    *received = 0;
    if (!IsOpen() || !datagram_ || buffer.Size() == 0)
    {
        return Status::Of(ErrorCode::Invalid);
    }
    sockaddr_storage storage{};
    int length = static_cast<int>(sizeof(storage));
    const int n = ::recvfrom(SocketOf(handle_), reinterpret_cast<char*>(buffer.Data()), WinsockLength(buffer.Size()), 0,
                             reinterpret_cast<sockaddr*>(&storage), &length);
    if (n == SOCKET_ERROR)
    {
        return LastWsaStatus();  // WSAEMSGSIZE: cut, the rest dropped (MessageTooLarge)
    }
    *received = static_cast<usize>(n);
    if (!windows::FromSockaddr(storage, from))
    {
        *from = Endpoint();
    }
    return Status::Success();
}

Status Socket::Shutdown(ShutdownMode mode)
{
    if (!IsOpen())
    {
        return Status::Of(ErrorCode::Invalid);
    }
    const int how = mode == ShutdownMode::Read ? SD_RECEIVE : (mode == ShutdownMode::Write ? SD_SEND : SD_BOTH);
    if (::shutdown(SocketOf(handle_), how) != 0)
    {
        return LastWsaStatus();
    }
    return Status::Success();
}

Status Socket::Close()
{
    if (!IsOpen())
    {
        return Status::Of(ErrorCode::Invalid);
    }
    const SOCKET socket = SocketOf(handle_);
    handle_ = -1;
    family_ = AddressFamily::None;
    datagram_ = false;
    connecting_ = false;
    // The handle is released even when closesocket reports an error, so it is never closed twice.
    if (::closesocket(socket) != 0)
    {
        return LastWsaStatus();
    }
    return Status::Success();
}

Status Socket::LocalEndpoint(Endpoint* out) const
{
    if (!IsOpen())
    {
        return Status::Of(ErrorCode::Invalid);
    }
    sockaddr_storage storage;
    int length = static_cast<int>(sizeof(storage));
    if (::getsockname(SocketOf(handle_), reinterpret_cast<sockaddr*>(&storage), &length) != 0)
    {
        return LastWsaStatus();
    }
    return windows::FromSockaddr(storage, out) ? Status::Success() : Status::Of(ErrorCode::Unsupported);
}

Status Socket::PeerEndpoint(Endpoint* out) const
{
    if (!IsOpen())
    {
        return Status::Of(ErrorCode::Invalid);
    }
    sockaddr_storage storage;
    int length = static_cast<int>(sizeof(storage));
    if (::getpeername(SocketOf(handle_), reinterpret_cast<sockaddr*>(&storage), &length) != 0)
    {
        return LastWsaStatus();
    }
    return windows::FromSockaddr(storage, out) ? Status::Success() : Status::Of(ErrorCode::Unsupported);
}

namespace
{

Status SetBufferSize(i64 handle, int name, usize bytes, usize* granted)
{
    const int clamped = bytes > 0x3fffffff ? 0x3fffffff : static_cast<int>(bytes);
    ENGINE_TRY(SetSocketOption(handle, SOL_SOCKET, name, clamped));
    if (granted != nullptr)
    {
        int value = 0;
        int size = static_cast<int>(sizeof(value));
        if (::getsockopt(SocketOf(handle), SOL_SOCKET, name, reinterpret_cast<char*>(&value), &size) != 0)
        {
            return LastWsaStatus();
        }
        *granted = static_cast<usize>(value);
    }
    return Status::Success();
}

}  // namespace

Status Socket::SetReuseAddress(bool on)
{
    if (!IsOpen())
    {
        return Status::Of(ErrorCode::Invalid);
    }
    // Not SO_REUSEADDR, which on Windows lets a second socket take over a port in use. Off is the exclusive port the
    // socket was opened with; on clears it, which is Windows' default: a port whose old connections wait to expire can
    // be bound again.
    return SetSocketOption(handle_, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, on ? 0 : 1);
}

Status Socket::SetNoDelay(bool on)
{
    if (!IsOpen() || datagram_)
    {
        return Status::Of(ErrorCode::Invalid);
    }
    return SetSocketOption(handle_, IPPROTO_TCP, TCP_NODELAY, on ? 1 : 0);
}

Status Socket::SetKeepAlive(bool on)
{
    if (!IsOpen())
    {
        return Status::Of(ErrorCode::Invalid);
    }
    return SetSocketOption(handle_, SOL_SOCKET, SO_KEEPALIVE, on ? 1 : 0);
}

Status Socket::SetKeepAliveTiming(u32 idle_seconds, u32 interval_seconds, u32 probes)
{
    if (!IsOpen() || datagram_)
    {
        return Status::Of(ErrorCode::Invalid);
    }
    // Windows 10 1709 and later have the same three options as Linux, in seconds (SIO_KEEPALIVE_VALS, the older way,
    // takes milliseconds and has no probe count).
    if (idle_seconds != 0)
    {
        ENGINE_TRY(SetSocketOption(handle_, IPPROTO_TCP, TCP_KEEPIDLE, static_cast<int>(idle_seconds)));
    }
    if (interval_seconds != 0)
    {
        ENGINE_TRY(SetSocketOption(handle_, IPPROTO_TCP, TCP_KEEPINTVL, static_cast<int>(interval_seconds)));
    }
    if (probes != 0)
    {
        ENGINE_TRY(SetSocketOption(handle_, IPPROTO_TCP, TCP_KEEPCNT, static_cast<int>(probes)));
    }
    return Status::Success();
}

Status Socket::SetLinger(bool on, u32 seconds)
{
    if (!IsOpen() || datagram_)
    {
        return Status::Of(ErrorCode::Invalid);
    }
    linger value{};
    value.l_onoff = static_cast<u_short>(on ? 1 : 0);
    value.l_linger = static_cast<u_short>(seconds > 0xffff ? 0xffff : seconds);
    if (::setsockopt(SocketOf(handle_), SOL_SOCKET, SO_LINGER, reinterpret_cast<const char*>(&value), sizeof(value)) !=
        0)
    {
        return LastWsaStatus();
    }
    return Status::Success();
}

Status Socket::SetReceiveBuffer(usize bytes, usize* granted)
{
    if (!IsOpen())
    {
        return Status::Of(ErrorCode::Invalid);
    }
    return SetBufferSize(handle_, SO_RCVBUF, bytes, granted);
}

Status Socket::SetSendBuffer(usize bytes, usize* granted)
{
    if (!IsOpen())
    {
        return Status::Of(ErrorCode::Invalid);
    }
    return SetBufferSize(handle_, SO_SNDBUF, bytes, granted);
}

Status Socket::SetDualStack(bool on)
{
    if (!IsOpen() || family_ != AddressFamily::V6)
    {
        return Status::Of(ErrorCode::Invalid);
    }
    return SetSocketOption(handle_, IPPROTO_IPV6, IPV6_V6ONLY, on ? 0 : 1);
}

Status Socket::SetBroadcast(bool on)
{
    if (!IsOpen())
    {
        return Status::Of(ErrorCode::Invalid);
    }
    return SetSocketOption(handle_, SOL_SOCKET, SO_BROADCAST, on ? 1 : 0);
}

}  // namespace vse::net
