#include "asset/filesystem_backend.h"

#include "algorithm.h"
#include "asset/blob.h"
#include "io/directory.h"
#include "io/file.h"
#include "io/file_system.h"

namespace vse::asset
{
namespace
{

Status FromIo(const io::Status& s)
{
    if (s.Ok())
    {
        return Status::Success();
    }
    if (s.Is(io::Error::NotFound))
    {
        return Status::Make(ErrorCode::NotFound, "the file does not exist");
    }
    return Status::Make(ErrorCode::Io, "a file operation failed");
}

// Reads the 64-byte header of `file` into `info`.
Status ReadHeader(const io::Path& file, BlobHeaderInfo* info, u64* file_size)
{
    io::FilePtr f;
    io::Status s = io::File::Open(file, io::OpenMode::Read, io::kOpenNone, &f);
    if (s.Failed())
    {
        return FromIo(s);
    }
    u8 header[kBlobHeaderBytes];
    usize got = 0;
    s = f->Read(header, sizeof(header), 0, &got);
    if (s.Failed())
    {
        return FromIo(s);
    }
    if (file_size != nullptr)
    {
        s = f->Size(file_size);
        if (s.Failed())
        {
            return FromIo(s);
        }
    }
    return PeekBlobHeader(ByteSpan(header, got), info);
}

class FilesystemReader : public ResourceReader
{
public:
    FilesystemReader(io::FilePtr file, u64 size) : file_(Move(file)), size_(size) {}

    u64 Size() const override { return size_; }

    Status ReadAt(u64 offset, void* buffer, usize length) override
    {
        if (offset > size_ || length > size_ - offset)
        {
            return Status::Make(ErrorCode::Invalid, "the read is not inside the resource");
        }
        if (length == 0)
        {
            return Status::Success();
        }
        return FromIo(io::ReadExact(*file_, buffer, length, offset));
    }

    void ReadAtAsync(u64 offset, void* buffer, usize length, ReadDone done, void* user) override
    {
        if (offset > size_ || length > size_ - offset)
        {
            done(user, Status::Make(ErrorCode::Invalid, "the read is not inside the resource"));
            return;
        }
        if (length == 0)
        {
            done(user, Status::Success());
            return;
        }
        done_ = done;
        user_ = user;
        length_ = length;
        file_->ReadAsync(&request_, buffer, length, offset, &FilesystemReader::OnRead, this);
    }

private:
    static void OnRead(void* self_pointer, const io::AsyncResult& result)
    {
        FilesystemReader* self = static_cast<FilesystemReader*>(self_pointer);
        Status status = FromIo(result.status);
        if (status.Ok() && result.bytes != self->length_)
        {
            status = Status::Make(ErrorCode::Io, "the file ended before the read did");
        }
        ReadDone done = self->done_;
        void* user = self->user_;
        done(user, status);
    }

    io::FilePtr file_;
    u64 size_;
    io::AsyncRequest request_;
    ReadDone done_ = nullptr;
    void* user_ = nullptr;
    usize length_ = 0;
};

}  // namespace

// The state one directory walk shares with its visitor.
struct FilesystemBackend::ScanState
{
    const FilesystemBackend* self = nullptr;
    const Vector<Item>* previous = nullptr;
    const Vector<u32>* previous_by_file = nullptr;  // indexes into `previous`, ordered by file path
    Vector<Item>* found = nullptr;
    Status error;
};

namespace
{

const char* const kStoredTableName = kStoredTableFileName;

bool IsStoredTable(StringView name)
{
    return name == StringView(kStoredTableName);
}

}  // namespace

io::WalkAction FilesystemBackend::VisitEntry(void* user, const io::WalkEntry& entry)
{
    ScanState& state = *static_cast<ScanState*>(user);
    if (!entry.error.Ok())
    {
        state.error = Status::Make(ErrorCode::Io, "a directory could not be entered");
        return io::WalkAction::Stop;
    }
    if (entry.type == io::FileType::Directory || entry.after_children)
    {
        return io::WalkAction::Continue;
    }
    io::FileInfo info;
    io::Path file = entry.path;
    if (entry.type == io::FileType::Symlink)
    {
        // A link counts only when it resolves (a loop does not) to a regular file inside the root.
        io::Path resolved;
        if (io::Canonical(entry.path, &resolved).Failed() || !resolved.IsInside(state.self->canonical_root_))
        {
            return io::WalkAction::Continue;
        }
    }
    else if (entry.type != io::FileType::Regular)
    {
        return io::WalkAction::Continue;
    }
    if (io::Stat(file, &info).Failed() || info.type != io::FileType::Regular)
    {
        return io::WalkAction::Continue;
    }
    if (entry.depth == 1 && IsStoredTable(entry.name))
    {
        return io::WalkAction::Continue;
    }

    io::Path relative;
    if (!file.RelativeTo(state.self->root_, &relative))
    {
        return io::WalkAction::Continue;
    }
    String<HeapAllocator> raw(HeapAllocator::Default());
    raw.Append('/');
    raw.Append(relative.View());
    String<HeapAllocator> virtual_path(HeapAllocator::Default());
    if (NormalizePath(raw.View(), &virtual_path).Failed())
    {
        state.error = Status::Make(ErrorCode::Invalid, "a file name under the root is not a valid resource path");
        return io::WalkAction::Stop;
    }

    Item item;
    item.path = Move(virtual_path);
    item.size = info.size;
    item.modified_ns = info.modified_unix_ns;
    item.file = file;

    // An unchanged file (same size and stamp) keeps what the last scan read from its header.
    const Item* old = nullptr;
    {
        usize lo = 0;
        usize hi = state.previous_by_file->Size();
        while (lo < hi)
        {
            const usize mid = lo + (hi - lo) / 2;
            const Item& candidate = (*state.previous)[(*state.previous_by_file)[mid]];
            if (candidate.file < file)
            {
                lo = mid + 1;
            }
            else
            {
                hi = mid;
            }
        }
        if (lo < state.previous_by_file->Size())
        {
            const Item& candidate = (*state.previous)[(*state.previous_by_file)[lo]];
            if (candidate.file == file)
            {
                old = &candidate;
            }
        }
    }
    if (old != nullptr && old->size == item.size && old->modified_ns == item.modified_ns)
    {
        item.id = old->id;
        item.type = old->type;
    }
    else
    {
        BlobHeaderInfo header;
        const Status s = ReadHeader(file, &header, nullptr);
        if (s.Is(ErrorCode::Corrupt))
        {
            return io::WalkAction::Continue;  // not a blob: the directory may hold other files
        }
        if (s.Failed())
        {
            state.error = s;
            return io::WalkAction::Stop;
        }
        if (header.type == types::kNames)
        {
            return io::WalkAction::Continue;
        }
        if (header.id.IsNone())
        {
            state.error = Status::Make(ErrorCode::Corrupt, "a resource blob carries the null id");
            return io::WalkAction::Stop;
        }
        item.id = header.id;
        item.type = header.type;
    }
    state.found->Push(Move(item));
    return io::WalkAction::Continue;
}

FilesystemBackend::FilesystemBackend(const io::Path& root, io::Path canonical_root, const TypeRegistry& types,
                                     bool stored_table, u32 target)
    : root_(root), canonical_root_(Move(canonical_root)), types_(&types), stored_table_(stored_table), target_(target)
{
}

Status FilesystemBackend::Create(const io::Path& root, const TypeRegistry& types, UniquePtr<FilesystemBackend>* out,
                                 u32 target)
{
    io::FileInfo info;
    io::Status s = io::Stat(root, &info);
    if (s.Failed() || info.type != io::FileType::Directory)
    {
        return Status::Make(ErrorCode::NotFound, "the root is not a directory");
    }
    io::Path canonical;
    s = io::Canonical(root, &canonical);
    if (s.Failed())
    {
        return Status::Make(ErrorCode::Io, "the root could not be resolved");
    }
    io::FileInfo table_info;
    const bool stored = io::Stat(root / StringView(kStoredTableName), &table_info).Ok();
    *out = MakeUnique<FilesystemBackend>(root, Move(canonical), types, stored, target);
    if (!*out)
    {
        return Status::Make(ErrorCode::Other, "memory for the backend ran out");
    }
    return Status::Success();
}

Status FilesystemBackend::ScanLocked(Vector<Item>* out) const
{
    out->Clear();
    Vector<u32> by_file;
    for (usize i = 0; i < items_.Size(); ++i)
    {
        by_file.Push(static_cast<u32>(i));
    }
    Sort(Span<u32>(by_file), [this](const u32& a, const u32& b) { return items_[a].file < items_[b].file; });

    if (stored_table_)
    {
        Vector<u8> bytes;
        const io::Status read = io::ReadFile(root_ / StringView(kStoredTableName), &bytes);
        if (read.Failed())
        {
            return FromIo(read);
        }
        BlobReader blob;
        ENGINE_TRY(blob.Open(ByteSpan(bytes.Data(), bytes.Size()), *types_));
        ResourceTable table;
        ENGINE_TRY(table.ReadNamesBlob(blob));
        for (usize i = 0; i < table.Size(); ++i)
        {
            const TableEntryView e = table.At(i);
            Item item;
            item.id = e.id;
            item.type = e.type;
            item.path = String<HeapAllocator>(HeapAllocator::Default(), e.path);
            item.file = root_ / e.path.Substring(1);
            io::FileInfo info;
            if (io::Stat(item.file, &info).Failed() || info.type != io::FileType::Regular)
            {
                continue;  // the table names a file that is not there: it is not served
            }
            io::Path resolved;
            if (io::Canonical(item.file, &resolved).Failed() || !resolved.IsInside(canonical_root_))
            {
                continue;  // a link on the way leads out of the root
            }
            item.size = info.size;
            item.modified_ns = info.modified_unix_ns;
            out->Push(Move(item));
        }
    }
    else
    {
        io::WalkOptions options;
        options.max_depth = 64;
        ScanState state;
        state.self = this;
        state.previous = &items_;
        state.previous_by_file = &by_file;
        state.found = out;
        const io::Status walked = io::WalkDirectory(root_, options, &FilesystemBackend::VisitEntry, &state);
        if (walked.Failed())
        {
            return FromIo(walked);
        }
        if (state.error.Failed())
        {
            return state.error;
        }
    }
    Sort(Span<Item>(*out), [](const Item& a, const Item& b) { return a.id < b.id; });
    for (usize i = 1; i < out->Size(); ++i)
    {
        if ((*out)[i].id == (*out)[i - 1].id)
        {
            return Status::Make(ErrorCode::DuplicateId, "two files under the root claim one resource id");
        }
    }
    return Status::Success();
}

Status FilesystemBackend::EnsureIndexLocked()
{
    if (indexed_)
    {
        return Status::Success();
    }
    Vector<Item> scanned;
    ENGINE_TRY(ScanLocked(&scanned));
    items_ = Move(scanned);
    indexed_ = true;
    return Status::Success();
}

const FilesystemBackend::Item* FilesystemBackend::FindLocked(ResourceId id) const
{
    usize lo = 0;
    usize hi = items_.Size();
    while (lo < hi)
    {
        const usize mid = lo + (hi - lo) / 2;
        if (items_[mid].id < id)
        {
            lo = mid + 1;
        }
        else
        {
            hi = mid;
        }
    }
    return lo < items_.Size() && items_[lo].id == id ? &items_[lo] : nullptr;
}

Status FilesystemBackend::Locate(ResourceId id, io::Path* file)
{
    LockGuard<Mutex> lock(mutex_);
    ENGINE_TRY(EnsureIndexLocked());
    const Item* found = FindLocked(id);
    if (found == nullptr)
    {
        return Status::Make(ErrorCode::NotFound, "the backend does not have the resource");
    }
    *file = found->file;
    return Status::Success();
}

// Opens `file` once and checks its own header against `id`: the file's id is the truth, so a stale table cannot point
// an id at another resource's file.
Status FilesystemBackend::OpenChecked(ResourceId id, const io::Path& file, io::FilePtr* out, BlobHeaderInfo* header,
                                      u64* size)
{
    io::Status s = io::File::Open(file, io::OpenMode::Read, io::kOpenNone, out);
    if (s.Failed())
    {
        return FromIo(s);
    }
    u8 bytes[kBlobHeaderBytes];
    usize got = 0;
    s = (*out)->Read(bytes, sizeof(bytes), 0, &got);
    if (s.Failed())
    {
        return FromIo(s);
    }
    s = (*out)->Size(size);
    if (s.Failed())
    {
        return FromIo(s);
    }
    ENGINE_TRY(PeekBlobHeader(ByteSpan(bytes, got), header));
    if (header->id != id)
    {
        return Status::Make(ErrorCode::Corrupt, "the table and the file disagree on the resource id");
    }
    if (header->target != target_)
    {
        return Status::Make(ErrorCode::WrongTarget,
                            "the blob was laid out for another target; run asset_mint pack --target");
    }
    return Status::Success();
}

Status FilesystemBackend::Stat(ResourceId id, ResourceInfo* out)
{
    io::Path file;
    ENGINE_TRY(Locate(id, &file));
    io::FilePtr f;
    BlobHeaderInfo header;
    u64 size = 0;
    ENGINE_TRY(OpenChecked(id, file, &f, &header, &size));
    io::FileInfo info;
    if (io::Stat(file, &info).Failed())
    {
        return Status::Make(ErrorCode::NotFound, "the file disappeared");
    }
    out->size = size;
    out->hash = header.content_hash;
    out->has_hash = true;
    out->modified_ns = info.modified_unix_ns;
    out->type = header.type;
    return Status::Success();
}

Status FilesystemBackend::Open(ResourceId id, UniquePtr<ResourceReader>* out)
{
    io::Path file;
    ENGINE_TRY(Locate(id, &file));
    io::FilePtr f;
    BlobHeaderInfo header;
    u64 size = 0;
    ENGINE_TRY(OpenChecked(id, file, &f, &header, &size));
    UniquePtr<FilesystemReader> reader = MakeUnique<FilesystemReader>(Move(f), size);
    if (!reader)
    {
        return Status::Make(ErrorCode::Other, "memory for the reader ran out");
    }
    *out = Move(reader);
    return Status::Success();
}

Status FilesystemBackend::ReadTable(ResourceTable* out)
{
    LockGuard<Mutex> lock(mutex_);
    ENGINE_TRY(EnsureIndexLocked());
    for (usize i = 0; i < items_.Size(); ++i)
    {
        ENGINE_TRY(out->Add(items_[i].path.View(), items_[i].id, items_[i].type));
    }
    return Status::Success();
}

Status FilesystemBackend::Watch(Vector<ChangeRecord>* out)
{
    out->Clear();
    LockGuard<Mutex> lock(mutex_);
    if (!indexed_)
    {
        return EnsureIndexLocked();  // nothing changed before the first read
    }
    Vector<Item> scanned;
    ENGINE_TRY(ScanLocked(&scanned));
    usize a = 0;
    usize b = 0;
    while (a < items_.Size() || b < scanned.Size())
    {
        if (b == scanned.Size() || (a < items_.Size() && items_[a].id < scanned[b].id))
        {
            out->Push(ChangeRecord{items_[a].id, ChangeKind::Removed});
            ++a;
        }
        else if (a == items_.Size() || scanned[b].id < items_[a].id)
        {
            out->Push(ChangeRecord{scanned[b].id, ChangeKind::Added});
            ++b;
        }
        else
        {
            if (items_[a].size != scanned[b].size || items_[a].modified_ns != scanned[b].modified_ns ||
                !(items_[a].file == scanned[b].file))
            {
                out->Push(ChangeRecord{scanned[b].id, ChangeKind::Replaced});
            }
            ++a;
            ++b;
        }
    }
    items_ = Move(scanned);
    return Status::Success();
}

}  // namespace vse::asset
