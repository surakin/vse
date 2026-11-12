#include "net/socket_stream.h"

#include "lang/status_try.h"
#include "lang/utility.h"

namespace engine_placeholder::net
{
namespace
{

class SocketStream final : public StreamIo
{
public:
    explicit SocketStream(Socket&& socket, bool connecting) : socket_(Move(socket)), connecting_(connecting) {}

    Status FinishConnect() override
    {
        if (!connecting_)
        {
            return Status::Success();
        }
        const Status s = socket_.FinishConnect();
        if (!s.Is(ErrorCode::WouldBlock))
        {
            connecting_ = false;
        }
        return s;
    }
    Status Receive(MutableByteSpan buffer, usize* received) override { return socket_.Receive(buffer, received); }
    Status Send(ByteSpan bytes, usize* sent) override { return socket_.Send(bytes, sent); }
    Status ShutdownWrite() override { return socket_.Shutdown(ShutdownMode::Write); }

private:
    Socket socket_;
    bool connecting_;
};

}  // namespace

Status SocketStreamProvider::Listen(const Endpoint& at)
{
    Socket socket;
    ENGINE_TRY(Socket::OpenTcp(at.GetAddress().Family(), &socket));
    ENGINE_TRY(socket.SetReuseAddress(true));
    ENGINE_TRY(socket.Bind(at));
    ENGINE_TRY(socket.Listen(512));
    Endpoint bound;
    ENGINE_TRY(socket.LocalEndpoint(&bound));
    listener_ = Move(socket);
    local_ = bound;
    return Status::Success();
}

Status SocketStreamProvider::Accept(UniquePtr<StreamIo>* stream, Endpoint* peer)
{
    Socket accepted;
    Endpoint from;
    ENGINE_TRY(listener_.Accept(&accepted, &from));
    if (accepted.SetNoDelay(true).Failed())
    {
        // Nagle's delay stays: slower, not wrong.
    }
    *stream = MakeUnique<SocketStream>(Move(accepted), false);
    *peer = from;
    return Status::Success();
}

Status SocketStreamProvider::Connect(const Endpoint& to, UniquePtr<StreamIo>* stream)
{
    Socket socket;
    ENGINE_TRY(Socket::OpenTcp(to.GetAddress().Family(), &socket));
    if (socket.SetNoDelay(true).Failed())
    {
        // Nagle's delay stays: slower, not wrong.
    }
    const Status s = socket.Connect(to);
    if (s.Failed() && !s.Is(ErrorCode::WouldBlock))
    {
        return s;
    }
    *stream = MakeUnique<SocketStream>(Move(socket), s.Is(ErrorCode::WouldBlock));
    return Status::Success();
}

}  // namespace engine_placeholder::net
