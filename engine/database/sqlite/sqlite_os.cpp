#include "database/sqlite/sqlite_os.h"

#include <sqlite3.h>

#include "containers/unique_ptr.h"
#include "cstr/cstr.h"
#include "os/os.h"
#include "sync/atomic.h"
#include "sync/mutex.h"
#include "sync/thread.h"
#include "types.h"

// SQLITE_OS_OTHER (third_party/sqlite/VENDORED.md): SQLite has no operating-system layer of its own. This file is it: the mutex methods (the engine's
// RecursiveMutex), the VFS and the two hooks SQLite calls from sqlite3_initialize and sqlite3_shutdown.
//
// The VFS of Day 253 serves databases that live in memory: it opens no file (xOpen fails with SQLITE_CANTOPEN, so a file database is refused cleanly) and has
// no clock or entropy source of the operating system yet. Day 254 replaces xOpen, xDelete, xAccess and the locks with the file layer's, and takes randomness
// and the wall clock from `os/` wrappers added then.

struct sqlite3_mutex {
    engine_placeholder::RecursiveMutex mutex;
};

namespace engine_placeholder::database::sqlite_detail {
namespace {

// --- mutexes -------------------------------------------------------------------------------------------------------------------------------------

// SQLite's static mutexes are types 2 to 13. Every mutex here is recursive: a superset of what SQLite asks of its fast mutexes.
constexpr int kFirstStaticMutex = SQLITE_MUTEX_STATIC_MAIN;
constexpr int kStaticMutexCount = 12;

sqlite3_mutex* StaticMutexes() {
    static sqlite3_mutex mutexes[kStaticMutexCount];
    return mutexes;
}

int MutexInit() { return SQLITE_OK; }
int MutexEnd() { return SQLITE_OK; }

sqlite3_mutex* MutexAlloc(int type) {
    if (type == SQLITE_MUTEX_FAST || type == SQLITE_MUTEX_RECURSIVE) {
        return MakeUnique<sqlite3_mutex>().Release();
    }
    if (type < kFirstStaticMutex || type >= kFirstStaticMutex + kStaticMutexCount) {
        return nullptr;
    }
    return &StaticMutexes()[type - kFirstStaticMutex];
}

void MutexFree(sqlite3_mutex* mutex) { UniquePtr<sqlite3_mutex> release(mutex); }
void MutexEnter(sqlite3_mutex* mutex) { mutex->mutex.Lock(); }
int MutexTry(sqlite3_mutex* mutex) { return mutex->mutex.TryLock() ? SQLITE_OK : SQLITE_BUSY; }
void MutexLeave(sqlite3_mutex* mutex) { mutex->mutex.Unlock(); }

// --- the VFS -------------------------------------------------------------------------------------------------------------------------------------

constexpr int kMaxPathname = 512;

int VfsOpen(sqlite3_vfs*, sqlite3_filename, sqlite3_file*, int, int*) { return SQLITE_CANTOPEN; }
int VfsDelete(sqlite3_vfs*, const char*, int) { return SQLITE_IOERR_DELETE_NOENT; }
int VfsAccess(sqlite3_vfs*, const char*, int, int* result) {
    *result = 0;
    return SQLITE_OK;
}

int VfsFullPathname(sqlite3_vfs*, const char* name, int capacity, char* out) {
    const usize length = CStrLength(name);
    if (length + 1 > static_cast<usize>(capacity)) {
        return SQLITE_CANTOPEN;
    }
    for (usize i = 0; i <= length; ++i) {
        out[i] = name[i];
    }
    return SQLITE_OK;
}

Atomic<u64> g_random_counter(0);

// Not a source of secrets: the monotonic clock mixed with a counter through SplitMix64. SQLite uses it for journal nonces and temporary names, and in-memory
// databases need neither. Day 254 replaces it with the operating system's entropy.
int VfsRandomness(sqlite3_vfs*, int count, char* out) {
    for (int i = 0; i < count; i += 8) {
        u64 z = os::MonotonicTimeNanos() + g_random_counter.FetchAdd(0x9E3779B97F4A7C15ull) + 0x9E3779B97F4A7C15ull;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        for (int b = 0; b < 8 && i + b < count; ++b) {
            out[i + b] = static_cast<char>(z >> (8 * b));
        }
    }
    return count;
}

int VfsSleep(sqlite3_vfs*, int microseconds) {
    ThisThread::SleepForMicroseconds(static_cast<u64>(microseconds));
    return microseconds;
}

// No wall clock yet: the date and time functions fail with an error instead of returning a wrong date.
int VfsCurrentTime(sqlite3_vfs*, double*) { return SQLITE_ERROR; }
int VfsGetLastError(sqlite3_vfs*, int, char*) { return 0; }

sqlite3_vfs* Vfs() {
    static sqlite3_vfs vfs = {
        1,                       // iVersion
        static_cast<int>(sizeof(sqlite3_file)), // szOsFile
        kMaxPathname,            // mxPathname
        nullptr,                 // pNext
        "engine",                // zName
        nullptr,                 // pAppData
        VfsOpen,
        VfsDelete,
        VfsAccess,
        VfsFullPathname,
        nullptr,                 // xDlOpen (extension loading is omitted)
        nullptr,                 // xDlError
        nullptr,                 // xDlSym
        nullptr,                 // xDlClose
        VfsRandomness,
        VfsSleep,
        VfsCurrentTime,
        VfsGetLastError,
        nullptr, nullptr, nullptr, nullptr, // version 2 and 3 members, unused at iVersion 1
    };
    return &vfs;
}

int InitializeOnce() {
    sqlite3_mutex_methods methods = {};
    methods.xMutexInit = MutexInit;
    methods.xMutexEnd = MutexEnd;
    methods.xMutexAlloc = MutexAlloc;
    methods.xMutexFree = MutexFree;
    methods.xMutexEnter = MutexEnter;
    methods.xMutexTry = MutexTry;
    methods.xMutexLeave = MutexLeave;
    // xMutexHeld and xMutexNotheld are only called from assert() in a SQLITE_DEBUG build of the library, which this is not.
    const int rc = sqlite3_config(SQLITE_CONFIG_MUTEX, &methods);
    if (rc != SQLITE_OK) {
        return rc;
    }
    return sqlite3_initialize();
}

} // namespace

int InitializeSqlite() {
    static const int result = InitializeOnce();
    return result;
}

} // namespace engine_placeholder::database::sqlite_detail

extern "C" int sqlite3_os_init() { return sqlite3_vfs_register(engine_placeholder::database::sqlite_detail::Vfs(), 1); }
extern "C" int sqlite3_os_end() { return sqlite3_vfs_unregister(engine_placeholder::database::sqlite_detail::Vfs()); }
