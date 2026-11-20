#include "net/udp_port.h"

#include "lang/status_try.h"
#include "lang/utility.h"

namespace vse::net
{

Status SocketDatagramPort::Open(const Endpoint& local, SocketDatagramPort* out, usize receive_buffer_bytes)
{
    Socket socket;
    ENGINE_TRY(Socket::OpenUdp(local.GetAddress().Family(), &socket));
    ENGINE_TRY(socket.Bind(local));
    if (receive_buffer_bytes != 0)
    {
        usize granted = 0;
        if (socket.SetReceiveBuffer(receive_buffer_bytes, &granted).Failed())
        {
            // The system's default buffer stays; a datagram transport copes with loss.
        }
    }
    Endpoint bound;
    ENGINE_TRY(socket.LocalEndpoint(&bound));
    out->socket_ = Move(socket);
    out->local_ = bound;
    return Status::Success();
}

Status SocketDatagramPort::SendTo(ByteSpan bytes, const Endpoint& to)
{
    usize sent = 0;
    return socket_.SendTo(bytes, to, &sent);
}

Status SocketDatagramPort::ReceiveFrom(MutableByteSpan buffer, usize* received, Endpoint* from)
{
    return socket_.ReceiveFrom(buffer, received, from);
}

}  // namespace vse::net
