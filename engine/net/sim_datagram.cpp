#include "net/sim_datagram.h"

#include "cstr/mem_ops.h"
#include "lang/utility.h"

namespace vse::net
{

SimDatagramPort::SimDatagramPort(SimNetwork& network, const Endpoint& local) : network_(&network), local_(local)
{
    attached_ = network.Attach(local, this).Ok();
}

SimDatagramPort::~SimDatagramPort()
{
    if (attached_)
    {
        network_->Detach(local_);
    }
}

Status SimDatagramPort::SendTo(ByteSpan bytes, const Endpoint& to)
{
    network_->Transmit(local_, to, bytes);
    return Status::Success();
}

Status SimDatagramPort::ReceiveFrom(MutableByteSpan buffer, usize* received, Endpoint* from)
{
    if (head_ >= queue_.Size())
    {
        queue_.Clear();
        head_ = 0;
        return Status::Of(ErrorCode::WouldBlock);
    }
    Datagram& d = queue_[head_++];
    if (d.data.Size() > buffer.Size())
    {
        return Status::Of(ErrorCode::MessageTooLarge);
    }
    if (d.data.Size() != 0)
    {
        MemCopy(buffer.Data(), d.data.Data(), d.data.Size());
    }
    *received = d.data.Size();
    *from = d.from;
    return Status::Success();
}

void SimDatagramPort::OnPacket(const Endpoint& from, ByteSpan packet)
{
    Datagram d;
    d.from = from;
    d.data.TryResizeUninitialized(packet.Size());
    if (packet.Size() != 0)
    {
        MemCopy(d.data.Data(), packet.Data(), packet.Size());
    }
    queue_.Push(Move(d));
}

void SimDatagramPort::OnTick(u64) {}

}  // namespace vse::net
