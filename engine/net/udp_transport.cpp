#include "net/udp_transport.h"

#include "containers/array.h"
#include "cstr/mem_ops.h"
#include "lang/utility.h"
#include "mem/heap_allocator.h"
#include "os/os.h"

namespace engine_placeholder::net
{

namespace
{

constexpr usize kUdpMaxMessageBytes = 65535u * 1024u;  // fragment counts are 16 bits

// The configuration with the values the wire format cannot carry brought down to what it can.
UdpTransportConfig ClampedConfig(UdpTransportConfig c)
{
    if (c.max_message_bytes > kUdpMaxMessageBytes)
    {
        c.max_message_bytes = kUdpMaxMessageBytes;
    }
    if (c.max_connections > 65535)
    {
        c.max_connections = 65535;  // wire ids are 16 bits
    }
    return c;
}

constexpr u8 kUdpFrameData = 1;
constexpr usize kUdpDataHeaderBytes = 1 + 1 + 1 + 2 + 2;  // type, channel, kind and flags, message sequence, length
constexpr usize kUdpFragmentExtraBytes = 2 + 2 + 4;       // fragment index, fragment count, total size
constexpr usize kUdpFragmentBytes = 1024;                 // payload of every fragment but the last
constexpr usize kUdpSingleMax = udp::kMaxPayloadBytes - kUdpDataHeaderBytes;  // largest message sent as one frame
constexpr u8 kUdpFragmentFlag = 0x80;
constexpr usize kUdpRingSize = 1024;       // packets remembered for acknowledgement
constexpr u16 kUdpLookahead = 1024;        // how far ahead of the next expected a reliable message may be
constexpr u32 kUdpLossThreshold = 3;       // packets behind an acknowledged one that count as lost
constexpr u32 kUdpAckEvery = 8;            // packets with frames received before an acknowledgement is sent at once
constexpr usize kUdpPacketsPerFlush = 32;  // one acknowledgement covers 33 packets: a burst may not be longer
constexpr u64 kUdpMinRto = 30'000'000ull;
constexpr u64 kUdpMaxRto = 2'000'000'000ull;
constexpr u64 kUdpInitialRto = 200'000'000ull;
constexpr usize kReceiveBatch = 256;        // datagrams taken from the port per Poll
constexpr usize kUdpRetiredMemory = 16384;  // closed handshakes remembered while their cookie lives
constexpr usize
    kUdpRetiredPerPrefix = 1024;  // per /24 (IPv4) or /48 (IPv6): sixteen networks are needed to fill the table
constexpr usize kUdpRetiredPerAddress = 64;  // of them, per source address: a churning address is denied alone
constexpr u64 kMaxHandshakeRetry = 1'000'000'000ull;

enum class UdpState : u8
{
    Connecting,
    Connected,
    Closed,
};

enum class Phase : u8
{
    Request,   // client: waiting for the Challenge
    Response,  // client: waiting for Accepted
};

struct UdpOutFrame
{
    bool live = false;
    u32 gen = 0;
    u8 channel = 0;
    u8 rel = 0;
    u16 seq = 0;
    u16 frag_index = 0;
    u16 frag_count = 0;  // 0: a whole message
    u32 total = 0;       // fragments: the message's size
    Vector<byte> data;
    u16 packet_seq = 0;  // the packet that carried it last
    u64 sent_time = 0;
    u32 tries = 0;
    bool sent = false;
    bool resend_queued = false;
};

struct UdpSentPacket
{
    u16 seq = 0;
    bool used = false;
    bool acked = false;
    bool resolved = false;  // declared lost
    u64 time = 0;
    Vector<u64> frames;  // handles of the reliable frames it carried
};

struct UdpChannelOut
{
    Vector<u64> queue;  // handles, oldest first from `head`
    usize head = 0;
    u16 next_seq[4] = {};
};

struct UdpBuffered
{
    u16 seq = 0;
    Vector<byte> data;
};

struct UdpRecvSpace
{
    bool any = false;
    u16 next = 0;  // sequenced: the newest delivered; reliable: the next expected (ordered) or the lowest missing
    Vector<u16> above;
    Vector<UdpBuffered> buffered;
};

struct UdpChannelIn
{
    UdpRecvSpace space[4];
};

struct UdpReassembly
{
    u8 channel = 0;
    u8 rel = 0;
    u16 seq = 0;
    u32 total = 0;
    u16 count = 0;
    u16 got = 0;
    Vector<u8> have;
    Vector<byte> data;
    u64 started = 0;
};

struct UdpConn
{
    ConnectionId id = 0;
    u16 wire_id = 0;  // the server's id for the connection, in every payload header
    Endpoint peer;
    bool incoming = false;
    UdpState state = UdpState::Connecting;
    Phase phase = Phase::Request;
    u64 client_salt = 0;
    u64 cookie = 0;
    u64 epoch = 0;
    u64 created = 0;
    u64 last_heard = 0;
    u64 last_sent = 0;
    u64 last_try = 0;
    u64 retry_nanos = 0;
    u64 phase_started = 0;  // when the client entered its current handshake phase
    bool ack_pending = false;
    u32 unacked_packets = 0;  // packets with frames received since our last packet: an acknowledgement covers 33 only
    u16 send_seq = 0;
    udp::ReceiveWindow recv;
    Vector<UdpOutFrame> frames;  // slots; a handle is (generation << 32) | slot
    Vector<u32> free_slots;
    Vector<UdpChannelOut> out;
    Vector<UdpChannelIn> in;
    Vector<u64> resend;  // handles to send again, oldest first from `resend_head`
    usize resend_head = 0;
    Vector<UdpSentPacket> ring;
    u16 tail_seq = 0;  // the oldest packet not yet resolved
    usize queued_frames = 0;
    usize queued_bytes = 0;
    usize inflight_frames = 0;
    usize receive_bytes = 0;  // reassembly plus out-of-order buffers
    Vector<UdpReassembly> reassemblies;
    bool has_rtt = false;
    f64 srtt = 0;
    f64 rttvar = 0;
    u64 frames_first_sent = 0;
    UdpConnectionInfo info;
    ConnectionStats stats;
};

struct UdpEventRec
{
    EventKind kind = EventKind::Opened;
    ConnectionId connection = 0;
    bool incoming = false;
    Endpoint peer;
    u8 channel = 0;
    Reliability reliability = Reliability::Unreliable;
    Vector<byte> data;
    DisconnectReason reason = DisconnectReason::None;
    u32 code = 0;
};

struct UdpLingering
{
    Endpoint peer;
    u16 wire_id = 0;
    u32 code = 0;
    u32 sends_left = 0;
    u64 next_send = 0;
};

struct UdpRetired
{
    u64 key = 0;     // keyed hash of (endpoint, salt): unpredictable to an attacker, so it cannot be made to collide
    Address group;   // the source address, or its /64 for IPv6: the unit of the quota
    Address prefix;  // its /24 (IPv4) or /48 (IPv6): the unit of the wider quota
};

u64 ReadU64(ByteSpan b, usize at)
{
    u64 v = 0;
    for (u32 i = 0; i < 8; ++i)
    {
        v |= static_cast<u64>(static_cast<u8>(b[at + i])) << (8 * i);
    }
    return v;
}

u16 ReadU16(ByteSpan b, usize at)
{
    return static_cast<u16>(static_cast<u8>(b[at]) | (static_cast<u8>(b[at + 1]) << 8));
}

void PutU64(Vector<byte>* out, u64 v)
{
    for (u32 i = 0; i < 8; ++i)
    {
        out->Push(static_cast<byte>((v >> (8 * i)) & 0xff));
    }
}

void PutU16(Vector<byte>* out, u16 v)
{
    out->Push(static_cast<byte>(v & 0xff));
    out->Push(static_cast<byte>(v >> 8));
}

void PutU32(Vector<byte>* out, u32 v)
{
    for (u32 i = 0; i < 4; ++i)
    {
        out->Push(static_cast<byte>((v >> (8 * i)) & 0xff));
    }
}

void PadTo(Vector<byte>* out, usize size)
{
    while (out->Size() < size)
    {
        out->Push(static_cast<byte>(0));
    }
}

}  // namespace

struct UdpTransport::Impl
{
    Impl(DatagramPort& p, const Clock& c, const UdpTransportConfig& cfg)
        : port(&p), clock(&c), config(ClampedConfig(cfg)), self(p.LocalEndpoint()), by_id(HeapAllocator::Default()),
          by_wire(HeapAllocator::Default()), retired_keys(HeapAllocator::Default()),
          retired_per_group(HeapAllocator::Default()), retired_per_prefix(HeapAllocator::Default())
    {
        state = cfg.seed;
        key0 = Random();
        key1 = Random();
    }

    DatagramPort* port;
    const Clock* clock;
    UdpTransportConfig config;
    Endpoint self;
    bool listening = false;
    u64 state;
    u64 key0 = 0;
    u64 key1 = 0;
    ConnectionId next_id = 1;
    u16 next_wire = 1;
    Vector<UniquePtr<UdpConn>> conns;
    HashMap<ConnectionId, UdpConn*, HeapAllocator> by_id;  // every connection by its local id
    HashMap<u16, UdpConn*, HeapAllocator> by_wire;         // incoming connections by their wire id
    Vector<UdpEventRec> queue;
    usize queue_head = 0;
    UdpEventRec current;
    Vector<UdpLingering> lingering;
    // Remembered handshakes, in two buckets by the period in which their cookie expires (a cookie expires after the
    // period following the one it was made in, and an entry is only kept while that is not over, so the current and the
    // next period are the only two that can be live).
    Vector<UdpRetired> retired[2];
    u64 retired_expiry[2] = {0, 0};
    usize retired_count = 0;
    HashMap<u64, u8, HeapAllocator> retired_keys;
    HashMap<Address, u32, HeapAllocator> retired_per_group;
    HashMap<Address, u32, HeapAllocator> retired_per_prefix;
    UdpCounters counters;
    Array<byte, 2048> buffer;

    u64 Now() const { return clock->NowNanos(); }

    u64 Random()
    {
        if (state == 0 && config.seed == 0)
        {
            u64 v = 0;
            os::FillRandomBytes(&v, sizeof(v));
            return v;
        }
        if (state == 0)
        {
            state = 0x9e3779b97f4a7c15ull;
        }
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    }

    UdpConn* FindById(ConnectionId id) const
    {
        UdpConn* const* found = by_id.Find(id);
        return found == nullptr ? nullptr : *found;
    }

    UdpConn* Create(const Endpoint& peer, bool incoming)
    {
        UniquePtr<UdpConn> conn = MakeUnique<UdpConn>();
        conn->id = next_id++;
        conn->peer = peer;
        conn->incoming = incoming;
        conn->created = Now();
        conn->last_heard = conn->created;
        conn->last_sent = conn->created;
        conn->send_seq = config.initial_sequence;
        conn->tail_seq = config.initial_sequence;
        conn->out.Resize(config.channels);
        conn->in.Resize(config.channels);
        conn->ring.Resize(kUdpRingSize);
        UdpConn* raw = conn.Get();
        by_id.Insert(raw->id, raw);
        conns.Push(Move(conn));
        return raw;
    }

    // The quota groups IPv6 addresses by their /64, the size of one subscriber's allocation; a wider quota groups IPv4
    // by /24 and IPv6 by /48, so no single network can fill the table.
    static Address GroupOf(const Address& a, usize keep_bytes_v6)
    {
        if (!a.IsV6())
        {
            return a;
        }
        Array<u8, 16> bytes = a.Bytes();
        for (usize i = keep_bytes_v6; i < 16; ++i)
        {
            bytes[i] = 0;
        }
        return Address::V6(bytes, 0);
    }
    static Address GroupOf(const Address& a) { return GroupOf(a, 8); }
    static Address PrefixOf(const Address& a)
    {
        if (a.IsV6())
        {
            return GroupOf(a, 6);
        }
        const Array<u8, 16>& b = a.Bytes();
        return Address::V4(static_cast<u8>(b[0]), static_cast<u8>(b[1]), static_cast<u8>(b[2]), 0);
    }

    u64 HandshakeKey(const Endpoint& from, u64 salt) const { return Cookie(from, salt, ~u64{0}); }

    static void CountUp(HashMap<Address, u32, HeapAllocator>& map, const Address& key)
    {
        u32* count = map.Find(key);
        if (count == nullptr)
        {
            map.Insert(key, 1);
        }
        else
        {
            ++*count;
        }
    }

    static void CountDown(HashMap<Address, u32, HeapAllocator>& map, const Address& key)
    {
        u32* count = map.Find(key);
        if (count != nullptr && --*count == 0)
        {
            map.Remove(key);
        }
    }

    void ClearBucket(usize bucket)
    {
        for (usize i = 0; i < retired[bucket].Size(); ++i)
        {
            const UdpRetired& r = retired[bucket][i];
            retired_keys.Remove(r.key);
            CountDown(retired_per_group, r.group);
            CountDown(retired_per_prefix, r.prefix);
        }
        retired_count -= retired[bucket].Size();
        retired[bucket].Clear();
    }

    // Remembers that this handshake produced a connection that has ended, until its cookie has expired. A cookie that
    // has expired already needs no memory.
    void Retire(const UdpConn* c)
    {
        const u64 expiry = c->epoch + 1;
        if (Epoch() > expiry)
        {
            return;
        }
        const usize bucket = expiry & 1;
        if (retired_expiry[bucket] != expiry)
        {
            ClearBucket(bucket);  // what is left in it expired two periods ago
            retired_expiry[bucket] = expiry;
        }
        UdpRetired r;
        r.key = HandshakeKey(c->peer, c->client_salt);
        r.group = GroupOf(c->peer.GetAddress());
        r.prefix = PrefixOf(c->peer.GetAddress());
        retired[bucket].Push(r);
        ++retired_count;
        retired_keys.Insert(r.key, 1);
        CountUp(retired_per_group, r.group);
        CountUp(retired_per_prefix, r.prefix);
    }

    // Forgets the handshakes whose cookies can no longer be presented.
    void PurgeRetired()
    {
        for (usize bucket = 0; bucket < 2; ++bucket)
        {
            if (!retired[bucket].Empty() && Epoch() > retired_expiry[bucket])
            {
                ClearBucket(bucket);
            }
        }
    }

    void Remove(UdpConn* c)
    {
        by_id.Remove(c->id);
        if (c->incoming)
        {
            by_wire.Remove(c->wire_id);
            Retire(c);
        }
        for (usize i = 0; i < conns.Size(); ++i)
        {
            if (conns[i].Get() == c)
            {
                conns.Remove(i);
                return;
            }
        }
    }

    // ---- sending ----

    void Emit(const Endpoint& to, const Vector<byte>& packet)
    {
        ++counters.datagrams_sent;
        if (port->SendTo(ByteSpan(packet.Data(), packet.Size()), to).Failed())
        {
            ++counters.send_failures;
        }
    }

    u64 Cookie(const Endpoint& from, u64 salt, u64 epoch) const
    {
        Vector<byte> in;
        PutU64(&in, epoch);
        PutU64(&in, salt);
        in.Push(static_cast<byte>(from.GetAddress().Family()));
        const Array<u8, 16>& bytes = from.GetAddress().Bytes();
        for (usize i = 0; i < 16; ++i)
        {
            in.Push(static_cast<byte>(bytes[i]));
        }
        PutU32(&in, from.GetAddress().ScopeId());
        PutU16(&in, from.Port());
        return udp::SipHash24(key0, key1, ByteSpan(in.Data(), in.Size()));
    }

    u64 Epoch() const { return Now() / config.cookie_period_nanos; }

    bool CookieValid(const Endpoint& from, u64 salt, u64 cookie) const
    {
        const u64 epoch = Epoch();
        return cookie == Cookie(from, salt, epoch) || (epoch > 0 && cookie == Cookie(from, salt, epoch - 1));
    }

    void SendRequest(UdpConn* c)
    {
        Vector<byte> p;
        p.Push(static_cast<byte>(udp::FirstByte(udp::PacketKind::ConnectRequest)));
        PutU64(&p, c->client_salt);
        PadTo(&p, udp::kHandshakeBytes);
        Emit(c->peer, p);
    }

    void SendResponse(UdpConn* c)
    {
        Vector<byte> p;
        p.Push(static_cast<byte>(udp::FirstByte(udp::PacketKind::ChallengeResponse)));
        PutU64(&p, c->client_salt);
        PutU64(&p, c->cookie);
        PadTo(&p, udp::kHandshakeBytes);
        Emit(c->peer, p);
    }

    void SendAccepted(UdpConn* c)
    {
        Vector<byte> p;
        p.Push(static_cast<byte>(udp::FirstByte(udp::PacketKind::Accepted)));
        PutU64(&p, c->client_salt);
        PutU16(&p, c->wire_id);
        Emit(c->peer, p);
        c->last_sent = Now();
    }

    void SendDenied(const Endpoint& to, u64 salt, udp::DenyReason reason)
    {
        Vector<byte> p;
        p.Push(static_cast<byte>(udp::FirstByte(udp::PacketKind::Denied)));
        PutU64(&p, salt);
        p.Push(static_cast<byte>(reason));
        ++counters.denied_sent;
        Emit(to, p);
    }

    // ---- frames: queueing, packing, acknowledgement ----

    static UdpOutFrame* Resolve(UdpConn* c, u64 handle)
    {
        const u32 slot = static_cast<u32>(handle & 0xffffffffu);
        const u32 gen = static_cast<u32>(handle >> 32);
        if (slot >= c->frames.Size() || !c->frames[slot].live || c->frames[slot].gen != gen)
        {
            return nullptr;
        }
        return &c->frames[slot];
    }

    static bool IsReliableKind(u8 rel) { return rel >= static_cast<u8>(Reliability::ReliableOrdered); }

    u64 NewFrame(UdpConn* c, u8 channel, u8 rel, u16 seq, u16 frag_index, u16 frag_count, u32 total, ByteSpan data)
    {
        u32 slot = 0;
        if (!c->free_slots.Empty())
        {
            slot = c->free_slots[c->free_slots.Size() - 1];
            c->free_slots.Pop();
        }
        else
        {
            slot = static_cast<u32>(c->frames.Size());
            c->frames.Push(UdpOutFrame());
        }
        UdpOutFrame& f = c->frames[slot];
        const u32 gen = f.gen + 1;
        f = UdpOutFrame();
        f.gen = gen;
        f.live = true;
        f.channel = channel;
        f.rel = rel;
        f.seq = seq;
        f.frag_index = frag_index;
        f.frag_count = frag_count;
        f.total = total;
        f.data.TryResizeUninitialized(data.Size());
        MemCopy(f.data.Data(), data.Data(), data.Size());
        ++c->queued_frames;
        c->queued_bytes += data.Size();
        return (static_cast<u64>(gen) << 32) | slot;
    }

    void FreeFrame(UdpConn* c, u64 handle)
    {
        UdpOutFrame* f = Resolve(c, handle);
        if (f == nullptr)
        {
            return;
        }
        if (f->sent && IsReliableKind(f->rel))
        {
            --c->inflight_frames;
        }
        --c->queued_frames;
        c->queued_bytes -= f->data.Size();
        f->live = false;
        f->data = Vector<byte>();
        c->free_slots.Push(static_cast<u32>(handle & 0xffffffffu));
    }

    // Splits a message into frames and queues them on its channel.
    Status QueueMessage(UdpConn* c, u8 channel, u8 rel, ByteSpan bytes)
    {
        const usize count = bytes.Size() <= kUdpSingleMax ? 1
                                                          : (bytes.Size() + kUdpFragmentBytes - 1) / kUdpFragmentBytes;
        if (c->queued_frames + count > config.max_queued_frames)
        {
            return Status::Of(ErrorCode::LimitExceeded);
        }
        UdpChannelOut& out = c->out[channel];
        const u16 seq = out.next_seq[rel]++;
        if (count == 1)
        {
            out.queue.Push(NewFrame(c, channel, rel, seq, 0, 0, 0, bytes));
            return Status::Success();
        }
        for (usize i = 0; i < count; ++i)
        {
            const usize at = i * kUdpFragmentBytes;
            const usize len = bytes.Size() - at < kUdpFragmentBytes ? bytes.Size() - at : kUdpFragmentBytes;
            out.queue.Push(NewFrame(c, channel, rel, seq, static_cast<u16>(i), static_cast<u16>(count),
                                    static_cast<u32>(bytes.Size()), ByteSpan(bytes.Data() + at, len)));
        }
        return Status::Success();
    }

    static usize EncodedSize(const UdpOutFrame& f)
    {
        return kUdpDataHeaderBytes + (f.frag_count > 1 ? kUdpFragmentExtraBytes : 0) + f.data.Size();
    }

    static void AppendFrame(const UdpOutFrame& f, Vector<byte>* p)
    {
        p->Push(static_cast<byte>(kUdpFrameData));
        p->Push(static_cast<byte>(f.channel));
        p->Push(static_cast<byte>(f.rel | (f.frag_count > 1 ? kUdpFragmentFlag : 0)));
        PutU16(p, f.seq);
        PutU16(p, static_cast<u16>(f.data.Size()));
        if (f.frag_count > 1)
        {
            PutU16(p, f.frag_index);
            PutU16(p, f.frag_count);
            PutU32(p, f.total);
        }
        for (usize i = 0; i < f.data.Size(); ++i)
        {
            p->Push(f.data[i]);
        }
    }

    u64 Rto(const UdpConn* c) const
    {
        if (!c->has_rtt)
        {
            return kUdpInitialRto;
        }
        const u64 nanos = static_cast<u64>((c->srtt + 4.0 * c->rttvar) * 1e9);
        return nanos < kUdpMinRto ? kUdpMinRto : (nanos > kUdpMaxRto ? kUdpMaxRto : nanos);
    }

    void RttSample(UdpConn* c, f64 seconds)
    {
        if (!c->has_rtt)
        {
            c->has_rtt = true;
            c->srtt = seconds;
            c->rttvar = seconds / 2;
        }
        else
        {
            const f64 diff = c->srtt > seconds ? c->srtt - seconds : seconds - c->srtt;
            c->rttvar = 0.75 * c->rttvar + 0.25 * diff;
            c->srtt = 0.875 * c->srtt + 0.125 * seconds;
        }
        c->stats.rtt_seconds = c->srtt;
    }

    void QueueResend(UdpConn* c, u64 handle, u16 from_packet)
    {
        UdpOutFrame* f = Resolve(c, handle);
        if (f != nullptr && f->packet_seq == from_packet && !f->resend_queued)
        {
            f->resend_queued = true;
            c->resend.Push(handle);
        }
    }

    // Builds one packet from what is waiting and sends it; an empty one only if `force`. False if nothing was sent.
    bool SendPacket(UdpConn* c, bool force)
    {
        Vector<byte> p;
        udp::PayloadHeader h;
        h.connection = c->wire_id;
        h.sequence = c->send_seq;
        h.has_ack = c->recv.Any();
        h.ack = c->recv.Latest();
        h.ack_bits = c->recv.Bits();
        udp::WritePayloadHeader(h, &p);
        Vector<u64> carried;
        usize new_reliable = 0;
        bool full = false;
        while (!full && c->resend_head < c->resend.Size())
        {
            const u64 handle = c->resend[c->resend_head];
            UdpOutFrame* f = Resolve(c, handle);
            if (f == nullptr || !f->resend_queued)
            {
                ++c->resend_head;
                continue;
            }
            if (p.Size() + EncodedSize(*f) > udp::kMtu)
            {
                full = true;
                break;
            }
            f->resend_queued = false;
            AppendFrame(*f, &p);
            carried.Push(handle);
            ++c->resend_head;
        }
        if (c->resend_head > 64 && c->resend_head * 2 > c->resend.Size())
        {
            c->resend.RemoveRange(0, c->resend_head);
            c->resend_head = 0;
        }
        for (usize ch = 0; ch < c->out.Size() && !full; ++ch)
        {
            UdpChannelOut& out = c->out[ch];
            while (out.head < out.queue.Size())
            {
                const u64 handle = out.queue[out.head];
                UdpOutFrame* f = Resolve(c, handle);
                if (f == nullptr)
                {
                    ++out.head;
                    continue;
                }
                if (IsReliableKind(f->rel) && c->inflight_frames + new_reliable >= config.window_frames)
                {
                    break;  // the window is full: this channel waits for acknowledgements
                }
                if (p.Size() + EncodedSize(*f) > udp::kMtu)
                {
                    full = true;
                    break;
                }
                AppendFrame(*f, &p);
                carried.Push(handle);
                new_reliable += IsReliableKind(f->rel) ? 1 : 0;
                ++out.head;
            }
            if (out.head > 64 && out.head * 2 > out.queue.Size())
            {
                out.queue.RemoveRange(0, out.head);
                out.head = 0;
            }
        }
        if (carried.Empty() && !force)
        {
            return false;
        }
        const u64 now = Now();
        const u16 seq = c->send_seq;
        UdpSentPacket& entry = c->ring[seq % kUdpRingSize];
        if (entry.used && !entry.acked && !entry.resolved)
        {
            for (usize i = 0; i < entry.frames.Size(); ++i)
            {
                QueueResend(c, entry.frames[i], entry.seq);  // its record is about to be overwritten: treat it as lost
            }
        }
        entry.seq = seq;
        entry.used = true;
        entry.acked = false;
        entry.resolved = false;
        entry.time = now;
        entry.frames.Clear();
        for (usize i = 0; i < carried.Size(); ++i)
        {
            UdpOutFrame* f = Resolve(c, carried[i]);
            if (f == nullptr)
            {
                continue;
            }
            if (IsReliableKind(f->rel))
            {
                f->packet_seq = seq;
                f->sent_time = now;
                ++f->tries;
                if (!f->sent)
                {
                    f->sent = true;
                    ++c->inflight_frames;
                }
                else
                {
                    ++c->info.frames_resent;
                }
                entry.frames.Push(carried[i]);
            }
            else
            {
                FreeFrame(c, carried[i]);  // unreliable: sent once and forgotten
            }
        }
        ++c->send_seq;
        c->info.next_sequence = c->send_seq;
        c->last_sent = now;
        c->ack_pending = false;
        c->unacked_packets = 0;
        ++c->stats.packets_sent;
        c->stats.loss = c->stats.packets_sent == 0
                            ? 0
                            : static_cast<f64>(c->info.frames_resent) / static_cast<f64>(c->stats.packets_sent);
        Emit(c->peer, p);
        return true;
    }

    void Flush(UdpConn* c)
    {
        for (usize n = 0; n < kUdpPacketsPerFlush; ++n)
        {
            if (!SendPacket(c, false))
            {
                break;
            }
        }
    }

    // How old an unacknowledged packet must be before three newer acknowledged ones make it lost: a network that
    // reorders deeply would otherwise get every overtaken packet sent again.
    u64 LossDelay(const UdpConn* c) const { return static_cast<u64>((c->srtt + 2.0 * c->rttvar) * 1e9); }

    void OnAckFields(UdpConn* c, u16 ack, u32 bits)
    {
        const u64 now = Now();
        if (!udp::SeqNewer(c->send_seq, ack))
        {
            return;  // acknowledges a packet we have not sent
        }
        for (u32 i = 0; i <= 32; ++i)
        {
            if (i > 0 && ((bits >> (i - 1)) & 1u) == 0)
            {
                continue;
            }
            const u16 seq = static_cast<u16>(ack - i);
            UdpSentPacket& e = c->ring[seq % kUdpRingSize];
            if (!e.used || e.seq != seq || e.acked)
            {
                continue;
            }
            e.acked = true;
            ++c->info.packets_acked;
            if (i == 0)
            {
                RttSample(c,
                          static_cast<f64>(now - e.time) / 1e9);  // only the newest: older ones were acknowledged late
            }
            for (usize k = 0; k < e.frames.Size(); ++k)
            {
                FreeFrame(c, e.frames[k]);
            }
            e.frames.Clear();
        }
        // Packets three or more behind an acknowledged one that were not themselves acknowledged are lost.
        if (udp::SeqDistance(c->send_seq, c->tail_seq) > kUdpRingSize)
        {
            c->tail_seq = static_cast<u16>(c->send_seq - kUdpRingSize);
        }
        while (udp::SeqNewer(ack, c->tail_seq) && udp::SeqDistance(ack, c->tail_seq) >= kUdpLossThreshold)
        {
            UdpSentPacket& e = c->ring[c->tail_seq % kUdpRingSize];
            if (e.used && e.seq == c->tail_seq && !e.acked && !e.resolved)
            {
                if (c->has_rtt && now - e.time < LossDelay(c))
                {
                    break;  // newer packets overtook it, but it has not had the time a late one would need
                }
                e.resolved = true;
                for (usize k = 0; k < e.frames.Size(); ++k)
                {
                    QueueResend(c, e.frames[k], e.seq);
                }
            }
            ++c->tail_seq;
        }
    }

    // Sends again the frames whose time-out has passed.
    void CheckTimeouts(UdpConn* c, u64 now)
    {
        const u64 base = Rto(c);
        if (udp::SeqDistance(c->send_seq, c->tail_seq) > kUdpRingSize)
        {
            c->tail_seq = static_cast<u16>(c->send_seq - kUdpRingSize);
        }
        for (u16 seq = c->tail_seq; seq != c->send_seq; ++seq)
        {
            UdpSentPacket& e = c->ring[seq % kUdpRingSize];
            if (!e.used || e.seq != seq || e.acked || e.resolved)
            {
                continue;
            }
            for (usize k = 0; k < e.frames.Size(); ++k)
            {
                UdpOutFrame* f = Resolve(c, e.frames[k]);
                if (f == nullptr || f->packet_seq != seq || f->resend_queued)
                {
                    continue;
                }
                u64 rto = base << (f->tries > 6 ? 5 : f->tries - 1);
                rto = rto > kUdpMaxRto ? kUdpMaxRto : rto;
                if (now - f->sent_time >= rto)
                {
                    f->resend_queued = true;
                    c->resend.Push(e.frames[k]);
                }
            }
        }
    }

    // ---- events ----

    void Queue(UdpEventRec&& rec) { queue.Push(Move(rec)); }

    void PurgeMessages(ConnectionId id)
    {
        usize out = queue_head;
        for (usize i = queue_head; i < queue.Size(); ++i)
        {
            if (queue[i].kind == EventKind::Message && queue[i].connection == id)
            {
                continue;
            }
            if (out != i)
            {
                queue[out] = Move(queue[i]);
            }
            ++out;
        }
        queue.Resize(out);
    }

    void End(UdpConn* c, DisconnectReason reason, u32 code)
    {
        if (c->state == UdpState::Closed)
        {
            return;
        }
        c->state = UdpState::Closed;
        c->info.connected = false;
        UdpEventRec rec;
        rec.kind = EventKind::Closed;
        rec.connection = c->id;
        rec.reason = reason;
        rec.code = code;
        Queue(Move(rec));
    }

    void Open(UdpConn* c)
    {
        c->state = UdpState::Connected;
        c->info.connected = true;
        c->last_heard = Now();
        UdpEventRec rec;
        rec.kind = EventKind::Opened;
        rec.connection = c->id;
        rec.incoming = c->incoming;
        rec.peer = c->peer;
        Queue(Move(rec));
    }

    // ---- receiving ----

    UdpConn* FindPayloadTarget(const Endpoint& from, u16 wire_id) const
    {
        UdpConn* const* found = by_wire.Find(wire_id);
        if (found != nullptr && (*found)->peer == from)
        {
            return *found;
        }
        for (usize i = 0; i < conns.Size(); ++i)
        {
            UdpConn* c = conns[i].Get();
            if (!c->incoming && c->state == UdpState::Connected && c->wire_id == wire_id && c->peer == from)
            {
                return c;
            }
        }
        return nullptr;
    }

    void OnRequest(const Endpoint& from, ByteSpan packet)
    {
        if (packet.Size() < udp::kHandshakeBytes)
        {
            ++counters.dropped_short;  // not full-size: never answered, so no amplification
            return;
        }
        const u64 salt = ReadU64(packet, 1);
        if (!listening)
        {
            SendDenied(from, salt, udp::DenyReason::NotListening);
            return;
        }
        Vector<byte> p;
        p.Push(static_cast<byte>(udp::FirstByte(udp::PacketKind::Challenge)));
        PutU64(&p, salt);
        PutU64(&p, Cookie(from, salt, Epoch()));
        ++counters.challenges_sent;
        Emit(from, p);
    }

    void OnResponse(const Endpoint& from, ByteSpan packet)
    {
        if (packet.Size() < udp::kHandshakeBytes)
        {
            ++counters.dropped_short;
            return;
        }
        const u64 salt = ReadU64(packet, 1);
        const u64 cookie = ReadU64(packet, 9);
        if (!listening || !CookieValid(from, salt, cookie))
        {
            ++counters.dropped_bad_cookie;
            return;
        }
        usize from_address = 0;
        for (usize i = 0; i < conns.Size(); ++i)
        {
            UdpConn* c = conns[i].Get();
            if (!c->incoming)
            {
                continue;
            }
            if (c->peer == from && c->client_salt == salt)
            {
                if (c->state == UdpState::Connected)
                {
                    SendAccepted(c);  // our answer was lost: say it again
                }
                return;
            }
            from_address += c->state != UdpState::Closed && c->peer.GetAddress() == from.GetAddress() ? 1 : 0;
        }
        PurgeRetired();
        if (retired_keys.Find(HandshakeKey(from, salt)) != nullptr)
        {
            ++counters.dropped_stale;  // a replay of a handshake whose connection already ended
            return;
        }
        // Fail closed: an entry whose cookie can still be presented is never forgotten to make room. One address (or
        // IPv6 /64) that churns through handshakes is denied alone; a full table denies new handshakes until the
        // oldest cookies expire.
        const u32* here = retired_per_group.Find(GroupOf(from.GetAddress()));
        const u32* wider = retired_per_prefix.Find(PrefixOf(from.GetAddress()));
        if ((here != nullptr && *here >= kUdpRetiredPerAddress) || (wider != nullptr && *wider >= kUdpRetiredPerPrefix))
        {
            SendDenied(from, salt, udp::DenyReason::AddressLimit);
            return;
        }
        if (retired_count >= kUdpRetiredMemory)
        {
            SendDenied(from, salt, udp::DenyReason::ServerFull);
            return;
        }
        if (conns.Size() >= config.max_connections)
        {
            SendDenied(from, salt, udp::DenyReason::ServerFull);
            return;
        }
        if (from_address >= config.max_connections_per_address)
        {
            SendDenied(from, salt, udp::DenyReason::AddressLimit);
            return;
        }
        UdpConn* c = Create(from, true);
        c->client_salt = salt;
        c->cookie = cookie;
        c->epoch = Epoch();
        for (u32 tries = 0; tries < 65536; ++tries)
        {
            if (next_wire == 0)
            {
                next_wire = 1;
            }
            if (by_wire.Find(next_wire) == nullptr)
            {
                break;
            }
            ++next_wire;
        }
        c->wire_id = next_wire++;
        by_wire.Insert(c->wire_id, c);
        c->last_try = Now();
        c->retry_nanos = config.handshake_retry_nanos;
        SendAccepted(c);
        Open(c);
    }

    void OnChallenge(const Endpoint& from, ByteSpan packet)
    {
        if (packet.Size() < 17)
        {
            ++counters.dropped_short;
            return;
        }
        const u64 salt = ReadU64(packet, 1);
        for (usize i = 0; i < conns.Size(); ++i)
        {
            UdpConn* c = conns[i].Get();
            if (!c->incoming && c->state == UdpState::Connecting && c->peer == from && c->client_salt == salt)
            {
                c->cookie = ReadU64(packet, 9);
                if (c->phase != Phase::Response)
                {
                    c->phase_started = Now();
                }
                c->phase = Phase::Response;
                c->retry_nanos = config.handshake_retry_nanos;
                c->last_try = Now();
                SendResponse(c);
                return;
            }
        }
        ++counters.dropped_stale;
    }

    void OnAccepted(const Endpoint& from, ByteSpan packet)
    {
        if (packet.Size() < 11)
        {
            ++counters.dropped_short;
            return;
        }
        const u64 salt = ReadU64(packet, 1);
        for (usize i = 0; i < conns.Size(); ++i)
        {
            UdpConn* c = conns[i].Get();
            if (!c->incoming && c->state == UdpState::Connecting && c->phase == Phase::Response && c->peer == from &&
                c->client_salt == salt)
            {
                c->wire_id = ReadU16(packet, 9);
                Open(c);
                return;
            }
        }
        ++counters.dropped_stale;
    }

    void OnDenied(const Endpoint& from, ByteSpan packet)
    {
        if (packet.Size() < 10)
        {
            ++counters.dropped_short;
            return;
        }
        const u64 salt = ReadU64(packet, 1);
        for (usize i = 0; i < conns.Size(); ++i)
        {
            UdpConn* c = conns[i].Get();
            if (!c->incoming && c->state == UdpState::Connecting && c->peer == from && c->client_salt == salt)
            {
                End(c, DisconnectReason::Refused, 0);
                return;
            }
        }
    }

    void OnPayload(const Endpoint& from, ByteSpan packet)
    {
        udp::PayloadHeader h;
        ByteSpan body;
        if (!udp::ReadPayloadHeader(packet, &h, &body))
        {
            ++counters.dropped_short;
            return;
        }
        UdpConn* c = FindPayloadTarget(from, h.connection);
        if (c == nullptr)
        {
            for (usize i = 0; i < lingering.Size(); ++i)
            {
                if (lingering[i].wire_id == h.connection && lingering[i].peer == from)
                {
                    return;  // we are telling this peer it is closed: a Reset would race the Disconnect
                }
            }
            if (listening)
            {
                Vector<byte> p;  // tell a client whose connection we lost, instead of leaving it to time out
                p.Push(static_cast<byte>(udp::FirstByte(udp::PacketKind::Reset)));
                PutU16(&p, h.connection);
                ++counters.resets_sent;
                Emit(from, p);
            }
            return;
        }
        if (c->state != UdpState::Connected)
        {
            return;
        }
        const udp::SeqVerdict verdict = c->recv.OnReceive(h.sequence);
        if (verdict == udp::SeqVerdict::Duplicate)
        {
            ++c->info.duplicates;
            ++counters.dropped_duplicate;
            return;
        }
        if (verdict == udp::SeqVerdict::TooOld)
        {
            ++c->info.too_old;
            ++counters.dropped_stale;
            return;
        }
        c->info.any_received = true;
        c->info.latest_received = c->recv.Latest();
        c->last_heard = Now();
        ++c->stats.packets_received;
        if (h.has_ack)
        {
            OnAckFields(c, h.ack, h.ack_bits);
        }
        bool had_frame = false;
        if (!ProcessFrames(c, body, &had_frame))
        {
            Abort(c, DisconnectReason::ProtocolError);  // the peer broke a bound it must keep
            return;
        }
        c->ack_pending = c->ack_pending ||
                         had_frame;  // an empty packet is never answered, or two peers would ping-pong
        if (had_frame && ++c->unacked_packets >= kUdpAckEvery)
        {
            // A burst can hold more packets than one acknowledgement covers (the latest and the 32 before it): answer
            // every few packets, or the sender would take the uncovered ones for lost.
            SendPacket(c, true);
        }
    }

    // Ends a connection for a reason of ours and tells the peer (code 0), as Close does.
    void Abort(UdpConn* c, DisconnectReason reason)
    {
        if (c->state == UdpState::Connected)
        {
            UdpLingering l;
            l.peer = c->peer;
            l.wire_id = c->wire_id;
            l.code = 0;
            l.sends_left = config.close_repeats;
            l.next_send = Now();
            lingering.Push(l);
        }
        End(c, reason, 0);
    }

    void Deliver(UdpConn* c, u8 channel, u8 rel, ByteSpan data)
    {
        UdpEventRec rec;
        rec.kind = EventKind::Message;
        rec.connection = c->id;
        rec.channel = channel;
        rec.reliability = static_cast<Reliability>(rel);
        rec.data.TryResizeUninitialized(data.Size());
        MemCopy(rec.data.Data(), data.Data(), data.Size());
        ++c->stats.messages_received;
        c->stats.bytes_received += data.Size();
        Queue(Move(rec));
    }

    // Reads the frames of one payload; false if the peer broke a bound it must keep.
    bool ProcessFrames(UdpConn* c, ByteSpan body, bool* had_frame)
    {
        usize at = 0;
        while (at < body.Size())
        {
            if (body.Size() - at < kUdpDataHeaderBytes || static_cast<u8>(body[at]) != kUdpFrameData)
            {
                break;  // a frame we do not know: the rest of the packet cannot be read
            }
            const u8 channel = static_cast<u8>(body[at + 1]);
            const u8 kind = static_cast<u8>(body[at + 2]);
            const u16 seq = ReadU16(body, at + 3);
            const usize length = ReadU16(body, at + 5);
            at += kUdpDataHeaderBytes;
            const bool fragment = (kind & kUdpFragmentFlag) != 0;
            const u8 rel = kind & 3;
            u16 index = 0;
            u16 count = 0;
            u32 total = 0;
            if ((kind & 0x7c) != 0 || channel >= config.channels || length == 0)
            {
                break;
            }
            if (fragment)
            {
                if (body.Size() - at < kUdpFragmentExtraBytes)
                {
                    break;
                }
                index = ReadU16(body, at);
                count = ReadU16(body, at + 2);
                for (u32 i = 0; i < 4; ++i)
                {
                    total |= static_cast<u32>(static_cast<u8>(body[at + 4 + i])) << (8 * i);
                }
                at += kUdpFragmentExtraBytes;
            }
            if (length > body.Size() - at)
            {
                break;
            }
            const ByteSpan payload(body.Data() + at, length);
            at += length;
            *had_frame = true;
            const bool ok = fragment ? OnFragment(c, channel, rel, seq, index, count, total, payload)
                                     : OnMessage(c, channel, rel, seq, payload);
            if (!ok)
            {
                return false;
            }
        }
        return true;
    }

    // A whole message (or a reassembled one) arrived; false on a protocol violation.
    bool OnMessage(UdpConn* c, u8 channel, u8 rel, u16 seq, ByteSpan data)
    {
        if (data.Size() > config.max_message_bytes)
        {
            return !IsReliableKind(rel);
        }
        UdpRecvSpace& rs = c->in[channel].space[rel];
        switch (static_cast<Reliability>(rel))
        {
        case Reliability::Unreliable: Deliver(c, channel, rel, data); return true;
        case Reliability::UnreliableSequenced:
            if (!rs.any || udp::SeqNewer(seq, rs.next))
            {
                rs.any = true;
                rs.next = seq;
                Deliver(c, channel, rel, data);
            }
            return true;
        case Reliability::ReliableUnordered:
        {
            const u16 d = udp::SeqDistance(seq, rs.next);
            if (d >= 0x8000)
            {
                return true;  // delivered before
            }
            if (d > kUdpLookahead)
            {
                return false;
            }
            for (usize i = 0; i < rs.above.Size(); ++i)
            {
                if (rs.above[i] == seq)
                {
                    return true;
                }
            }
            rs.above.Push(seq);
            Deliver(c, channel, rel, data);
            for (bool again = true; again;)
            {
                again = false;
                for (usize i = 0; i < rs.above.Size(); ++i)
                {
                    if (rs.above[i] == rs.next)
                    {
                        rs.above.Remove(i);
                        ++rs.next;
                        again = true;
                        break;
                    }
                }
            }
            return true;
        }
        case Reliability::ReliableOrdered:
        {
            const u16 d = udp::SeqDistance(seq, rs.next);
            if (d >= 0x8000)
            {
                return true;  // delivered before
            }
            if (d > kUdpLookahead)
            {
                return false;  // a gap beyond the reorder buffer
            }
            if (d != 0)
            {
                for (usize i = 0; i < rs.buffered.Size(); ++i)
                {
                    if (rs.buffered[i].seq == seq)
                    {
                        return true;
                    }
                }
                if (c->receive_bytes + data.Size() > config.max_receive_buffer_bytes)
                {
                    return false;
                }
                UdpBuffered b;
                b.seq = seq;
                b.data.TryResizeUninitialized(data.Size());
                MemCopy(b.data.Data(), data.Data(), data.Size());
                c->receive_bytes += data.Size();
                rs.buffered.Push(Move(b));
                return true;
            }
            Deliver(c, channel, rel, data);
            ++rs.next;
            for (bool again = true; again;)
            {
                again = false;
                for (usize i = 0; i < rs.buffered.Size(); ++i)
                {
                    if (rs.buffered[i].seq == rs.next)
                    {
                        Deliver(c, channel, rel, ByteSpan(rs.buffered[i].data.Data(), rs.buffered[i].data.Size()));
                        c->receive_bytes -= rs.buffered[i].data.Size();
                        rs.buffered.Remove(i);
                        ++rs.next;
                        again = true;
                        break;
                    }
                }
            }
            return true;
        }
        }
        return true;
    }

    // True if a reliable message of this sequence number is already complete (delivered, or held for its turn).
    static bool AlreadyHave(const UdpRecvSpace& rs, u8 rel, u16 seq)
    {
        if (udp::SeqDistance(seq, rs.next) >= 0x8000)
        {
            return true;
        }
        if (rel == static_cast<u8>(Reliability::ReliableUnordered))
        {
            for (usize i = 0; i < rs.above.Size(); ++i)
            {
                if (rs.above[i] == seq)
                {
                    return true;
                }
            }
            return false;
        }
        for (usize i = 0; i < rs.buffered.Size(); ++i)
        {
            if (rs.buffered[i].seq == seq)
            {
                return true;
            }
        }
        return false;
    }

    // Removes reliable reassemblies of messages that were completed meanwhile (a duplicate fragment can start one
    // after its message was delivered, and no further fragment will come to finish it).
    void DropObsoleteReassemblies(UdpConn* c)
    {
        for (usize i = c->reassemblies.Size(); i > 0; --i)
        {
            const UdpReassembly& r = c->reassemblies[i - 1];
            if (IsReliableKind(r.rel) && AlreadyHave(c->in[r.channel].space[r.rel], r.rel, r.seq))
            {
                c->receive_bytes -= r.total;
                c->reassemblies.Remove(i - 1);
            }
        }
    }

    // A fragment arrived. Everything it declares is checked against what the message must look like before any memory
    // is committed: the size, the count, and the exact length of this fragment (so fragments cannot overlap).
    bool OnFragment(UdpConn* c, u8 channel, u8 rel, u16 seq, u16 index, u16 count, u32 total, ByteSpan payload)
    {
        const bool reliable = IsReliableKind(rel);
        const usize max_count = (config.max_message_bytes + kUdpFragmentBytes - 1) / kUdpFragmentBytes;
        if (count < 2 || count > max_count || index >= count || total > config.max_message_bytes ||
            total <= static_cast<usize>(count - 1) * kUdpFragmentBytes ||
            total > static_cast<usize>(count) * kUdpFragmentBytes)
        {
            return !reliable;
        }
        const usize expected = index + 1 < count ? kUdpFragmentBytes
                                                 : total - static_cast<usize>(count - 1) * kUdpFragmentBytes;
        if (payload.Size() != expected)
        {
            return !reliable;
        }
        UdpRecvSpace& rs = c->in[channel].space[rel];
        if (reliable)
        {
            const u16 d = udp::SeqDistance(seq, rs.next);
            if (d >= 0x8000)
            {
                return true;  // a fragment of a message already delivered
            }
            if (d > kUdpLookahead)
            {
                return false;
            }
            if (AlreadyHave(rs, rel, seq))
            {
                return true;  // a fragment of a message that is complete and waiting its turn
            }
        }
        usize found = c->reassemblies.Size();
        for (usize i = 0; i < c->reassemblies.Size(); ++i)
        {
            const UdpReassembly& r = c->reassemblies[i];
            if (r.channel == channel && r.rel == rel && r.seq == seq)
            {
                found = i;
                break;
            }
        }
        if (found == c->reassemblies.Size())
        {
            if (c->reassemblies.Size() >= config.max_reassemblies ||
                c->receive_bytes + total > config.max_receive_buffer_bytes)
            {
                DropObsoleteReassemblies(c);  // duplicates of delivered messages leave partial ones behind
            }
            if (c->reassemblies.Size() >= config.max_reassemblies ||
                c->receive_bytes + total > config.max_receive_buffer_bytes)
            {
                return !reliable;
            }
            UdpReassembly r;
            r.channel = channel;
            r.rel = rel;
            r.seq = seq;
            r.total = total;
            r.count = count;
            r.started = Now();
            r.have.Resize(count);
            r.data.Resize(total);
            c->receive_bytes += total;
            c->reassemblies.Push(Move(r));
        }
        UdpReassembly& r = c->reassemblies[found];
        if (r.total != total || r.count != count)
        {
            return !reliable;  // inconsistent with the first fragment
        }
        if (r.have[index] != 0)
        {
            return true;
        }
        r.have[index] = 1;
        MemCopy(r.data.Data() + static_cast<usize>(index) * kUdpFragmentBytes, payload.Data(), payload.Size());
        if (++r.got < r.count)
        {
            return true;
        }
        Vector<byte> whole = Move(r.data);
        c->receive_bytes -= total;
        c->reassemblies.Remove(found);
        return OnMessage(c, channel, rel, seq, ByteSpan(whole.Data(), whole.Size()));
    }

    void OnDisconnect(const Endpoint& from, ByteSpan packet)
    {
        if (packet.Size() < 7)
        {
            ++counters.dropped_short;
            return;
        }
        UdpConn* c = FindPayloadTarget(from, ReadU16(packet, 1));
        if (c != nullptr && c->state == UdpState::Connected)
        {
            u32 code = 0;
            for (u32 i = 0; i < 4; ++i)
            {
                code |= static_cast<u32>(static_cast<u8>(packet[3 + i])) << (8 * i);
            }
            End(c, DisconnectReason::PeerClosed, code);
        }
    }

    void OnReset(const Endpoint& from, ByteSpan packet)
    {
        if (packet.Size() < 3)
        {
            ++counters.dropped_short;
            return;
        }
        UdpConn* c = FindPayloadTarget(from, ReadU16(packet, 1));
        if (c != nullptr && !c->incoming && c->state == UdpState::Connected)
        {
            End(c, DisconnectReason::Reset, 0);
        }
    }

    void Receive(const Endpoint& from, ByteSpan packet)
    {
        ++counters.datagrams_received;
        if (packet.Size() > udp::kMtu)
        {
            ++counters.dropped_oversize;
            return;
        }
        udp::PacketKind kind = udp::PacketKind::Payload;
        if (packet.Size() == 0 || !udp::ParseFirstByte(static_cast<u8>(packet[0]), &kind))
        {
            ++counters.dropped_unknown;
            return;
        }
        switch (kind)
        {
        case udp::PacketKind::ConnectRequest: OnRequest(from, packet); break;
        case udp::PacketKind::Challenge: OnChallenge(from, packet); break;
        case udp::PacketKind::ChallengeResponse: OnResponse(from, packet); break;
        case udp::PacketKind::Accepted: OnAccepted(from, packet); break;
        case udp::PacketKind::Denied: OnDenied(from, packet); break;
        case udp::PacketKind::Payload: OnPayload(from, packet); break;
        case udp::PacketKind::Disconnect: OnDisconnect(from, packet); break;
        case udp::PacketKind::Reset: OnReset(from, packet); break;
        }
    }

    // ---- time ----

    void Tick()
    {
        const u64 now = Now();
        for (usize i = 0; i < conns.Size(); ++i)
        {
            UdpConn* c = conns[i].Get();
            if (c->state == UdpState::Connecting)
            {
                if (now - c->created >= config.connect_timeout_nanos)
                {
                    End(c, DisconnectReason::TimedOut, 0);
                }
                else if (now - c->last_try >= c->retry_nanos)
                {
                    c->last_try = now;
                    c->retry_nanos = c->retry_nanos * 2 > kMaxHandshakeRetry ? kMaxHandshakeRetry : c->retry_nanos * 2;
                    if (c->phase == Phase::Response && now - c->phase_started >= config.cookie_period_nanos)
                    {
                        c->phase = Phase::Request;  // the cookie may have expired: ask for a new one
                    }
                    if (c->phase == Phase::Request)
                    {
                        SendRequest(c);
                    }
                    else
                    {
                        SendResponse(c);
                    }
                }
            }
            else if (c->state == UdpState::Connected)
            {
                if (now - c->last_heard >= config.timeout_nanos)
                {
                    End(c, DisconnectReason::TimedOut, 0);
                }
                else
                {
                    if (c->incoming && !c->info.any_received && now - c->last_try >= c->retry_nanos)
                    {
                        // The client has not spoken since the handshake: our Accepted may have been lost, and its
                        // repeated answers may be lost as well, so say it again on our own, with a growing delay.
                        c->last_try = now;
                        c->retry_nanos = c->retry_nanos * 2 > kMaxHandshakeRetry ? kMaxHandshakeRetry
                                                                                 : c->retry_nanos * 2;
                        SendAccepted(c);
                    }
                    CheckTimeouts(c, now);
                    Flush(c);
                    if ((c->ack_pending && now - c->last_sent >= config.ack_delay_nanos) ||
                        now - c->last_sent >= config.keepalive_nanos)
                    {
                        SendPacket(c, true);
                    }
                    for (usize k = 0; k < c->reassemblies.Size();)
                    {
                        const UdpReassembly& r = c->reassemblies[k];
                        if (!IsReliableKind(r.rel) && now - r.started >= config.reassembly_timeout_nanos)
                        {
                            c->receive_bytes -= r.total;  // an unreliable message missing a fragment is never completed
                            c->reassemblies.Remove(k);
                        }
                        else
                        {
                            ++k;
                        }
                    }
                }
            }
        }
        for (usize i = 0; i < lingering.Size();)
        {
            UdpLingering& l = lingering[i];
            if (now >= l.next_send)
            {
                Vector<byte> p;
                p.Push(static_cast<byte>(udp::FirstByte(udp::PacketKind::Disconnect)));
                PutU16(&p, l.wire_id);
                PutU32(&p, l.code);
                Emit(l.peer, p);
                l.next_send = now + config.close_interval_nanos;
                --l.sends_left;
            }
            if (l.sends_left == 0)
            {
                lingering.Remove(i);
            }
            else
            {
                ++i;
            }
        }
    }

    void Service()
    {
        for (usize n = 0; n < kReceiveBatch; ++n)
        {
            usize received = 0;
            Endpoint from;
            const Status s = port->ReceiveFrom(MutableByteSpan(buffer.Data(), buffer.Size()), &received, &from);
            if (s.Is(ErrorCode::WouldBlock))
            {
                break;
            }
            if (s.Is(ErrorCode::MessageTooLarge))
            {
                ++counters.datagrams_received;
                ++counters.dropped_oversize;
                continue;
            }
            if (s.Failed())
            {
                break;
            }
            Receive(from, ByteSpan(buffer.Data(), received));
        }
        Tick();
    }
};

UdpTransport::UdpTransport(DatagramPort& port, const Clock& clock, const UdpTransportConfig& config)
    : config_(ClampedConfig(config)), impl_(MakeUnique<Impl>(port, clock, config_))
{
}

UdpTransport::~UdpTransport() = default;

Status UdpTransport::Listen(const Endpoint& at)
{
    if (!(at == impl_->self) || impl_->listening)
    {
        return Status::Of(ErrorCode::Invalid);
    }
    impl_->listening = true;
    return Status::Success();
}

Status UdpTransport::Connect(const Endpoint& to, ConnectionId* connection)
{
    Impl& m = *impl_;
    if (m.conns.Size() >= m.config.max_connections)
    {
        return Status::Of(ErrorCode::LimitExceeded);
    }
    UdpConn* c = m.Create(to, false);
    c->client_salt = m.Random();
    c->phase = Phase::Request;
    c->retry_nanos = config_.handshake_retry_nanos;
    c->last_try = m.Now();
    m.SendRequest(c);
    *connection = c->id;
    return Status::Success();
}

Status UdpTransport::Send(ConnectionId connection, u8 channel, ByteSpan bytes, Reliability reliability)
{
    Impl& m = *impl_;
    UdpConn* c = m.FindById(connection);
    if (c == nullptr || channel >= config_.channels || bytes.Size() == 0 || static_cast<u8>(reliability) > 3)
    {
        return Status::Of(ErrorCode::Invalid);
    }
    if (c->state == UdpState::Closed)
    {
        return Status::Of(ErrorCode::Closed);
    }
    if (bytes.Size() > MaxMessageBytes())
    {
        return Status::Of(ErrorCode::MessageTooLarge);
    }
    if (c->state != UdpState::Connected)
    {
        return Status::Of(ErrorCode::NotConnected);
    }
    const Status queued = m.QueueMessage(c, channel, static_cast<u8>(reliability), bytes);
    if (queued.Ok())
    {
        ++c->stats.messages_sent;
        c->stats.bytes_sent += bytes.Size();
    }
    return queued;
}

bool UdpTransport::Poll(Event* event)
{
    Impl& m = *impl_;
    if (m.queue_head >= m.queue.Size())
    {
        m.queue.Clear();
        m.queue_head = 0;
        m.Service();  // only when nothing is waiting, so draining many events does not rescan every connection
        if (m.queue_head >= m.queue.Size())
        {
            return false;
        }
    }
    m.current = Move(m.queue[m.queue_head++]);
    event->kind = m.current.kind;
    event->connection = m.current.connection;
    event->incoming = m.current.incoming;
    event->peer = m.current.peer;
    event->channel = m.current.channel;
    event->reliability = m.current.reliability;
    event->message = ByteSpan(m.current.data.Data(), m.current.data.Size());
    event->reason = m.current.reason;
    event->close_code = m.current.code;
    if (m.current.kind == EventKind::Closed)
    {
        UdpConn* c = m.FindById(m.current.connection);
        if (c != nullptr)
        {
            m.Remove(c);
        }
    }
    return true;
}

Status UdpTransport::Close(ConnectionId connection, u32 code)
{
    Impl& m = *impl_;
    UdpConn* c = m.FindById(connection);
    if (c == nullptr)
    {
        return Status::Of(ErrorCode::Invalid);
    }
    if (c->state == UdpState::Closed)
    {
        return Status::Of(ErrorCode::Closed);
    }
    if (c->state == UdpState::Connected)
    {
        UdpLingering l;
        l.peer = c->peer;
        l.wire_id = c->wire_id;
        l.code = code;
        l.sends_left = config_.close_repeats;
        l.next_send = m.Now();
        m.lingering.Push(l);
    }
    m.PurgeMessages(connection);
    m.End(c, DisconnectReason::LocalClose, 0);
    return Status::Success();
}

Status UdpTransport::Statistics(ConnectionId connection, ConnectionStats* stats) const
{
    const UdpConn* c = impl_->FindById(connection);
    if (c == nullptr)
    {
        return Status::Of(ErrorCode::Invalid);
    }
    *stats = c->stats;
    stats->send_queue_messages = c->queued_frames;  // frames: a fragmented message counts once per fragment
    stats->send_queue_bytes = c->queued_bytes;
    return Status::Success();
}

Status UdpTransport::Info(ConnectionId connection, UdpConnectionInfo* info) const
{
    const UdpConn* c = impl_->FindById(connection);
    if (c == nullptr)
    {
        return Status::Of(ErrorCode::Invalid);
    }
    *info = c->info;
    info->buffered_bytes = c->receive_bytes;
    info->reassemblies = c->reassemblies.Size();
    return Status::Success();
}

usize UdpTransport::ConnectionCount() const
{
    return impl_->conns.Size();
}

const UdpCounters& UdpTransport::Counters() const
{
    return impl_->counters;
}

}  // namespace engine_placeholder::net
