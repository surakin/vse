#include "asset/network_backend.h"

#include "asset/blob.h"
#include "cstr/mem_ops.h"
#include "io/path.h"
#include "settings/setting.h"

namespace engine_placeholder::asset
{

namespace
{

// A request that waits for its answer.
constexpr u32 kHelloRequest = 0xFFFFFFFFu;

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Settings

void RegisterNetworkSettings(settings::Registry* registry)
{
    auto declare =
        [&](settings::SettingDesc desc, const char* env, const char* flag, const char* label, const char* description)
    {
        if (registry->Find(StringView(desc.key)).IsValid())
        {
            return;
        }
        desc.env = env;
        desc.flag = flag;
        desc.label = label;
        desc.description = description;
        desc.group = "assets";
        desc.flags = settings::kAdvanced;
        registry->Register(desc);
    };
    declare(settings::MakeInt("assets.net.timeout_ms", 5000, 10, 600000), "ENGINE_ASSETS_NET_TIMEOUT_MS",
            "assets-net-timeout-ms", "Network backend: request timeout",
            "how long one request to the resource server, a connection or the handshake may take");
    declare(settings::MakeInt("assets.net.max_retries", 3, 0, 100), "ENGINE_ASSETS_NET_MAX_RETRIES",
            "assets-net-max-retries", "Network backend: retries",
            "the attempts after the first before the network backend reports Offline");
    declare(settings::MakeInt("assets.net.backoff_ms", 100, 1, 60000), "ENGINE_ASSETS_NET_BACKOFF_MS",
            "assets-net-backoff-ms", "Network backend: first back-off",
            "the delay after the first failed attempt; it doubles with each further one");
    declare(settings::MakeInt("assets.net.prefetch_bytes", 1 << 20, 0, i64(1) << 30),
            "ENGINE_ASSETS_NET_PREFETCH_BYTES", "assets-net-prefetch-bytes", "Network backend: whole-blob fetch size",
            "resources up to this size are fetched whole (and cached); larger ones are read by range");
    declare(settings::MakeInt("assets.net.cache_bytes", i64(1) << 30, 0, i64(1) << 44), "ENGINE_ASSETS_NET_CACHE_BYTES",
            "assets-net-cache-bytes", "Network backend: disk cache size",
            "the most bytes of blobs the disk cache holds; the least recently used go first");
    declare(settings::MakeText("assets.cache_dir", ""), "ENGINE_ASSETS_CACHE_DIR", "assets-cache-dir",
            "Network backend: disk cache directory",
            "where the network backend keeps the blobs it fetched; empty for none");
}

NetworkBackendConfig NetworkBackendConfigFromSettings(const settings::Registry& registry)
{
    auto read = [&](const char* key)
    { return static_cast<u64>(settings::Setting<i64>::Find(registry, StringView(key)).Get()); };
    NetworkBackendConfig config;
    config.timeout_nanos = read("assets.net.timeout_ms") * 1'000'000ull;
    config.max_retries = static_cast<u32>(read("assets.net.max_retries"));
    config.backoff_nanos = read("assets.net.backoff_ms") * 1'000'000ull;
    config.prefetch_bytes = read("assets.net.prefetch_bytes");
    return config;
}

Status NetworkCacheFromSettings(const settings::Registry& registry, UniquePtr<BlobCache>* out)
{
    out->Reset();
    settings::TextSetting text(registry, registry.Find(StringView("assets.cache_dir")));
    char buffer[1024];
    const usize length = text.Get(buffer, sizeof(buffer) - 1);
    if (length == 0)
    {
        return Status::Success();
    }
    io::Path path;
    if (!io::Path::Parse(StringView(buffer, length), io::NativePathStyle(), &path))
    {
        return Status::Make(ErrorCode::Invalid, "assets.cache_dir is not a path");
    }
    const u64 bytes = static_cast<u64>(
        settings::Setting<i64>::Find(registry, StringView("assets.net.cache_bytes")).Get());
    return BlobCache::Open(path, bytes, out);
}

// ---------------------------------------------------------------------------------------------------------------------
// The pieces

struct NetworkBackend::Slot
{
    u32 request = 0;
    bool done = false;
    bool lost = false;  // the connection ended before the answer came
    ProtoData response;
};

// A table read in progress: pages are requested one at a time and handed to the ticket as they arrive.
struct NetworkBackend::Fetch
{
    TableTicket* ticket = nullptr;
    u32 page = 0;
    u32 failures = 0;
    u64 not_before = 0;  // back-off: no request before this time
    u64 deadline = 0;    // the answer to the request in flight is due
    Slot* waiting = nullptr;
    UniquePtr<ResourceTable> collected;  // the pages so far (becomes the cached table when the read ends)
    u64 generation = 0;
    bool unchanged = false;
};

NetworkBackend::NetworkBackend(const NetworkBackendOptions& options)
    : options_(options), rng_(options.config.jitter_seed | 1)
{
    {
        LockGuard<Mutex> lock(mutex_);
        next_connect_ = 0;
        PumpLocked();
    }
    Drain();
    if (options_.background_thread)
    {
        thread_ = Thread(&NetworkBackend::RunThread, this);
    }
}

NetworkBackend::~NetworkBackend()
{
    stop_.Store(true);
    if (thread_.Joinable())
    {
        thread_.Join();
    }
    {
        LockGuard<Mutex> lock(mutex_);
        FinishFetchLocked(Status::Make(ErrorCode::Cancelled, "the backend was destroyed"));
        if (connection_ != 0)
        {
            (void)options_.transport->Close(connection_, 0);
            net::Event event;
            while (options_.transport->Poll(&event))
            {
            }
        }
    }
    Drain();
}

// A ticket is completed outside the mutex: its completion function may call into code that holds a lock of its own
// while it calls into this backend (a mount table starting a read), so completing under `mutex_` could deadlock.
void NetworkBackend::Drain()
{
    Vector<Pending> done;
    {
        LockGuard<Mutex> lock(mutex_);
        for (usize i = 0; i < pending_.Size(); ++i)
        {
            done.Push(pending_[i]);
        }
        pending_.Clear();
    }
    for (usize i = 0; i < done.Size(); ++i)
    {
        done[i].ticket->Complete(done[i].status);
    }
}

void NetworkBackend::RunThread()
{
    while (!stop_.Load())
    {
        {
            LockGuard<Mutex> lock(mutex_);
            PumpLocked();
        }
        Drain();
        ThisThread::SleepForMicroseconds(200);
    }
}

void NetworkBackend::Idle()
{
    Drain();
    if (!options_.background_thread && options_.idle != nullptr)
    {
        options_.idle(options_.idle_user);
    }
    else
    {
        ThisThread::SleepForMicroseconds(100);
    }
}

void NetworkBackend::Poll()
{
    {
        LockGuard<Mutex> lock(mutex_);
        PumpLocked();
    }
    Drain();
}

NetworkState NetworkBackend::State() const
{
    LockGuard<Mutex> lock(mutex_);
    return state_;
}

NetworkBackendStats NetworkBackend::Stats() const
{
    LockGuard<Mutex> lock(mutex_);
    return stats_;
}

u64 NetworkBackend::Jitter(u64 limit)
{
    rng_ = rng_ * 6364136223846793005ull + 1442695040888963407ull;
    return limit == 0 ? 0 : (rng_ >> 33) % limit;
}

void NetworkBackend::SetOfflineLocked()
{
    state_ = NetworkState::Offline;
    if (connection_ != 0)
    {
        // A connection that is up but is not being answered (or only gets `Busy`) is replaced by the reconnect, which
        // is what brings the state back to `Online`.
        (void)options_.transport->Close(connection_, 0);
        LostLocked(false);
    }
}

void NetworkBackend::LostLocked(bool counts)
{
    connection_ = 0;
    open_ = false;
    greeted_ = false;
    if (state_ == NetworkState::Online)
    {
        state_ = NetworkState::Connecting;
    }
    for (usize i = 0; i < slots_.Size(); ++i)
    {
        if (!slots_[i]->done)
        {
            slots_[i]->done = true;
            slots_[i]->lost = true;
        }
    }
    const u64 now = options_.clock->NowNanos();
    failures_ += counts ? 1 : 0;
    next_connect_ = now +
                    (state_ == NetworkState::Offline ? options_.config.reconnect_nanos : options_.config.backoff_nanos);
    if (failures_ > options_.config.max_retries)
    {
        state_ = NetworkState::Offline;
    }
}

void NetworkBackend::PumpLocked()
{
    const u64 now = options_.clock->NowNanos();
    net::Event event;
    while (options_.transport->Poll(&event))
    {
        if (event.connection != connection_)
        {
            continue;  // an event of a connection that is gone
        }
        if (event.kind == net::EventKind::Opened)
        {
            open_ = true;
            ProtoData hello;
            hello.type = ProtoMessage::Hello;
            hello.request = kHelloRequest;
            hello.version = kProtoVersion;
            hello.max_read_bytes = 1u << 20;
            byte buffer[64];
            usize size = 0;
            if (EncodeProto(hello, MutableByteSpan(buffer, sizeof(buffer)), &size) == ProtoError::None)
            {
                (void)options_.transport->Send(connection_, 0, ByteSpan(buffer, size),
                                               net::Reliability::ReliableOrdered);
            }
        }
        else if (event.kind == net::EventKind::Closed)
        {
            LostLocked(true);
        }
        else if (event.kind == net::EventKind::Message)
        {
            ProtoData message;
            if (DecodeProto(event.message, 1u << 22, &message) != ProtoError::None)
            {
                (void)options_.transport->Close(connection_, 1);
                LostLocked(true);
                continue;
            }
            if (message.type == ProtoMessage::HelloAck)
            {
                greeted_ = true;
                max_read_ = message.max_read_bytes;
                page_entries_ = message.page_entries;
                state_ = NetworkState::Online;
                failures_ = 0;
                if (connected_once_)
                {
                    stats_.reconnects += 1;
                }
                connected_once_ = true;
                continue;
            }
            for (usize i = 0; i < slots_.Size(); ++i)
            {
                if (!slots_[i]->done && slots_[i]->request == message.request)
                {
                    slots_[i]->response = static_cast<ProtoData&&>(message);
                    slots_[i]->done = true;
                    break;
                }
            }
        }
    }
    // The connection: make one when there is none and it is time; give up on one that does not open or greet.
    if (connection_ == 0)
    {
        if (now >= next_connect_)
        {
            if (options_.transport->Connect(options_.server, &connection_).Ok())
            {
                connect_started_ = now;
            }
            else
            {
                connection_ = 0;
                failures_ += 1;
                next_connect_ = now + options_.config.reconnect_nanos;
                if (failures_ > options_.config.max_retries)
                {
                    SetOfflineLocked();
                }
            }
        }
    }
    else if (!greeted_ && now - connect_started_ > options_.config.timeout_nanos)
    {
        (void)options_.transport->Close(connection_, 0);
        LostLocked(true);
    }
    StepFetchLocked();
}

// ---------------------------------------------------------------------------------------------------------------------
// Calls

Status NetworkBackend::ToStatus(ProtoError error) const
{
    switch (error)
    {
    case ProtoError::NotFound: return Status::Make(ErrorCode::NotFound, "the server does not have the resource");
    case ProtoError::Range: return Status::Make(ErrorCode::Invalid, "the read is not inside the resource");
    case ProtoError::TooLarge: return Status::Make(ErrorCode::TooLarge, "the request or the response is too large");
    case ProtoError::Busy: return Status::Make(ErrorCode::Offline, "the server is busy");
    case ProtoError::Invalid: return Status::Make(ErrorCode::Invalid, "the server refused the request");
    case ProtoError::Unsupported: return Status::Make(ErrorCode::Invalid, "the server does not support the protocol");
    default: return Status::Make(ErrorCode::Io, "the server failed");
    }
}

// One try: connect if need be, send, wait. Offline here means "try again": the connection or the answer did not come.
Status NetworkBackend::Attempt(const ProtoData& request, ProtoData* response)
{
    const u64 started = options_.clock->NowNanos();
    const u64 deadline = started + options_.config.timeout_nanos;
    // Greeted?
    for (;;)
    {
        {
            LockGuard<Mutex> lock(mutex_);
            if (!options_.background_thread)
            {
                PumpLocked();
            }
            if (greeted_)
            {
                break;
            }
        }
        if (options_.clock->NowNanos() >= deadline)
        {
            return Status::Make(ErrorCode::Offline, "the connection did not come up in time");
        }
        Idle();
    }
    Slot* slot = nullptr;
    {
        LockGuard<Mutex> lock(mutex_);
        if (!greeted_)
        {
            return Status::Make(ErrorCode::Offline, "the connection was lost");
        }
        UniquePtr<Slot> made = MakeUnique<Slot>();
        made->request = next_request_++;
        if (next_request_ == kHelloRequest)
        {
            next_request_ = 1;
        }
        slot = made.Get();
        ProtoData message;
        // The request is copied by value fields: build it again with this request id.
        message.type = request.type;
        message.request = slot->request;
        message.id = request.id;
        message.offset = request.offset;
        message.length = request.length;
        message.generation = request.generation;
        message.page = request.page;
        message.since = request.since;
        message.prefix = String<HeapAllocator>(HeapAllocator::Default(), request.prefix.View());
        Vector<byte> buffer;
        buffer.Resize(2048 + request.prefix.Length());
        usize size = 0;
        if (EncodeProto(message, MutableByteSpan(buffer.Data(), buffer.Size()), &size) != ProtoError::None ||
            options_.transport->Send(connection_, 0, ByteSpan(buffer.Data(), size), net::Reliability::ReliableOrdered)
                .Failed())
        {
            return Status::Make(ErrorCode::Offline, "the request could not be sent");
        }
        stats_.requests += 1;
        slots_.Push(static_cast<UniquePtr<Slot>&&>(made));
    }
    // Wait for the answer.
    const u64 answer_by = options_.clock->NowNanos() + options_.config.timeout_nanos;
    Status result = Status::Make(ErrorCode::Offline, "the request timed out");
    bool timed_out = false;
    for (;;)
    {
        {
            LockGuard<Mutex> lock(mutex_);
            if (!options_.background_thread)
            {
                PumpLocked();
            }
            if (slot->done)
            {
                if (slot->lost)
                {
                    result = Status::Make(ErrorCode::Offline, "the connection was lost");
                }
                else if (slot->response.type == ProtoMessage::Error)
                {
                    result = ToStatus(slot->response.error);
                }
                else
                {
                    *response = static_cast<ProtoData&&>(slot->response);
                    result = Status::Success();
                }
                break;
            }
        }
        if (options_.clock->NowNanos() >= answer_by)
        {
            timed_out = true;
            break;
        }
        Idle();
    }
    LockGuard<Mutex> lock(mutex_);
    if (timed_out)
    {
        stats_.timeouts += 1;
        // A connection that does not answer is dropped; the next attempt (or the reconnect) makes a new one.
        if (connection_ != 0)
        {
            (void)options_.transport->Close(connection_, 0);
            LostLocked(false);  // the caller counts the failure
        }
    }
    for (usize i = 0; i < slots_.Size(); ++i)
    {
        if (slots_[i].Get() == slot)
        {
            slots_[i] = static_cast<UniquePtr<Slot>&&>(slots_[slots_.Size() - 1]);
            slots_.Pop();
            break;
        }
    }
    return result;
}

Status NetworkBackend::Call(const ProtoData& request, ProtoData* response)
{
    for (u32 attempt = 0;; ++attempt)
    {
        {
            LockGuard<Mutex> lock(mutex_);
            if (state_ == NetworkState::Offline)
            {
                return Status::Make(ErrorCode::Offline, "the server cannot be reached");
            }
        }
        const Status status = Attempt(request, response);
        if (!status.Is(ErrorCode::Offline))
        {
            if (status.Ok())
            {
                LockGuard<Mutex> lock(mutex_);
                failures_ = 0;
            }
            return status;
        }
        {
            LockGuard<Mutex> lock(mutex_);
            failures_ += 1;
            if (attempt >= options_.config.max_retries)
            {
                SetOfflineLocked();
                return Status::Make(ErrorCode::Offline, "the server cannot be reached");
            }
            stats_.retries += 1;
        }
        // The back-off: base * 2^attempt, at most the limit, and a random share on top.
        u64 delay = options_.config.backoff_nanos << (attempt < 32 ? attempt : 32);
        if (delay > options_.config.backoff_max_nanos || delay < options_.config.backoff_nanos)
        {
            delay = options_.config.backoff_max_nanos;
        }
        {
            LockGuard<Mutex> lock(mutex_);
            delay += Jitter(delay / 2 + 1);
        }
        const u64 until = options_.clock->NowNanos() + delay;
        while (options_.clock->NowNanos() < until)
        {
            {
                LockGuard<Mutex> lock(mutex_);
                if (!options_.background_thread)
                {
                    PumpLocked();
                }
            }
            Idle();
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Resources

Status NetworkBackend::Stat(ResourceId id, ResourceInfo* out)
{
    ProtoData request;
    request.type = ProtoMessage::Stat;
    request.id = id.Value();
    ProtoData response;
    const Status status = Call(request, &response);
    if (status.Ok())
    {
        out->size = response.size;
        return Status::Success();
    }
    if (status.Is(ErrorCode::Offline) && options_.cache != nullptr)
    {
        ContentHash hash;
        u64 size = 0;
        if (options_.cache->Find(id, &hash, &size))
        {
            out->size = size;
            LockGuard<Mutex> lock(mutex_);
            stats_.offline_serves += 1;
            return Status::Success();
        }
    }
    return status;
}

Status NetworkBackend::ReadRange(ResourceId id, u64 offset, void* buffer, usize length)
{
    byte* out = static_cast<byte*>(buffer);
    usize done = 0;
    while (done < length)
    {
        u32 limit = 0;
        {
            LockGuard<Mutex> lock(mutex_);
            limit = max_read_ != 0 ? max_read_ : 4096;
        }
        const usize chunk = length - done < limit ? length - done : limit;
        ProtoData request;
        request.type = ProtoMessage::Read;
        request.id = id.Value();
        request.offset = offset + done;
        request.length = static_cast<u32>(chunk);
        ProtoData response;
        ENGINE_TRY(Call(request, &response));
        if (response.data.Size() != chunk)
        {
            return Status::Make(ErrorCode::Io, "the server returned a short range");
        }
        MemCopy(out + done, response.data.Data(), chunk);
        done += chunk;
        LockGuard<Mutex> lock(mutex_);
        stats_.fetched_bytes += chunk;
    }
    return Status::Success();
}

namespace
{

// A resource held in memory.
class BytesReader final : public ResourceReader
{
public:
    explicit BytesReader(Vector<byte>&& bytes) : bytes_(static_cast<Vector<byte>&&>(bytes)) {}
    u64 Size() const override { return bytes_.Size(); }
    Status ReadAt(u64 offset, void* buffer, usize length) override
    {
        if (offset > bytes_.Size() || length > bytes_.Size() - offset)
        {
            return Status::Make(ErrorCode::Invalid, "the read is not inside the resource");
        }
        MemCopy(buffer, bytes_.Data() + offset, length);
        return Status::Success();
    }

private:
    Vector<byte> bytes_;
};

}  // namespace

// A large resource: every read is a request.
class RangeReader final : public ResourceReader
{
public:
    RangeReader(NetworkBackend* owner, ResourceId id, u64 size) : owner_(owner), id_(id), size_(size) {}
    u64 Size() const override { return size_; }
    Status ReadAt(u64 offset, void* buffer, usize length) override
    {
        if (offset > size_ || length > size_ - offset)
        {
            return Status::Make(ErrorCode::Invalid, "the read is not inside the resource");
        }
        return owner_->ReadRange(id_, offset, buffer, length);
    }

private:
    NetworkBackend* owner_;
    ResourceId id_;
    u64 size_;
};

Status NetworkBackend::FetchBlob(ResourceId id, u64 size, Vector<byte>* bytes, ContentHash* hash)
{
    // The header first: its hash says whether the cache holds the blob.
    byte header[kBlobHeaderBytes];
    ENGINE_TRY(ReadRange(id, 0, header, kBlobHeaderBytes));
    BlobHeaderInfo info;
    ENGINE_TRY(PeekBlobHeader(ByteSpan(header, sizeof(header)), &info));
    *hash = info.content_hash;
    if (options_.cache != nullptr && options_.cache->Has(info.content_hash))
    {
        if (options_.cache->Read(info.content_hash, bytes).Ok() && bytes->Size() == size)
        {
            options_.cache->Note(id, info.content_hash, size);
            LockGuard<Mutex> lock(mutex_);
            stats_.cache_serves += 1;
            return Status::Success();
        }
    }
    bytes->Resize(static_cast<usize>(size));
    MemCopy(bytes->Data(), header, kBlobHeaderBytes);
    if (size > kBlobHeaderBytes)
    {
        ENGINE_TRY(ReadRange(id, kBlobHeaderBytes, bytes->Data() + kBlobHeaderBytes,
                             static_cast<usize>(size - kBlobHeaderBytes)));
    }
    if (ComputeBlobHash(ByteSpan(bytes->Data(), bytes->Size())) != info.content_hash)
    {
        return Status::Make(ErrorCode::Corrupt, "the blob from the server does not match its recorded hash");
    }
    if (options_.cache != nullptr)
    {
        (void)options_.cache->Put(id, info.content_hash, ByteSpan(bytes->Data(), bytes->Size()));
    }
    return Status::Success();
}

Status NetworkBackend::Open(ResourceId id, UniquePtr<ResourceReader>* out)
{
    ResourceInfo info;
    const Status stat = Stat(id, &info);
    if (stat.Failed() && !stat.Is(ErrorCode::Offline))
    {
        return stat;
    }
    bool offline = stat.Failed();
    {
        LockGuard<Mutex> lock(mutex_);
        offline = offline || state_ == NetworkState::Offline;
    }
    Vector<byte> bytes;
    if (!offline)
    {
        if (info.size < kBlobHeaderBytes || info.size > options_.config.prefetch_bytes)
        {
            UniquePtr<RangeReader> reader = MakeUnique<RangeReader>(this, id, info.size);
            *out = UniquePtr<ResourceReader>(reader.Release());
            return Status::Success();
        }
        ContentHash hash;
        Status fetched = FetchBlob(id, info.size, &bytes, &hash);
        // A resource replaced on the server between the requests of one fetch (the size from `Stat`, the header, the
        // body) does not match itself: look again, a few times, before reporting it.
        for (u32 again = 0; again < 2 && (fetched.Is(ErrorCode::Corrupt) || fetched.Is(ErrorCode::Invalid)); ++again)
        {
            fetched = Stat(id, &info);
            if (fetched.Ok())
            {
                fetched = FetchBlob(id, info.size, &bytes, &hash);
            }
        }
        if (fetched.Ok())
        {
            UniquePtr<BytesReader> reader = MakeUnique<BytesReader>(static_cast<Vector<byte>&&>(bytes));
            *out = UniquePtr<ResourceReader>(reader.Release());
            return Status::Success();
        }
        if (!fetched.Is(ErrorCode::Offline))
        {
            return fetched;
        }
    }
    // Offline: what the cache knows and holds.
    if (options_.cache != nullptr)
    {
        ContentHash hash;
        u64 size = 0;
        if (options_.cache->Find(id, &hash, &size) && options_.cache->Read(hash, &bytes).Ok())
        {
            {
                LockGuard<Mutex> lock(mutex_);
                stats_.cache_serves += 1;
                stats_.offline_serves += 1;
            }
            UniquePtr<BytesReader> reader = MakeUnique<BytesReader>(static_cast<Vector<byte>&&>(bytes));
            *out = UniquePtr<ResourceReader>(reader.Release());
            return Status::Success();
        }
    }
    return Status::Make(ErrorCode::Offline, "the server cannot be reached and the cache does not hold the resource");
}

// ---------------------------------------------------------------------------------------------------------------------
// The table and the changes

Status NetworkBackend::FetchTable(ResourceTable* out)
{
    u64 have = 0;
    {
        LockGuard<Mutex> lock(mutex_);
        have = table_ ? generation_ : 0;
    }
    UniquePtr<ResourceTable> fresh = MakeUnique<ResourceTable>();
    u32 pages = 1;
    u64 generation = 0;
    bool unchanged = false;
    u32 restarts = 0;
    for (u32 page = 0; page < pages; ++page)
    {
        ProtoData request;
        request.type = ProtoMessage::Table;
        request.generation = have;
        request.page = page;
        ProtoData response;
        ENGINE_TRY(Call(request, &response));
        if (page != 0 && response.generation != generation)
        {
            // The table changed between two pages: start over (a few times), the pages read so far are of two tables.
            if (++restarts > 3)
            {
                return Status::Make(ErrorCode::Other, "the table kept changing while it was read");
            }
            fresh = MakeUnique<ResourceTable>();
            page = ~u32{0};  // the loop's increment makes it page 0
            pages = 1;
            continue;
        }
        generation = response.generation;
        pages = response.page_count;
        if (response.unchanged)
        {
            unchanged = true;
            break;
        }
        for (usize i = 0; i < response.entries.Size(); ++i)
        {
            const ProtoEntry& entry = response.entries[i];
            ENGINE_TRY(
                fresh->Add(entry.path.View(), ResourceId::FromValue(entry.id), ResourceType::FromTag(entry.type_tag)));
        }
    }
    LockGuard<Mutex> lock(mutex_);
    if (!unchanged)
    {
        table_ = static_cast<UniquePtr<ResourceTable>&&>(fresh);
    }
    generation_ = generation;
    if (!table_)
    {
        return Status::Make(ErrorCode::Other, "the server said the table was unchanged and none is held");
    }
    Vector<ShadowedEntry> shadowed;
    return out->Merge(*table_, &shadowed);
}

Status NetworkBackend::ReadTable(ResourceTable* out)
{
    Status status = FetchTable(out);
    if (status.Is(ErrorCode::Offline))
    {
        // Offline: the last table read, if there is one (the state says it is stale).
        LockGuard<Mutex> lock(mutex_);
        if (table_)
        {
            Vector<ShadowedEntry> shadowed;
            return out->Merge(*table_, &shadowed);
        }
    }
    return status;
}

Status NetworkBackend::Watch(Vector<ChangeRecord>* out)
{
    out->Clear();
    u64 since = 0;
    {
        LockGuard<Mutex> lock(mutex_);
        since = epoch_known_ ? epoch_ : ~u64{0};  // the first call learns the epoch and reports nothing
    }
    ProtoData request;
    request.type = ProtoMessage::Changes;
    request.since = since;
    ProtoData response;
    const Status status = Call(request, &response);
    if (status.Is(ErrorCode::Offline))
    {
        return Status::Success();  // nothing is known while the server cannot be reached
    }
    ENGINE_TRY(status);
    const bool first = since == ~u64{0};
    UniquePtr<ResourceTable> before;
    u64 before_generation = 0;
    {
        LockGuard<Mutex> lock(mutex_);
        if (first || !response.reset)
        {
            epoch_known_ = true;
            epoch_ = response.epoch;
        }
        else if (table_)
        {
            before = static_cast<UniquePtr<ResourceTable>&&>(table_);
            before_generation = generation_;
            generation_ = 0;
        }
    }
    if (first)
    {
        return Status::Success();
    }
    if (!response.reset)
    {
        for (usize i = 0; i < response.ids.Size(); ++i)
        {
            ChangeRecord record;
            record.id = ResourceId::FromValue(response.ids[i]);
            record.kind = static_cast<ChangeKind>(response.kinds[i]);
            out->Push(record);
        }
        return Status::Success();
    }
    // The server cannot say what changed since our epoch: read the table again and report the ids that came and went.
    ResourceTable now;
    const Status reread = FetchTable(&now);
    if (reread.Failed())
    {
        // Nothing is lost: the epoch was not taken, so the next call asks again and gets the reset again.
        LockGuard<Mutex> lock(mutex_);
        if (!table_ && before)
        {
            table_ = static_cast<UniquePtr<ResourceTable>&&>(before);
            generation_ = before_generation;
        }
        return reread.Is(ErrorCode::Offline) ? Status::Success() : reread;
    }
    {
        LockGuard<Mutex> lock(mutex_);
        epoch_known_ = true;
        epoch_ = response.epoch;
    }
    if (before)
    {
        for (usize i = 0; i < now.Size(); ++i)
        {
            ResourceType type;
            if (!before->TypeOf(now.At(i).id, &type))
            {
                ChangeRecord record;
                record.id = now.At(i).id;
                record.kind = ChangeKind::Added;
                out->Push(record);
            }
        }
        for (usize i = 0; i < before->Size(); ++i)
        {
            ResourceType type;
            if (!now.TypeOf(before->At(i).id, &type))
            {
                ChangeRecord record;
                record.id = before->At(i).id;
                record.kind = ChangeKind::Removed;
                out->Push(record);
            }
        }
    }
    return Status::Success();
}

// ---------------------------------------------------------------------------------------------------------------------
// The asynchronous table read

UniquePtr<TableTicket> NetworkBackend::ReadTableAsync()
{
    UniquePtr<TableTicket> ticket = MakeUnique<TableTicket>();
    {
        LockGuard<Mutex> lock(mutex_);
        if (fetch_)
        {
            // One at a time: the one in progress ends first (the caller asked again, so the older ticket is cancelled).
            FinishFetchLocked(Status::Make(ErrorCode::Cancelled, "another table read was started"));
        }
        fetch_ = MakeUnique<Fetch>();
        fetch_->ticket = ticket.Get();
        fetch_->collected = MakeUnique<ResourceTable>();
    }
    Drain();
    return ticket;
}

void NetworkBackend::FinishFetchLocked(const Status& status)
{
    if (!fetch_)
    {
        return;
    }
    TableTicket* ticket = fetch_->ticket;
    if (status.Ok() && !fetch_->unchanged)
    {
        table_ = static_cast<UniquePtr<ResourceTable>&&>(fetch_->collected);
        generation_ = fetch_->generation;
    }
    if (fetch_->waiting != nullptr)
    {
        for (usize i = 0; i < slots_.Size(); ++i)
        {
            if (slots_[i].Get() == fetch_->waiting)
            {
                slots_[i] = static_cast<UniquePtr<Slot>&&>(slots_[slots_.Size() - 1]);
                slots_.Pop();
                break;
            }
        }
    }
    fetch_.Reset();
    pending_.Push(Pending{ticket, status});  // completed by Drain, outside the mutex
}

// Ends the table read with the status of a step that failed.
#define ENGINE_TRY_FINISH(expression)                                                                                  \
    do                                                                                                                 \
    {                                                                                                                  \
        const Status try_status = (expression);                                                                        \
        if (try_status.Failed())                                                                                       \
        {                                                                                                              \
            FinishFetchLocked(try_status);                                                                             \
            return;                                                                                                    \
        }                                                                                                              \
    } while (false)

void NetworkBackend::StepFetchLocked()
{
    if (!fetch_)
    {
        return;
    }
    Fetch& f = *fetch_;
    const u64 now = options_.clock->NowNanos();
    if (f.ticket->IsCancelled())
    {
        FinishFetchLocked(Status::Make(ErrorCode::Cancelled, "the table read was cancelled"));
        return;
    }
    if (f.waiting != nullptr)
    {
        Slot* slot = f.waiting;
        if (slot->done && !slot->lost && slot->response.type == ProtoMessage::TableOk)
        {
            ProtoData response = static_cast<ProtoData&&>(slot->response);
            for (usize i = 0; i < slots_.Size(); ++i)
            {
                if (slots_[i].Get() == slot)
                {
                    slots_[i] = static_cast<UniquePtr<Slot>&&>(slots_[slots_.Size() - 1]);
                    slots_.Pop();
                    break;
                }
            }
            f.waiting = nullptr;
            f.failures = 0;
            if (f.page == 0)
            {
                f.generation = response.generation;
            }
            else if (response.generation != f.generation)
            {
                // The pages already handed out are of the older table and cannot be taken back: the read ends and the
                // caller reads again.
                FinishFetchLocked(Status::Make(ErrorCode::Other, "the table changed while it was read; read it again"));
                return;
            }
            ResourceTable page;
            if (response.unchanged)
            {
                if (!table_)
                {
                    FinishFetchLocked(
                        Status::Make(ErrorCode::Other, "the server says the table is unchanged and none is held"));
                    return;
                }
                f.unchanged = true;
                // The server has the table we hold: hand it out in one piece without asking for the other pages.
                for (usize i = 0; i < table_->Size(); ++i)
                {
                    ENGINE_TRY_FINISH(page.Add(table_->At(i).path, table_->At(i).id, table_->At(i).type));
                }
                const Status held = f.ticket->Publish(page);
                FinishFetchLocked(held);
                return;
            }
            for (usize i = 0; i < response.entries.Size(); ++i)
            {
                const ProtoEntry& entry = response.entries[i];
                ENGINE_TRY_FINISH(page.Add(entry.path.View(), ResourceId::FromValue(entry.id),
                                           ResourceType::FromTag(entry.type_tag)));
                ENGINE_TRY_FINISH(f.collected->Add(entry.path.View(), ResourceId::FromValue(entry.id),
                                                   ResourceType::FromTag(entry.type_tag)));
            }
            const Status published = f.ticket->Publish(page);
            if (published.Failed())
            {
                FinishFetchLocked(published);
                return;
            }
            f.page += 1;
            if (f.page >= response.page_count)
            {
                FinishFetchLocked(Status::Success());
            }
            return;
        }
        const bool failed = slot->done && (slot->lost || slot->response.type == ProtoMessage::Error);
        if (!failed && now < f.deadline)
        {
            return;  // still waiting
        }
        // A failed or timed-out page: back off and ask again, or give up.
        const bool timed_out = now >= f.deadline && !slot->done;
        const bool server_said_no = slot->done && !slot->lost && slot->response.type == ProtoMessage::Error &&
                                    slot->response.error != ProtoError::Busy;
        for (usize i = 0; i < slots_.Size(); ++i)
        {
            if (slots_[i].Get() == slot)
            {
                slots_[i] = static_cast<UniquePtr<Slot>&&>(slots_[slots_.Size() - 1]);
                slots_.Pop();
                break;
            }
        }
        f.waiting = nullptr;
        if (server_said_no)
        {
            FinishFetchLocked(Status::Make(ErrorCode::Other, "the server refused the table request"));
            return;
        }
        stats_.timeouts += timed_out ? 1 : 0;
        f.failures += 1;
        if (f.failures > options_.config.max_retries)
        {
            SetOfflineLocked();
            // Offline: the table held, if any, is what the read gives (the state says it is stale).
            if (table_)
            {
                ResourceTable page;
                for (usize i = 0; i < table_->Size(); ++i)
                {
                    (void)page.Add(table_->At(i).path, table_->At(i).id, table_->At(i).type);
                }
                (void)f.ticket->Publish(page);
                f.unchanged = true;
                FinishFetchLocked(Status::Success());
            }
            else
            {
                FinishFetchLocked(Status::Make(ErrorCode::Offline, "the server cannot be reached"));
            }
            return;
        }
        stats_.retries += 1;
        u64 delay = options_.config.backoff_nanos << (f.failures < 32 ? f.failures - 1 : 32);
        if (delay > options_.config.backoff_max_nanos || delay < options_.config.backoff_nanos)
        {
            delay = options_.config.backoff_max_nanos;
        }
        f.not_before = now + delay + Jitter(delay / 2 + 1);
        return;
    }
    // Send the next request when connected and not backing off.
    if (!greeted_ || now < f.not_before)
    {
        if (state_ == NetworkState::Offline && !greeted_ && now >= f.not_before)
        {
            f.failures = options_.config.max_retries + 1;
            f.waiting = nullptr;
            // Let the next step finish the read from the held table.
            UniquePtr<Slot> dead = MakeUnique<Slot>();
            dead->done = true;
            dead->lost = true;
            dead->request = 0;
            f.waiting = dead.Get();
            f.deadline = now;
            slots_.Push(static_cast<UniquePtr<Slot>&&>(dead));
        }
        return;
    }
    UniquePtr<Slot> made = MakeUnique<Slot>();
    made->request = next_request_++;
    if (next_request_ == kHelloRequest)
    {
        next_request_ = 1;
    }
    ProtoData request;
    request.type = ProtoMessage::Table;
    request.request = made->request;
    request.generation = table_ ? generation_ : 0;
    request.page = f.page;
    byte buffer[128];
    usize size = 0;
    if (EncodeProto(request, MutableByteSpan(buffer, sizeof(buffer)), &size) == ProtoError::None &&
        options_.transport->Send(connection_, 0, ByteSpan(buffer, size), net::Reliability::ReliableOrdered).Ok())
    {
        stats_.requests += 1;
        f.deadline = now + options_.config.timeout_nanos;
        f.waiting = made.Get();
        slots_.Push(static_cast<UniquePtr<Slot>&&>(made));
    }
}

#undef ENGINE_TRY_FINISH

}  // namespace engine_placeholder::asset
