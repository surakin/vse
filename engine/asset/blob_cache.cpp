#include "asset/blob_cache.h"

#include "cstr/format.h"
#include "cstr/mem_ops.h"
#include "io/directory.h"
#include "io/file.h"
#include "io/file_system.h"
#include "string/string.h"
#include "containers/unique_ptr.h"
#include "string_view.h"

namespace vse::asset
{

namespace
{

const char* const kIndexName = "index";

int HexValue(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    return -1;
}

bool ParseHash(StringView text, ContentHash* out)
{
    if (text.Length() != 32)
    {
        return false;
    }
    for (usize i = 0; i < 16; ++i)
    {
        const int hi = HexValue(text[i * 2]);
        const int lo = HexValue(text[i * 2 + 1]);
        if (hi < 0 || lo < 0)
        {
            return false;
        }
        out->bytes[i] = static_cast<u8>(hi * 16 + lo);
    }
    return true;
}

bool ParseNumber(StringView text, u64* out)
{
    if (text.Length() == 0 || text.Length() > 20)
    {
        return false;
    }
    u64 value = 0;
    for (usize i = 0; i < text.Length(); ++i)
    {
        if (text[i] < '0' || text[i] > '9')
        {
            return false;
        }
        value = value * 10 + static_cast<u64>(text[i] - '0');
    }
    *out = value;
    return true;
}

// The next space-separated word of `line` from `*at`; empty at the end.
StringView NextWord(StringView line, usize* at)
{
    while (*at < line.Length() && line[*at] == ' ')
    {
        ++*at;
    }
    const usize start = *at;
    while (*at < line.Length() && line[*at] != ' ')
    {
        ++*at;
    }
    return line.Substring(start, *at - start);
}

}  // namespace

BlobCache::BlobCache(const io::Path& directory, u64 max_bytes) : directory_(directory), max_bytes_(max_bytes) {}

io::Path BlobCache::FileOf(const ContentHash& hash) const
{
    char hex[33];
    FormatContentHash(hash, hex);
    char name[48];
    FormatTo(name, sizeof(name), "%s.blob", hex);
    return directory_ / StringView(name, CStrLength(name));
}

Status BlobCache::Open(const io::Path& directory, u64 max_bytes, UniquePtr<BlobCache>* out)
{
    if (io::CreateDirectories(directory).Failed())
    {
        return Status::Make(ErrorCode::Io, "the cache directory cannot be made");
    }
    UniquePtr<BlobCache> cache = MakeUnique<BlobCache>(directory, max_bytes);
    // The index.
    String<HeapAllocator> text(HeapAllocator::Default());
    if (io::ReadTextFile(directory / StringView(kIndexName), &text).Ok())
    {
        usize pos = 0;
        const StringView all = text.View();
        while (pos < all.Length())
        {
            usize end = pos;
            while (end < all.Length() && all[end] != '\n')
            {
                ++end;
            }
            const StringView line = all.Substring(pos, end - pos);
            pos = end + 1;
            usize at = 0;
            const StringView kind = NextWord(line, &at);
            if (kind == StringView("N"))
            {
                Name name;
                u64 id = 0;
                const StringView id_text = NextWord(line, &at);
                const StringView hash_text = NextWord(line, &at);
                const StringView size_text = NextWord(line, &at);
                if (id_text.Length() == 16 && ParseResourceId(id_text, &name.id).Ok() &&
                    ParseHash(hash_text, &name.hash) && ParseNumber(size_text, &id))
                {
                    name.size = id;
                    cache->names_.Push(name);
                }
            }
            else if (kind == StringView("B"))
            {
                Blob blob;
                if (ParseHash(NextWord(line, &at), &blob.hash) && ParseNumber(NextWord(line, &at), &blob.size) &&
                    ParseNumber(NextWord(line, &at), &blob.used))
                {
                    // A second line for a hash is ignored (it would count the file twice and, evicted once, leave a
                    // line that names a file that is gone).
                    bool known = false;
                    for (usize k = 0; k < cache->blobs_.Size(); ++k)
                    {
                        known = known || cache->blobs_[k].hash == blob.hash;
                    }
                    if (!known)
                    {
                        cache->blobs_.Push(blob);
                    }
                }
            }
        }
    }
    // Every file the index names must be there and hash to its name; the others go.
    for (usize i = 0; i < cache->blobs_.Size();)
    {
        const Blob blob = cache->blobs_[i];
        Vector<u8> bytes;
        bool good = io::ReadFile(cache->FileOf(blob.hash), &bytes).Ok() && bytes.Size() == blob.size &&
                    ComputeBlobHash(ByteSpan(bytes.Data(), bytes.Size())) == blob.hash;
        if (!good)
        {
            (void)io::Remove(cache->FileOf(blob.hash));
            cache->stats_.dropped_corrupt += 1;
            cache->blobs_[i] = cache->blobs_[cache->blobs_.Size() - 1];
            cache->blobs_.Pop();
            continue;
        }
        cache->bytes_ += blob.size;
        cache->clock_ = blob.used > cache->clock_ ? blob.used : cache->clock_;
        ++i;
    }
    // Files that no entry names (a crash between a file and its index line).
    Vector<io::DirectoryItem> items;
    if (io::ListDirectory(directory, false, &items).Ok())
    {
        for (usize i = 0; i < items.Size(); ++i)
        {
            const StringView name = items[i].name.View();
            if (name.Length() == 37 && name.Substring(32) == StringView(".blob"))
            {
                ContentHash hash;
                if (ParseHash(name.Substring(0, 32), &hash) && cache->FindBlob(hash) == nullptr)
                {
                    (void)io::Remove(directory / name);
                }
            }
        }
    }
    cache->EvictLocked();
    *out = static_cast<UniquePtr<BlobCache>&&>(cache);
    return Status::Success();
}

BlobCache::~BlobCache()
{
    (void)Flush();
}

BlobCache::Blob* BlobCache::FindBlob(const ContentHash& hash)
{
    for (usize i = 0; i < blobs_.Size(); ++i)
    {
        if (blobs_[i].hash == hash)
        {
            return &blobs_[i];
        }
    }
    return nullptr;
}

bool BlobCache::Find(ResourceId id, ContentHash* hash, u64* size) const
{
    LockGuard<Mutex> lock(mutex_);
    for (usize i = 0; i < names_.Size(); ++i)
    {
        if (names_[i].id == id)
        {
            *hash = names_[i].hash;
            *size = names_[i].size;
            return true;
        }
    }
    return false;
}

void BlobCache::Note(ResourceId id, const ContentHash& hash, u64 size)
{
    LockGuard<Mutex> lock(mutex_);
    NoteLocked(id, hash, size);
}

void BlobCache::NoteLocked(ResourceId id, const ContentHash& hash, u64 size)
{
    for (usize i = 0; i < names_.Size(); ++i)
    {
        if (names_[i].id == id)
        {
            names_[i].hash = hash;
            names_[i].size = size;
            return;
        }
    }
    Name name;
    name.id = id;
    name.hash = hash;
    name.size = size;
    names_.Push(name);
}

bool BlobCache::Has(const ContentHash& hash) const
{
    LockGuard<Mutex> lock(mutex_);
    for (usize i = 0; i < blobs_.Size(); ++i)
    {
        if (blobs_[i].hash == hash)
        {
            return true;
        }
    }
    return false;
}

Status BlobCache::Read(const ContentHash& hash, Vector<byte>* out)
{
    LockGuard<Mutex> lock(mutex_);
    Blob* blob = FindBlob(hash);
    if (blob == nullptr)
    {
        stats_.misses += 1;
        return Status::Make(ErrorCode::NotFound, "the cache does not hold the blob");
    }
    if (io::ReadFile(FileOf(hash), out).Failed())
    {
        for (usize i = 0; i < blobs_.Size(); ++i)
        {
            if (&blobs_[i] == blob)
            {
                RemoveBlobLocked(i);  // the file vanished: forget it
                break;
            }
        }
        stats_.misses += 1;
        return Status::Make(ErrorCode::NotFound, "the cache file is gone");
    }
    blob->used = ++clock_;
    stats_.hits += 1;
    return Status::Success();
}

void BlobCache::RemoveBlobLocked(usize index)
{
    bytes_ -= blobs_[index].size;
    (void)io::Remove(FileOf(blobs_[index].hash));
    blobs_[index] = blobs_[blobs_.Size() - 1];
    blobs_.Pop();
}

void BlobCache::EvictLocked()
{
    while (bytes_ > max_bytes_ && blobs_.Size() != 0)
    {
        usize oldest = 0;
        for (usize i = 1; i < blobs_.Size(); ++i)
        {
            if (blobs_[i].used < blobs_[oldest].used)
            {
                oldest = i;
            }
        }
        RemoveBlobLocked(oldest);
        stats_.evictions += 1;
    }
}

Status BlobCache::Put(ResourceId id, const ContentHash& hash, ByteSpan bytes)
{
    if (ComputeBlobHash(bytes) != hash)
    {
        return Status::Make(ErrorCode::Corrupt, "the blob does not hash to the hash it was put under");
    }
    LockGuard<Mutex> lock(mutex_);
    NoteLocked(id, hash, bytes.Size());
    Blob* held = FindBlob(hash);
    if (held != nullptr)
    {
        held->used = ++clock_;  // the same bytes are in already (another id, or the same one again)
        return Status::Success();
    }
    if (bytes.Size() > max_bytes_)
    {
        return Status::Success();  // known by name, too large to hold
    }
    if (io::WriteFileAtomic(FileOf(hash), bytes, 0644).Failed())
    {
        return Status::Make(ErrorCode::Io, "the cache file cannot be written");
    }
    Blob blob;
    blob.hash = hash;
    blob.size = bytes.Size();
    blob.used = ++clock_;
    blobs_.Push(blob);
    bytes_ += blob.size;
    EvictLocked();
    return WriteIndexLocked();
}

Status BlobCache::Flush()
{
    LockGuard<Mutex> lock(mutex_);
    return WriteIndexLocked();
}

BlobCacheStats BlobCache::Stats() const
{
    LockGuard<Mutex> lock(mutex_);
    BlobCacheStats copy = stats_;
    copy.entries = static_cast<u32>(blobs_.Size());
    copy.bytes = bytes_;
    return copy;
}

Status BlobCache::WriteIndexLocked()
{
    String<HeapAllocator> text(HeapAllocator::Default());
    for (usize i = 0; i < names_.Size(); ++i)
    {
        char id[17];
        FormatResourceId(names_[i].id, id);
        char hash[33];
        FormatContentHash(names_[i].hash, hash);
        char line[96];
        FormatTo(line, sizeof(line), "N %s %s %llu\n", id, hash, static_cast<unsigned long long>(names_[i].size));
        text.Append(StringView(line, CStrLength(line)));
    }
    for (usize i = 0; i < blobs_.Size(); ++i)
    {
        char hash[33];
        FormatContentHash(blobs_[i].hash, hash);
        char line[96];
        FormatTo(line, sizeof(line), "B %s %llu %llu\n", hash, static_cast<unsigned long long>(blobs_[i].size),
                 static_cast<unsigned long long>(blobs_[i].used));
        text.Append(StringView(line, CStrLength(line)));
    }
    if (io::WriteFileAtomic(directory_ / StringView(kIndexName), text.CStr(), text.Length(), 0644).Failed())
    {
        return Status::Make(ErrorCode::Io, "the cache index cannot be written");
    }
    return Status::Success();
}

}  // namespace vse::asset
