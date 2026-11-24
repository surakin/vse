#include "script_media/replay_log.h"

#include "cstr/mem_ops.h"
#include "lang/utility.h"
#include "log/assert.h"

namespace vse::script_media
{

void ReplayLog::Begin(u32 random_seed, u64 start_tick, u64 start_hash)
{
    random_seed_ = random_seed;
    start_tick_ = start_tick;
    start_hash_ = start_hash;
    entries_.Clear();
}

bool ReplayLog::Record(u64 tick, const InputSnapshot& snapshot, u64 hash)
{
    if (entries_.Size() >= kMaxReplayTicks || tick != start_tick_ + entries_.Size())
    {
        return false;
    }
    ReplayEntry entry;
    entry.tick = tick;
    entry.hash = hash;
    bytes::ByteWriter writer;
    snapshot.Serialize(&writer);
    InputSnapshot check;
    bytes::ByteReader back(writer.Bytes());
    if (!check.Deserialize(&back) || !back.AtEnd())
    {
        return false;  // Deserialize would refuse the log: keep it readable
    }
    for (usize i = 0; i < writer.Size(); ++i)
    {
        entry.input.Push(static_cast<u8>(writer.Bytes()[i]));
    }
    entries_.Push(Move(entry));
    return true;
}

const ReplayEntry& ReplayLog::At(usize index) const
{
    ENGINE_ASSERT(index < entries_.Size(), "ReplayLog::At: out of range");
    return entries_[index];
}

bool ReplayLog::Snapshot(usize index, InputSnapshot* out) const
{
    const ReplayEntry& entry = At(index);
    bytes::ByteReader reader(ByteSpan(reinterpret_cast<const byte*>(entry.input.Data()), entry.input.Size()));
    return out->Deserialize(&reader) && reader.AtEnd();
}

void ReplayLog::Serialize(bytes::ByteWriter* out) const
{
    out->WriteU32(kReplayLogVersion);
    out->WriteU32(random_seed_);
    out->WriteU64(start_tick_);
    out->WriteU64(start_hash_);
    out->WriteVarint(entries_.Size());
    for (usize i = 0; i < entries_.Size(); ++i)
    {
        const ReplayEntry& e = entries_[i];
        out->WriteU64(e.tick);
        out->WriteU64(e.hash);
        out->WriteVarint(e.input.Size());
        out->WriteBytes(ByteSpan(reinterpret_cast<const byte*>(e.input.Data()), e.input.Size()));
    }
}

bool ReplayLog::Deserialize(bytes::ByteReader* in)
{
    bytes::ByteReader r = *in;
    u32 version = 0;
    u32 seed = 0;
    u64 start_tick = 0;
    u64 start_hash = 0;
    u64 count = 0;
    if (r.ReadU32(&version) != bytes::ByteError::Ok || version != kReplayLogVersion ||
        r.ReadU32(&seed) != bytes::ByteError::Ok || r.ReadU64(&start_tick) != bytes::ByteError::Ok ||
        r.ReadU64(&start_hash) != bytes::ByteError::Ok ||
        r.ReadLength(kMaxReplayTicks, &count) != bytes::ByteError::Ok || count > r.Remaining())
    {
        return false;
    }
    Vector<ReplayEntry> entries;
    for (u64 i = 0; i < count; ++i)
    {
        ReplayEntry e;
        u64 length = 0;
        ByteSpan data;
        if (r.ReadU64(&e.tick) != bytes::ByteError::Ok || r.ReadU64(&e.hash) != bytes::ByteError::Ok ||
            r.ReadLength(r.Remaining(), &length) != bytes::ByteError::Ok ||
            r.ReadBytes(static_cast<usize>(length), &data) != bytes::ByteError::Ok)
        {
            return false;
        }
        if (e.tick != start_tick + i)
        {
            return false;
        }
        for (usize b = 0; b < data.Size(); ++b)
        {
            e.input.Push(static_cast<u8>(data[b]));
        }
        InputSnapshot check;
        bytes::ByteReader snap(data);
        if (!check.Deserialize(&snap) || !snap.AtEnd())
        {
            return false;
        }
        entries.Push(Move(e));
    }
    random_seed_ = seed;
    start_tick_ = start_tick;
    start_hash_ = start_hash;
    entries_ = Move(entries);
    *in = r;
    return true;
}

bool ReplayLog::Equals(const ReplayLog& other) const
{
    if (random_seed_ != other.random_seed_ || start_tick_ != other.start_tick_ || start_hash_ != other.start_hash_ ||
        entries_.Size() != other.entries_.Size())
    {
        return false;
    }
    for (usize i = 0; i < entries_.Size(); ++i)
    {
        const ReplayEntry& a = entries_[i];
        const ReplayEntry& b = other.entries_[i];
        if (a.tick != b.tick || a.hash != b.hash || a.input.Size() != b.input.Size() ||
            MemCompare(a.input.Data(), b.input.Data(), a.input.Size()) != 0)
        {
            return false;
        }
    }
    return true;
}

}  // namespace vse::script_media
