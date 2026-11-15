#include "asset/resource_server.h"

#include "asset/content_hash.h"
#include "cstr/mem_ops.h"
#include "os/os.h"
#include "settings/setting.h"

namespace engine_placeholder::asset
{

namespace
{

constexpr usize kMaxBufferBytes = usize(4) << 20;  // the largest response buffer, whatever the transport allows
constexpr u32 kCloseProtocol = 1;                  // the code a closed connection's peer gets

}  // namespace

ResourceServerConfig ResourceServerConfigFromSettings(const settings::Registry& registry)
{
    ResourceServerConfig config;
    config.max_read_bytes = static_cast<u32>(
        settings::Setting<i64>::Find(registry, StringView("assets.net.max_read_bytes")).Get());
    config.max_inflight = static_cast<u32>(
        settings::Setting<i64>::Find(registry, StringView("assets.net.max_inflight")).Get());
    config.page_entries = static_cast<u32>(
        settings::Setting<i64>::Find(registry, StringView("assets.net.page_entries")).Get());
    return config;
}

ResourceServer::ResourceServer(ResourceBackend& backend, net::Transport& transport, const ResourceServerConfig& config)
    : backend_(&backend), transport_(&transport), config_(config),
      epoch_(
          os::MonotonicTimeNanos())  // a server that starts again has another epoch, so a client's old one is a reset
{
    usize buffer = transport.MaxMessageBytes();
    if (buffer > kMaxBufferBytes)
    {
        buffer = kMaxBufferBytes;
    }
    usize read = config_.max_read_bytes;
    if (read + kProtoHeaderSlack > buffer)
    {
        read = buffer > kProtoHeaderSlack ? buffer - kProtoHeaderSlack : 0;
    }
    max_read_ = static_cast<u32>(read);
    if (config_.page_entries == 0 || config_.page_entries > kProtoMaxPageEntries)
    {
        config_.page_entries = kProtoMaxPageEntries;
    }
    // A page of the longest entries must fit a message (an entry is at most 12 bytes and a path of 1,024 bytes).
    const usize entry_bytes = 12 + 3 + kProtoMaxPathBytes;
    usize fit = buffer > kProtoHeaderSlack ? (buffer - kProtoHeaderSlack) / entry_bytes : 0;
    fit = fit == 0 ? 1 : fit;
    if (config_.page_entries > fit)
    {
        config_.page_entries = static_cast<u32>(fit);
    }
    if (config_.max_inflight == 0)
    {
        config_.max_inflight = 1;
    }
    buffer_.Resize(buffer);
}

ResourceServer::Connection* ResourceServer::Find(net::ConnectionId id)
{
    for (usize i = 0; i < connections_.Size(); ++i)
    {
        if (connections_[i].id == id)
        {
            return &connections_[i];
        }
    }
    return nullptr;
}

void ResourceServer::Drop(net::ConnectionId id)
{
    for (usize i = 0; i < connections_.Size(); ++i)
    {
        if (connections_[i].id == id)
        {
            connections_[i] = connections_[connections_.Size() - 1];
            connections_.Pop();
            break;
        }
    }
    stats_.connections = static_cast<u32>(connections_.Size());
}

void ResourceServer::Violation(net::ConnectionId id)
{
    stats_.closed_protocol += 1;
    (void)transport_->Close(id, kCloseProtocol);
    Drop(id);
}

void ResourceServer::Update()
{
    for (usize i = 0; i < connections_.Size(); ++i)
    {
        connections_[i].served = 0;
    }
    net::Event event;
    while (transport_->Poll(&event))
    {
        if (event.kind == net::EventKind::Opened)
        {
            if (event.incoming)
            {
                Connection connection;
                connection.id = event.connection;
                connections_.Push(connection);
                stats_.connections = static_cast<u32>(connections_.Size());
            }
        }
        else if (event.kind == net::EventKind::Closed)
        {
            Drop(event.connection);
        }
        else if (event.kind == net::EventKind::Message)
        {
            Connection* connection = Find(event.connection);
            if (connection != nullptr)
            {
                Handle(*connection, event.message);
            }
        }
    }
}

void ResourceServer::Reply(Connection& connection, const ProtoData& response, ByteSpan payload)
{
    usize size = 0;
    const ProtoError encoded = EncodeProto(response, payload, MutableByteSpan(buffer_.Data(), buffer_.Size()), &size);
    if (encoded != ProtoError::None)
    {
        // The response does not fit a message: tell the client so (a message of its own always fits).
        ProtoData error;
        error.type = ProtoMessage::Error;
        error.request = response.request;
        error.error = ProtoError::TooLarge;
        if (EncodeProto(error, MutableByteSpan(buffer_.Data(), buffer_.Size()), &size) != ProtoError::None)
        {
            Violation(connection.id);
            return;
        }
        stats_.errors += 1;
    }
    const net::Status sent = transport_->Send(connection.id, 0, ByteSpan(buffer_.Data(), size),
                                              net::Reliability::ReliableOrdered);
    if (sent.Failed())
    {
        const net::ConnectionId id = connection.id;
        if (sent.Is(net::ErrorCode::LimitExceeded))
        {
            // The client's queue is full, so this answer is dropped: say Busy, so the request is not left waiting. When
            // even that does not fit, the connection ends.
            ProtoData busy;
            busy.type = ProtoMessage::Error;
            busy.request = response.request;
            busy.error = ProtoError::Busy;
            usize busy_size = 0;
            if (EncodeProto(busy, MutableByteSpan(buffer_.Data(), buffer_.Size()), &busy_size) == ProtoError::None &&
                transport_->Send(id, 0, ByteSpan(buffer_.Data(), busy_size), net::Reliability::ReliableOrdered).Ok())
            {
                stats_.busy += 1;
                return;
            }
        }
        (void)transport_->Close(id, kCloseProtocol);
        Drop(id);
    }
}

void ResourceServer::Fail(Connection& connection, u32 request, ProtoError error)
{
    ProtoData response;
    response.type = ProtoMessage::Error;
    response.request = request;
    response.error = error;
    stats_.errors += 1;
    Reply(connection, response);
}

void ResourceServer::PollBackend()
{
    // The backend is asked about changes only when a client asks for the table or the changes (a scan can be costly).
    constexpr usize kLogEntries = 4096;
    Vector<ChangeRecord> changes;
    if (backend_->Watch(&changes).Failed())
    {
        return;
    }
    for (usize i = 0; i < changes.Size(); ++i)
    {
        if (changes[i].kind != ChangeKind::Replaced)
        {
            table_valid_ = false;  // an id or a path appeared or went; new bytes under the same names change nothing
        }
        if (log_.Size() >= 2 * kLogEntries)
        {
            // Dropped in one go (the oldest half), so a burst of changes does not move the log for each of them.
            for (usize k = kLogEntries; k < log_.Size(); ++k)
            {
                log_[k - kLogEntries] = log_[k];
            }
            while (log_.Size() > kLogEntries)
            {
                log_.Pop();
            }
        }
        log_.Push(changes[i]);
        epoch_ += 1;
    }
}

bool ResourceServer::EnsureTable()
{
    PollBackend();
    if (table_valid_)
    {
        return true;
    }
    UniquePtr<ResourceTable> fresh = MakeUnique<ResourceTable>();
    if (!fresh || backend_->ReadTable(fresh.Get()).Failed())
    {
        return false;
    }
    // The generation is a hash of the table: the same table has the same generation after a restart, and a different
    // one never does.
    ContentHasher hasher;
    for (usize i = 0; i < fresh->Size(); ++i)
    {
        const TableEntryView view = fresh->At(i);
        byte head[12];
        const u64 id = view.id.Value();
        const u32 tag = view.type.Tag();
        for (u32 k = 0; k < 8; ++k)
        {
            head[k] = static_cast<byte>(id >> (8 * k));
        }
        for (u32 k = 0; k < 4; ++k)
        {
            head[8 + k] = static_cast<byte>(tag >> (8 * k));
        }
        hasher.Update(ByteSpan(head, sizeof(head)));
        hasher.Update(ByteSpan(reinterpret_cast<const byte*>(view.path.Data()), view.path.Length()));
        const byte zero = 0;
        hasher.Update(ByteSpan(&zero, 1));
    }
    const ContentHash hash = hasher.Finish();
    u64 generation = 0;
    for (u32 k = 0; k < 8; ++k)
    {
        generation |= static_cast<u64>(hash.bytes[k]) << (8 * k);
    }
    table_ = static_cast<UniquePtr<ResourceTable>&&>(fresh);
    table_valid_ = true;
    generation_ = generation == 0 ? 1 : generation;
    return true;
}

void ResourceServer::Handle(Connection& connection, ByteSpan message)
{
    ProtoData request;
    if (DecodeProto(message, max_read_, &request) != ProtoError::None)
    {
        Violation(connection.id);
        return;
    }
    if (request.type == ProtoMessage::Hello)
    {
        if (connection.hello)
        {
            Violation(connection.id);  // a second Hello
            return;
        }
        if (request.version != kProtoVersion)
        {
            const net::ConnectionId id = connection.id;  // before the reply: a failed send drops the connection and
                                                         // moves another into its slot
            Fail(connection, request.request, ProtoError::Unsupported);  // the client learns why it is let go
            (void)transport_->Close(id, kCloseProtocol);
            Drop(id);
            return;
        }
        connection.hello = true;
        connection.max_read = request.max_read_bytes < max_read_ ? request.max_read_bytes : max_read_;
        ProtoData ack;
        ack.type = ProtoMessage::HelloAck;
        ack.request = request.request;
        ack.version = kProtoVersion;
        ack.max_read_bytes = connection.max_read;
        ack.page_entries = config_.page_entries;
        ack.max_inflight = config_.max_inflight;
        Reply(connection, ack);
        return;
    }
    if (!connection.hello)
    {
        Violation(connection.id);  // a request before Hello
        return;
    }
    if (request.type != ProtoMessage::Stat && request.type != ProtoMessage::Table &&
        request.type != ProtoMessage::List && request.type != ProtoMessage::Read &&
        request.type != ProtoMessage::Changes)
    {
        Violation(connection.id);  // a response, or a message the server never takes
        return;
    }
    stats_.requests += 1;
    if (connection.served >= config_.max_inflight)
    {
        stats_.busy += 1;
        Fail(connection, request.request, ProtoError::Busy);
        return;
    }
    connection.served += 1;
    Serve(connection, request);
}

void ResourceServer::Serve(Connection& connection, const ProtoData& request)
{
    ProtoData response;
    response.request = request.request;
    switch (request.type)
    {
    case ProtoMessage::Stat:
    {
        ResourceInfo info;
        const Status status = backend_->Stat(ResourceId::FromValue(request.id), &info);
        if (status.Failed())
        {
            Fail(connection, request.request,
                 status.Is(ErrorCode::NotFound) ? ProtoError::NotFound : ProtoError::Other);
            return;
        }
        response.type = ProtoMessage::StatOk;
        response.size = info.size;
        Reply(connection, response);
        return;
    }
    case ProtoMessage::Table:
    {
        if (!EnsureTable())
        {
            Fail(connection, request.request, ProtoError::Other);
            return;
        }
        const u32 per_page = config_.page_entries;
        const u32 pages = table_->Size() == 0 ? 1 : static_cast<u32>((table_->Size() + per_page - 1) / per_page);
        if (request.page >= pages)
        {
            Fail(connection, request.request, ProtoError::Invalid);
            return;
        }
        response.type = ProtoMessage::TableOk;
        response.generation = generation_;
        response.page = request.page;
        response.page_count = pages;
        if (request.generation == generation_)
        {
            response.unchanged = true;  // the client has this table
        }
        else
        {
            const usize first = static_cast<usize>(request.page) * per_page;
            for (usize i = first; i < table_->Size() && i < first + per_page; ++i)
            {
                const TableEntryView view = table_->At(i);
                ProtoEntry entry;
                entry.id = view.id.Value();
                entry.type_tag = view.type.Tag();
                entry.path = String<HeapAllocator>(HeapAllocator::Default(), view.path);
                response.entries.Push(static_cast<ProtoEntry&&>(entry));
            }
        }
        Reply(connection, response);
        return;
    }
    case ProtoMessage::List:
    {
        if (!EnsureTable())
        {
            Fail(connection, request.request, ProtoError::Other);
            return;
        }
        Vector<TableEntryView> found;
        table_->ListPrefix(request.prefix.View(), &found);
        const u32 per_page = config_.page_entries;
        const usize first = static_cast<usize>(request.page) * per_page;
        if (request.page != 0 && first >= found.Size())
        {
            Fail(connection, request.request, ProtoError::Invalid);
            return;
        }
        response.type = ProtoMessage::ListOk;
        response.generation = generation_;
        response.page = request.page;
        for (usize i = first; i < found.Size() && i < first + per_page; ++i)
        {
            response.ids.Push(found[i].id.Value());
        }
        response.more = found.Size() > first + per_page;
        Reply(connection, response);
        return;
    }
    case ProtoMessage::Changes:
    {
        PollBackend();
        response.type = ProtoMessage::ChangesOk;
        response.epoch = epoch_;
        const u64 held = log_.Size();
        // The client continues from its epoch when the log still holds everything after it; otherwise it starts over.
        if (request.since > epoch_ || epoch_ - request.since > held || epoch_ - request.since > kProtoMaxPageEntries)
        {
            response.reset = true;
        }
        else
        {
            for (usize i = log_.Size() - static_cast<usize>(epoch_ - request.since); i < log_.Size(); ++i)
            {
                response.kinds.Push(static_cast<u8>(log_[i].kind));
                response.ids.Push(log_[i].id.Value());
            }
        }
        Reply(connection, response);
        return;
    }
    case ProtoMessage::Read:
    {
        const u32 limit = connection.max_read;
        if (request.length > limit)
        {
            Fail(connection, request.request, ProtoError::TooLarge);
            return;
        }
        UniquePtr<ResourceReader> reader;
        const Status opened = backend_->Open(ResourceId::FromValue(request.id), &reader);
        if (opened.Failed())
        {
            Fail(connection, request.request,
                 opened.Is(ErrorCode::NotFound) ? ProtoError::NotFound : ProtoError::Other);
            return;
        }
        const u64 size = reader->Size();
        if (request.offset > size)
        {
            Fail(connection, request.request, ProtoError::Range);
            return;
        }
        const u64 left = size - request.offset;
        const usize count = static_cast<usize>(left < request.length ? left
                                                                     : request.length);  // the last range is short
        if (!data_.TryResizeUninitialized(count))
        {
            Fail(connection, request.request, ProtoError::Other);
            return;
        }
        if (count != 0 && reader->ReadAt(request.offset, data_.Data(), count).Failed())
        {
            Fail(connection, request.request, ProtoError::Other);
            return;
        }
        response.type = ProtoMessage::ReadOk;
        response.offset = request.offset;
        stats_.bytes_served += count;
        Reply(connection, response, ByteSpan(data_.Data(), count));
        return;
    }
    default: Fail(connection, request.request, ProtoError::Unsupported); return;
    }
}

}  // namespace engine_placeholder::asset
