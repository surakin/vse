#include "net/udp_protocol.h"

namespace engine_placeholder::net::udp
{

namespace
{

constexpr u64 Rotl(u64 x, u32 b)
{
    return (x << b) | (x >> (64 - b));
}

struct SipState
{
    u64 v0, v1, v2, v3;
    void Round()
    {
        v0 += v1;
        v1 = Rotl(v1, 13);
        v1 ^= v0;
        v0 = Rotl(v0, 32);
        v2 += v3;
        v3 = Rotl(v3, 16);
        v3 ^= v2;
        v0 += v3;
        v3 = Rotl(v3, 21);
        v3 ^= v0;
        v2 += v1;
        v1 = Rotl(v1, 17);
        v1 ^= v2;
        v2 = Rotl(v2, 32);
    }
};

}  // namespace

u8 FirstByte(PacketKind kind, bool has_ack)
{
    return static_cast<u8>(static_cast<u8>(kind) | (has_ack ? 0x08 : 0) | (kVersion << 5));
}

bool ParseFirstByte(u8 first, PacketKind* kind, bool* has_ack)
{
    const bool flag = (first & 0x08) != 0;
    if ((first >> 5) != kVersion || (first & 0x10) != 0 || (first & 0x07) > static_cast<u8>(PacketKind::Reset))
    {
        return false;
    }
    const PacketKind k = static_cast<PacketKind>(first & 0x07);
    if (flag && k != PacketKind::Payload)
    {
        return false;
    }
    *kind = k;
    if (has_ack != nullptr)
    {
        *has_ack = flag;
    }
    return true;
}

u64 SipHash24(u64 key0, u64 key1, ByteSpan data)
{
    SipState s{key0 ^ 0x736f6d6570736575ull, key1 ^ 0x646f72616e646f6dull, key0 ^ 0x6c7967656e657261ull,
               key1 ^ 0x7465646279746573ull};
    const usize size = data.Size();
    usize i = 0;
    for (; i + 8 <= size; i += 8)
    {
        u64 m = 0;
        for (u32 b = 0; b < 8; ++b)
        {
            m |= static_cast<u64>(static_cast<u8>(data[i + b])) << (8 * b);
        }
        s.v3 ^= m;
        s.Round();
        s.Round();
        s.v0 ^= m;
    }
    u64 last = static_cast<u64>(size) << 56;
    for (u32 b = 0; i + b < size; ++b)
    {
        last |= static_cast<u64>(static_cast<u8>(data[i + b])) << (8 * b);
    }
    s.v3 ^= last;
    s.Round();
    s.Round();
    s.v0 ^= last;
    s.v2 ^= 0xff;
    s.Round();
    s.Round();
    s.Round();
    s.Round();
    return s.v0 ^ s.v1 ^ s.v2 ^ s.v3;
}

bool SeqNewer(u16 a, u16 b)
{
    return a != b && static_cast<u16>(a - b) < 0x8000;
}

u16 SeqDistance(u16 newer, u16 older)
{
    return static_cast<u16>(newer - older);
}

SeqVerdict ReceiveWindow::OnReceive(u16 seq)
{
    if (!any_)
    {
        any_ = true;
        latest_ = seq;
        bits_ = 0;
        return SeqVerdict::Fresh;
    }
    if (seq == latest_)
    {
        return SeqVerdict::Duplicate;
    }
    if (SeqNewer(seq, latest_))
    {
        const u32 shift = SeqDistance(seq, latest_);
        // The old latest becomes bit shift-1 of the new window; the older bits move up by `shift`.
        bits_ = shift < 32 ? (bits_ << shift) : 0;
        if (shift <= 32)
        {
            bits_ |= 1u << (shift - 1);
        }
        latest_ = seq;
        return SeqVerdict::Fresh;
    }
    const u32 behind = SeqDistance(latest_, seq);  // 1 or more
    if (behind > 32)
    {
        return SeqVerdict::TooOld;
    }
    const u32 mask = 1u << (behind - 1);
    if ((bits_ & mask) != 0)
    {
        return SeqVerdict::Duplicate;
    }
    bits_ |= mask;
    return SeqVerdict::Fresh;
}

void SendLog::OnSent(u16 seq)
{
    Entry& e = entries_[seq % kCapacity];
    e.seq = seq;
    e.used = true;
    e.acked = false;
}

u32 SendLog::OnAck(u16 ack, u32 bits, Vector<u16>* newly)
{
    u32 count = 0;
    for (u32 i = 0; i <= 32; ++i)
    {
        const bool present = i == 0 || ((bits >> (i - 1)) & 1u) != 0;
        if (!present)
        {
            continue;
        }
        const u16 seq = static_cast<u16>(ack - i);
        Entry& e = entries_[seq % kCapacity];
        if (e.used && e.seq == seq && !e.acked)
        {
            e.acked = true;
            ++count;
            if (newly != nullptr)
            {
                newly->Push(seq);
            }
        }
    }
    return count;
}

void WritePayloadHeader(const PayloadHeader& header, Vector<byte>* out)
{
    out->Push(static_cast<byte>(FirstByte(PacketKind::Payload, header.has_ack)));
    const u32 fields[3] = {header.connection, header.sequence, header.ack};
    for (u32 f : fields)
    {
        out->Push(static_cast<byte>(f & 0xff));
        out->Push(static_cast<byte>((f >> 8) & 0xff));
    }
    for (u32 b = 0; b < 4; ++b)
    {
        out->Push(static_cast<byte>((header.ack_bits >> (8 * b)) & 0xff));
    }
}

bool ReadPayloadHeader(ByteSpan packet, PayloadHeader* header, ByteSpan* body)
{
    if (packet.Size() < kPayloadHeaderBytes)
    {
        return false;
    }
    const auto u16_at = [&](usize at)
    { return static_cast<u16>(static_cast<u8>(packet[at]) | (static_cast<u8>(packet[at + 1]) << 8)); };
    PacketKind kind = PacketKind::Payload;
    if (!ParseFirstByte(static_cast<u8>(packet[0]), &kind, &header->has_ack) || kind != PacketKind::Payload)
    {
        return false;
    }
    header->connection = u16_at(1);
    header->sequence = u16_at(3);
    header->ack = u16_at(5);
    u32 bits = 0;
    for (u32 b = 0; b < 4; ++b)
    {
        bits |= static_cast<u32>(static_cast<u8>(packet[7 + b])) << (8 * b);
    }
    header->ack_bits = bits;
    *body = ByteSpan(packet.Data() + kPayloadHeaderBytes, packet.Size() - kPayloadHeaderBytes);
    return true;
}

}  // namespace engine_placeholder::net::udp
