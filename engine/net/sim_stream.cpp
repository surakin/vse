#include "net/sim_stream.h"

#include "cstr/mem_ops.h"
#include "lang/utility.h"

namespace vse::net
{
namespace
{

// Bytes in flight in one direction, each chunk with the time it arrives.
struct Chunk
{
    u64 deliver = 0;
    usize used = 0;
    Vector<byte> data;
};

struct Pipe
{
    Vector<Chunk> chunks;
    usize head = 0;
    usize in_flight = 0;  // bytes written and not yet read
    bool fin = false;
    u64 fin_time = 0;
};

// The two directions of one connection, shared by its two ends.
struct Link
{
    Pipe to_server;
    Pipe to_client;
};

class SimStream final : public StreamIo
{
public:
    SimStream(const Clock& clock, const SimStreamConfig& config, SharedPtr<Link> link, bool client, u64 connected_at)
        : clock_(&clock), config_(config), link_(Move(link)), client_(client), connected_at_(connected_at)
    {
    }

    Status FinishConnect() override
    {
        return clock_->NowNanos() >= connected_at_ ? Status::Success() : Status::Of(ErrorCode::WouldBlock);
    }

    Status Receive(MutableByteSpan buffer, usize* received) override
    {
        Pipe& in = client_ ? link_->to_client : link_->to_server;
        const u64 now = clock_->NowNanos();
        *received = 0;
        while (in.head < in.chunks.Size() && *received < buffer.Size())
        {
            Chunk& c = in.chunks[in.head];
            if (c.deliver > now)
            {
                break;
            }
            usize n = c.data.Size() - c.used;
            n = n > buffer.Size() - *received ? buffer.Size() - *received : n;
            if (config_.max_read_bytes != 0 && n > config_.max_read_bytes - *received)
            {
                n = config_.max_read_bytes - *received;
            }
            MemCopy(buffer.Data() + *received, c.data.Data() + c.used, n);
            *received += n;
            c.used += n;
            in.in_flight -= n;
            if (c.used == c.data.Size())
            {
                ++in.head;
            }
            if (config_.max_read_bytes != 0 && *received >= config_.max_read_bytes)
            {
                break;
            }
        }
        if (in.head > 32 && in.head * 2 > in.chunks.Size())
        {
            in.chunks.RemoveRange(0, in.head);
            in.head = 0;
        }
        if (*received != 0)
        {
            return Status::Success();
        }
        if (in.fin && in.fin_time <= now && in.head >= in.chunks.Size())
        {
            return Status::Of(ErrorCode::Closed);
        }
        return Status::Of(ErrorCode::WouldBlock);
    }

    Status Send(ByteSpan bytes, usize* sent) override
    {
        Pipe& out = client_ ? link_->to_server : link_->to_client;
        *sent = 0;
        if (out.fin)
        {
            return Status::Of(ErrorCode::Closed);
        }
        const usize room = config_.send_buffer_bytes > out.in_flight ? config_.send_buffer_bytes - out.in_flight : 0;
        const usize n = bytes.Size() < room ? bytes.Size() : room;
        if (n == 0)
        {
            return Status::Of(ErrorCode::WouldBlock);
        }
        Chunk c;
        c.deliver = clock_->NowNanos() + config_.latency_nanos;
        c.data.TryResizeUninitialized(n);
        MemCopy(c.data.Data(), bytes.Data(), n);
        out.chunks.Push(Move(c));
        out.in_flight += n;
        *sent = n;
        return Status::Success();
    }

    Status ShutdownWrite() override
    {
        Pipe& out = client_ ? link_->to_server : link_->to_client;
        if (!out.fin)
        {
            out.fin = true;
            out.fin_time = clock_->NowNanos() + config_.latency_nanos;
        }
        return Status::Success();
    }

    ~SimStream() override
    {
        Pipe& out = client_ ? link_->to_server : link_->to_client;
        if (!out.fin)
        {
            out.fin = true;
            out.fin_time = clock_->NowNanos() + config_.latency_nanos;
        }
    }

private:
    const Clock* clock_;
    SimStreamConfig config_;
    SharedPtr<Link> link_;
    bool client_;
    u64 connected_at_;
};

struct StreamPending
{
    SharedPtr<Link> link;
    Endpoint peer;
    u64 ready = 0;
};

}  // namespace

// The provider of one endpoint: its listener's accept queue lives here.
class SimStreamProvider final : public StreamProvider
{
public:
    SimStreamProvider(SimStreamNetwork& network, const Endpoint& local) : network_(&network), local_(local) {}

    ~SimStreamProvider() override
    {
        Vector<SimStreamNetwork::ListenerEntry>& l = network_->listeners_;
        for (usize i = 0; i < l.Size(); ++i)
        {
            if (l[i].provider == this)
            {
                l.Remove(i);
                break;
            }
        }
    }

    Status Listen(const Endpoint& at) override
    {
        if (!(at == local_) || listening_)
        {
            return Status::Of(ErrorCode::Invalid);
        }
        for (usize i = 0; i < network_->listeners_.Size(); ++i)
        {
            if (network_->listeners_[i].at == at)
            {
                return Status::Of(ErrorCode::AddressInUse);
            }
        }
        SimStreamNetwork::ListenerEntry e;
        e.at = at;
        e.provider = this;
        network_->listeners_.Push(e);
        listening_ = true;
        return Status::Success();
    }

    Status Accept(UniquePtr<StreamIo>* stream, Endpoint* peer) override
    {
        const u64 now = network_->clock_->NowNanos();
        if (head_ < pending_.Size() && pending_[head_].ready <= now)
        {
            StreamPending& p = pending_[head_++];
            *stream = MakeUnique<SimStream>(*network_->clock_, network_->config_, p.link, false, 0);
            *peer = p.peer;
            if (head_ > 32 && head_ * 2 > pending_.Size())
            {
                pending_.RemoveRange(0, head_);
                head_ = 0;
            }
            return Status::Success();
        }
        return Status::Of(ErrorCode::WouldBlock);
    }

    Status Connect(const Endpoint& to, UniquePtr<StreamIo>* stream) override
    {
        SimStreamProvider* server = nullptr;
        for (usize i = 0; i < network_->listeners_.Size(); ++i)
        {
            if (network_->listeners_[i].at == to)
            {
                server = static_cast<SimStreamProvider*>(network_->listeners_[i].provider);
            }
        }
        if (server == nullptr)
        {
            return Status::Of(ErrorCode::Refused);
        }
        const u64 now = network_->clock_->NowNanos();
        SharedPtr<Link> link = MakeShared<Link>();
        StreamPending p;
        p.link = link;
        p.peer = local_;
        p.ready = now + network_->config_.latency_nanos;
        server->pending_.Push(Move(p));
        *stream = MakeUnique<SimStream>(*network_->clock_, network_->config_, link, true,
                                        now + 2 * network_->config_.latency_nanos);
        return Status::Success();
    }

private:
    SimStreamNetwork* network_;
    Endpoint local_;
    bool listening_ = false;
    Vector<StreamPending> pending_;
    usize head_ = 0;
};

SimStreamNetwork::SimStreamNetwork(const Clock& clock, const SimStreamConfig& config) : clock_(&clock), config_(config)
{
}

Endpoint SimStreamNetwork::AllocateEndpoint()
{
    return Endpoint(Address::Loopback(AddressFamily::V4), next_port_++);
}

UniquePtr<StreamProvider> SimStreamNetwork::MakeProvider(const Endpoint& local)
{
    return MakeUnique<SimStreamProvider>(*this, local);
}

}  // namespace vse::net
