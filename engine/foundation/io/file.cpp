#include "io/file.h"

#include "sync/atomic.h"
#include "sync/mutex.h"
#include "sync/thread.h"

#include "log/diagnostics_hooks.h"
#include "io/file_system.h"
#include "log/assert.h"
#include "cstr/mem_ops.h"

namespace engine_placeholder::io {

LogChannel LogIo("io", LogVerbosity::Warn);

// --- stream operations built on the offset ones -----------------------------------------------------------------------

Status File::Seek(u64 position) {
    if (!IsOpen()) {
        return Status::Of(Error::Invalid);
    }
    if ((Capabilities() & kCapSeekable) == 0) {
        return Status::Of(Error::Unsupported);
    }
    position_ = position;
    return Status::Success();
}

Status File::Read(void* buffer, usize length, usize* bytes_read) {
    const Status s = Read(buffer, length, position_, bytes_read);
    if (s.Ok()) {
        position_ += *bytes_read;
    }
    return s;
}

Status File::Write(const void* data, usize length) {
    if ((Capabilities() & kCapAppendOnly) != 0) {
        return Append(data, length);
    }
    const Status s = Write(data, length, position_);
    if (s.Ok()) {
        position_ += length;
    }
    return s;
}

// --- asynchronous operations ------------------------------------------------------------------------------------------

namespace {


// The built-in executor: one thread, FIFO, drains on destruction.
class ThreadExecutor final : public IoExecutor {
public:
    ThreadExecutor() : thread_([this] { Loop(); }) {}
    ~ThreadExecutor() override {
        {
            LockGuard<Mutex> lock(mutex_);
            stopping_ = true;
        }
        cv_.NotifyAll();
        thread_.Join();
    }
    bool Submit(IoWork* work) override {
        work->next = nullptr;
        {
            LockGuard<Mutex> lock(mutex_);
            if (stopping_) return false;
            (tail_ != nullptr ? tail_->next : head_) = work;
            tail_ = work;
        }
        cv_.NotifyOne();
        return true;
    }

private:
    void Loop() {
        NotifyThreadStart("io");
        for (;;) {
            IoWork* work = nullptr;
            {
                UniqueLock<Mutex> lock(mutex_);
                cv_.Wait(lock, [&] { return head_ != nullptr || stopping_; });
                if (head_ == nullptr) return;
                work = head_;
                head_ = work->next;
                if (head_ == nullptr) tail_ = nullptr;
            }
            work->run(work);
        }
    }
    Mutex mutex_;
    ConditionVariable cv_;
    IoWork* head_ = nullptr;
    IoWork* tail_ = nullptr;
    bool stopping_ = false;
    Thread thread_;
};

Atomic<IoExecutor*> g_executor{nullptr};

} // namespace

void SetDefaultExecutor(IoExecutor* executor) { g_executor.Store(executor, MemoryOrder::Release); }

void ClearDefaultExecutorIf(IoExecutor* executor) {
    IoExecutor* expected = executor;
    g_executor.CompareExchangeStrong(expected, nullptr, MemoryOrder::AcqRel);
}

IoExecutor& DefaultExecutor() {
    if (IoExecutor* e = g_executor.Load(MemoryOrder::Acquire)) return *e;
    static ThreadExecutor builtin; // created on first use, drained and joined at exit
    return builtin;
}

void File::CheckNoRequestsInFlight() {
    ENGINE_ENSURE(in_flight_.Load() == 0, "io::File destroyed with asynchronous requests in flight (the backend must call WaitForAsyncRequests)");
}

void File::WaitForAsyncRequests() {
    // Normally short. Yield first, then back off so a long wait does not burn a core.
    for (int spins = 0; in_flight_.Load(MemoryOrder::Acquire) != 0; ++spins) {
        if (spins < 100) {
            ThisThread::Yield();
        } else {
            ThisThread::SleepForMicroseconds(50);
        }
    }
}

bool File::ClaimAsync(AsyncRequest* request) {
    u8 expected = kAsyncPending;
    return request->state.CompareExchangeStrong(expected, kAsyncRunning);
}

bool File::CancelPending(AsyncRequest* request) {
    u8 expected = kAsyncPending;
    return request->state.CompareExchangeStrong(expected, kAsyncCancelled);
}

void File::CompleteAsync(AsyncRequest* request, const AsyncResult& result) {
    request->state.Store(kAsyncDone, MemoryOrder::Release);
    const Completion done = request->done;
    void* const user = request->user;
    // Count the request out before the callback: the callback may destroy this file.
    in_flight_.FetchSub(1, MemoryOrder::AcqRel);
    done(user, result);
}

void File::RunOnExecutor(IoWork* work) {
    AsyncRequest* r = reinterpret_cast<AsyncRequest*>(work);
    File* file = r->file;
    AsyncResult result;
    if (!ClaimAsync(r)) {
        result.status = Status::Of(Error::Interrupted); // cancelled while queued
    } else {
        switch (r->kind) {
        case AsyncKind::Read: result.status = file->Read(r->buffer, r->length, r->offset, &result.bytes); break;
        case AsyncKind::Write:
            result.status = file->Write(r->data, r->length, r->offset);
            if (result.status.Ok()) result.bytes = r->length;
            break;
        case AsyncKind::Flush: result.status = file->Flush(); break;
        }
    }
    file->CompleteAsync(r, result);
}

void File::StartOnExecutor(AsyncRequest* request) {
    request->work.run = &File::RunOnExecutor;
    if (!DefaultExecutor().Submit(&request->work)) {
        AsyncResult result;
        result.status = Status::Of(Error::Interrupted);
        CompleteAsync(request, result);
    }
}

namespace {
void Prepare(File* file, AsyncRequest* r, AsyncKind kind, Completion done, void* user) {
    ENGINE_ASSERT(r != nullptr && done != nullptr, "io::File async start: null request or completion");
    r->file = file;
    r->kind = kind;
    r->done = done;
    r->user = user;
    r->state.Store(kAsyncPending, MemoryOrder::Relaxed);
}
} // namespace

void File::ReadAsync(AsyncRequest* request, void* buffer, usize length, u64 offset, Completion done, void* user) {
    Prepare(this, request, AsyncKind::Read, done, user);
    request->buffer = buffer;
    request->data = nullptr;
    request->length = length;
    request->offset = offset;
    in_flight_.FetchAdd(1, MemoryOrder::AcqRel);
    StartAsync(request);
}

void File::WriteAsync(AsyncRequest* request, const void* data, usize length, u64 offset, Completion done, void* user) {
    Prepare(this, request, AsyncKind::Write, done, user);
    request->buffer = nullptr;
    request->data = data;
    request->length = length;
    request->offset = offset;
    in_flight_.FetchAdd(1, MemoryOrder::AcqRel);
    StartAsync(request);
}

void File::FlushAsync(AsyncRequest* request, Completion done, void* user) {
    Prepare(this, request, AsyncKind::Flush, done, user);
    request->buffer = nullptr;
    request->data = nullptr;
    request->length = 0;
    request->offset = 0;
    in_flight_.FetchAdd(1, MemoryOrder::AcqRel);
    StartAsync(request);
}

// --- scheme registry --------------------------------------------------------------------------------------------------

namespace {

constexpr usize kMaxSchemes = 16;
constexpr usize kMaxSchemeLength = 31;

struct SchemeEntry {
    char name[kMaxSchemeLength + 1];
    usize length;
    FileFactory factory;
};

Mutex g_scheme_mutex;
SchemeEntry g_schemes[kMaxSchemes];
usize g_scheme_count = 0;

bool ValidScheme(StringView s) {
    if (s.Empty() || s.Length() > kMaxSchemeLength || !(s[0] >= 'a' && s[0] <= 'z')) {
        return false;
    }
    for (usize i = 1; i < s.Length(); ++i) {
        const char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.')) {
            return false;
        }
    }
    return true;
}

FileFactory FindFactory(StringView scheme) {
    LockGuard<Mutex> lock(g_scheme_mutex);
    for (usize i = 0; i < g_scheme_count; ++i) {
        if (StringView(g_schemes[i].name, g_schemes[i].length) == scheme) {
            return g_schemes[i].factory;
        }
    }
    return nullptr;
}

} // namespace

Status RegisterFileScheme(StringView scheme, FileFactory factory) {
    if (!ValidScheme(scheme) || factory == nullptr || scheme == StringView("file")) {
        return Status::Of(Error::Invalid);
    }
    LockGuard<Mutex> lock(g_scheme_mutex);
    for (usize i = 0; i < g_scheme_count; ++i) {
        if (StringView(g_schemes[i].name, g_schemes[i].length) == scheme) {
            return Status::Of(Error::AlreadyExists);
        }
    }
    if (g_scheme_count == kMaxSchemes) {
        return Status::Of(Error::OutOfMemory);
    }
    SchemeEntry& e = g_schemes[g_scheme_count++];
    MemCopy(e.name, scheme.Data(), scheme.Length());
    e.name[scheme.Length()] = '\0';
    e.length = scheme.Length();
    e.factory = factory;
    return Status::Success();
}

void UnregisterFileScheme(StringView scheme) {
    LockGuard<Mutex> lock(g_scheme_mutex);
    for (usize i = 0; i < g_scheme_count; ++i) {
        if (StringView(g_schemes[i].name, g_schemes[i].length) == scheme) {
            g_schemes[i] = g_schemes[--g_scheme_count];
            return;
        }
    }
}

Status File::OpenLocation(StringView location, OpenMode mode, u32 flags, FilePtr* out) {
    const usize colon = location.Find("://");
    const StringView scheme = location.Substring(0, colon);
    if (colon != location.Length() && ValidScheme(scheme)) {
        const StringView rest = location.Substring(colon + 3);
        if (scheme == StringView("file")) {
            if (rest.Empty() || rest[0] != '/') {
                return Status::Of(Error::Unsupported); // `file://host/path`: no remote hosts
            }
            char decoded[kMaxPathBytes + 1];
            usize length = 0;
            for (usize i = 0; i < rest.Length(); ++i) {
                char c = rest[i];
                if (c == '%') {
                    const auto hex = [](char h) { return h >= '0' && h <= '9' ? h - '0' : (h >= 'a' && h <= 'f' ? h - 'a' + 10 : (h >= 'A' && h <= 'F' ? h - 'A' + 10 : -1)); };
                    if (i + 2 >= rest.Length()) return Status::Of(Error::Invalid);
                    const int hi = hex(rest[i + 1]);
                    const int lo = hex(rest[i + 2]);
                    if (hi < 0 || lo < 0) return Status::Of(Error::Invalid);
                    c = static_cast<char>(hi * 16 + lo);
                    i += 2;
                }
                if (c == '\0') return Status::Of(Error::Invalid); // %00
                if (length >= kMaxPathBytes) return Status::Of(Error::TooLong);
                decoded[length++] = c;
            }
            StringView text(decoded, length);
            if (NativePathStyle() == PathStyle::Windows && length >= 3 && text[0] == '/' && text[2] == ':') {
                text = text.Substring(1); // file:///C:/x -> C:/x
            }
            Path path;
            if (!Path::Parse(text, NativePathStyle(), &path)) {
                return Status::Of(Error::TooLong);
            }
            return Open(path, mode, flags, out);
        }
        const FileFactory factory = FindFactory(scheme);
        if (factory == nullptr) {
            return Status::Of(Error::Unsupported);
        }
        return factory(location, mode, flags, out);
    }
    Path path;
    if (!Path::Parse(location, NativePathStyle(), &path)) {
        return Status::Of(Error::TooLong);
    }
    return Open(path, mode, flags, out);
}

// --- helpers that work through any File -------------------------------------------------------------------------------

Status ReadExact(File& file, void* buffer, usize length, u64 offset) {
    u8* dst = static_cast<u8*>(buffer);
    usize done = 0;
    while (done < length) {
        usize n = 0;
        const Status s = file.Read(dst + done, length - done, offset + done, &n);
        if (s.Failed()) return s;
        if (n == 0) return Status::Of(Error::EndOfFile);
        done += n;
    }
    return Status::Success();
}

namespace {

// Reads in chunks until a read returns zero, handing each chunk to `sink(data, n)`; a size is never trusted.
template <typename Sink>
Status ReadChunks(File& file, u64 max_bytes, Sink&& sink) {
    u8 chunk[16 * 1024];
    u64 offset = 0;
    for (;;) {
        usize n = 0;
        const Status s = file.Read(chunk, sizeof(chunk), offset, &n);
        if (s.Failed()) return s;
        if (n == 0) return Status::Success();
        offset += n;
        if (offset > max_bytes) return Status::Of(Error::TooLong);
        if (!sink(chunk, n)) return Status::Of(Error::OutOfMemory);
    }
}

} // namespace

Status ReadAll(File& file, Vector<u8>* out, u64 max_bytes) {
    Vector<u8> result;
    u64 hint = 0;
    if (file.Size(&hint).Ok() && hint > 0 && hint <= max_bytes) {
        if (!result.TryReserve(static_cast<usize>(hint))) return Status::Of(Error::OutOfMemory);
    }
    const Status s = ReadChunks(file, max_bytes, [&](const u8* data, usize n) {
        const usize old = result.Size();
        if (!result.TryResizeUninitialized(old + n)) return false;
        MemCopy(result.Data() + old, data, n);
        return true;
    });
    if (s.Failed()) return s;
    *out = static_cast<Vector<u8>&&>(result);
    return Status::Success();
}

Status ReadAll(File& file, ArenaAllocator& arena, u8** data, usize* size, u64 max_bytes) {
    // A known size lets the bytes go straight into the arena. The size is only a hint: a short file ends the read early, and a
    // file that holds more than it reported (a `/proc` file, a file that grew) takes the general path below.
    u64 hint = 0;
    if (file.Size(&hint).Ok() && hint > 0 && hint <= max_bytes) {
        u8* block = static_cast<u8*>(arena.Alloc(static_cast<usize>(hint), 1));
        if (block == nullptr) return Status::Of(Error::OutOfMemory);
        usize filled = 0;
        while (filled < hint) {
            usize n = 0;
            const Status s = file.Read(block + filled, static_cast<usize>(hint) - filled, filled, &n);
            if (s.Failed()) return s;
            if (n == 0) break;
            filled += n;
        }
        u8 probe;
        usize extra = 0;
        const Status ps = file.Read(&probe, 1, filled, &extra);
        if (ps.Failed()) return ps;
        if (extra == 0) {
            *data = block;
            *size = filled;
            return Status::Success();
        }
    }
    Vector<u8> bytes;
    if (Status s = ReadAll(file, &bytes, max_bytes); s.Failed()) return s;
    u8* block = static_cast<u8*>(arena.Alloc(bytes.Size() == 0 ? 1 : bytes.Size(), 1));
    if (block == nullptr) return Status::Of(Error::OutOfMemory);
    if (bytes.Size() != 0) MemCopy(block, bytes.Data(), bytes.Size());
    *data = block;
    *size = bytes.Size();
    return Status::Success();
}

Status ReadFile(const Path& path, Vector<u8>* out, u64 max_bytes) {
    FilePtr file;
    if (Status s = File::Open(path, OpenMode::Read, kOpenNone, &file); s.Failed()) return s;
    return ReadAll(*file, out, max_bytes);
}

Status ReadFile(const Path& path, ArenaAllocator& arena, u8** data, usize* size, u64 max_bytes) {
    FilePtr file;
    if (Status s = File::Open(path, OpenMode::Read, kOpenNone, &file); s.Failed()) return s;
    return ReadAll(*file, arena, data, size, max_bytes);
}

Status ReadTextFile(const Path& path, String<HeapAllocator>* out, u64 max_bytes) {
    Vector<u8> bytes;
    if (Status s = ReadFile(path, &bytes, max_bytes); s.Failed()) return s;
    String<HeapAllocator> text(HeapAllocator::Default());
    text.AppendRawBytes(reinterpret_cast<const char*>(bytes.Data()), bytes.Size());
    *out = static_cast<String<HeapAllocator>&&>(text);
    return Status::Success();
}

Status WriteFile(const Path& path, const void* data, usize length) {
    FilePtr file;
    if (Status s = File::Open(path, OpenMode::Write, kOpenNone, &file); s.Failed()) return s;
    const Status w = file->Write(data, length, 0);
    const Status c = file->Close();
    return w.Failed() ? w : c;
}

namespace {
Atomic<int> g_atomic_failure_step{0}; // set only by tests
}
namespace detail {
void SetAtomicWriteFailureStep(int step) { g_atomic_failure_step.Store(step, MemoryOrder::Relaxed); }
} // namespace detail

Status WriteFileAtomic(const Path& requested, const void* data, usize length, u32 new_file_permissions) {
    if (requested.FileName().Empty()) return Status::Of(Error::Invalid);
    // Replace the file a symbolic link points to, not the link.
    Path path = requested;
    FileInfo link_info;
    if (Lstat(requested, &link_info).Ok() && link_info.type == FileType::Symlink) {
        if (Status s = Canonical(requested, &path); s.Failed()) return s;
    }
    FileInfo existing;
    const bool had_target = Stat(path, &existing).Ok() && existing.type == FileType::Regular;
    Path dir = path.Parent();
    if (dir.IsEmpty()) dir = Path(".", path.Style());
    Path temp;
    if (Status s = CreateTempFile(dir, ".atomic", &temp); s.Failed()) return s;
    auto fail_at = [](int step) { return g_atomic_failure_step.Load(MemoryOrder::Relaxed) == step ? Status::Of(Error::Io) : Status::Success(); };

    Status result;
    {
        FilePtr file;
        result = File::Open(temp, OpenMode::Write, kOpenNone, &file);
        if (result.Ok()) result = fail_at(1);
        if (result.Ok()) result = file->Write(data, length, 0);
        if (result.Ok()) result = fail_at(2);
        if (result.Ok()) result = file->Flush();
        if (result.Ok()) result = fail_at(3);
        if (result.Ok()) result = file->Sync();
        if (file) {
            const Status closed = file->Close();
            if (result.Ok()) result = closed;
        }
    }
    if (result.Ok() && (had_target || new_file_permissions != 0600)) result = SetPermissions(temp, had_target ? existing.permissions : new_file_permissions);
    if (result.Ok()) result = fail_at(4);
    if (result.Ok()) result = Rename(temp, path);
    if (result.Ok()) result = SyncDirectory(dir);
    if (result.Failed()) {
        Remove(temp); // best effort; the original target is untouched
    }
    return result;
}

} // namespace engine_placeholder::io
