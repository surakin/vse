#include "net/sim_network.h"

#include "cstr/mem_ops.h"
#include "lang/utility.h"

namespace vse::net
{

namespace
{

u32 Checksum(ByteSpan data)
{
    u32 hash = 2166136261u;
    for (usize i = 0; i < data.Size(); ++i)
    {
        hash ^= static_cast<u8>(data[i]);
        hash *= 16777619u;
    }
    return hash;
}

enum TraceKind : u8
{
    kDropLoss = 0,
    kDropMtu = 1,
    kDropQueue = 2,
    kDeliver = 3,
    kUnreachable = 4,
};

}  // namespace

SimNetwork::SimNetwork(u64 seed) : state_(seed == 0 ? 0x9e3779b97f4a7c15ull : seed) {}

u64 SimNetwork::Next()
{
    state_ ^= state_ << 13;
    state_ ^= state_ >> 7;
    state_ ^= state_ << 17;
    return state_;
}

f64 SimNetwork::Uniform()
{
    return static_cast<f64>(Next() >> 11) / static_cast<f64>(u64{1} << 53);
}

void SimNetwork::SetLink(const Endpoint& from, const Endpoint& to, const LinkConfig& config)
{
    LinkState& link = LinkFor(from, to);
    link.has_config = true;
    link.config = config;
}

SimNetwork::LinkState& SimNetwork::LinkFor(const Endpoint& from, const Endpoint& to)
{
    for (usize i = 0; i < links_.Size(); ++i)
    {
        if (links_[i].from == from && links_[i].to == to)
        {
            return links_[i];
        }
    }
    LinkState fresh;
    fresh.from = from;
    fresh.to = to;
    links_.Push(Move(fresh));
    return links_[links_.Size() - 1];
}

Status SimNetwork::Attach(const Endpoint& at, PacketSink* sink)
{
    for (usize i = 0; i < attached_.Size(); ++i)
    {
        if (attached_[i].at == at)
        {
            return Status::Of(ErrorCode::AddressInUse);
        }
    }
    Attached entry;
    entry.at = at;
    entry.sink = sink;
    attached_.Push(entry);
    return Status::Success();
}

void SimNetwork::Detach(const Endpoint& at)
{
    for (usize i = 0; i < attached_.Size(); ++i)
    {
        if (attached_[i].at == at)
        {
            attached_.Remove(i);
            return;
        }
    }
}

Endpoint SimNetwork::AllocateEndpoint()
{
    for (;;)
    {
        const Endpoint candidate(Address::Loopback(AddressFamily::V4), next_port_++);
        bool taken = false;
        for (usize i = 0; i < attached_.Size(); ++i)
        {
            taken = taken || attached_[i].at == candidate;
        }
        if (!taken)
        {
            return candidate;
        }
    }
}

void SimNetwork::Trace(u8 kind, const Endpoint& from, const Endpoint& to, ByteSpan data)
{
    if (!tracing_)
    {
        return;
    }
    trace_.WriteU8(kind);
    trace_.WriteU64(NowNanos());
    trace_.WriteU16(from.Port());
    trace_.WriteU16(to.Port());
    trace_.WriteU32(static_cast<u32>(data.Size()));
    trace_.WriteU32(Checksum(data));
}

bool SimNetwork::Before(const UniquePtr<InFlight>& a, const UniquePtr<InFlight>& b) const
{
    return a->deliver != b->deliver ? a->deliver < b->deliver : a->order < b->order;
}

void SimNetwork::SiftUp(usize index)
{
    while (index > 0)
    {
        const usize parent = (index - 1) / 2;
        if (!Before(heap_[index], heap_[parent]))
        {
            break;
        }
        Swap(heap_[index], heap_[parent]);
        index = parent;
    }
}

void SimNetwork::SiftDown(usize index)
{
    for (;;)
    {
        usize best = index;
        const usize left = index * 2 + 1;
        const usize right = left + 1;
        if (left < heap_.Size() && Before(heap_[left], heap_[best]))
        {
            best = left;
        }
        if (right < heap_.Size() && Before(heap_[right], heap_[best]))
        {
            best = right;
        }
        if (best == index)
        {
            return;
        }
        Swap(heap_[index], heap_[best]);
        index = best;
    }
}

void SimNetwork::Push(u64 deliver, const Endpoint& from, const Endpoint& to, const Vector<byte>& data)
{
    UniquePtr<InFlight> packet = MakeUnique<InFlight>();
    packet->deliver = deliver;
    packet->order = order_++;
    packet->from = from;
    packet->to = to;
    packet->data.TryResizeUninitialized(data.Size());
    if (data.Size() != 0)
    {
        MemCopy(packet->data.Data(), data.Data(), data.Size());
    }
    heap_.Push(Move(packet));
    SiftUp(heap_.Size() - 1);
}

void SimNetwork::Transmit(const Endpoint& from, const Endpoint& to, ByteSpan packet)
{
    ++counters_.sent;
    LinkState& link = LinkFor(from, to);
    const LinkConfig cfg = link.has_config ? link.config : default_link_;
    if (packet.Size() > cfg.mtu)
    {
        ++counters_.dropped_mtu;
        Trace(kDropMtu, from, to, packet);
        return;
    }
    // Loss: a burst in progress, a new burst, or uniform loss.
    bool lost = false;
    if (link.burst_left > 0)
    {
        --link.burst_left;
        lost = true;
    }
    else if (cfg.burst_start > 0 && cfg.burst_length > 0 && Uniform() < cfg.burst_start)
    {
        link.burst_left = cfg.burst_length - 1;
        lost = true;
    }
    else if (cfg.loss > 0 && Uniform() < cfg.loss)
    {
        lost = true;
    }
    if (lost)
    {
        ++counters_.dropped_loss;
        Trace(kDropLoss, from, to, packet);
        return;
    }
    // Bandwidth: the packet leaves when the link has sent what is queued before it.
    const u64 now = NowNanos();
    u64 depart = now;
    if (cfg.bandwidth_bps > 0)
    {
        const u64 start = link.free_at > now ? link.free_at : now;
        if (cfg.queue_bytes > 0)
        {
            // Microseconds of backlog, so the product cannot overflow.
            const u64 backlog_bytes = (start - now) / 1000 * cfg.bandwidth_bps / 8'000'000ull;
            if (backlog_bytes + packet.Size() > cfg.queue_bytes)
            {
                ++counters_.dropped_queue;
                Trace(kDropQueue, from, to, packet);
                return;
            }
        }
        const u64 tx = static_cast<u64>(packet.Size()) * 8'000'000'000ull / cfg.bandwidth_bps;
        link.free_at = start + tx;
        depart = link.free_at;
    }
    u64 delay = cfg.latency_nanos;
    if (cfg.jitter_nanos > 0)
    {
        delay += static_cast<u64>(Uniform() * static_cast<f64>(cfg.jitter_nanos));
    }
    if (cfg.reorder > 0 && Uniform() < cfg.reorder)
    {
        delay += cfg.reorder_delay_nanos != 0 ? cfg.reorder_delay_nanos : 3 * cfg.latency_nanos + kTickNanos;
        ++counters_.reordered;
    }
    Vector<byte> data;
    data.TryResizeUninitialized(packet.Size());
    if (packet.Size() != 0)
    {
        MemCopy(data.Data(), packet.Data(), packet.Size());
    }
    if (cfg.corrupt > 0 && packet.Size() > 0 && Uniform() < cfg.corrupt)
    {
        const u64 bit = Next() % (static_cast<u64>(packet.Size()) * 8);
        data[bit / 8] = static_cast<byte>(static_cast<u8>(data[bit / 8]) ^ (1u << (bit % 8)));
        ++counters_.corrupted;
    }
    Push(depart + delay, from, to, data);
    if (cfg.duplicate > 0 && Uniform() < cfg.duplicate)
    {
        u64 extra = cfg.latency_nanos;
        if (cfg.jitter_nanos > 0)
        {
            extra += static_cast<u64>(Uniform() * static_cast<f64>(cfg.jitter_nanos));
        }
        Push(depart + extra + kTickNanos, from, to, data);
        ++counters_.duplicated;
    }
}

void SimNetwork::DeliverDue()
{
    const u64 now = NowNanos();
    while (heap_.Size() > 0 && heap_[0]->deliver <= now)
    {
        UniquePtr<InFlight> packet = Move(heap_[0]);
        Swap(heap_[0], heap_[heap_.Size() - 1]);
        heap_.Pop();
        if (heap_.Size() > 0)
        {
            SiftDown(0);
        }
        PacketSink* sink = nullptr;
        PacketSink* sender = nullptr;
        for (usize i = 0; i < attached_.Size(); ++i)
        {
            if (attached_[i].at == packet->to)
            {
                sink = attached_[i].sink;
            }
            if (attached_[i].at == packet->from)
            {
                sender = attached_[i].sink;
            }
        }
        const ByteSpan bytes(packet->data.Data(), packet->data.Size());
        if (sink == nullptr)
        {
            ++counters_.unreachable;
            Trace(kUnreachable, packet->from, packet->to, bytes);
            if (sender != nullptr)
            {
                sender->OnUnreachable(packet->to);
            }
            continue;
        }
        ++counters_.delivered;
        Trace(kDeliver, packet->from, packet->to, bytes);
        sink->OnPacket(packet->from, bytes);
    }
}

void SimNetwork::Advance(u64 nanos)
{
    const u64 ticks = (nanos + kTickNanos - 1) / kTickNanos;
    for (u64 t = 0; t < ticks; ++t)
    {
        clock_.Advance(kTickNanos);
        DeliverDue();
        // A sink may attach or detach during its tick: walk a copy of the list.
        tick_sinks_.Clear();
        for (usize i = 0; i < attached_.Size(); ++i)
        {
            tick_sinks_.Push(attached_[i]);
        }
        for (usize i = 0; i < tick_sinks_.Size(); ++i)
        {
            bool still = false;
            for (usize j = 0; j < attached_.Size() && !still; ++j)
            {
                still = attached_[j].sink == tick_sinks_[i].sink;
            }
            if (still)
            {
                tick_sinks_[i].sink->OnTick(NowNanos());
            }
        }
    }
}

}  // namespace vse::net
