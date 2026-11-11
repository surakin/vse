#include "database/sqlite/sqlite_vfs.h"

#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "cstr/cstr.h"
#include "cstr/mem_ops.h"
#include "database/sqlite/sqlite_os.h"
#include "io/file.h"
#include "io/file_system.h"
#include "io/path.h"
#include "lang/new.h"
#include "os/os.h"
#include "sync/atomic.h"
#include "sync/mutex.h"
#include "sync/thread.h"
#include "types.h"

// SQLite reaches the disk only through this file: every open, read, write, sync, truncation and deletion goes through
// the file layer (`io::File`, `io::Path`), so `file_access_check` holds and a new platform needs no SQLite port. The
// file layer has no file locks and no shared memory, so:
//
// - The five SQLite file locks (shared, reserved, pending, exclusive) are kept in a table in this process, keyed by the
// file's absolute path.
//   Several connections in one process follow SQLite's real rules; **a second process is not excluded** (a database
//   belongs to one process at a time; a cross-process lock needs a file-layer facility, KNOWN-ISSUES.md).
// - The write-ahead-log index (the "shared memory" of WAL) lives on the heap, per database file, shared by the
// connections of this process, and is
//   freed when the last of them closes; reopening rebuilds it from the log file, as SQLite does after any crash.
//
// A version-1 VFS silently refuses WAL (Day 250 spike (a)), hence iVersion 2 and the four shared-memory methods.

namespace engine_placeholder::database::sqlite_detail
{
namespace
{

// --- fault injection (tests)
// ---------------------------------------------------------------------------------------------------------------------

Atomic<unsigned> g_fault_ops(0);
Atomic<long long> g_fault_countdown(-1);
Atomic<bool> g_fault_crash(false);
Atomic<bool> g_fault_dead(false);
Atomic<long long> g_fault_seen(0);

// True when `op` must fail now. A dead (crashed) process fails every operation, whatever its kind.
bool FaultNow(unsigned op)
{
    if (g_fault_dead.Load())
    {
        return true;
    }
    if ((g_fault_ops.Load() & op) == 0)
    {
        return false;
    }
    g_fault_seen.FetchAdd(1);
    if (g_fault_countdown.FetchSub(1) != 0)
    {
        return false;
    }
    if (g_fault_crash.Load())
    {
        g_fault_dead.Store(true);
    }
    return true;
}

// --- per-file shared state: locks and the WAL index
// -----------------------------------------------------------------------------------------------

constexpr int kShmSlots = SQLITE_SHM_NLOCK;

struct Identity
{
    Vector<char> key;  // the absolute path, with a terminating NUL
    u32 refs = 0;
    // File locks (SQLITE_LOCK_*): how many connections hold SHARED, and who holds the others.
    u32 shared = 0;
    const void* reserved = nullptr;
    const void* pending = nullptr;
    const void* exclusive = nullptr;
    // The WAL index: regions of the size SQLite asked for, and its eight locks.
    Vector<UniquePtr<Vector<u8>>> regions;
    u32 shm_shared[kShmSlots] = {};
    const void* shm_exclusive[kShmSlots] = {};
};

struct Table
{
    Mutex mutex;
    Vector<Identity*> identities;
    ~Table()
    {
        for (usize i = 0; i < identities.Size(); ++i)
        {
            UniquePtr<Identity> release(identities[i]);
        }
    }
};

Table& GetTable()
{
    static Table table;
    return table;
}

bool SameKey(const Vector<char>& a, const char* b, usize b_length)
{
    return a.Size() == b_length + 1 && MemCompare(a.Data(), b, b_length) == 0;
}

Identity* AcquireIdentity(const io::Path& absolute)
{
    Table& table = GetTable();
    LockGuard<Mutex> guard(table.mutex);
    const StringView text = absolute.View();
    for (usize i = 0; i < table.identities.Size(); ++i)
    {
        if (SameKey(table.identities[i]->key, text.Data(), text.Length()))
        {
            ++table.identities[i]->refs;
            return table.identities[i];
        }
    }
    UniquePtr<Identity> made = MakeUnique<Identity>();
    made->key.Resize(text.Length() + 1);
    MemCopy(made->key.Data(), text.Data(), text.Length());
    made->key[text.Length()] = '\0';
    made->refs = 1;
    Identity* raw = made.Release();
    table.identities.Push(raw);
    return raw;
}

void ReleaseIdentity(Identity* identity)
{
    Table& table = GetTable();
    LockGuard<Mutex> guard(table.mutex);
    if (--identity->refs != 0)
    {
        return;
    }
    for (usize i = 0; i < table.identities.Size(); ++i)
    {
        if (table.identities[i] == identity)
        {
            table.identities.Remove(i);
            break;
        }
    }
    UniquePtr<Identity> release(identity);
}

// --- the open file
// ---------------------------------------------------------------------------------------------------------------------

struct EngineFile : sqlite3_file
{
    UniquePtr<io::File> file;
    io::Path path;
    Identity* identity = nullptr;
    int level = SQLITE_LOCK_NONE;
    bool delete_on_close = false;
    bool sync_directory = false;  // a journal or log was created: its directory entry is synced with the first sync
    unsigned shm_shared_held = 0;
    unsigned shm_exclusive_held = 0;
};

EngineFile* Of(sqlite3_file* f)
{
    return static_cast<EngineFile*>(f);
}

int FromStatus(const io::Status& status, int fallback)
{
    if (status.Is(io::Error::NoSpace))
    {
        return SQLITE_FULL;
    }
    if (status.Is(io::Error::ReadOnly))
    {
        return SQLITE_READONLY;
    }
    return fallback;
}

io::Path ParentOf(const io::Path& path)
{
    return path.Parent();
}

int FileClose(sqlite3_file* f)
{
    EngineFile* self = Of(f);
    // Give up every lock the connection still holds.
    if (self->identity != nullptr)
    {
        {
            Table& table = GetTable();
            LockGuard<Mutex> guard(table.mutex);
            Identity& id = *self->identity;
            if (self->level >= SQLITE_LOCK_SHARED)
            {
                if (id.reserved == self)
                {
                    id.reserved = nullptr;
                }
                if (id.pending == self)
                {
                    id.pending = nullptr;
                }
                if (id.exclusive == self)
                {
                    id.exclusive = nullptr;
                }
                --id.shared;
            }
            for (int slot = 0; slot < kShmSlots; ++slot)
            {
                if ((self->shm_shared_held & (1u << slot)) != 0)
                {
                    --id.shm_shared[slot];
                }
                if ((self->shm_exclusive_held & (1u << slot)) != 0)
                {
                    id.shm_exclusive[slot] = nullptr;
                }
            }
        }
        ReleaseIdentity(self->identity);
    }
    if (self->file != nullptr)
    {
        self->file->Close();
    }
    if (self->delete_on_close)
    {
        io::Remove(self->path);
    }
    self->~EngineFile();
    return SQLITE_OK;
}

int FileRead(sqlite3_file* f, void* buffer, int amount, sqlite3_int64 offset)
{
    EngineFile* self = Of(f);
    char* out = static_cast<char*>(buffer);
    usize done = 0;
    const usize want = static_cast<usize>(amount);
    while (done < want)
    {
        usize got = 0;
        const io::Status status = self->file->Read(out + done, want - done, static_cast<u64>(offset) + done, &got);
        if (status.Failed())
        {
            return SQLITE_IOERR_READ;
        }
        if (got == 0)
        {
            break;
        }
        done += got;
    }
    if (done < want)
    {  // SQLite requires the unread part zero-filled
        MemSet(out + done, 0, want - done);
        return SQLITE_IOERR_SHORT_READ;
    }
    return SQLITE_OK;
}

int FileWrite(sqlite3_file* f, const void* buffer, int amount, sqlite3_int64 offset)
{
    if (FaultNow(kFaultWrite))
    {
        return SQLITE_IOERR_WRITE;
    }
    const io::Status status = Of(f)->file->Write(buffer, static_cast<usize>(amount), static_cast<u64>(offset));
    return status.Ok() ? SQLITE_OK : FromStatus(status, SQLITE_IOERR_WRITE);
}

int FileTruncate(sqlite3_file* f, sqlite3_int64 size)
{
    if (FaultNow(kFaultTruncate))
    {
        return SQLITE_IOERR_TRUNCATE;
    }
    const io::Status status = Of(f)->file->Truncate(static_cast<u64>(size));
    return status.Ok() ? SQLITE_OK : SQLITE_IOERR_TRUNCATE;
}

int FileSync(sqlite3_file* f, int)
{
    EngineFile* self = Of(f);
    if (FaultNow(kFaultSync))
    {
        return SQLITE_IOERR_FSYNC;
    }
    if (self->file->Sync().Failed())
    {
        return SQLITE_IOERR_FSYNC;
    }
    if (self->sync_directory)
    {
        if (io::SyncDirectory(ParentOf(self->path)).Failed())
        {
            return SQLITE_IOERR_DIR_FSYNC;
        }
        self->sync_directory = false;
    }
    return SQLITE_OK;
}

int FileSize(sqlite3_file* f, sqlite3_int64* size)
{
    u64 bytes = 0;
    if (Of(f)->file->Size(&bytes).Failed())
    {
        return SQLITE_IOERR_FSTAT;
    }
    *size = static_cast<sqlite3_int64>(bytes);
    return SQLITE_OK;
}

// --- file locks: SQLite's five levels, among the connections of this process
// -----------------------------------------------------------------------

int FileLock(sqlite3_file* f, int want)
{
    EngineFile* self = Of(f);
    if (self->level >= want)
    {
        return SQLITE_OK;
    }
    Table& table = GetTable();
    LockGuard<Mutex> guard(table.mutex);
    Identity& id = *self->identity;
    const void* me = self;
    if (self->level == SQLITE_LOCK_NONE)
    {  // SHARED: refused while a writer is pending or exclusive
        if ((id.pending != nullptr && id.pending != me) || (id.exclusive != nullptr && id.exclusive != me))
        {
            return SQLITE_BUSY;
        }
        ++id.shared;
        self->level = SQLITE_LOCK_SHARED;
        if (want == SQLITE_LOCK_SHARED)
        {
            return SQLITE_OK;
        }
    }
    if (want == SQLITE_LOCK_RESERVED)
    {
        if (id.reserved != nullptr && id.reserved != me)
        {
            return SQLITE_BUSY;
        }
        id.reserved = me;
        self->level = SQLITE_LOCK_RESERVED;
        return SQLITE_OK;
    }
    if (want == SQLITE_LOCK_PENDING || want == SQLITE_LOCK_EXCLUSIVE)
    {
        if ((id.pending != nullptr && id.pending != me) || (id.exclusive != nullptr && id.exclusive != me))
        {
            return SQLITE_BUSY;
        }
        if (self->level < SQLITE_LOCK_PENDING)
        {
            id.pending = me;  // stops new readers while this one waits for the old ones
            self->level = SQLITE_LOCK_PENDING;
        }
        if (want == SQLITE_LOCK_PENDING)
        {
            return SQLITE_OK;
        }
        if (id.shared > 1)
        {  // other readers remain; the pending lock stays, and the caller retries
            return SQLITE_BUSY;
        }
        id.exclusive = me;
        self->level = SQLITE_LOCK_EXCLUSIVE;
    }
    return SQLITE_OK;
}

int FileUnlock(sqlite3_file* f, int want)
{
    EngineFile* self = Of(f);
    if (self->level <= want)
    {
        return SQLITE_OK;
    }
    Table& table = GetTable();
    LockGuard<Mutex> guard(table.mutex);
    Identity& id = *self->identity;
    if (id.reserved == self)
    {
        id.reserved = nullptr;
    }
    if (id.pending == self)
    {
        id.pending = nullptr;
    }
    if (id.exclusive == self)
    {
        id.exclusive = nullptr;
    }
    if (want == SQLITE_LOCK_NONE && self->level >= SQLITE_LOCK_SHARED)
    {
        --id.shared;
    }
    self->level = want;
    return SQLITE_OK;
}

int FileCheckReservedLock(sqlite3_file* f, int* result)
{
    EngineFile* self = Of(f);
    Table& table = GetTable();
    LockGuard<Mutex> guard(table.mutex);
    const Identity& id = *self->identity;
    *result = (id.reserved != nullptr || id.pending != nullptr || id.exclusive != nullptr) ? 1 : 0;
    return SQLITE_OK;
}

int FileControl(sqlite3_file*, int, void*)
{
    return SQLITE_NOTFOUND;
}
int FileSectorSize(sqlite3_file*)
{
    return 4096;
}
int FileDeviceCharacteristics(sqlite3_file*)
{
    return 0;
}  // no promise of atomic writes: SQLite journals every page it changes

// --- the write-ahead-log index, on the heap
// ---------------------------------------------------------------------------------------------------------

int FileShmMap(sqlite3_file* f, int region, int size, int extend, void volatile** out)
{
    EngineFile* self = Of(f);
    Table& table = GetTable();
    LockGuard<Mutex> guard(table.mutex);
    Identity& id = *self->identity;
    const usize index = static_cast<usize>(region);
    if (index >= id.regions.Size())
    {
        if (extend == 0)
        {
            *out = nullptr;
            return SQLITE_OK;
        }
        while (id.regions.Size() <= index)
        {
            UniquePtr<Vector<u8>> made = MakeUnique<Vector<u8>>();
            made->Resize(static_cast<usize>(size));
            MemSet(made->Data(), 0, made->Size());
            id.regions.Push(Move(made));
        }
    }
    *out = id.regions[index]->Data();
    return SQLITE_OK;
}

int FileShmLock(sqlite3_file* f, int offset, int count, int flags)
{
    EngineFile* self = Of(f);
    Table& table = GetTable();
    LockGuard<Mutex> guard(table.mutex);
    Identity& id = *self->identity;
    const void* me = self;
    const bool exclusive = (flags & SQLITE_SHM_EXCLUSIVE) != 0;
    if ((flags & SQLITE_SHM_LOCK) != 0)
    {
        for (int slot = offset; slot < offset + count; ++slot)
        {  // all or none
            const bool others_share = id.shm_shared[slot] - (((self->shm_shared_held >> slot) & 1u) != 0 ? 1u : 0u) !=
                                      0;
            const bool other_exclusive = id.shm_exclusive[slot] != nullptr && id.shm_exclusive[slot] != me;
            if (exclusive ? (others_share || other_exclusive) : other_exclusive)
            {
                return SQLITE_BUSY;
            }
        }
        for (int slot = offset; slot < offset + count; ++slot)
        {
            if (exclusive)
            {
                id.shm_exclusive[slot] = me;
                self->shm_exclusive_held |= 1u << slot;
            }
            else
            {
                ++id.shm_shared[slot];
                self->shm_shared_held |= 1u << slot;
            }
        }
        return SQLITE_OK;
    }
    for (int slot = offset; slot < offset + count; ++slot)
    {
        if (exclusive)
        {
            if (id.shm_exclusive[slot] == me)
            {
                id.shm_exclusive[slot] = nullptr;
            }
            self->shm_exclusive_held &= ~(1u << slot);
        }
        else if ((self->shm_shared_held & (1u << slot)) != 0)
        {
            --id.shm_shared[slot];
            self->shm_shared_held &= ~(1u << slot);
        }
    }
    return SQLITE_OK;
}

void FileShmBarrier(sqlite3_file*)
{
    Table& table = GetTable();  // taking and releasing a lock is a full memory barrier
    LockGuard<Mutex> guard(table.mutex);
}

int FileShmUnmap(sqlite3_file*, int)
{
    return SQLITE_OK;
}  // the index lives as long as any connection to the file

const sqlite3_io_methods* IoMethods()
{
    static const sqlite3_io_methods methods = {
        2,  // iVersion: the shared-memory methods
        FileClose,
        FileRead,
        FileWrite,
        FileTruncate,
        FileSync,
        FileSize,
        FileLock,
        FileUnlock,
        FileCheckReservedLock,
        FileControl,
        FileSectorSize,
        FileDeviceCharacteristics,
        FileShmMap,
        FileShmLock,
        FileShmBarrier,
        FileShmUnmap,
        nullptr,
        nullptr,  // xFetch, xUnfetch (memory-mapped I/O is not used)
    };
    return &methods;
}

// --- the VFS
// ---------------------------------------------------------------------------------------------------------------------

constexpr int kMaxPathname = 1024;

bool Absolute(StringView name, io::Path* out)
{
    return io::Absolute(io::Path(name), out).Ok();
}

int VfsOpen(sqlite3_vfs*, sqlite3_filename name, sqlite3_file* file, int flags, int* out_flags)
{
    file->pMethods = nullptr;  // until the open has succeeded, SQLite must not call xClose
    EngineFile* self = ConstructAt<EngineFile>(file);
    auto fail = [&](int rc)
    {
        self->~EngineFile();
        file->pMethods = nullptr;
        return rc;
    };
    const bool create = (flags & SQLITE_OPEN_CREATE) != 0;
    io::OpenMode mode = (flags & SQLITE_OPEN_READWRITE) != 0 ? io::OpenMode::ReadWrite : io::OpenMode::Read;
    u32 open_flags = io::kOpenNone;
    if (name == nullptr)
    {  // a temporary file (a statement journal that outgrew memory): created now, removed when closed
        io::Path directory;
        io::Path made;
        if (io::TempDirectory(&directory).Failed() || io::CreateTempFile(directory, "sqlite-temp", &made).Failed())
        {
            return fail(SQLITE_CANTOPEN);
        }
        self->path = made;
        self->delete_on_close = true;
        mode = io::OpenMode::ReadWrite;
    }
    else
    {
        io::Path absolute;
        if (!Absolute(StringView(name, CStrLength(name)), &absolute))
        {
            return fail(SQLITE_CANTOPEN);
        }
        self->path = absolute;
        self->delete_on_close = (flags & SQLITE_OPEN_DELETEONCLOSE) != 0;
        if (create)
        {
            open_flags |= io::kOpenCreate;
            if ((flags & SQLITE_OPEN_EXCLUSIVE) != 0)
            {
                open_flags |= io::kOpenCreateNew;
            }
        }
    }
    const bool existed = io::Exists(self->path);
    if (!existed && create && FaultNow(0))
    {  // FaultNow(0) asks only whether the process has crashed: a crashed process creates nothing
        return fail(SQLITE_CANTOPEN);
    }
    if (io::File::Open(self->path, mode, open_flags, &self->file).Failed())
    {
        if (name == nullptr)
        {
            static_cast<void>(io::Remove(self->path));  // the temporary file was created above and nobody will close it
        }
        return fail(SQLITE_CANTOPEN);
    }
    self->sync_directory = !existed && ((flags & (SQLITE_OPEN_MAIN_JOURNAL | SQLITE_OPEN_WAL)) != 0);
    self->identity = AcquireIdentity(self->path);
    if (out_flags != nullptr)
    {
        *out_flags = flags;
    }
    file->pMethods = IoMethods();
    return SQLITE_OK;
}

int VfsDelete(sqlite3_vfs*, const char* name, int sync_directory)
{
    if (FaultNow(0))
    {
        return SQLITE_IOERR_DELETE;
    }
    io::Path absolute;
    if (!Absolute(StringView(name, CStrLength(name)), &absolute))
    {
        return SQLITE_IOERR_DELETE;
    }
    const io::Status removed = io::Remove(absolute);
    if (removed.Is(io::Error::NotFound))
    {
        return SQLITE_IOERR_DELETE_NOENT;
    }
    if (removed.Failed())
    {
        return SQLITE_IOERR_DELETE;
    }
    if (sync_directory != 0 && io::SyncDirectory(ParentOf(absolute)).Failed())
    {
        return SQLITE_IOERR_DIR_FSYNC;
    }
    return SQLITE_OK;
}

int VfsAccess(sqlite3_vfs*, const char* name, int, int* result)
{
    io::Path absolute;
    *result = Absolute(StringView(name, CStrLength(name)), &absolute) && io::Exists(absolute) ? 1 : 0;
    return SQLITE_OK;
}

int VfsFullPathname(sqlite3_vfs*, const char* name, int capacity, char* out)
{
    io::Path absolute;
    if (!Absolute(StringView(name, CStrLength(name)), &absolute))
    {
        return SQLITE_CANTOPEN;
    }
    const StringView text = absolute.View();
    if (text.Length() + 1 > static_cast<usize>(capacity))
    {
        return SQLITE_CANTOPEN;
    }
    MemCopy(out, text.Data(), text.Length());
    out[text.Length()] = '\0';
    return SQLITE_OK;
}

int VfsRandomness(sqlite3_vfs*, int count, char* out)
{
    os::FillRandomBytes(out, static_cast<usize>(count));
    return count;
}

int VfsSleep(sqlite3_vfs*, int microseconds)
{
    ThisThread::SleepForMicroseconds(static_cast<u64>(microseconds));
    return microseconds;
}

// Julian day number 0 is 4714 BC; the Unix epoch is 210866760000000 ms after it.
constexpr sqlite3_int64 kUnixEpochInJulianMillis = 210866760000000LL;

int VfsCurrentTimeInt64(sqlite3_vfs*, sqlite3_int64* out)
{
    *out = kUnixEpochInJulianMillis + static_cast<sqlite3_int64>(os::WallClockUnixMillis());
    return SQLITE_OK;
}

int VfsCurrentTime(sqlite3_vfs* vfs, double* out)
{
    sqlite3_int64 millis = 0;
    VfsCurrentTimeInt64(vfs, &millis);
    *out = static_cast<double>(millis) / 86400000.0;
    return SQLITE_OK;
}

int VfsGetLastError(sqlite3_vfs*, int, char*)
{
    return 0;
}

}  // namespace

sqlite3_vfs* EngineVfs()
{
    static sqlite3_vfs vfs = {
        2,                                     // iVersion
        static_cast<int>(sizeof(EngineFile)),  // szOsFile
        kMaxPathname,                          // mxPathname
        nullptr,                               // pNext
        "engine",                              // zName
        nullptr,                               // pAppData
        VfsOpen,
        VfsDelete,
        VfsAccess,
        VfsFullPathname,
        nullptr,
        nullptr,
        nullptr,
        nullptr,  // xDlOpen, xDlError, xDlSym, xDlClose (extension loading is omitted)
        VfsRandomness,
        VfsSleep,
        VfsCurrentTime,
        VfsGetLastError,
        VfsCurrentTimeInt64,
        nullptr,
        nullptr,
        nullptr,  // xSetSystemCall, xGetSystemCall, xNextSystemCall
    };
    return &vfs;
}

void SetFileFaults(unsigned ops, long long fail_at, bool crash)
{
    g_fault_dead.Store(false);
    g_fault_seen.Store(0);
    g_fault_countdown.Store(fail_at);
    g_fault_crash.Store(crash);
    g_fault_ops.Store(ops);
}

void ClearFileFaults()
{
    g_fault_ops.Store(0);
    g_fault_dead.Store(false);
    g_fault_crash.Store(false);
    g_fault_countdown.Store(-1);
}

long long FileFaultOpsSeen()
{
    return g_fault_seen.Load();
}

}  // namespace engine_placeholder::database::sqlite_detail
