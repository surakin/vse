// net::Socket on POSIX (Linux and macOS): the one file in the engine that calls the BSD socket API, with the hooks of linux/ or macos/.

#include <net/if.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include "cstr/mem_ops.h"
#include "net/posix/posix_net.h"
#include "net/socket.h"
#include "os/last_error.h"
#include "os/os.h"

namespace engine_placeholder::net {

using posix::StatusFromErrno;

namespace posix {

socklen_t ToSockaddr(const Endpoint& endpoint, AddressFamily family, sockaddr_storage* out, Status* status) {
    MemSet(out, 0, sizeof(*out));
    Address address = endpoint.GetAddress();
    if (family == AddressFamily::V6 && address.IsV4()) {
        address = address.Mapped();
    } else if (family == AddressFamily::V4 && address.IsV4Mapped()) {
        address = address.Unmapped();
    }
    if (!address.IsValid() || address.Family() != family) {
        *status = Status::Of(ErrorCode::Invalid);
        return 0;
    }
    if (family == AddressFamily::V4) {
        sockaddr_in in{};
        in.sin_family = AF_INET;
        in.sin_port = htons(endpoint.Port());
        MemCopy(&in.sin_addr, address.Bytes().Data(), 4);
        MemCopy(out, &in, sizeof(in));
        return sizeof(in);
    }
    sockaddr_in6 in6{};
    in6.sin6_family = AF_INET6;
    in6.sin6_port = htons(endpoint.Port());
    MemCopy(&in6.sin6_addr, address.Bytes().Data(), 16);
    in6.sin6_scope_id = address.ScopeId();
    if (address.ScopeName().Length() != 0) {
        char name[Address::kMaxScopeNameLength + 1] = {};
        MemCopy(name, address.ScopeName().Data(), address.ScopeName().Length());
        const unsigned index = ::if_nametoindex(name);
        if (index == 0) {
            *status = Status::Of(ErrorCode::AddressNotAvailable);
            return 0;
        }
        in6.sin6_scope_id = index;
    }
    MemCopy(out, &in6, sizeof(in6));
    return sizeof(in6);
}

bool FromSockaddr(const sockaddr_storage& storage, Endpoint* out) {
    if (storage.ss_family == AF_INET) {
        sockaddr_in in{};
        MemCopy(&in, &storage, sizeof(in));
        const u8* b = reinterpret_cast<const u8*>(&in.sin_addr);
        *out = Endpoint(Address::V4(b[0], b[1], b[2], b[3]), ntohs(in.sin_port));
        return true;
    }
    if (storage.ss_family == AF_INET6) {
        sockaddr_in6 in6{};
        MemCopy(&in6, &storage, sizeof(in6));
        Array<u8, 16> bytes{};
        MemCopy(bytes.Data(), &in6.sin6_addr, 16);
        const Address a = Address::V6(bytes, in6.sin6_scope_id);
        *out = Endpoint(a.Unmapped(), ntohs(in6.sin6_port));
        return true;
    }
    return false;
}

}  // namespace posix

namespace {

int Fd(i64 handle) { return static_cast<int>(handle); }

// Runs `call` until it stops failing with EINTR.
template <typename Call>
auto RetryOnInterrupt(Call call) {
    auto result = call();
    while (result < 0 && os::LastError() == EINTR) {
        result = call();
    }
    return result;
}

}  // namespace

Socket::~Socket() {
    if (IsOpen()) {
        os::Close(Fd(handle_));
    }
}

Socket::Socket(Socket&& other) noexcept : handle_(other.handle_), family_(other.family_), datagram_(other.datagram_), connecting_(other.connecting_) {
    other.handle_ = -1;
    other.connecting_ = false;
    other.family_ = AddressFamily::None;
    other.datagram_ = false;
}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        if (IsOpen()) os::Close(Fd(handle_));
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

Status Socket::Open(AddressFamily family, bool datagram, Socket* out) {
    if (family == AddressFamily::None || out->IsOpen()) return Status::Of(ErrorCode::Invalid);
    const int fd = posix::OpenSocketFd(family == AddressFamily::V4 ? AF_INET : AF_INET6, datagram ? SOCK_DGRAM : SOCK_STREAM);
    if (fd < 0) return StatusFromErrno(os::LastError());
    out->handle_ = fd;
    out->family_ = family;
    out->datagram_ = datagram;
    out->connecting_ = false;
    return Status::Success();
}

Status Socket::OpenTcp(AddressFamily family, Socket* out) { return Open(family, false, out); }

Status Socket::OpenUdp(AddressFamily family, Socket* out) { return Open(family, true, out); }

Status Socket::Bind(const Endpoint& local) {
    if (!IsOpen()) return Status::Of(ErrorCode::Invalid);
    sockaddr_storage storage;
    Status status;
    const socklen_t length = posix::ToSockaddr(local, family_, &storage, &status);
    if (length == 0) return status;
    if (::bind(Fd(handle_), reinterpret_cast<const sockaddr*>(&storage), length) != 0) return StatusFromErrno(os::LastError());
    return Status::Success();
}

Status Socket::Listen(i32 backlog) {
    if (!IsOpen() || datagram_) return Status::Of(ErrorCode::Invalid);
    if (::listen(Fd(handle_), backlog) != 0) return StatusFromErrno(os::LastError());
    return Status::Success();
}

Status Socket::Accept(Socket* connection, Endpoint* peer) {
    if (!IsOpen() || datagram_ || connection->IsOpen()) return Status::Of(ErrorCode::Invalid);
    for (;;) {
        sockaddr_storage storage;
        socklen_t length = sizeof(storage);
        const int fd = posix::AcceptFd(Fd(handle_), reinterpret_cast<sockaddr*>(&storage), &length);
        if (fd < 0) {
            const int error = os::LastError();
            // A connection that died before it was accepted, or a network error of that one connection (accept(2) lists them), is not a result for the listener.
            if (error == EINTR || error == ECONNABORTED || error == EPROTO || error == ENETDOWN || error == ENOPROTOOPT || error == EHOSTDOWN ||
                error == EHOSTUNREACH || error == EOPNOTSUPP || error == ENETUNREACH) {
                continue;
            }
            return StatusFromErrno(error);
        }
        connection->handle_ = fd;
        connection->family_ = family_;
        connection->datagram_ = false;
        if (!posix::FromSockaddr(storage, peer)) {
            *peer = Endpoint();
        }
        return Status::Success();
    }
}

Status Socket::Connect(const Endpoint& remote) {
    if (!IsOpen()) return Status::Of(ErrorCode::Invalid);
    sockaddr_storage storage;
    Status status;
    const socklen_t length = posix::ToSockaddr(remote, family_, &storage, &status);
    if (length == 0) return status;
    if (::connect(Fd(handle_), reinterpret_cast<const sockaddr*>(&storage), length) == 0) return Status::Success();
    const int error = os::LastError();
    if (error == EINPROGRESS || error == EINTR) {
        connecting_ = true;
        return Status::Of(ErrorCode::WouldBlock);
    }
    return StatusFromErrno(error);
}

Status Socket::FinishConnect() {
    if (!IsOpen() || datagram_ || !connecting_) return Status::Of(ErrorCode::Invalid);
    int pending = 0;
    socklen_t size = sizeof(pending);
    if (::getsockopt(Fd(handle_), SOL_SOCKET, SO_ERROR, &pending, &size) != 0) return StatusFromErrno(os::LastError());
    if (pending != 0) {
        connecting_ = false;  // the failure is reported once
        return StatusFromErrno(pending);
    }
    sockaddr_storage storage;
    socklen_t length = sizeof(storage);
    if (::getpeername(Fd(handle_), reinterpret_cast<sockaddr*>(&storage), &length) != 0) {
        const int error = os::LastError();
        if (error == ENOTCONN) return Status::Of(ErrorCode::WouldBlock);  // no error recorded and no peer yet: still under way
        connecting_ = false;
        return StatusFromErrno(error);
    }
    connecting_ = false;
    return Status::Success();
}

Status Socket::Send(ByteSpan bytes, usize* sent) {
    *sent = 0;
    if (!IsOpen()) return Status::Of(ErrorCode::Invalid);
    const auto n = RetryOnInterrupt([&] { return ::send(Fd(handle_), bytes.Data(), bytes.Size(), posix::SendFlags()); });
    if (n < 0) return StatusFromErrno(os::LastError());
    *sent = static_cast<usize>(n);
    return Status::Success();
}

Status Socket::Receive(MutableByteSpan buffer, usize* received) {
    *received = 0;
    if (!IsOpen() || buffer.Size() == 0) return Status::Of(ErrorCode::Invalid);
    const int flags = datagram_ ? posix::TruncationFlag() : 0;
    const auto n = RetryOnInterrupt([&] { return ::recv(Fd(handle_), buffer.Data(), buffer.Size(), flags); });
    if (n < 0) return StatusFromErrno(os::LastError());
    if (n == 0 && !datagram_) return Status::Of(ErrorCode::Closed);  // the peer shut down its writing half
    if (datagram_ && static_cast<usize>(n) > buffer.Size()) return Status::Of(ErrorCode::MessageTooLarge);  // cut: the real size, from the truncation flag
    *received = static_cast<usize>(n);
    return Status::Success();
}

Status Socket::SendTo(ByteSpan bytes, const Endpoint& to, usize* sent) {
    *sent = 0;
    if (!IsOpen() || !datagram_) return Status::Of(ErrorCode::Invalid);
    sockaddr_storage storage;
    Status status;
    const socklen_t length = posix::ToSockaddr(to, family_, &storage, &status);
    if (length == 0) return status;
    const auto n = RetryOnInterrupt(
        [&] { return ::sendto(Fd(handle_), bytes.Data(), bytes.Size(), posix::SendFlags(), reinterpret_cast<const sockaddr*>(&storage), length); });
    if (n < 0) return StatusFromErrno(os::LastError());
    *sent = static_cast<usize>(n);
    return Status::Success();
}

Status Socket::ReceiveFrom(MutableByteSpan buffer, usize* received, Endpoint* from) {
    *received = 0;
    if (!IsOpen() || !datagram_ || buffer.Size() == 0) return Status::Of(ErrorCode::Invalid);
    sockaddr_storage storage{};
    socklen_t length = sizeof(storage);
    const auto n = RetryOnInterrupt(
        [&] { return ::recvfrom(Fd(handle_), buffer.Data(), buffer.Size(), posix::TruncationFlag(), reinterpret_cast<sockaddr*>(&storage), &length); });
    if (n < 0) return StatusFromErrno(os::LastError());
    if (static_cast<usize>(n) > buffer.Size()) return Status::Of(ErrorCode::MessageTooLarge);  // the real size, reported by the truncation flag
    *received = static_cast<usize>(n);
    if (!posix::FromSockaddr(storage, from)) {
        *from = Endpoint();
    }
    return Status::Success();
}

Status Socket::Shutdown(ShutdownMode mode) {
    if (!IsOpen()) return Status::Of(ErrorCode::Invalid);
    const int how = mode == ShutdownMode::Read ? SHUT_RD : (mode == ShutdownMode::Write ? SHUT_WR : SHUT_RDWR);
    if (::shutdown(Fd(handle_), how) != 0) return StatusFromErrno(os::LastError());
    return Status::Success();
}

Status Socket::Close() {
    if (!IsOpen()) return Status::Of(ErrorCode::Invalid);
    const int fd = Fd(handle_);
    handle_ = -1;
    family_ = AddressFamily::None;
    datagram_ = false;
    connecting_ = false;
    // The handle is released even when close reports an error (on Linux the descriptor is gone either way), so it is never closed twice.
    if (os::Close(fd) != 0 && os::LastError() != EINTR) return StatusFromErrno(os::LastError());
    return Status::Success();
}

Status Socket::LocalEndpoint(Endpoint* out) const {
    if (!IsOpen()) return Status::Of(ErrorCode::Invalid);
    sockaddr_storage storage;
    socklen_t length = sizeof(storage);
    if (::getsockname(Fd(handle_), reinterpret_cast<sockaddr*>(&storage), &length) != 0) return StatusFromErrno(os::LastError());
    return posix::FromSockaddr(storage, out) ? Status::Success() : Status::Of(ErrorCode::Unsupported);
}

Status Socket::PeerEndpoint(Endpoint* out) const {
    if (!IsOpen()) return Status::Of(ErrorCode::Invalid);
    sockaddr_storage storage;
    socklen_t length = sizeof(storage);
    if (::getpeername(Fd(handle_), reinterpret_cast<sockaddr*>(&storage), &length) != 0) return StatusFromErrno(os::LastError());
    return posix::FromSockaddr(storage, out) ? Status::Success() : Status::Of(ErrorCode::Unsupported);
}

namespace {

Status SetIntOption(i64 handle, int level, int name, int value) {
    if (::setsockopt(Fd(handle), level, name, &value, sizeof(value)) != 0) return StatusFromErrno(os::LastError());
    return Status::Success();
}

Status SetBufferOption(i64 handle, int name, usize bytes, usize* granted) {
    const int clamped = bytes > 0x3fffffff ? 0x3fffffff : static_cast<int>(bytes);
    const Status s = SetIntOption(handle, SOL_SOCKET, name, clamped);
    if (s.Failed()) return s;
    if (granted != nullptr) {
        int value = 0;
        socklen_t size = sizeof(value);
        if (::getsockopt(Fd(handle), SOL_SOCKET, name, &value, &size) != 0) return StatusFromErrno(os::LastError());
        *granted = static_cast<usize>(value);
    }
    return Status::Success();
}

}  // namespace

Status Socket::SetReuseAddress(bool on) {
    if (!IsOpen()) return Status::Of(ErrorCode::Invalid);
    return SetIntOption(handle_, SOL_SOCKET, SO_REUSEADDR, on ? 1 : 0);
}

Status Socket::SetNoDelay(bool on) {
    if (!IsOpen() || datagram_) return Status::Of(ErrorCode::Invalid);
    return SetIntOption(handle_, IPPROTO_TCP, TCP_NODELAY, on ? 1 : 0);
}

Status Socket::SetKeepAlive(bool on) {
    if (!IsOpen()) return Status::Of(ErrorCode::Invalid);
    return SetIntOption(handle_, SOL_SOCKET, SO_KEEPALIVE, on ? 1 : 0);
}

Status Socket::SetReceiveBuffer(usize bytes, usize* granted) {
    if (!IsOpen()) return Status::Of(ErrorCode::Invalid);
    return SetBufferOption(handle_, SO_RCVBUF, bytes, granted);
}

Status Socket::SetSendBuffer(usize bytes, usize* granted) {
    if (!IsOpen()) return Status::Of(ErrorCode::Invalid);
    return SetBufferOption(handle_, SO_SNDBUF, bytes, granted);
}

Status Socket::SetDualStack(bool on) {
    if (!IsOpen() || family_ != AddressFamily::V6) return Status::Of(ErrorCode::Invalid);
    return SetIntOption(handle_, IPPROTO_IPV6, IPV6_V6ONLY, on ? 0 : 1);
}

Status Socket::SetBroadcast(bool on) {
    if (!IsOpen()) return Status::Of(ErrorCode::Invalid);
    return SetIntOption(handle_, SOL_SOCKET, SO_BROADCAST, on ? 1 : 0);
}

}  // namespace engine_placeholder::net
